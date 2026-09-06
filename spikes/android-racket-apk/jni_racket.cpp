// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Racket driving fluxus on an Android SCREEN.
//
// The whole chain inside one app process: a `.scm` sketch evaluated by the real
// RacketScriptHost, calling the real flux_* command layer, building into the
// real SceneGraph, drawn by Renderer through GLESBackend into the EGL surface a
// GLSurfaceView provides.
//
// The frame follows app/FluxusScene.cpp's immediate model: Clear, re-eval the
// whole sketch, Render. That is what makes (time) animate a sketch without any
// per-frame plumbing here — the script is re-run each frame, exactly as it is on
// the desktop app.
//
// The sketch comes from a file in the app's own storage and from the control
// port (android_control.cpp), not from a string in this file. kSketch below is
// only the seed written on first launch.

#include "GLESBackend.h"
#include "android_control.h"

#include "FluxusCommands.h"
#include "RacketScriptHost.h"
#include "RenderBackend.h"
#include "Renderer.h"
#include "Camera.h"

#include <GLES3/gl32.h>
#include <android/log.h>
#include <jni.h>

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "fluxus", __VA_ARGS__)

namespace {

GLESBackend      g_backend;
Fluxus::Renderer g_renderer;
RacketScriptHost g_host;
bool  g_ready = false;
int   g_w = 1, g_h = 1;
long  g_frame = 0;

std::string g_sketch;          // GL thread only — what is evaluated each frame
std::string g_lastError;       // ditto, to avoid re-reporting the same one

// --- camera, driven by touch -------------------------------------------------
//
// The gesture feeds flux_camera_drag / flux_camera_zoom — the SAME orbit state
// the desktop mouse drives — and flux_camera_finalize() applies it to the
// engine Camera each frame, where FluxusScene::renderFrame applies it.
//
// This spike used to build its own view matrix and push it with
// backend.loadMatrix. That never had any effect: Renderer::PreRender applies
// Camera's own matrix afterwards, so the hand-rolled orbit was overwritten
// every frame and the view stayed at the engine default. Same mistake as
// setResolution / SetClearFrame / SetSceneInfo before it — a port has to DRIVE
// Renderer, not stand in for it.
//
// Events arrive on the UI thread; the engine is the GL thread's. So the gesture
// arithmetic happens here under a mutex and only the accumulated deltas cross,
// applied on the GL thread in nativeDraw. Nothing on the UI thread touches the
// script engine — the rule the desktop audio and mouse hosts follow.

struct Gesture { bool active = false; int n = 0; float x0, y0, x1, y1; };

std::mutex g_camMutex;
Gesture    g_g;
double     g_dragX = 0, g_dragY = 0, g_zoom = 0;   // pending, consumed per frame
bool       g_resetCam = false;

// Everything here is on the supported list in ANDROID-SUBSET.md: shapes,
// transforms, colours, solid/wire/unlit hints, and (time). Written to
// <filesDir>/sketch.scm on first launch and editable from there on.
const char* kSketch =
    "; Live-code this: adb forward tcp:8020 tcp:8020\n"
    ";                 cli/fluxus watch this-file.scm\n"
    "; Drag to orbit, pinch to zoom.\n"
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

// Android gives an app no console: stdout and stderr go to /dev/null, so a
// Racket error — the exact text that would tell you what is wrong — vanishes.
// Pipe both into logcat once, at startup.
//
// setvbuf line-buffers them as well. Racket's stdout is block-buffered and the
// embedded process exits without unwinding, so a message written just before a
// crash would otherwise never be flushed.
void pumpStdioToLog() {
  int fds[2];
  if (::pipe(fds) != 0) return;
  ::dup2(fds[1], STDOUT_FILENO);
  ::dup2(fds[1], STDERR_FILENO);
  ::close(fds[1]);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setvbuf(stderr, nullptr, _IOLBF, 0);

  std::thread([rd = fds[0]] {
    std::string line;
    char c;
    while (::read(rd, &c, 1) == 1) {
      if (c == '\n') { __android_log_write(ANDROID_LOG_INFO, "fluxus-racket", line.c_str()); line.clear(); }
      else if (line.size() < 4096) line += c;
    }
  }).detach();
}

bool readFile(const std::string& path, std::string& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  out.clear();
  char buf[16 * 1024];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  std::fclose(f);
  return true;
}

// The seed sketch, written once. It is NOT re-written on upgrade: the file is
// the user's from the first launch onwards, and an APK update that silently
// reverted an edited sketch would be the worst kind of surprise.
std::string seedSketch(const std::string& path) {
  std::string src;
  if (readFile(path, src) && !src.empty()) return src;
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f) { std::fputs(kSketch, f); std::fclose(f); LOG("seeded %s", path.c_str()); }
  else   { LOG("could not write %s — running the built-in sketch", path.c_str()); }
  return kSketch;
}

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_cc_fluxus_racket_MainActivity_nativeInit(JNIEnv* env, jclass, jstring root) {
  if (g_ready) return;
  pumpStdioToLog();

  const char* r = env->GetStringUTFChars(root, nullptr);
  const std::string filesDir(r);
  // The runtime lives in the app's files directory — a path only the Java side
  // knows, and nothing about it is derivable from the executable the way a .app
  // bundle is. Telling the host beats baking it in: a compile-time path breaks
  // the moment the package name or the user profile changes.
  RacketScriptHost::setRuntimeRoot(filesDir);
  LOG("runtime root: %s", r);
  env->ReleaseStringUTFChars(root, r);

  if (!g_backend.init()) { LOG("backend init FAILED"); return; }
  Fluxus::SetBackend(&g_backend);
  glEnable(GL_DEPTH_TEST);

  const std::string sketchPath = filesDir + "/sketch.scm";
  g_sketch = seedSketch(sketchPath);
  fluxctl::start(8020, sketchPath, g_sketch);

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
  glViewport(0, 0, g_w, g_h);   // the aspect correction lives in nativeDraw
}

// action: 0 = gesture begins or the pointer count changed, 1 = move, 2 = ends,
// 3 = double tap. Java normalises MotionEvent's action mask down to those, so
// the gesture arithmetic lives here in one place instead of straddling the
// language boundary.
JNIEXPORT void JNICALL
Java_cc_fluxus_racket_MainActivity_nativeTouch(JNIEnv*, jclass, jint action, jint n,
                                               jfloat x0, jfloat y0,
                                               jfloat x1, jfloat y1) {
  std::lock_guard<std::mutex> lk(g_camMutex);

  if (action == 3) { g_resetCam = true; return; }   // double tap: back to default
  if (action == 2) { g_g.active = false; return; }
  if (action == 0 || !g_g.active || n != g_g.n) {
    // A finger going down or up moves the reference points discontinuously;
    // rebase instead of turning that jump into a huge orbit.
    g_g = Gesture { true, n, x0, y0, x1, y1 };
    return;
  }

  if (n >= 2) {
    // Pinch. flux_camera_zoom is additive on the orbit distance (and clamps to
    // 2..80), so the pinch is expressed as a distance delta, not a ratio.
    const float dPrev = std::hypot(g_g.x1 - g_g.x0, g_g.y1 - g_g.y0);
    const float dNow  = std::hypot(x1 - x0, y1 - y0);
    if (dPrev > 1.0f && dNow > 1.0f) g_zoom += (dPrev - dNow) * 0.02;
  } else {
    // flux_camera_drag is in the mouse's units — 0.5 degrees per unit — so 0.4
    // here is 0.2 degrees per pixel: most of a half turn across a phone screen.
    g_dragX += (x0 - g_g.x0) * 0.4;
    g_dragY += (y0 - g_g.y0) * 0.4;
  }

  g_g = Gesture { true, n, x0, y0, x1, y1 };
}

JNIEXPORT void JNICALL
Java_cc_fluxus_racket_MainActivity_nativeDraw(JNIEnv*, jclass) {
  if (!g_ready) return;
  const double t = (double) g_frame * (1.0 / 60.0);

  std::string incoming;
  if (fluxctl::poll(incoming)) {
    g_sketch = incoming;
    LOG("sketch reloaded (%zu bytes)", g_sketch.size());
  }

  // Drain the gesture on the GL thread — the engine belongs to this thread.
  double dragX, dragY, zoom;
  bool   reset;
  {
    std::lock_guard<std::mutex> lk(g_camMutex);
    dragX = g_dragX; dragY = g_dragY; zoom = g_zoom; reset = g_resetCam;
    g_dragX = g_dragY = g_zoom = 0; g_resetCam = false;
  }
  if (reset)                    flux_camera_reset();
  if (dragX != 0 || dragY != 0) flux_camera_drag(dragX, dragY);
  if (zoom != 0)                flux_camera_zoom(zoom);

  std::string err;
  g_renderer.Clear();

  g_host.setFrameInfo(t, (int) g_frame);
  g_host.eval(g_sketch, err);
  if (err != g_lastError) {
    g_lastError = err;
    fluxctl::setError(err);        // what `cli/fluxus error` and a load reply see
    if (!err.empty()) LOG("script: %s", err.c_str());
  }

  // Both of these run AFTER the eval, every frame, and the order is the whole
  // point — the sketch's own (clear) calls Renderer::Clear(), which replaces
  // the camera vector with a fresh default Camera, so anything set before the
  // eval is gone. FluxusScene::renderFrame finalizes the camera in the same
  // place, for the same reason.
  flux_camera_finalize();          // orbit state -> the engine Camera's matrix

  // The default frustum is square, so on a 2.22:1 screen everything renders
  // horizontally squashed. Two other places this does NOT work: on the backend
  // (Camera::DoProjection overwrites it inside PreRender) and in nativeResize
  // (Clear() replaces the camera again).
  const float aspect = (float) g_w / (float) g_h;
  for (auto& c : g_renderer.GetCameraVec())
    c.SetFrustum(-0.5f * aspect, 0.5f * aspect, -0.5f, 0.5f);

  g_renderer.Render();
  ++g_frame;
}

}  // extern "C"
