#include "AudioHost.h"
#include "FluxusCommands.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <algorithm>
#include <cmath>

// JUCE (CoreAudio) mic -> FFT -> bands + gain -> flux_set_audio.
class JuceAudioHost : public IAudioHost,
                      private juce::AudioIODeviceCallback {
public:
  void start() override {
    adm.initialiseWithDefaultDevices(1, 0);   // 1 input, 0 outputs
    adm.addAudioCallback(this);
  }
  void stop() override {
    adm.removeAudioCallback(this);
    adm.closeAudioDevice();
  }

private:
  static constexpr int fftOrder = 10;          // 1024-point
  static constexpr int fftSize  = 1 << fftOrder;
  static constexpr int nBands   = fftSize / 2;  // 512 per-bin harmonics (fluxus (gh n))

  juce::AudioDeviceManager adm;
  juce::dsp::FFT fft { fftOrder };
  juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                               juce::dsp::WindowingFunction<float>::hann };
  std::array<float, fftSize>     fifo {};
  std::array<float, fftSize * 2> fftData {};
  int    fifoIndex = 0;
  double lastGain  = 0.0;

  void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
  void audioDeviceStopped() override {}

  void audioDeviceIOCallbackWithContext(const float* const* in, int numIn,
                                        float* const* out, int numOut,
                                        int numSamples,
                                        const juce::AudioIODeviceCallbackContext&) override {
    for (int c = 0; c < numOut; ++c)
      if (out[c]) juce::FloatVectorOperations::clear(out[c], numSamples);

    if (numIn <= 0 || in[0] == nullptr) return;
    const float* ch = in[0];

    double sumsq = 0.0;
    for (int i = 0; i < numSamples; ++i) {
      const float s = ch[i];
      sumsq += (double) s * s;
      fifo[(size_t) fifoIndex++] = s;
      if (fifoIndex == fftSize) { processBlock(); fifoIndex = 0; }
    }
    lastGain = std::sqrt(sumsq / juce::jmax(1, numSamples));
  }

  void processBlock() {
    std::copy(fifo.begin(), fifo.end(), fftData.begin());
    std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable(fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform(fftData.data());   // magnitudes [0..fftSize/2]

    // per-bin harmonics: (gh n) reads magnitude of FFT bin n, normalised
    float bands[nBands];
    const float norm = 4.0f / (float) fftSize;
    for (int b = 0; b < nBands; ++b)
      bands[b] = fftData[(size_t) b] * norm;   // ~0..1 for typical audio
    flux_set_audio(bands, nBands, lastGain * 4.0);
  }
};

std::unique_ptr<IAudioHost> makeJuceAudioHost() { return std::make_unique<JuceAudioHost>(); }
