// SPDX-License-Identifier: AGPL-3.0-or-later
//
// GLES cube spike — the driver.
//
// Renders a REAL libfluxus PolyPrimitive (built by the engine's own MakeCube)
// through GLESBackend into an offscreen EGL pbuffer, and dumps the framebuffer
// so the result can be looked at on the host. Three passes, written to three
// files, because each answers a different question:
//
//   solid.raw  — does the solid path work? (it already goes through
//                IRenderBackend, so this mostly tests the shader/matrix path)
//   wire.raw   — does wireframe survive WITHOUT glPolygonMode? This is the
//                one the whole spike exists for.
//   both.raw   — hidden-line: solid fill + wire on top, the look most of our
//                sketches actually use.
//
// No JUCE, no Activity, no APK. Run it over adb.

#include "GLESBackend.h"

#include "GraphicsUtils.h"
#include "ParticlePrimitive.h"
#include "PolyPrimitive.h"
#include "RenderBackend.h"
#include "RibbonPrimitive.h"
#include "State.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const int kW = 512, kH = 512;

void perspective(float* m, float fovyDeg, float aspect, float zn, float zf) {
  const float f = 1.0f / std::tan(fovyDeg * 3.14159265f / 360.0f);
  std::memset(m, 0, 16 * sizeof(float));
  m[0] = f / aspect; m[5] = f;
  m[10] = (zf + zn) / (zn - zf); m[11] = -1.0f;
  m[14] = (2.0f * zf * zn) / (zn - zf);
}

// modelview: pull back along -Z, then spin so three faces are visible and any
// winding/normal mistake shows up instead of hiding on a flat-on face.
void modelview(float* m, float yaw, float pitch, float dist) {
  const float cy = std::cos(yaw),   sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  float r[16] = { cy, sp * sy, -cp * sy, 0,
                   0, cp,       sp,      0,
                  sy, -sp * cy, cp * cy, 0,
                   0, 0,        0,       1 };
  std::memcpy(m, r, sizeof(r));
  m[14] = -dist;
}

bool dump(const char* path) {
  std::vector<unsigned char> px((size_t) kW * kH * 4);
  glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  FILE* f = std::fopen(path, "wb");
  if (!f) { std::fprintf(stderr, "[spike] cannot write %s\n", path); return false; }
  // GL reads bottom-up; flip so the host tool does not have to know that.
  for (int y = kH - 1; y >= 0; --y) std::fwrite(&px[(size_t) y * kW * 4], 1, kW * 4, f);
  std::fclose(f);
  std::printf("[spike] wrote %s (%dx%d rgba)\n", path, kW, kH);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : ".";

  // --- EGL offscreen ---------------------------------------------------------
  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (dpy == EGL_NO_DISPLAY) { std::printf("[spike] no EGL display\n"); return 1; }
  EGLint major = 0, minor = 0;
  if (!eglInitialize(dpy, &major, &minor)) { std::printf("[spike] eglInitialize failed\n"); return 1; }
  std::printf("[spike] EGL %d.%d — %s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));

  const EGLint cfgAttr[] = {
      EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_NONE};
  EGLConfig cfg; EGLint nCfg = 0;
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &nCfg) || nCfg < 1) {
    std::printf("[spike] no matching EGL config\n"); return 1;
  }
  const EGLint pbAttr[] = {EGL_WIDTH, kW, EGL_HEIGHT, kH, EGL_NONE};
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT) {
    std::printf("[spike] pbuffer/context creation failed\n"); return 1;
  }
  eglMakeCurrent(dpy, surf, surf, ctx);
  std::printf("[spike] GL_VERSION  : %s\n", (const char*) glGetString(GL_VERSION));
  std::printf("[spike] GL_RENDERER : %s\n", (const char*) glGetString(GL_RENDERER));

  // --- backend ---------------------------------------------------------------
  GLESBackend backend;
  if (!backend.init()) { std::printf("[spike] backend init failed\n"); return 1; }
  Fluxus::SetBackend(&backend);

  float proj[16], mv[16];
  perspective(proj, 45.0f, (float) kW / kH, 0.1f, 100.0f);
  modelview(mv, 0.6f, 0.5f, 4.0f);
  backend.setProjection(proj);
  backend.loadMatrix(mv);

  glViewport(0, 0, kW, kH);
  glEnable(GL_DEPTH_TEST);

  // --- the engine's own cube -------------------------------------------------
  Fluxus::PolyPrimitive cube(Fluxus::PolyPrimitive::QUADS);
  Fluxus::MakeCube(&cube, 1.0f);
  cube.GetState()->Colour = Fluxus::dColour(0.9f, 0.5f, 0.15f, 1.0f);
  cube.GetState()->WireColour = Fluxus::dColour(0.2f, 1.0f, 0.9f, 1.0f);
  std::printf("[spike] cube: %d verts\n", (int) cube.GetDataVec<Fluxus::dVector>("p")->size());

  struct Pass { const char* name; unsigned hints; };
  const Pass passes[] = {
      {"solid", HINT_SOLID},
      {"wire",  HINT_WIRE},
      {"both",  HINT_SOLID | HINT_WIRE},
  };

  for (const Pass& p : passes) {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    backend.setColour(0.9f, 0.5f, 0.15f, 1.0f);
    cube.GetState()->Hints = p.hints;
    cube.Render();
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) std::printf("[spike] %s: GL error 0x%04x\n", p.name, err);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.raw", outDir, p.name);
    dump(path);
  }

  // --- a small scene: the shapes Milestone A is about ------------------------
  // No SceneGraph and no Renderer (both are still fixed-function, see
  // ANDROID-SUBSET.md), so the harness plays their part: it sets each
  // primitive's transform on the backend and calls Render() itself.
  {
    Fluxus::PolyPrimitive sphere(Fluxus::PolyPrimitive::TRISTRIP);
    Fluxus::MakeSphere(&sphere, 0.6f, 12, 12);
    sphere.GetState()->Colour = Fluxus::dColour(0.25f, 0.75f, 0.95f, 1.0f);
    sphere.GetState()->Hints  = HINT_SOLID;

    Fluxus::RibbonPrimitive ribbon;
    ribbon.Resize(48);
    {
      auto* p = ribbon.GetDataVec<Fluxus::dVector>("p");
      auto* w = ribbon.GetDataVec<float>("w");
      for (unsigned i = 0; i < p->size(); ++i) {
        const float t = (float) i / (p->size() - 1);
        (*p)[i] = Fluxus::dVector(-1.6f + 3.2f * t, 0.45f * std::sin(t * 12.0f), 0.0f);
        (*w)[i] = 0.07f;
      }
    }
    ribbon.GetState()->Colour = Fluxus::dColour(1.0f, 0.55f, 0.15f, 1.0f);
    ribbon.GetState()->Hints  = HINT_SOLID | HINT_UNLIT;

    Fluxus::ParticlePrimitive particles;
    particles.Resize(200);
    {
      auto* p = particles.GetDataVec<Fluxus::dVector>("p");
      auto* c = particles.GetDataVec<Fluxus::dColour>("c");
      auto* s = particles.GetDataVec<Fluxus::dVector>("s");
      for (unsigned i = 0; i < p->size(); ++i) {
        const float a = 6.2831853f * i / (float) p->size();
        const float r = 0.5f + 1.1f * i / (float) p->size();
        (*p)[i] = Fluxus::dVector(r * std::cos(a * 7.0f), r * std::sin(a * 5.0f),
                                  0.4f * std::cos(a * 3.0f));
        (*c)[i] = Fluxus::dColour(i / (float) p->size(), 0.85f,
                                  1.0f - i / (float) p->size(), 1.0f);
        (*s)[i] = Fluxus::dVector(0.07f, 0.07f, 0.07f);
      }
    }
    particles.GetState()->Hints = HINT_SOLID;

    // Ribbon and particles orient themselves to the camera; without a Renderer
    // nothing else tells them where it is.
    const Fluxus::dVector camDir(0, 0, 1), camUp(0, 1, 0);
    sphere.SetSceneInfo(camDir, camUp);
    ribbon.SetSceneInfo(camDir, camUp);
    particles.SetSceneInfo(camDir, camUp);

    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    struct Item { Fluxus::Primitive* prim; float x, y, z; };
    cube.GetState()->Hints = HINT_SOLID | HINT_WIRE;
    const Item items[] = {
        {&cube,      -1.0f,  0.7f, 0.0f},
        {&sphere,     1.3f,  0.7f, 0.0f},
        {&ribbon,     0.0f, -0.5f, 0.0f},
        {&particles,  0.0f, -1.4f, 0.0f},
    };
    // Mirror SceneGraph::RenderWalk exactly: push, ApplyState (which multiplies
    // in the primitive's own transform AND sets its colour/material/blend/cull
    // through the backend), Render, UnapplyState, pop. Doing the transform by
    // hand and skipping ApplyState is what made every primitive inherit the
    // previous one's colour in the first version of this scene.
    for (const Item& it : items) {
      it.prim->GetState()->Transform.init();
      it.prim->GetState()->Transform.translate(it.x, it.y, it.z);
      backend.loadMatrix(mv);
      backend.pushMatrix();
      it.prim->ApplyState();
      it.prim->Render();
      it.prim->UnapplyState();
      backend.popMatrix();
    }
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) std::printf("[spike] scene: GL error 0x%04x\n", err);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/scene.raw", outDir);
    dump(path);
  }

  backend.shutdown();
  eglDestroyContext(dpy, ctx);
  eglDestroySurface(dpy, surf);
  eglTerminate(dpy);
  std::printf("[spike] done\n");
  return 0;
}
