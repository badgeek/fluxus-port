#pragma once
#include <memory>

// Audio seam. JUCE audio (CoreAudio) today — no JACK. The host analyses mic input
// into FFT bands + gain and pushes them to FluxusCommands (flux_set_audio), where
// scripts read them via (gh n) / (gain).
struct IAudioHost {
  virtual ~IAudioHost() {}
  virtual void start() = 0;
  virtual void stop()  = 0;
};

std::unique_ptr<IAudioHost> makeJuceAudioHost();
