// SPDX-License-Identifier: AGPL-3.0-or-later
// Growing unicode glyph atlas for the (build-terminal …) primitive. FluxusCommands
// is JUCE-free, so glyph rasterization (juce::Graphics) + GL upload live here, like
// TextureLoader.cpp. Unlike flux_font_atlas() (a fixed 16x16 ASCII-only grid indexed
// by byte value), this atlas lazily bakes ARBITRARY codepoints — box-drawing, block
// elements, braille, geometric shapes — into a single texture that grows in place
// (glTexSubImage2D one cell at a time), returning texcoords per codepoint.
//
// Cell 0 is reserved as a fully-opaque WHITE square: the terminal's background quads
// sample it so the fill colour comes entirely from the per-vertex colour, while glyph
// quads sample their own cell (white glyph on transparent -> fg colour * coverage).
//
// Must run on the GL thread (scripts do). Glyphs are uploaded top-down (data row 0 =
// glyph top); the mesh builder maps t0 (small) to the quad's TOP vertices, so no
// vertical flip is applied here — this convention is independent of flux_font_atlas.
#include "FluxusCommands.h"
#include <juce_graphics/juce_graphics.h>
#include "GLHeaders.h"

#include <map>
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstdio>

namespace {
const int  kAtlasPx = 1024;                 // texture is kAtlasPx x kAtlasPx
const int  kCellPx  = 32;                   // each glyph cell is kCellPx x kCellPx
const int  kCols    = kAtlasPx / kCellPx;   // 32 cells per row
const int  kSlots   = kCols * kCols;        // 1024 glyph slots
const float kCW = (float) kCellPx / (float) kAtlasPx;   // cell size in texcoords
const float kCH = kCW;

std::mutex               g_mutex;
GLuint                   g_tex     = 0;      // the atlas texture (stable id)
int                      g_next    = 1;      // next free cell (0 = reserved white)
bool                     g_full    = false;
std::map<uint32_t, int>  g_cpToCell;         // codepoint -> cell index

// Upload one kCellPx x kCellPx RGBA cell (top-down rows) into the atlas at cell idx.
void uploadCell(int idx, const unsigned char* rgba) {
  const int cx = (idx % kCols) * kCellPx;
  const int cy = (idx / kCols) * kCellPx;
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, cx, cy, kCellPx, kCellPx, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glBindTexture(GL_TEXTURE_2D, 0);
}

// Rasterize a codepoint into a kCellPx cell (white glyph, transparent bg, filling the
// cell so box-drawing glyphs connect edge-to-edge) and return its RGBA rows top-down.
std::vector<unsigned char> bakeGlyph(uint32_t cp) {
  juce::Image img(juce::Image::ARGB, kCellPx, kCellPx, true);   // transparent
  {
    juce::Graphics g(img);
    g.setColour(juce::Colours::white);
    juce::Font f(juce::Font::getDefaultMonospacedFontName(), (float) kCellPx, juce::Font::plain);
    // widen so a monospace advance ~= the (square) cell -> box/block glyphs fill the
    // cell width; the render quad then squishes it back to the terminal cell aspect.
    const float adv = f.getStringWidthFloat(juce::String::charToString((juce::juce_wchar) 'M'));
    if (adv > 0.5f) f = f.withHorizontalScale((float) kCellPx / adv);
    g.setFont(f);
    g.drawText(juce::String::charToString((juce::juce_wchar) cp),
               0, 0, kCellPx, kCellPx, juce::Justification::centred, false);
  }
  std::vector<unsigned char> buf((size_t) kCellPx * kCellPx * 4);
  for (int y = 0; y < kCellPx; ++y) {
    unsigned char* row = &buf[(size_t) y * kCellPx * 4];   // top-down (no flip)
    for (int x = 0; x < kCellPx; ++x) {
      const juce::Colour c = img.getPixelAt(x, y);
      row[(size_t) x * 4 + 0] = c.getRed();
      row[(size_t) x * 4 + 1] = c.getGreen();
      row[(size_t) x * 4 + 2] = c.getBlue();
      row[(size_t) x * 4 + 3] = c.getAlpha();
    }
  }
  return buf;
}

// Create the texture (once) and bake cell 0 = opaque white. Caller holds g_mutex.
void ensureAtlas() {
  if (g_tex) return;
  glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasPx, kAtlasPx, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // crisp cells
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);   // Metal-GL sampler completeness
  glBindTexture(GL_TEXTURE_2D, 0);
  std::vector<unsigned char> white((size_t) kCellPx * kCellPx * 4, 255);   // opaque white
  uploadCell(0, white.data());
}

// texcoords for cell idx: (s0,t0) top-left, (s1,t1) bottom-right, t0 < t1 (glyph top
// at t0). Small inset avoids sampling into a neighbour cell.
void cellCoords(int idx, float* s0, float* t0, float* s1, float* t1) {
  const int gx = idx % kCols, gy = idx / kCols;
  const float inset = 0.25f / (float) kAtlasPx;
  *s0 = gx * kCW + inset;  *s1 = (gx + 1) * kCW - inset;
  *t0 = gy * kCH + inset;  *t1 = (gy + 1) * kCH - inset;
}
} // namespace

// Stable atlas texture id (the engine binds it as State.Textures[0]). Created lazily.
extern "C" unsigned flux_glyph_atlas_texture(void) {
  std::lock_guard<std::mutex> lk(g_mutex);
  ensureAtlas();
  return g_tex;
}

// Lazily bake `cp` into the atlas and return its texcoords. cp==0 (or blank) returns
// the reserved white cell (used by background quads). On a full atlas, unbaked new
// codepoints fall back to the white cell (logged once).
extern "C" void flux_glyph_cell(unsigned cp, float* s0, float* t0, float* s1, float* t1) {
  std::lock_guard<std::mutex> lk(g_mutex);
  ensureAtlas();
  int idx = 0;
  if (cp != 0 && cp != 32) {
    auto it = g_cpToCell.find(cp);
    if (it != g_cpToCell.end()) {
      idx = it->second;
    } else if (g_next < kSlots) {
      idx = g_next++;
      uploadCell(idx, bakeGlyph(cp).data());
      g_cpToCell[cp] = idx;
    } else {
      if (!g_full) { std::fprintf(stderr, "[glyph-atlas] full (%d slots) — extra glyphs blank\n", kSlots); g_full = true; }
      idx = 0;   // white cell -> effectively blank on a fg quad (bg colour shows)
    }
  }
  cellCoords(idx, s0, t0, s1, t1);
}
