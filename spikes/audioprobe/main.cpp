#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <cstdio>
#include <thread>
#include <chrono>

struct Probe : juce::AudioIODeviceCallback {
  std::atomic<long> calls{0};
  std::atomic<int>  frames{0};
  std::atomic<float> peak{0.f};
  void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
    std::printf("device start: %s  inCh=%d rate=%.0f\n",
      d->getName().toRawUTF8(), d->getActiveInputChannels().countNumberOfSetBits(),
      d->getCurrentSampleRate());
  }
  void audioDeviceStopped() override {}
  void audioDeviceIOCallbackWithContext(const float* const* in,int nIn,float* const*,int,
       int n,const juce::AudioIODeviceCallbackContext&) override {
    calls++; frames += n;
    if (nIn>0 && in[0]) { float p=0; for(int i=0;i<n;i++){float a=in[0][i]; a=a<0?-a:a; if(a>p)p=a;} float e=peak.load(); if(p>e)peak=p; }
  }
};

int main() {
  juce::AudioDeviceManager adm;
  auto err = adm.initialiseWithDefaultDevices(1,0);
  std::printf("init err: '%s'\n", err.toRawUTF8());
  if (auto* dev = adm.getCurrentAudioDevice())
    std::printf("current device: %s open=%d\n", dev->getName().toRawUTF8(), dev->isOpen());
  else std::printf("NO current device\n");
  Probe p; adm.addAudioCallback(&p);
  std::this_thread::sleep_for(std::chrono::seconds(3));
  adm.removeAudioCallback(&p);
  std::printf("RESULT: callbacks=%ld frames=%d peak=%.5f\n", p.calls.load(), p.frames.load(), p.peak.load());
  return 0;
}
