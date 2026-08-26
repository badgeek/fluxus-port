#include "AudioHost.h"
#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <algorithm>

// minimal stand-in for FluxusCommands' audio sink, so we link ONLY AudioHost.
static std::mutex m; static std::vector<float> bands; static double g=0;
extern "C" void flux_set_audio(const float* b,int n,double gain){ std::lock_guard<std::mutex> lk(m); bands.assign(b,b+(n>0?n:0)); g=gain; }
extern "C" double flux_audio_harmonic(int n){ std::lock_guard<std::mutex> lk(m); if(bands.empty())return 0; if(n<0)n=-n; return bands[n%(int)bands.size()]; }
extern "C" double flux_audio_gain(){ std::lock_guard<std::mutex> lk(m); return g; }

int main(){
  auto h = makeJuceAudioHost();
  h->start();
  for(int i=0;i<6;i++){
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    float mx=0; for(int k=0;k<16;k++) mx=std::max(mx,(float)flux_audio_harmonic(k));
    std::printf("t=%.1fs gain=%.4f  gh_max=%.4f  gh0=%.4f gh4=%.4f\n",
      (i+1)*0.5, flux_audio_gain(), mx, flux_audio_harmonic(0), flux_audio_harmonic(4));
  }
  h->stop();
  return 0;
}
