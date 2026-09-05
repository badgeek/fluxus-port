// SPDX-License-Identifier: AGPL-3.0-or-later
// JUCE-based texture loader. FluxusCommands is JUCE-free, so image decoding +
// GL upload live here: images via juce::ImageFileFormat, plus a generated font
// atlas for build-text. Raw GL texture ids are returned (the engine binds
// State.Textures[0] directly). Must run on the GL thread (scripts do).
#include "FluxusCommands.h"
#include <juce_graphics/juce_graphics.h>
#include "GLHeaders.h"

#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <cstdio>

namespace {
std::mutex g_texMutex;
std::map<std::string, unsigned> g_texCache;   // path -> GL id (uploaded once)

// upload a JUCE image to a GL texture (RGBA8, optional vertical flip). Metal-GL
// needs GL_TEXTURE_MAX_LEVEL=0 so a non-mipmapped texture is sampler-complete.
unsigned uploadImage(const juce::Image& src, bool flipY) {
  juce::Image img = src.convertedToFormat(juce::Image::ARGB);
  const int w = img.getWidth(), h = img.getHeight();
  if (w <= 0 || h <= 0) return 0;
  std::vector<unsigned char> buf((size_t) w * h * 4);
  for (int y = 0; y < h; ++y) {
    const int dy = flipY ? (h - 1 - y) : y;
    unsigned char* row = &buf[(size_t) dy * (size_t) w * 4];
    for (int x = 0; x < w; ++x) {
      const juce::Colour c = img.getPixelAt(x, y);
      row[(size_t) x * 4 + 0] = c.getRed();
      row[(size_t) x * 4 + 1] = c.getGreen();
      row[(size_t) x * 4 + 2] = c.getBlue();
      row[(size_t) x * 4 + 3] = c.getAlpha();
    }
  }
  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glFlush();
  glBindTexture(GL_TEXTURE_2D, 0);
  return id;
}
}

extern "C" unsigned flux_load_texture(const char* path) {
  if (!path || !*path) return 0;
  {
    std::lock_guard<std::mutex> lk(g_texMutex);
    auto it = g_texCache.find(path);
    if (it != g_texCache.end()) return it->second;
  }
  juce::Image img = juce::ImageFileFormat::loadFrom(juce::File(juce::String::fromUTF8(path)));
  if (!img.isValid()) return 0;
  const unsigned id = uploadImage(img, /*flipY*/ true);
  std::lock_guard<std::mutex> lk(g_texMutex);
  g_texCache[path] = id;
  return id;
}

// Same, for an image that lives INSIDE another file: glTF/FBX embed png/jpg bytes
// (assimp reports the path as "*0"), so there is nothing on disk to open. cacheKey
// is what identifies it in the shared cache — the model loader passes
// "<model dir>#<embedded name>" so two models can't collide.
extern "C" unsigned flux_load_texture_mem(const void* bytes, int len, const char* cacheKey) {
  if (!bytes || len <= 0) return 0;
  const std::string key = cacheKey ? cacheKey : "";
  if (!key.empty()) {
    std::lock_guard<std::mutex> lk(g_texMutex);
    auto it = g_texCache.find(key);
    if (it != g_texCache.end()) return it->second;
  }
  juce::Image img = juce::ImageFileFormat::loadFrom(bytes, (size_t) len);
  if (!img.isValid()) return 0;
  const unsigned id = uploadImage(img, /*flipY*/ true);
  if (!key.empty()) {
    std::lock_guard<std::mutex> lk(g_texMutex);
    g_texCache[key] = id;
  }
  return id;
}

// 16x16 ASCII glyph atlas for build-text: cell (c%16, c/16) holds char c, matching
// TextPrimitive's texcoords (S=(c%16)/16, T=(c/16)/16). Generated once with JUCE.
extern "C" unsigned flux_font_atlas(void) {
  static unsigned cached = 0;
  if (cached) return cached;
  const int cell = 32, grid = 16, sz = cell * grid;   // 512x512
  juce::Image img(juce::Image::ARGB, sz, sz, true);    // transparent
  juce::Graphics g(img);
  g.setColour(juce::Colours::white);
  g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), (float) cell * 0.82f, juce::Font::plain));
  for (int c = 32; c < 127; ++c) {
    const int col = c % grid, row = c / grid;
    g.drawText(juce::String::charToString((juce::juce_wchar) c),
               col * cell, row * cell, cell, cell, juce::Justification::centred, false);
  }
  cached = uploadImage(img, /*flipY*/ true);   // consistent with image textures
  return cached;
}

// Write an RGBA framebuffer grab (bottom-up, from glReadPixels) to a PNG. Runs on
// the GL thread; used by the (screenshot "path") command for self-calibration.
extern "C" void flux_write_png(const char* path, const unsigned char* rgba, int w, int h) {
  if (!path || !rgba || w <= 0 || h <= 0) return;
  juce::Image img(juce::Image::ARGB, w, h, false);
  juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
  for (int y = 0; y < h; ++y) {
    const unsigned char* row = rgba + (size_t) (h - 1 - y) * (size_t) w * 4;  // flip Y
    for (int x = 0; x < w; ++x) {
      const unsigned char* p = row + (size_t) x * 4;
      bd.setPixelColour(x, y, juce::Colour(p[0], p[1], p[2], (juce::uint8) 255));
    }
  }
  juce::File f(juce::String::fromUTF8(path));
  f.deleteFile();
  if (auto os = f.createOutputStream()) {
    juce::PNGImageFormat png;
    png.writeImageToStream(img, *os);
  }
}
