// JUCE-based texture loader. FluxusCommands is JUCE-free, so image loading lives
#include <cstdio>
// here: decode via juce::ImageFileFormat, upload a GL texture, return its raw GL
// id. The engine binds State.Textures[0] directly with glBindTexture, so a raw id
// is exactly what (texture id) needs. Must be called on the GL thread (scripts are).
#include "FluxusCommands.h"   // prototype for flux_load_texture
#include <juce_graphics/juce_graphics.h>
#include <OpenGL/gl.h>

#include <string>
#include <map>
#include <mutex>
#include <vector>

namespace {
std::mutex g_texMutex;
std::map<std::string, unsigned> g_texCache;   // path -> GL id (compiled once)
}

extern "C" unsigned flux_load_texture(const char* path) {
  if (!path || !*path) return 0;
  {
    std::lock_guard<std::mutex> lk(g_texMutex);
    auto it = g_texCache.find(path);
    if (it != g_texCache.end()) return it->second;   // already uploaded
  }

  juce::Image img = juce::ImageFileFormat::loadFrom(juce::File(juce::String::fromUTF8(path)));
  if (!img.isValid()) return 0;
  img = img.convertedToFormat(juce::Image::ARGB);
  const int w = img.getWidth(), h = img.getHeight();
  if (w <= 0 || h <= 0) return 0;

  // pack tight RGBA, flipping vertically (GL origin is bottom-left)
  std::vector<unsigned char> buf((size_t) w * h * 4);
  for (int y = 0; y < h; ++y) {
    unsigned char* row = &buf[(size_t) (h - 1 - y) * (size_t) w * 4];
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
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);   // Metal-GL: declare mip range complete
  glFlush();
  glBindTexture(GL_TEXTURE_2D, 0);

  std::lock_guard<std::mutex> lk(g_texMutex);
  g_texCache[path] = id;
  return id;
}
