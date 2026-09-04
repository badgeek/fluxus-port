// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NTSCEffect.h"
#include "FluxusCommands.h"    // NtscParams

#include "GLSLShader.h"
#include "dada.h"

#include <OpenGL/gl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

// C ABI of the ntsc-rs staticlib (vendor/ntsc-rs/ffi). The signal path is the
// full ntsc-rs NTSC/VHS simulation; settings arrive as JSON presets.
extern "C" {
void* ntscrs_new(void);
void  ntscrs_free(void* h);
int   ntscrs_load_json(void* h, const char* json);
void  ntscrs_process(void* h, unsigned char* rgba, int w, int hgt,
                     int row_bytes, int frame_num, int flip_y);
}

using namespace Fluxus;

// The blit runs through a tiny GLSL program (like PostFX) rather than the
// fixed-function pipeline: the fluxus renderer leaves state bound that makes a
// fixed-function quad draw black, whereas an explicit shader is self-contained.
static const char* kVert =
  "varying vec2 uv;\n"
  "void main() { uv = gl_MultiTexCoord0.xy; gl_Position = gl_Vertex; }\n";
static const char* kFrag =
  "uniform sampler2D tex;\n"
  "varying vec2 uv;\n"
  "void main() { gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0); }\n";

struct NTSCEffect::Impl {
  void*       rs = nullptr;        // ntscrs handle (Context + settings + scratch)
  GLSLShader* shader = nullptr;
  int         lastRev = -1;        // NtscParams::presetRev last loaded
  bool        havePrev = false;    // `out` holds last frame's result (for blend)
  // The effect runs at a reduced INTERNAL resolution: NTSC is a ~480-line
  // medium, so integer-decimate the retina framebuffer down to <=600 rows
  // (1440 -> 480, 1080 -> 540), run the (CPU-heavy, per-pixel IIR) simulation
  // there, and let the GL blit stretch it back up with linear filtering.
  // ~9x less filter work at 2360x1440, and the artifact scale reads MORE
  // authentic (the filters are tuned for 480-line content).
  int         decim = 1;           // decimation factor (full rows / small rows)
  int         pw = 0, ph = 0;      // processed (small) dimensions
  std::vector<unsigned char> small_; // downsampled working buffer (pw*ph*4)
  // GPU downsample chain: the frame is copied into fullTex, rendered scaled
  // into readTex via the FBO, and ONLY pw*ph pixels are read back. A full-res
  // retina glReadPixels (sync GPU stall + CPU pixel convert of ~3.4M px) was
  // the dominant cost in the profile — this cuts the readback ~decim^2 x.
  unsigned int fullTex = 0;        // w x h copy of the framebuffer
  unsigned int readTex = 0;        // pw x ph downsample target
  unsigned int fbo = 0;
  // Async readback: two PIXEL_PACK PBOs ping-pong so glReadPixels returns
  // immediately (DMA) and we map LAST frame's buffer, which the GPU has long
  // finished — the synchronous readback stall (GLDContextRec::finishResource,
  // ~1/3 of the whole NTSC cost in the profile) disappears for a one-frame
  // latency on the effect input, invisible at 30 fps.
  unsigned int pbo[2] = {0, 0};
  int          pboIdx = 0;
  bool         pboPrimed = false;  // pbo[1-idx] holds a mappable prior frame
};

// When no JSON preset is set, derive the ntsc-rs settings from the classic
// (ntsc-noise n) / (ntsc-hue deg) knobs: n scales the four ntsc-rs noise
// defaults linearly (n=12 == stock ntsc-rs defaults, n=0 == clean signal),
// hue becomes chroma_phase_error. use_field=1 (Upper) halves the row count.
static std::string overlayJson(const NtscParams& p) {
  const double k = p.noise / 12.0;
  char buf[512];
  std::snprintf(buf, sizeof buf,
    "{\"version\":1,\"use_field\":1,"
    "\"composite_noise_intensity\":%.6f,"
    "\"snow_intensity\":%.7f,"
    "\"luma_noise_intensity\":%.6f,"
    "\"chroma_noise_intensity\":%.6f,"
    "\"chroma_phase_error\":%.5f}",
    0.05 * k, 0.00025 * k, 0.01 * k, 0.1 * k, p.hue / 360.0);
  return buf;
}

NTSCEffect::~NTSCEffect() { release(); }

void NTSCEffect::release() {
  if (tex) { glDeleteTextures(1, &tex); tex = 0; }
  if (impl) {
    if (impl->rs) ntscrs_free(impl->rs);
    if (impl->shader && impl->shader->DecRef()) delete impl->shader;
    if (impl->fullTex) glDeleteTextures(1, &impl->fullTex);
    if (impl->readTex) glDeleteTextures(1, &impl->readTex);
    if (impl->fbo)     glDeleteFramebuffersEXT(1, &impl->fbo);
    if (impl->pbo[0])  glDeleteBuffers(2, impl->pbo);
    delete impl;
    impl = nullptr;
  }
  in.clear(); out.clear();
  w = h = 0; field = 0;
}

bool NTSCEffect::ensure(int W, int H) {
  if (W <= 0 || H <= 0) return false;
  if (impl && W == w && H == h) return true;

  w = W; h = H;

  if (!impl) impl = new Impl();
  impl->decim = std::max(1, (h + 479) / 480);        // cap internal res at 480 rows (true NTSC raster)
  impl->pw = std::max(1, w / impl->decim);
  impl->ph = std::max(1, h / impl->decim);
  impl->small_.assign((size_t) impl->pw * impl->ph * 4, 0);
  out.assign((size_t) impl->pw * impl->ph * 4, 0);   // previous-frame store (blend), small res

  if (!tex) glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  // texture holds the SMALL processed frame; the blit quad stretches it to the
  // full viewport with LINEAR filtering (the CRT-ish upscale is free on the GPU)
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, impl->pw, impl->ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  // GPU downsample chain (see Impl). Non-mipmapped textures MUST pin
  // GL_LINEAR/GL_NEAREST min filters (default LINEAR_MIPMAP_LINEAR renders
  // an FBO texture incomplete on this Metal-GL context — known gotcha).
  auto mkTex = [](unsigned int& t, int tw, int th) {
    if (!t) glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
  };
  mkTex(impl->fullTex, w, h);
  mkTex(impl->readTex, impl->pw, impl->ph);
  if (!impl->fbo) glGenFramebuffersEXT(1, &impl->fbo);

  // async-readback PBOs (PBO is core in the 2.1 context; no EXT suffix needed)
  if (!impl->pbo[0]) glGenBuffers(2, impl->pbo);
  for (int i = 0; i < 2; ++i) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, impl->pbo[i]);
    glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr) impl->pw * impl->ph * 4,
                 nullptr, GL_STREAM_READ);
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  impl->pboPrimed = false;                           // stale PBO data after a resize

  if (!impl->rs) impl->rs = ntscrs_new();
  if (!impl->shader) {
    GLSLShader::Init();                              // enable GLSL before compiling
    GLSLShaderPair pair(false, kVert, kFrag);        // compile from source
    impl->shader = new GLSLShader(pair);
  }
  impl->havePrev = false;                            // stale prev after a resize
  return true;
}

void NTSCEffect::apply(int W, int H, const NtscParams& p) {
  if (!ensure(W, H)) return;
  if (!impl->shader || !impl->shader->IsValid()) return;  // no blit path -> leave scene as-is

  // 1. GPU downsample, then a SMALL readback. Copy the finished framebuffer
  //    into fullTex (GPU blit), render it scaled into the pw*ph FBO, and
  //    glReadPixels only pw*ph pixels — the full-res retina readback (sync GPU
  //    stall + CPU pixel convert) dominated the profile. Orientation: the copy
  //    and the FBO both keep row 0 == screen bottom, so flip_y round-trips as
  //    before. FBO state save/restore per the GPGPU gotchas (glDrawBuffer +
  //    viewport leak blanks the scene).
  const int pw = impl->pw, ph = impl->ph;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, impl->fullTex);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, impl->fbo);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                            GL_TEXTURE_2D, impl->readTex, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
  glViewport(0, 0, pw, ph);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
  impl->shader->Apply();
  impl->shader->SetInt("tex", 0);
  glColor4f(1, 1, 1, 1);
  glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f( 1, -1);
    glTexCoord2f(1, 1); glVertex2f( 1,  1);
    glTexCoord2f(0, 1); glVertex2f(-1,  1);
  glEnd();
  impl->shader->Unapply();
  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  glPopMatrix();

  // Async ping-pong readback (see Impl::pbo): queue THIS frame's read into
  // pbo[idx] (returns immediately), then map the OTHER pbo — last frame's
  // pixels, long since DMA'd — into small_. First frame after (re)init has no
  // prior data: skip the filter entirely for that one frame.
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  const size_t smallBytes = (size_t) pw * ph * 4;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, impl->pbo[impl->pboIdx]);
  glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  impl->pboIdx ^= 1;
  bool haveData = impl->pboPrimed;
  if (haveData) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, impl->pbo[impl->pboIdx]);
    if (void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY)) {
      std::memcpy(impl->small_.data(), ptr, smallBytes);
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    } else {
      haveData = false;
    }
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  impl->pboPrimed = true;

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
  glDrawBuffer(GL_BACK);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!haveData) return;                    // state restored; effect resumes next frame

  // 2. settings: reload only when they changed (JSON parse is not per-frame).
  //    A user preset wins; otherwise noise/hue derive an overlay (see above).
  if (p.presetRev != impl->lastRev) {
    impl->lastRev = p.presetRev;
    const std::string json = p.preset.empty() ? overlayJson(p) : p.preset;
    if (ntscrs_load_json(impl->rs, json.c_str()) != 0)
      std::fprintf(stderr, "[fluxus] ntsc: bad preset JSON (using defaults)\n");
  }

  // 3. the ntsc-rs signal pass at the internal NTSC resolution, in place.
  //    `field` doubles as the frame counter driving noise animation and phase.
  ntscrs_process(impl->rs, impl->small_.data(), pw, ph, pw * 4, field++, /*flip_y=*/1);

  // 4. monitor post pass (ntsc-rs models the signal, not the monitor):
  //    saturation/brightness/contrast/monochrome per pixel, scanline darkening,
  //    and optional 50% blend with the previous output frame (VHS-ish lag).
  //    Runs on the SMALL buffer — scanlines land at the 480-line raster scale.
  {
    const float sat = p.monochrome ? 0.0f : (float) p.saturation / 10.0f;
    const float con = (float) p.contrast / 180.0f;
    const float bri = (float) p.brightness;
    const bool  identity = !p.scanlines && !p.blend && !p.monochrome &&
                           p.saturation == 10 && p.brightness == 0 && p.contrast == 180;
    if (!identity) {
      unsigned char* px = impl->small_.data();
      unsigned char* pv = out.data();
      const bool doBlend = p.blend && impl->havePrev;
      for (int y = 0; y < ph; ++y) {
        // buffer is bottom-up; darken every other raster line
        const float scan = (p.scanlines && (y & 1)) ? 0.65f : 1.0f;
        for (int x = 0; x < pw; ++x, px += 4, pv += 4) {
          float r = px[0], g = px[1], b = px[2];
          const float l = 0.299f * r + 0.587f * g + 0.114f * b;
          r = l + (r - l) * sat; g = l + (g - l) * sat; b = l + (b - l) * sat;
          r = ((r - 128.0f) * con + 128.0f + bri) * scan;
          g = ((g - 128.0f) * con + 128.0f + bri) * scan;
          b = ((b - 128.0f) * con + 128.0f + bri) * scan;
          if (doBlend) { r = (r + pv[0]) * 0.5f; g = (g + pv[1]) * 0.5f; b = (b + pv[2]) * 0.5f; }
          px[0] = (unsigned char) std::clamp(r, 0.0f, 255.0f);
          px[1] = (unsigned char) std::clamp(g, 0.0f, 255.0f);
          px[2] = (unsigned char) std::clamp(b, 0.0f, 255.0f);
        }
      }
    }
    if (p.blend) { out = impl->small_; impl->havePrev = true; }
  }

  // 5. blit the small result back over the full screen through the passthrough
  //    shader (LINEAR upscale). Select unit 0 FIRST — the shader samples `tex`
  //    on unit 0, and the renderer may leave a different unit active.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, impl->small_.data());

  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);                      // replace the screen, no accumulation
  glEnable(GL_TEXTURE_2D);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

  impl->shader->Apply();
  impl->shader->SetInt("tex", 0);
  glColor4f(1, 1, 1, 1);
  glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f( 1, -1);
    glTexCoord2f(1, 1); glVertex2f( 1,  1);
    glTexCoord2f(0, 1); glVertex2f(-1,  1);
  glEnd();
  impl->shader->Unapply();

  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  glPopMatrix();
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
}
