// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Racket -> fluxus -> GLES on Android, headless.
//
// The point of this spike: a real `.scm` string, evaluated by the real
// RacketScriptHost, calling the real flux_* command layer, building into the
// real SceneGraph, drawn by Renderer through GLESBackend into an EGL pbuffer on
// an Android device. No JUCE, no Activity, no APK — and no engine code written
// for the occasion.
//
// It mirrors app/FluxusScene.cpp's frame: Clear, eval the script, Render.
// What it does NOT mirror is Renderer::PreRender, whose camera/projection setup
// is fixed-function and stubbed out on GLES (see GLCompatES.h) — so the
// projection is supplied to the backend directly here, exactly as
// spikes/gles-cube does.

#include "GLESBackend.h"

#include "FluxusCommands.h"
#include "RacketScriptHost.h"
#include "RenderBackend.h"
#include "Renderer.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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

void lookFrom(float* m, float yaw, float pitch, float dist) {
  const float cy = std::cos(yaw),   sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  float r[16] = { cy, sp * sy, -cp * sy, 0,
                   0, cp,       sp,      0,
                  sy, -sp * cy, cp * cy, 0,
                   0, 0,        0,       1 };
  std::memcpy(m, r, sizeof(r));
  m[14] = -dist;
}

// Staged error checks. A single check at the end says only "something raised an
// enum error"; checking after each stage says WHICH stage.
void checkGL(const char* stage) {
  for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError())
    std::printf("[spike] GL error 0x%04x after %s\n", e, stage);
}

void dump(const char* path) {
  std::vector<unsigned char> px((size_t) kW * kH * 4);
  glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  FILE* f = std::fopen(path, "wb");
  if (!f) { std::fprintf(stderr, "[spike] cannot write %s\n", path); return; }
  for (int y = kH - 1; y >= 0; --y) std::fwrite(&px[(size_t) y * kW * 4], 1, kW * 4, f);
  std::fclose(f);
  std::printf("[spike] wrote %s\n", path);
}

// Deliberately plain: shapes, transforms, colours. Everything here is on the
// supported list in ANDROID-SUBSET.md.
const char* kSketch =
    "(clear)\n"
    // (clear) resets the renderer's background to black, so the colour has to
    // come from the sketch — setting it on the Renderer before eval is undone.
    "(background (vector 0.05 0.05 0.08))\n"
    "(colour (vector 0.9 0.5 0.15))\n"
    "(translate (vector -1.0 0 0))\n"
    "(build-cube)\n"
    "(identity)\n"
    "(colour (vector 0.25 0.75 0.95))\n"
    "(translate (vector 1.0 0 0))\n"
    "(build-sphere 12 12)\n"
    "(identity)\n"
    "(colour (vector 0.3 0.9 0.5))\n"
    "(translate (vector 0 1.3 0))\n"
    "(scale (vector 0.6 0.6 0.6))\n"
    "(build-cube)\n";

}  // namespace

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : ".";

  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major = 0, minor = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
    std::printf("[spike] EGL init failed\n"); return 1;
  }
  const EGLint cfgAttr[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24, EGL_NONE};
  EGLConfig cfg; EGLint n = 0;
  eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
  const EGLint pb[] = {EGL_WIDTH, kW, EGL_HEIGHT, kH, EGL_NONE};
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
  const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  eglMakeCurrent(dpy, surf, surf, ctx);
  std::printf("[spike] GL_RENDERER: %s\n", (const char*) glGetString(GL_RENDERER));

  GLESBackend backend;
  if (!backend.init()) { std::printf("[spike] backend init failed\n"); return 1; }
  Fluxus::SetBackend(&backend);

  float proj[16], view[16];
  perspective(proj, 45.0f, (float) kW / kH, 0.1f, 100.0f);
  lookFrom(view, 0.5f, 0.4f, 6.0f);
  backend.setProjection(proj);

  glViewport(0, 0, kW, kH);
  glEnable(GL_DEPTH_TEST);

  // --- the engine + the script host ------------------------------------------
  Fluxus::Renderer renderer;
  // Renderer::PreRender sets the viewport from ITS OWN resolution, which
  // defaults to 640x480 — that silently overrode the glViewport above and
  // stretched everything by 640/480 = 1.33 while shifting it off centre. The
  // app does this through FluxusScene::setResolution; the harness must too.
  renderer.SetResolution(kW, kH);
  RacketScriptHost host;
  std::printf("[spike] booting Racket...\n");
  host.init();
  host.setRenderer(&renderer);
  flux_set_renderer(&renderer);
  std::printf("[spike] Racket ready\n");

  checkGL("EGL + backend init");

  std::string err;
  renderer.Clear();
  // Renderer owns the background, but only clears when told to: m_ClearFrame
  // defaults off, so the first run was not "black background" at all — it was
  // no clear happening and the pbuffer showing through.
  renderer.SetClearFrame(true);
  renderer.SetClearZBuffer(true);
  renderer.SetBGColour(Fluxus::dColour(0.05f, 0.05f, 0.08f, 1.0f));
  host.setFrameInfo(0.0, 0);
  host.eval(kSketch, err);
  if (!err.empty()) std::printf("[spike] script error: %s\n", err.c_str());
  checkGL("script eval");

  backend.loadMatrix(view);        // stands in for Renderer::PreRender's camera
  renderer.Render();
  checkGL("Renderer::Render");

  char path[512];
  std::snprintf(path, sizeof(path), "%s/racket-scene.raw", outDir);
  dump(path);

  // A sphere ON the view axis, alone. An off-axis sphere projects to an ellipse
  // under perspective — that is correct, not a bug — so the only way to tell a
  // projection artefact from a real one is to put it where there is no
  // foreshortening and measure again.
  renderer.Clear();
  // (identity) first: the build transform survives (clear), so without it this
  // sphere inherits the last translate/scale from the sketch above.
  host.eval("(clear)\n(background (vector 0.05 0.05 0.08))\n(identity)\n"
            "(colour (vector 0.25 0.75 0.95))\n(build-sphere 16 16)\n", err);
  if (!err.empty()) std::printf("[spike] sphere script error: %s\n", err.c_str());
  backend.loadMatrix(view);
  renderer.Render();
  checkGL("centred sphere");
  std::snprintf(path, sizeof(path), "%s/sphere-centred.raw", outDir);
  dump(path);

  backend.shutdown();
  eglDestroyContext(dpy, ctx);
  eglDestroySurface(dpy, surf);
  eglTerminate(dpy);
  std::printf("[spike] done\n");
  return 0;
}
