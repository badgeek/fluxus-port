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

using namespace Fluxus;

static long long nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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
    postfx.end();
    const double t = (nowMs() - startMs) / 1000.0;
    postfx.draw(t, flux_audio_gain(), feedback);   // fullscreen pass to the screen
  } else {
    renderer->Render();
  }
}
