// SPDX-License-Identifier: AGPL-3.0-or-later
// Null NTSC filter for -DFLUXUS_ENABLE_NTSC=OFF builds (no Rust toolchain, or a
// GPU/CPU that can't afford the pass). Keeps NTSCEffect's surface so FluxusScene
// and the flux_ntsc_* command state are unchanged — (ntsc #t) is simply not
// applied. Warns once rather than dropping the frame silently.
#include "NTSCEffect.h"

#include <cstdio>

NTSCEffect::~NTSCEffect() = default;

void NTSCEffect::apply(int, int, const NtscParams&) {
  static bool said = false;
  if (said) return;
  said = true;
  std::fprintf(stderr, "[fluxus] ntsc: filter not built into this binary "
                       "(configure with -DFLUXUS_ENABLE_NTSC=ON)\n");
}

void NTSCEffect::release() {}
