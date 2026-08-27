#include <cstdio>
#include "FluxusScene.h"
#include "SharedScript.h"
#include "IScriptHost.h"
#include "FluxusCommands.h"   // feed pixel resolution for (get-screen-size)

// engine + system GL only
#include "Renderer.h"
#include "dada.h"
#include <OpenGL/gl.h>

#include <chrono>
#include <vector>

using namespace Fluxus;

static long long nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// column-major 4x4 multiply: out = a * b
static void mul4(float* out, const float* a, const float* b) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k*4+r] * b[c*4+k];
      out[c*4+r] = s;
    }
}
// 4x4 inverse (MESA gluInvertMatrix); returns false if singular
static bool invert4(float* o, const float* m) {
  float inv[16];
  inv[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
  inv[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
  inv[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
  inv[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
  inv[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
  inv[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
  inv[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
  inv[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
  inv[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
  inv[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
  inv[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
  inv[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
  inv[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
  inv[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
  inv[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
  inv[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
  float det = m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
  if (det == 0.0f) return false;
  det = 1.0f/det;
  for (int i = 0; i < 16; ++i) o[i] = inv[i]*det;
  return true;
}

FluxusScene::FluxusScene(SharedScript* s, std::unique_ptr<IScriptHost> h)
  : host(std::move(h)), shared(s) {}
FluxusScene::~FluxusScene() = default;

void FluxusScene::init() {
  renderer = std::make_unique<Renderer>();
  host->init();          // host was injected (s7 or racket)
  glThread = std::this_thread::get_id();   // script engine is bound to this thread
  startMs = nowMs();
}

void FluxusScene::setResolution(int w, int h) {
  if (renderer) renderer->SetResolution(w, h);
  flux_set_resolution(w, h);
  resW = w; resH = h;
}

void FluxusScene::renderFrame() {
  if (!renderer || !host) return;

  // JUCE renders synchronously on the MESSAGE thread during move/resize/fullscreen.
  // The script engine (Racket CS / s7) is bound to the GL thread and is NOT
  // thread-safe — calling it from another thread crashes. Skip those frames.
  if (std::this_thread::get_id() != glThread) return;

  ++frameCount;
  const double t = (nowMs() - startMs) / 1000.0;
  host->setRenderer(renderer.get());

  // Paint the WHOLE window opaque-black first. With an aspect lock the camera
  // renders into a letterbox sub-rect; the renderer's clear/scissor only covers
  // that rect, so without this the bars stay uncleared and the transparent JUCE
  // window shows the desktop through them. Full viewport + no scissor here.
  if (resW > 0 && resH > 0) {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, resW, resH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  // pull the latest editor buffer + dirty flag (message thread writes them)
  bool isDirty = false;
  if (shared) {
    std::lock_guard<std::mutex> lk(shared->m);
    currentScript = shared->pending;
    isDirty = shared->dirty;
    shared->dirty = false;
  }

  // Two models:
  //  - immediate (default): wipe + re-eval the whole buffer every frame.
  //  - retained ((retained) opt-in): eval the buffer ONCE (build persistent
  //    geometry + register the every-frame thunk), then per frame run only that
  //    thunk — no Clear, no rebuild. Fast for heavy static meshes.
  const bool commit = isDirty || !committedOnce;
  std::string err;
  if (commit) {
    renderer->Clear();
    renderer->SetBGColour(dColour(0.08f, 0.09f, 0.12f, 1.0f));
    flux_set_retained(0);                 // script re-declares (retained) if it wants it
    host->setFrameInfo(t, frameCount);
    if (!currentScript.empty()) host->eval(currentScript, err);
    committedOnce = true;
  } else if (flux_retained_on()) {
    host->setFrameInfo(t, frameCount);    // update time + camera, keep the scene
    host->runFrame(err);
  } else {
    renderer->Clear();
    renderer->SetBGColour(dColour(0.08f, 0.09f, 0.12f, 1.0f));
    host->setFrameInfo(t, frameCount);
    if (!currentScript.empty()) host->eval(currentScript, err);
  }
  if (shared) {
    std::lock_guard<std::mutex> lk(shared->m);
    shared->lastError = err;   // "" = ok
  }

  glEnable(GL_BLEND);   // per-prim blend factors (blend-mode) drive the result

  // optional line/polygon smoothing (anti-alias) for the wireframe look
  if (flux_antialias_on()) {
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  } else {
    glDisable(GL_LINE_SMOOTH);
  }

  // the script (just eval'd) may have installed a post-processing shader.
  std::string frag; bool dirty = false; double feedback = 0.0;
  const bool post = flux_post_state(frag, feedback, dirty) && resW > 0 && resH > 0
                    && postfx.ensure(resW, resH);
  if (post) {
    if (dirty) postfx.setFragment(frag);
    postfx.begin();                 // render scene into the FBO texture
    renderer->Render();
    // capture view + projection for the reprojection blur. The GL modelview at
    // this point is camera*lastPrim (prims multiply onto it), so take the pure
    // view from the engine camera; the projection isn't touched by prims.
    float mv[16], pr[16];
    { double v[16]; flux_get_camera_transform(v); for (int i=0;i<16;++i) mv[i]=(float)v[i]; }
    glGetFloatv(GL_PROJECTION_MATRIX, pr);
    postfx.end();

    float vp[16], vpInv[16];
    mul4(vp, pr, mv);               // view-projection this frame
    if (!invert4(vpInv, vp)) for (int i=0;i<16;++i) vpInv[i] = (i%5==0)?1.0f:0.0f;
    const long long now = nowMs();
    float dt = (lastRenderMs > 0) ? (float) ((now - lastRenderMs) / 1000.0) : 0.016f;
    lastRenderMs = now;

    const double t = (now - startMs) / 1000.0;
    postfx.draw(t, flux_audio_gain(), feedback, vpInv, prevVP, dt);
    for (int i = 0; i < 16; ++i) prevVP[i] = vp[i];   // remember for next frame
  } else {
    renderer->Render();
  }

  // one-shot screenshot of the finished frame (reads the default framebuffer, so
  // it captures exactly what's on screen including the post pass).
  char shotPath[1024];
  if (resW > 0 && resH > 0 && flux_take_screenshot(shotPath, (int) sizeof(shotPath))) {
    std::vector<unsigned char> px((size_t) resW * resH * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, resW, resH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    flux_write_png(shotPath, px.data(), resW, resH);
  }
}
