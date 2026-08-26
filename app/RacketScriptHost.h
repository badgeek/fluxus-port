#pragma once
#include "IScriptHost.h"

// Racket (CS) implementation of the fluxus command API. Drives the SAME
// FluxusCommands engine layer as the s7 host, but from real Racket via ffi/unsafe.
// JUCE-free TU (racketcs.h). Racket boots once per process (guarded).
class RacketScriptHost : public IScriptHost {
public:
  RacketScriptHost();
  ~RacketScriptHost() override;

  void init() override;
  void setRenderer(Fluxus::Renderer* r) override;
  void setFrameInfo(double timeSeconds, int frame) override;
  bool eval(const std::string& code, std::string& errorOut) override;
  bool runFrame(std::string& errorOut) override;   // retained-mode per-frame thunk
};
