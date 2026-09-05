// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The native side of the on-screen Android spike: the fluxus engine drawing
// through GLESBackend into the EGL surface a GLSurfaceView already set up.
//
// Unlike the offscreen spikes there is no EGL code here at all — GLSurfaceView
// owns the context and the render thread, and simply calls nativeDraw on it.
// That is the whole reason to use it.
//
// The scene is rebuilt every frame (the engine's own immediate model) and
// animated from a frame counter rather than wall-clock time, so what appears on
// screen is reproducible and does not depend on how fast the device runs.

#include "GLESBackend.h"

#include "GraphicsUtils.h"
#include "ParticlePrimitive.h"
#include "PolyPrimitive.h"
#include "RenderBackend.h"
#include "RibbonPrimitive.h"
#include "State.h"

#include <GLES3/gl32.h>
#include <jni.h>

#include <cmath>
#include <cstring>

namespace {

GLESBackend g_backend;
bool  g_ready = false;
int   g_w = 1, g_h = 1;
long  g_frame = 0;

void perspective(float* m, float fovyDeg, float aspect, float zn, float zf) {
  const float f = 1.0f / std::tan(fovyDeg * 3.14159265f / 360.0f);
  std::memset(m, 0, 16 * sizeof(float));
  m[0] = f / aspect; m[5] = f;
  m[10] = (zf + zn) / (zn - zf); m[11] = -1.0f;
  m[14] = (2.0f * zf * zn) / (zn - zf);
}

void orbit(float* m, float yaw, float pitch, float dist) {
  const float cy = std::cos(yaw),   sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  float r[16] = { cy, sp * sy, -cp * sy, 0,
                   0, cp,       sp,      0,
                  sy, -sp * cy, cp * cy, 0,
                   0, 0,        0,       1 };
  std::memcpy(m, r, sizeof(r));
  m[14] = -dist;
}

// Stands in for SceneGraph::RenderWalk, which is what the app would use: push,
// apply the primitive's own state, render, unapply, pop.
void drawPrim(Fluxus::Primitive* p, float x, float y, float z, const float* view) {
  p->GetState()->Transform.init();
  p->GetState()->Transform.translate(x, y, z);
  g_backend.loadMatrix(view);
  g_backend.pushMatrix();
  p->ApplyState();
  p->Render();
  p->UnapplyState();
  g_backend.popMatrix();
}

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_cc_fluxus_spike_MainActivity_nativeInit(JNIEnv*, jclass) {
  g_ready = g_backend.init();
  if (g_ready) Fluxus::SetBackend(&g_backend);
  glEnable(GL_DEPTH_TEST);
}

JNIEXPORT void JNICALL
Java_cc_fluxus_spike_MainActivity_nativeResize(JNIEnv*, jclass, jint w, jint h) {
  g_w = w > 0 ? w : 1;
  g_h = h > 0 ? h : 1;
  glViewport(0, 0, g_w, g_h);
}

JNIEXPORT void JNICALL
Java_cc_fluxus_spike_MainActivity_nativeDraw(JNIEnv*, jclass) {
  if (!g_ready) return;
  const float t = (float) g_frame++ * 0.01f;

  float proj[16], view[16];
  perspective(proj, 45.0f, (float) g_w / (float) g_h, 0.1f, 100.0f);
  // Pulled back and framed for a phone screen: at dist 7 the top of the wire
  // cube ran off the top of the display.
  orbit(view, t, 0.35f, 11.0f);
  g_backend.setProjection(proj);

  glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // The camera vectors have to be derived from the CURRENT view every frame,
  // the way Renderer::PostRender does it (Renderer.cpp:380) — from the inverse
  // modelview, not from a constant. Ribbons and particles build themselves to
  // face the camera, so a stale direction makes them turn edge-on and then get
  // backface-culled as the view orbits away from where it was fixed.
  Fluxus::dMatrix mv;
  std::memcpy(mv.arr(), view, 16 * sizeof(float));
  Fluxus::dMatrix inv = mv.inverse();
  // SetSceneInfo is static — once per frame is the whole scene.
  Fluxus::Primitive::SetSceneInfo(inv.transform_no_trans(Fluxus::dVector(0, 0, 1)),
                                  inv.transform_no_trans(Fluxus::dVector(0, 1, 0)));

  Fluxus::PolyPrimitive cube(Fluxus::PolyPrimitive::QUADS);
  Fluxus::MakeCube(&cube, 1.0f);
  cube.GetState()->Colour     = Fluxus::dColour(0.9f, 0.5f, 0.15f, 1.0f);
  cube.GetState()->WireColour = Fluxus::dColour(0.2f, 1.0f, 0.9f, 1.0f);
  cube.GetState()->Hints      = HINT_SOLID | HINT_WIRE;   // hidden-line
  drawPrim(&cube, -1.6f, 0.0f, 0.0f, view);

  Fluxus::PolyPrimitive sphere(Fluxus::PolyPrimitive::TRISTRIP);
  Fluxus::MakeSphere(&sphere, 0.8f, 14, 14);
  sphere.GetState()->Colour = Fluxus::dColour(0.25f, 0.75f, 0.95f, 1.0f);
  sphere.GetState()->Hints  = HINT_SOLID;
  drawPrim(&sphere, 1.6f, 0.0f, 0.0f, view);

  // A see-through wireframe cube: the look the port had to earn, since GLES has
  // no glPolygonMode and the backend draws the edges as real line geometry.
  Fluxus::PolyPrimitive wire(Fluxus::PolyPrimitive::QUADS);
  Fluxus::MakeCube(&wire, 1.4f);
  wire.GetState()->WireColour = Fluxus::dColour(0.4f, 1.0f, 0.5f, 1.0f);
  wire.GetState()->Hints      = HINT_WIRE | HINT_UNLIT;
  drawPrim(&wire, 0.0f, 1.6f, 0.0f, view);

  Fluxus::RibbonPrimitive ribbon;
  ribbon.Resize(48);
  {
    auto* p = ribbon.GetDataVec<Fluxus::dVector>("p");
    auto* w = ribbon.GetDataVec<float>("w");
    for (unsigned i = 0; i < p->size(); ++i) {
      const float u = (float) i / (p->size() - 1);
      (*p)[i] = Fluxus::dVector(-2.0f + 4.0f * u,
                                0.5f * std::sin(u * 10.0f + t * 4.0f), 0.0f);
      (*w)[i] = 0.08f;
    }
  }
  ribbon.GetState()->Colour = Fluxus::dColour(1.0f, 0.55f, 0.15f, 1.0f);
  ribbon.GetState()->Hints  = HINT_SOLID | HINT_UNLIT;
  drawPrim(&ribbon, 0.0f, -1.5f, 0.0f, view);

  Fluxus::ParticlePrimitive particles;
  particles.Resize(300);
  {
    auto* p = particles.GetDataVec<Fluxus::dVector>("p");
    auto* c = particles.GetDataVec<Fluxus::dColour>("c");
    auto* s = particles.GetDataVec<Fluxus::dVector>("s");
    for (unsigned i = 0; i < p->size(); ++i) {
      const float a = 6.2831853f * i / (float) p->size();
      const float r = 0.6f + 1.4f * i / (float) p->size();
      (*p)[i] = Fluxus::dVector(r * std::cos(a * 7.0f + t),
                                r * std::sin(a * 5.0f + t),
                                0.6f * std::cos(a * 3.0f));
      (*c)[i] = Fluxus::dColour(i / (float) p->size(), 0.6f,
                                1.0f - i / (float) p->size(), 1.0f);
      (*s)[i] = Fluxus::dVector(0.06f, 0.06f, 0.06f);
    }
  }
  particles.GetState()->Hints = HINT_SOLID | HINT_UNLIT;
  drawPrim(&particles, 0.0f, 0.0f, -1.0f, view);
}

}  // extern "C"
