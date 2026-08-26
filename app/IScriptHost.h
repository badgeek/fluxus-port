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

  // retained mode: run the registered (every-frame ...) thunk without re-evaluating
  // the whole buffer. Default no-op (host stays immediate-only). false => errorOut set.
  virtual bool runFrame(std::string& /*errorOut*/) { return true; }
};
