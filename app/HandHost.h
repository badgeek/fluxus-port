// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <memory>

// Hand-tracking seam (mirrors IAudioHost / IMidiHost). Platform-agnostic adapter:
// the concrete host captures camera frames + runs a hand-landmark model on a
// background thread and pushes 21 landmarks/hand into FluxusCommands
// (flux_set_hands), where scripts read them via (hand-count) / (hand h j) /
// (hand-pinch h). The capture thread only touches the mutex-protected
// FluxusCommands state, never the script engine (gotcha #1).
//
// Cross-platform by construction: makeHandHost() resolves to the Apple Vision
// implementation on macOS (GPU/Neural-Engine) and to a no-op null host elsewhere,
// so the build + bindings stay identical on every platform. A future Linux/Windows
// backend just provides another makeHandHost() TU.
//
// Capture is OFF until a script asks for it — (hand-tracking #t) flips it on via an
// installed bridge (same opt-in shape as OSC's osc-source), so launching an app
// never grabs the camera unprompted.
struct IHandHost {
  virtual ~IHandHost() {}
  virtual void start() = 0;   // install the enable bridge (does NOT open the camera)
  virtual void stop()  = 0;   // stop capture + detach the bridge
};

std::unique_ptr<IHandHost> makeHandHost();
