#pragma once
#include <string>

namespace Fluxus { class Renderer; }

// The scripting seam. A pluggable embedded language that drives the fluxus
// engine. s7 today (S7ScriptHost); Lua/sol2 or others could implement the same
// interface. Kept JUCE-free so it lives in the engine-side TU (no GL-symbol clash).
class IScriptHost {
public:
  virtual ~IScriptHost() {}

  virtual void init() = 0;                                  // embed + register the fluxus command API
  virtual void setRenderer(Fluxus::Renderer* r) = 0;       // target for build-* / state ops
  virtual void setFrameInfo(double timeSeconds, int frame) = 0;
  virtual bool eval(const std::string& code, std::string& errorOut) = 0;  // false => errorOut set
};
