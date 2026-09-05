// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Racket driving fluxus on an Android SCREEN.
//
// The whole chain inside one app process: a `.scm` string evaluated by the real
// RacketScriptHost, calling the real flux_* command layer, building into the
// real SceneGraph, drawn by Renderer through GLESBackend into the EGL surface a
// GLSurfaceView provides.
//
// The frame follows app/FluxusScene.cpp's immediate model: Clear, re-eval the
// whole sketch, Render. That is what makes (time) animate a sketch without any
// per-frame plumbing here — the script is re-run each frame, exactly as it is on
// the desktop app.

#include "GLESBackend.h"

#include "FluxusCommands.h"
#include "RacketScriptHost.h"
#include "RenderBackend.h"
#include "Renderer.h"
#include "Camera.h"

#include <GLES3/gl32.h>
#include <android/log.h>
#include <jni.h>

#include <cmath>
#include <cstring>
#include <string>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "fluxus", __VA_ARGS__)

namespace {

GLESBackend      g_backend;
Fluxus::Renderer g_renderer;
RacketScriptHost g_host;
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

// Everything here is on the supported list in ANDROID-SUBSET.md: shapes,
// transforms, colours, solid/wire/unlit hints, and (time).
const char* kSketch =
    "(clear)\n"
    "(background (vector 0.05 0.05 0.08))\n"
    "(define tt (time))\n"
    "(colour (vector 0.9 0.5 0.15))\n"
    "(wire-colour (vector 0.2 1.0 0.9))\n"
    "(hint-wire)\n"
    "(translate (vector -1.8 0 0))\n"
    "(rotate (vector 0 (* tt 40) 0))\n"
    "(build-cube)\n"
    "(identity)\n"
    "(hint-none)\n"
    "(hint-solid)\n"
    "(colour (vector 0.25 0.75 0.95))\n"
    "(translate (vector 1.8 0 0))\n"
    "(build-sphere 14 14)\n"
    "(identity)\n"
    "(hint-none)\n"
    "(hint-wire)\n"
    "(hint-unlit)\n"
    "(backfacecull #f)\n"
    "(wire-colour (vector 0.4 1.0 0.5))\n"
    "(translate (vector 0 1.7 0))\n"
    "(rotate (vector (* tt 25) (* tt 35) 0))\n"
    "(scale (vector 1.3 1.3 1.3))\n"
    "(build-cube)\n"
    "(identity)\n"
    "(hint-none)\n"
    "(hint-solid)\n"
    "(hint-unlit)\n"
    "(colour (vector 1.0 0.55 0.15))\n"
    "(translate (vector 0 -1.7 (* 0.6 (sin tt))))\n"
    "(scale (vector 0.55 0.55 0.55))\n"
    "(build-cube)\n";

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_cc_fluxus_racket_MainActivity_nativeInit(JNIEnv* env, jclass, jstring root) {
  if (g_ready) return;

  const char* r = env->GetStringUTFChars(root, nullptr);
  // RacketScriptHost bakes its prefix at compile time (RACKET_DIR), and its
  // bundle lookup is macOS-only, so the build script bakes the app's files
  // directory instead. That path is deterministic from the package name — but a
  // real port should teach bundleRoot() about Android rather than rely on it.
  LOG("files dir: %s (compiled-in RACKET_DIR must match)", r);
  env->ReleaseStringUTFChars(root, r);

  if (!g_backend.init()) { LOG("backend init FAILED"); return; }
  Fluxus::SetBackend(&g_backend);
  glEnable(GL_DEPTH_TEST);

  LOG("booting Racket...");
  g_host.init();
  g_host.setRenderer(&g_renderer);
  flux_set_renderer(&g_renderer);
  LOG("Racket ready");

  g_renderer.SetClearFrame(true);
  g_renderer.SetClearZBuffer(true);
  g_ready = true;
}

JNIEXPORT void JNICALL
Java_cc_fluxus_racket_MainActivity_nativeResize(JNIEnv*, jclass, jint w, jint h) {
  g_w = w > 0 ? w : 1;
  g_h = h > 0 ? h : 1;
  // Renderer sets the viewport from its OWN resolution in PreRender, so telling
  // it the size is what actually matters — a bare glViewport here is overridden.
  g_renderer.SetResolution(g_w, g_h);
  // ...and the camera's frustum has to match, or everything comes out squashed.
  // Camera::DoProjection runs inside PreRender and OVERWRITES whatever
  // projection the harness set on the backend, so setting it there is pointless
  // — the aspect correction belongs to the engine's own camera. The default
  // frustum is square; widen it by the screen aspect.
  glViewport(0, 0, g_w, g_h);
}

JNIEXPORT void JNICALL
Java_cc_fluxus_racket_MainActivity_nativeDraw(JNIEnv*, jclass) {
  if (!g_ready) return;
  const double t = (double) g_frame * (1.0 / 60.0);

  float proj[16], view[16];
  perspective(proj, 45.0f, (float) g_w / (float) g_h, 0.1f, 100.0f);
  orbit(view, 0.4f, 0.3f, 11.0f);
  g_backend.setProjection(proj);

  std::string err;
  g_renderer.Clear();

  g_host.setFrameInfo(t, (int) g_frame);
  g_host.eval(kSketch, err);
  if (!err.empty() && (g_frame % 120) == 0) LOG("script: %s", err.c_str());

  // AFTER the eval, every frame, and the order is the whole point. The default
  // frustum is square, so on a 2.22:1 screen everything renders horizontally
  // squashed. Three places this does NOT work:
  //   - on the backend: Camera::DoProjection overwrites it inside PreRender
  //   - in nativeResize: Renderer::Clear() replaces the camera vector with a
  //     fresh default Camera
  //   - before the eval: the sketch's own (clear) calls Renderer::Clear() again
  // so it has to be set on the camera, after the script has finished touching it.
  const float aspect = (float) g_w / (float) g_h;
  for (auto& cam : g_renderer.GetCameraVec())
    cam.SetFrustum(-0.5f * aspect, 0.5f * aspect, -0.5f, 0.5f);

  g_backend.loadMatrix(view);      // stands in for Renderer::PreRender's camera
  g_renderer.Render();
  ++g_frame;
}

}  // extern "C"
