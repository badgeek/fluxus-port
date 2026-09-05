// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The app-level symbols the command layer needs that this spike cannot build.
// Both belong to the font atlas, which lives in app/TextureLoader.cpp — JUCE code
// (JUCE decodes the font image) — so it cannot compile for Android. They stub
// rather than block, and say so once instead of failing silently, like the rest
// of the Android compatibility work.

#include <cstdio>

namespace Fluxus { class Primitive; }

static void warnOnce(const char* what) {
  static const char* said = nullptr;
  if (said == what) return;
  said = what;
  std::fprintf(stderr, "[fluxus] android spike: %s is not built in (see ANDROID-SUBSET.md)\n", what);
}

// pixelsErase and pixelsCount used to be stubbed here. They are not any more:
// app/FluxusCommandsGpu.cpp compiles for Android now that the EXT/ARB framebuffer
// spellings map to their core GLES 3 names, so the real ones are linked in.
//
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
