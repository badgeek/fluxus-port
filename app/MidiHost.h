#pragma once
#include <memory>

// MIDI input seam (mirrors IAudioHost). A JUCE MidiInput callback pushes CC /
// note values into FluxusCommands (flux_set_midi_*), where scripts read them via
// (midi-cc) / (midi-ccn) / (midi-note). Callbacks run on a JUCE thread, so they
// only touch the mutex-protected FluxusCommands state — never the script engine.
struct IMidiHost {
  virtual ~IMidiHost() {}
  virtual void start() = 0;
  virtual void stop()  = 0;
};

std::unique_ptr<IMidiHost> makeJuceMidiHost();
