// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <memory>

// OSC seam (mirrors IAudioHost). Receives OSC messages (a JUCE OSCReceiver
// callback pushes the latest args per address into FluxusCommands via
// flux_set_osc) and sends them (start() installs a send/source/destination
// bridge into FluxusCommands so (osc-source)/(osc-destination)/(osc-send) can
// drive the transport without FluxusCommands depending on JUCE).
struct IOscHost {
  virtual ~IOscHost() {}
  virtual void start() = 0;
  virtual void stop()  = 0;
};

std::unique_ptr<IOscHost> makeJuceOscHost();
