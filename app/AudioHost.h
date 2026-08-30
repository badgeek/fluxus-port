// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <memory>
#include <vector>

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

// Offline analysis for the frame-locked exporter: decode an audio file and
// compute per-frame (gain + nBands FFT bands) using the SAME analysis as the live
// host, so an export reacts to the track deterministically, synced to each frame.
// Analyses the whole file (nFramesOut = ceil(duration*fps)); fills gains[nFramesOut]
// and bands[nFramesOut*nBandsOut]. Returns false on load error.
bool analyzeAudioFileToFrames(const char* path, int fps,
                              std::vector<float>& gains,
                              std::vector<float>& bands, int& nBandsOut, int& nFramesOut);
