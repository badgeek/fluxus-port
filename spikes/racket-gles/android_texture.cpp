// SPDX-License-Identifier: AGPL-3.0-or-later
//
// flux_load_texture / flux_load_texture_mem for Android. app/TextureLoader.cpp
// provides these on desktop via juce::ImageFileFormat, which has no TGA reader
// and isn't available in this JUCE-free APK anyway — astroBoy's diffuse map
// (boy_10.tga) needs a decoder that doesn't depend on JUCE. stb_image supports
// TGA/PNG/JPEG/BMP/PSD/GIF in one header; this file mirrors uploadImage() in
// TextureLoader.cpp exactly (RGBA8, vertical flip, GL_TEXTURE_MAX_LEVEL=0) so a
// model looks the same regardless of which loader ran.
#include "FluxusCommands.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_TGA
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <GLES3/gl32.h>

#include <cstdio>
#include <map>
#include <mutex>
#include <string>

namespace {
std::mutex g_texMutex;
std::map<std::string, unsigned> g_texCache;   // path/cacheKey -> GL id

unsigned upload(unsigned char* pixels, int w, int h, int comp) {
  if (!pixels) return 0;
  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const GLenum fmt = (comp == 4) ? GL_RGBA : (comp == 3) ? GL_RGB : GL_LUMINANCE;
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint) fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  // A non-mipmapped texture sampled with the default LINEAR_MIPMAP_LINEAR
  // filter is GL-incomplete and samples as zero — same gotcha as the GPU
  // particle FBOs (CLAUDE.md), pinned here the same way TextureLoader.cpp does.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return id;
}

}  // namespace

extern "C" unsigned flux_load_texture(const char* path) {
  if (!path || !*path) return 0;
  {
    std::lock_guard<std::mutex> lk(g_texMutex);
    auto it = g_texCache.find(path);
    if (it != g_texCache.end()) return it->second;
  }
  stbi_set_flip_vertically_on_load(1);   // matches flipY=true in TextureLoader.cpp
  int w = 0, h = 0, comp = 0;
  unsigned char* pixels = stbi_load(path, &w, &h, &comp, 0);
  if (!pixels) {
    std::fprintf(stderr, "[fluxus] android texture: %s: %s\n", path, stbi_failure_reason());
    return 0;
  }
  const unsigned id = upload(pixels, w, h, comp);
  stbi_image_free(pixels);
  std::lock_guard<std::mutex> lk(g_texMutex);
  g_texCache[path] = id;
  return id;
}

extern "C" unsigned flux_load_texture_mem(const void* bytes, int len, const char* cacheKey) {
  if (!bytes || len <= 0) return 0;
  const std::string key = cacheKey ? cacheKey : "";
  if (!key.empty()) {
    std::lock_guard<std::mutex> lk(g_texMutex);
    auto it = g_texCache.find(key);
    if (it != g_texCache.end()) return it->second;
  }
  stbi_set_flip_vertically_on_load(1);
  int w = 0, h = 0, comp = 0;
  unsigned char* pixels = stbi_load_from_memory(
      (const unsigned char*) bytes, len, &w, &h, &comp, 0);
  if (!pixels) {
    std::fprintf(stderr, "[fluxus] android texture (mem, key=%s): %s\n",
                 key.c_str(), stbi_failure_reason());
    return 0;
  }
  const unsigned id = upload(pixels, w, h, comp);
  stbi_image_free(pixels);
  if (!key.empty()) {
    std::lock_guard<std::mutex> lk(g_texMutex);
    g_texCache[key] = id;
  }
  return id;
}
