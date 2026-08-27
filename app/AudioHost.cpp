#include "AudioHost.h"
#include "FluxusCommands.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>

// JUCE (CoreAudio) mic OR a loaded audio file -> FFT -> bands + gain ->
// flux_set_audio. When a file is loaded it is streamed to the output AND fed to
// the analyser, so scripts react to the track instead of the mic.
class JuceAudioHost : public IAudioHost,
                      private juce::AudioIODeviceCallback {
public:
  void start() override {
    adm.initialiseWithDefaultDevices(2, 2);   // try mic + speakers on one device
    if (auto* d = adm.getCurrentAudioDevice()) {
      if (d->getActiveOutputChannels().countNumberOfSetBits() == 0) {
        adm.closeAudioDevice();               // opened an input-only device; need output
        adm.initialiseWithDefaultDevices(0, 2);
      }
    } else {
      adm.initialiseWithDefaultDevices(0, 2);
    }
    adm.addAudioCallback(this);
    if (auto* d = adm.getCurrentAudioDevice())
      std::fprintf(stderr, "[audio] device=%s out=%d in=%d sr=%g\n",
        d->getName().toRawUTF8(),
        d->getActiveOutputChannels().countNumberOfSetBits(),
        d->getActiveInputChannels().countNumberOfSetBits(),
        d->getCurrentSampleRate());
  }
  void stop() override {
    adm.removeAudioCallback(this);
    adm.closeAudioDevice();
  }

  bool loadAudioFile(const char* path) override {
    juce::File f { juce::String::fromUTF8(path) };
    std::fprintf(stderr, "[audio] loadAudioFile: %s exists=%d\n",
                 f.getFullPathName().toRawUTF8(), (int) f.existsAsFile());
    if (!f.existsAsFile()) return false;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r) { std::fprintf(stderr, "[audio] no reader for %s (unsupported format?)\n",
                           f.getFileExtension().toRawUTF8()); return false; }
    const int len = (int) r->lengthInSamples;
    const int chs = (int) juce::jmax(1u, r->numChannels);
    if (len <= 0) return false;
    juce::AudioBuffer<float> buf(chs, len);
    r->read(&buf, 0, len, 0, true, true);
    loading.store(true);                 // block the callback from touching fileBuf
    fileBuf = std::move(buf);
    fileLen = len;
    filePos = 0.0;
    fileSR  = r->sampleRate > 0 ? r->sampleRate : 44100.0;
    loading.store(false);
    playing.store(true);
    std::fprintf(stderr, "[audio] loaded %s (%d ch, %d smp, %g Hz)\n",
                 f.getFileName().toRawUTF8(), chs, len, fileSR);
    return true;
  }
  void stopAudioFile() override { playing.store(false); }

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

  // loaded track (decoded to memory); streamed + analysed when `playing`.
  juce::AudioBuffer<float> fileBuf;
  int              fileLen  = 0;
  double           filePos  = 0.0;      // fractional read position (for resampling)
  double           fileSR   = 44100.0;
  double           deviceSR = 44100.0;
  std::atomic<bool> playing { false };
  std::atomic<bool> loading { false };

  void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
    if (d) deviceSR = d->getCurrentSampleRate();
  }
  void audioDeviceStopped() override {}

  void audioDeviceIOCallbackWithContext(const float* const* in, int numIn,
                                        float* const* out, int numOut,
                                        int numSamples,
                                        const juce::AudioIODeviceCallbackContext&) override {
    for (int c = 0; c < numOut; ++c)
      if (out[c]) juce::FloatVectorOperations::clear(out[c], numSamples);

    // --- loaded file: stream to output + analyse (loops) ---
    if (playing.load() && !loading.load() && fileLen > 0) {
      const int chs  = fileBuf.getNumChannels();
      const double step = fileSR / juce::jmax(1.0, deviceSR);   // resample ratio
      double pos = filePos;
      double sumsq = 0.0;
      for (int i = 0; i < numSamples; ++i) {
        if (pos >= (double) fileLen) pos -= (double) fileLen;   // loop
        const int   i0 = (int) pos;
        const int   i1 = (i0 + 1 < fileLen) ? i0 + 1 : 0;
        const float fr = (float) (pos - i0);
        float mono = 0.0f;
        for (int oc = 0; oc < numOut; ++oc) {
          const int c = (oc < chs) ? oc : 0;
          const float s = fileBuf.getSample(c, i0) * (1.0f - fr)
                        + fileBuf.getSample(c, i1) * fr;         // linear interp
          if (out[oc]) out[oc][i] = s * 0.9f;
        }
        for (int c = 0; c < chs; ++c)
          mono += fileBuf.getSample(c, i0) * (1.0f - fr) + fileBuf.getSample(c, i1) * fr;
        mono /= (float) chs;
        sumsq += (double) mono * mono;
        fifo[(size_t) fifoIndex++] = mono;
        if (fifoIndex == fftSize) { processBlock(); fifoIndex = 0; }
        pos += step;
      }
      filePos = pos;
      lastGain = std::sqrt(sumsq / juce::jmax(1, numSamples));
      return;
    }

    // --- otherwise: live mic input ---
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
