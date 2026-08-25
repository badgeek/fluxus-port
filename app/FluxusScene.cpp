#include "FluxusScene.h"
#include "SharedScript.h"
#include "IScriptHost.h"

// engine + system GL only
#include "Renderer.h"
#include "dada.h"

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
}

void FluxusScene::renderFrame() {
  if (!renderer || !host) return;

  // JUCE renders synchronously on the MESSAGE thread during move/resize/fullscreen.
  // The script engine (Racket CS / s7) is bound to the GL thread and is NOT
  // thread-safe — calling it from another thread crashes. Skip those frames.
  if (std::this_thread::get_id() != glThread) return;

  // fluxus immediate model: wipe the scene each frame (Clear keeps lights,
  // re-adds the default camera), then let the script rebuild it.
  renderer->Clear();
  renderer->SetBGColour(dColour(0.08f, 0.09f, 0.12f, 1.0f));

  ++frameCount;
  const double t = (nowMs() - startMs) / 1000.0;
  host->setRenderer(renderer.get());
  host->setFrameInfo(t, frameCount);

  // pull the latest editor buffer (message thread writes it)
  if (shared) {
    std::lock_guard<std::mutex> lk(shared->m);
    currentScript = shared->pending;
  }

  if (!currentScript.empty()) {
    std::string err;
    host->eval(currentScript, err);
    if (shared) {
      std::lock_guard<std::mutex> lk(shared->m);
      shared->lastError = err;   // "" = ok
    }
  }

  renderer->Render();
}
