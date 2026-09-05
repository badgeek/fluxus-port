// SPDX-License-Identifier: AGPL-3.0-or-later
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

  // fluxus->JUCE port: point the host at a Racket runtime laid out like a
  // bundle — <path>/{lib/racket/*.boot, share/racket/collects, etc/racket,
  // fluxus-lib/}. Call BEFORE init(); it takes precedence over the macOS .app
  // lookup and over the compile-time RACKET_DIR.
  //
  // Android needs this: the runtime lives in the app's files directory, a path
  // only the Java side knows, and nothing about it can be derived from the
  // executable the way a .app bundle can.
  static void setRuntimeRoot(const std::string& path);
};
