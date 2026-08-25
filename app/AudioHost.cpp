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
  static constexpr int nBands   = 16;           // fluxus m_NumBars; (gh n) wraps n % 16

  juce::AudioDeviceManager adm;
  juce::dsp::FFT fft { fftOrder };
  juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                               juce::dsp::WindowingFunction<float>::hann };
  std::array<float, fftSize>     fifo {};
  std::array<float, fftSize * 2> fftData {};
  std::array<float, nBands>      smoothBars {};   // fluxus smoothing (bias 0.8)
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

    // fluxus AudioCollector::GetFFT — 16 bars, quadratic freq mapping, smoothing.
    const float usefulArea = fftSize / 2.0f;   // lower half (nyquist)
    const float bias = 0.8f, gain = 0.015f;
    for (int n = 0; n < nBands; ++n) {
      float f = (float) n / nBands, t = (float) (n + 1) / nBands;
      f *= f; t *= t;                            // quadratic: dense in the lows
      const int from = (int) (f * usefulArea), to = (int) (t * usefulArea);
      float v = 0.0f;
      for (int i = from; i <= to && i < fftSize; ++i) v += fftData[(size_t) i];
      if (v < 0) v = -v;
      v *= gain;
      smoothBars[(size_t) n] = smoothBars[(size_t) n] * bias + v * (1.0f - bias);
    }
    flux_set_audio(smoothBars.data(), nBands, lastGain * 4.0);
  }
};

std::unique_ptr<IAudioHost> makeJuceAudioHost() { return std::make_unique<JuceAudioHost>(); }
