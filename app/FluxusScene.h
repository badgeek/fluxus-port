#pragma once
#include <memory>
#include <string>
#include <thread>
#include "PostFX.h"

struct SharedScript;
namespace Fluxus { class Renderer; }
class IScriptHost;

// Engine boundary (JUCE-free TU). Owns the fluxus Renderer + the s7 script host,
// and runs fluxus's per-frame model: clear the scene, eval the current script
// buffer (which rebuilds the scene via build-* commands), then render.
class FluxusScene {
public:
  FluxusScene(SharedScript* shared, std::unique_ptr<IScriptHost> host);
  ~FluxusScene();

  void init();                        // needs a current GL context
  void setResolution(int w, int h);
  void renderFrame();

private:
  std::unique_ptr<Fluxus::Renderer> renderer;
  std::unique_ptr<IScriptHost>      host;
  SharedScript* shared = nullptr;
  std::string   currentScript;
  int           frameCount = 0;
  long long     startMs = 0;
  std::thread::id glThread;   // the thread Racket/s7 was init'd on
  int           resW = 0, resH = 0;
  PostFX        postfx;       // optional full-screen post-processing pass
};
