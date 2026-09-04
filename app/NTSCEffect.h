// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

struct NtscParams;   // app/FluxusCommands.h

// Final-stage NTSC/VHS filter (ntsc-rs signal simulation, vendored in
// vendor/ntsc-rs). GPU-downsamples the finished framebuffer to a ~480-row
// internal res, reads it back asynchronously (PBO ping-pong), runs the ntsc-rs
// pass on the CPU, then a GPU monitor post pass (saturation/scanlines/blend)
// and a LINEAR upscale blit back over the screen. GL only — created/used on
// the GL thread by FluxusScene, AFTER the scene + any PostFX pass, so
// screenshots/recordings capture the effect too.
class NTSCEffect {
public:
  ~NTSCEffect();
  // read the w*h framebuffer, apply the NTSC filter with p, draw the result back.
  void apply(int w, int h, const NtscParams& p);
  void release();                    // free GL objects + ntsc-rs state

private:
  bool ensure(int W, int H);         // (re)allocate buffers/textures for W*H

  struct Impl;
  Impl* impl = nullptr;              // ntscrs handle, shaders, FBO/PBO chain
  unsigned int tex = 0;              // filtered-frame upload texture
  int w = 0, h = 0;
  int field = 0;                     // frame counter (drives ntsc-rs noise/phase)
};
