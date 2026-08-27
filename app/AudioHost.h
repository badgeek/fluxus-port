#pragma once
#include <memory>

// Audio seam. JUCE audio (CoreAudio) today — no JACK. The host analyses mic input
// into FFT bands + gain and pushes them to FluxusCommands (flux_set_audio), where
// scripts read them via (gh n) / (gain).
struct IAudioHost {
  virtual ~IAudioHost() {}
  virtual void start() = 0;
  virtual void stop()  = 0;
  // load an audio file (ogg/wav/mp3/flac/aiff), play it out AND analyse it into
  // the same FFT bands so scripts react to the track. Returns false on failure.
  virtual bool loadAudioFile(const char* /*path*/) { return false; }
  virtual void stopAudioFile() {}   // back to live mic input
};

std::unique_ptr<IAudioHost> makeJuceAudioHost();
