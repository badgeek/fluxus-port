// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The two app-level symbols the command layer needs that this spike does not
// build. Both belong to subsystems already listed as unsupported on Android in
// ANDROID-SUBSET.md, so they stub rather than block — and, like the rest of the
// Android compatibility work, they say so once instead of failing silently.
//
//   pixelsErase      — lives in app/FluxusCommandsGpu.cpp, which is excluded
//                      until its FBO/float-texture calls drop the EXT spellings.
//   flux_font_atlas  — lives in app/TextureLoader.cpp, which is JUCE code
//                      (JUCE decodes the font image), so it cannot build here.

#include <cstdio>

namespace Fluxus { class Primitive; }

static void warnOnce(const char* what) {
  static const char* said = nullptr;
  if (said == what) return;
  said = what;
  std::fprintf(stderr, "[fluxus] android spike: %s is not built in (see ANDROID-SUBSET.md)\n", what);
}

// Called by flux_destroy for every primitive; the GPU-pixels registry it would
// clean up does not exist here, so there is nothing to erase.
void pixelsErase(Fluxus::Primitive*) {}

// flux_pdata_size asks this whether a primitive is a GPU-pixels one. None are.
int pixelsCount(Fluxus::Primitive*) { return -1; }

// The terminal primitive maps characters to cells in the glyph atlas, which is
// part of the same JUCE-built texture as flux_font_atlas.
extern "C" unsigned flux_glyph_atlas_texture(void) { return 0; }

extern "C" void flux_glyph_cell(int, float* s0, float* t0, float* s1, float* t1) {
  warnOnce("build-terminal / the glyph atlas");
  if (s0) *s0 = 0.0f;
  if (t0) *t0 = 0.0f;
  if (s1) *s1 = 1.0f;
  if (t1) *t1 = 1.0f;
}

extern "C" unsigned flux_font_atlas(void) {
  warnOnce("build-text / the font atlas");
  return 0;   // texture id 0 — (build-text) draws untextured quads
}
