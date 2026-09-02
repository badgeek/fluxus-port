// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <vector>

struct NtscParams;   // app/FluxusCommands.h

// Final-stage software NTSC/CRT filter (LMP88959/NTSC-CRT, vendored in
// vendor/ntsc-crt). Reads the finished default framebuffer back to the CPU, runs
// the composite-NTSC modulate/demodulate, then blits the decoded image back over
// the screen. GL only — created/used on the GL thread by FluxusScene, AFTER the
// scene + any PostFX pass, so screenshots/recordings capture the effect too.
class NTSCEffect {
public:
  ~NTSCEffect();
  // read the w*h framebuffer, apply the NTSC filter with p, draw the result back.
  void apply(int w, int h, const NtscParams& p);
  void release();                    // free GL objects + CRT state

private:
  bool ensure(int W, int H);         // (re)allocate buffers/texture/CRT for W*H

  struct Impl;
  Impl* impl = nullptr;              // owns the CRT + NTSC_SETTINGS C structs (~0.5 MB)
  unsigned int tex = 0;              // output blit texture
  int w = 0, h = 0;
  int field = 0;                     // interlace field, toggled each frame
  std::vector<unsigned char> in;     // framebuffer readback  (RGBA)
  std::vector<unsigned char> out;    // NTSC decoded output    (RGBA)
};
