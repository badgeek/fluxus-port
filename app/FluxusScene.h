#pragma once
#include <memory>
#include <string>

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
};
