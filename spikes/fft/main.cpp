// Test the FFT band analysis used by AudioHost: a synthetic sine whose bin sits
// in a known band must produce a peak in that band. Same window+FFT+banding as
// JuceAudioHost::processBlock.
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdio>

static int runTone(int targetBand) {
  constexpr int order = 10, N = 1 << order, nBands = 16;
  const int binsPer = (N / 2) / nBands;
  const int targetBin = targetBand * binsPer + binsPer / 2;

  juce::dsp::FFT fft(order);
  juce::dsp::WindowingFunction<float> win((size_t) N,
      juce::dsp::WindowingFunction<float>::hann);

  std::array<float, 2 * N> data {};
  for (int i = 0; i < N; ++i)
    data[(size_t) i] = std::sin(2.0 * M_PI * targetBin * i / N);

  win.multiplyWithWindowingTable(data.data(), (size_t) N);
  fft.performFrequencyOnlyForwardTransform(data.data());

  float bands[nBands]; int peak = 0; float pv = -1.0f;
  for (int b = 0; b < nBands; ++b) {
    float s = 0; for (int k = 0; k < binsPer; ++k) s += data[(size_t)(b * binsPer + k)];
    bands[b] = s / (float) binsPer;
    if (bands[b] > pv) { pv = bands[b]; peak = b; }
  }
  printf("tone in band %2d -> peak band %2d  (band value %.2f)  %s\n",
         targetBand, peak, pv, peak == targetBand ? "PASS" : "FAIL");
  return peak == targetBand ? 0 : 1;
}

int main() {
  printf("=== FFT band analysis test (AudioHost path) ===\n");
  int fails = 0;
  for (int b : {1, 3, 6, 10, 14}) fails += runTone(b);
  printf(fails == 0 ? "ALL PASS\n" : "%d FAILED\n", fails);
  return fails == 0 ? 0 : 1;
}
