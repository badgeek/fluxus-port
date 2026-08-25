#pragma once
#include "IScriptHost.h"

struct s7_scheme;   // opaque

// s7 Scheme implementation of the fluxus command API. JUCE-free TU.
class S7ScriptHost : public IScriptHost {
public:
  S7ScriptHost();
  ~S7ScriptHost() override;

  void init() override;
  void setRenderer(Fluxus::Renderer* r) override;
  void setFrameInfo(double timeSeconds, int frame) override;
  bool eval(const std::string& code, std::string& errorOut) override;

private:
  s7_scheme* sc = nullptr;
};
