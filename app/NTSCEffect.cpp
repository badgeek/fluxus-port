// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NTSCEffect.h"
#include "FluxusCommands.h"    // NtscParams

#include "GLSLShader.h"
#include "dada.h"

#include <OpenGL/gl.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <pthread/qos.h>

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

// Monitor post pass on the GPU (was a per-pixel C++ loop): saturation/
// brightness/contrast/monochrome, scanline darkening on odd raster lines, and
// the optional 50% blend with the PREVIOUS monitor output (a real IIR trail —
// prevTex is last frame's post-monitor result, so the feedback chain matches
// the old CPU version). Runs at the small pw*ph res into an FBO, so scanlines
// land at the 480-line raster scale exactly as before.
static const char* kMonFrag =
  "uniform sampler2D tex;\n"       // filtered current frame (unit 0)
  "uniform sampler2D prevTex;\n"   // previous monitor output (unit 1)
  "uniform float sat;\n"
  "uniform float con;\n"
  "uniform float bri;\n"           // brightness/255
  "uniform float scan;\n"          // 1 = darken odd rows
  "uniform float blendAmt;\n"      // 0 or 0.5
  "varying vec2 uv;\n"
  "void main() {\n"
  "  vec3 c = texture2D(tex, uv).rgb;\n"
  "  float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
  "  c = l + (c - l) * sat;\n"
  "  c = (c - 0.5) * con + 0.5 + bri;\n"
  "  if (scan > 0.5 && mod(floor(gl_FragCoord.y), 2.0) >= 1.0) c *= 0.65;\n"
  "  c = clamp(c, 0.0, 1.0);\n"
  "  c = mix(c, texture2D(prevTex, uv).rgb, blendAmt);\n"
  "  gl_FragColor = vec4(c, 1.0);\n"
  "}\n";

struct NTSCEffect::Impl {
  void*       rs = nullptr;        // ntscrs handle (Context + settings + scratch)
  GLSLShader* shader = nullptr;    // passthrough blit
  GLSLShader* monShader = nullptr; // monitor post pass (kMonFrag)
  int         lastRev = -1;        // NtscParams::presetRev last loaded
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
  // Monitor pass ping-pong targets (pw x ph): the pass renders into
  // monTex[monIdx] while sampling monTex[1-monIdx] as the previous output
  // (blend feedback). Skipped entirely when the monitor settings are identity.
  unsigned int monTex[2] = {0, 0};
  int          monIdx = 0;
  bool         monPrimed = false;  // monTex[1-idx] holds a valid prior frame
  // Filter pipeline thread. The ntsc-rs pass runs on OUR thread, created with
  // QoS USER_INTERACTIVE set as its first act — a plain std::thread accepts
  // it, whereas JUCE's GL render thread is opted out of QoS (explicit sched
  // policy), so in a LaunchServices-launched app ('open'/Finder) the scheduler
  // parks the busy render thread on an E-core: the SAME filter then takes
  // ~2.5-3x the wall time (3.9 -> ~12 ms/frame, app ~20% -> ~40% CPU;
  // terminal-child launches inherit an interactive policy and never show it).
  // Pipelining also frees the GL thread of the ~4 ms filter wait. Costs one
  // extra frame of effect latency (2 total with the PBO readback).
  std::thread worker;
  std::mutex  m;
  std::condition_variable cv;
  std::vector<unsigned char> jobBuf;    // GL -> worker (raw small frame)
  std::vector<unsigned char> resBuf;    // worker -> GL (filtered)
  std::vector<unsigned char> uploadBuf; // GL-side copy being uploaded/drawn
  std::string pendingJson;              // settings JSON for the worker
  bool jsonDirty = false;
  int  jobW = 0, jobH = 0, jobField = 0;
  int  resW = 0, resH = 0;              // dimensions of resBuf's content
  int  upW = 0, upH = 0;                // dimensions of uploadBuf's content
  bool jobPending = false, resultReady = false, uploadValid = false, stop = false;
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
    if (impl->worker.joinable()) {
      { std::lock_guard<std::mutex> lk(impl->m); impl->stop = true; }
      impl->cv.notify_one();
      impl->worker.join();
    }
    if (impl->rs) ntscrs_free(impl->rs);
    if (impl->shader && impl->shader->DecRef()) delete impl->shader;
    if (impl->monShader && impl->monShader->DecRef()) delete impl->monShader;
    if (impl->fullTex) glDeleteTextures(1, &impl->fullTex);
    if (impl->readTex) glDeleteTextures(1, &impl->readTex);
    if (impl->monTex[0]) glDeleteTextures(2, impl->monTex);
    if (impl->fbo)     glDeleteFramebuffersEXT(1, &impl->fbo);
    if (impl->pbo[0])  glDeleteBuffers(2, impl->pbo);
    delete impl;
    impl = nullptr;
  }
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
  mkTex(impl->monTex[0], impl->pw, impl->ph);
  mkTex(impl->monTex[1], impl->pw, impl->ph);
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

  // filter pipeline worker (see Impl) — after this point `rs` is used ONLY by
  // the worker (ntscrs_load_json + ntscrs_process both moved there).
  if (!impl->worker.joinable()) {
    Impl* im = impl;
    impl->worker = std::thread([im] {
      // Best effort; in a LaunchServices-launched (role `ui`) app the energy
      // policy still parks this thread on an E-core — ~2.5x the CPU time at
      // identical fps. Not fixable app-side (see AppActivity.mm for the full
      // list of levers tried); harmless and correct where the role permits.
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
      std::vector<unsigned char> work;
      for (;;) {
        std::string json;
        int fw, fh, ff;
        {
          std::unique_lock<std::mutex> lk(im->m);
          im->cv.wait(lk, [im] { return im->jobPending || im->stop; });
          if (im->stop) return;
          work.swap(im->jobBuf);
          fw = im->jobW; fh = im->jobH; ff = im->jobField;
          if (im->jsonDirty) { json = im->pendingJson; im->jsonDirty = false; }
        }
        if (!json.empty() && ntscrs_load_json(im->rs, json.c_str()) != 0)
          std::fprintf(stderr, "[fluxus] ntsc: bad preset JSON (using defaults)\n");
        ntscrs_process(im->rs, work.data(), fw, fh, fw * 4, ff, /*flip_y=*/1);
        {
          std::lock_guard<std::mutex> lk(im->m);
          im->resBuf.swap(work);
          im->resW = fw; im->resH = fh;
          im->resultReady = true;
          im->jobPending = false;
        }
      }
    });
  }

  // resize: drop any stale pipeline output (its dimensions no longer match)
  {
    std::lock_guard<std::mutex> lk(impl->m);
    impl->resultReady = false;
    impl->uploadValid = false;
  }

  if (!impl->shader) {
    GLSLShader::Init();                              // enable GLSL before compiling
    GLSLShaderPair pair(false, kVert, kFrag);        // compile from source
    impl->shader = new GLSLShader(pair);
  }
  if (!impl->monShader) {
    GLSLShaderPair pair(false, kVert, kMonFrag);
    impl->monShader = new GLSLShader(pair);
  }
  impl->monPrimed = false;                           // stale prev after a resize
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
    if (impl->small_.size() != smallBytes)     // swapped away by the pipeline handoff
      impl->small_.resize(smallBytes);
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

  // 2+3. hand the raw small frame to the pipeline worker and pick up its
  //    previous result (see Impl::worker — the ntsc-rs pass runs off-thread at
  //    USER_INTERACTIVE QoS). Settings changes travel as JSON with the job;
  //    `field` doubles as the frame counter driving noise animation and phase.
  //    If the worker is somehow still busy (never at 25 fps), drop this input.
  {
    std::lock_guard<std::mutex> lk(impl->m);
    if (p.presetRev != impl->lastRev) {
      impl->lastRev = p.presetRev;
      impl->pendingJson = p.preset.empty() ? overlayJson(p) : p.preset;
      impl->jsonDirty = true;
    }
    if (!impl->jobPending) {
      if (impl->resultReady) {
        impl->uploadBuf.swap(impl->resBuf);
        impl->upW = impl->resW; impl->upH = impl->resH;
        impl->resultReady = false;
        impl->uploadValid = true;
      }
      if (haveData) {
        impl->jobBuf.swap(impl->small_);
        impl->jobW = pw; impl->jobH = ph; impl->jobField = field++;
        impl->jobPending = true;
        impl->cv.notify_one();
      }
    }
  }
  // nothing filtered yet (startup / right after a resize): scene stays as-is
  if (!impl->uploadValid || impl->upW != pw || impl->upH != ph) return;

  // 4. upload the filtered small frame, then the GPU monitor post pass
  //    (ntsc-rs models the signal, not the monitor): saturation/brightness/
  //    contrast/monochrome, scanline darkening, optional 50% blend with the
  //    previous OUTPUT frame (IIR trail — see kMonFrag). Renders at the small
  //    res into monTex[monIdx] so scanlines land at the 480-line raster scale;
  //    identity settings skip the pass (and its FBO round-trip) entirely.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, impl->uploadBuf.data());

  const bool identity = !p.scanlines && !p.blend && !p.monochrome &&
                        p.saturation == 10 && p.brightness == 0 && p.contrast == 180;
  unsigned int blitSrc = tex;                        // what step 5 upscales
  if (!identity && impl->monShader && impl->monShader->IsValid()) {
    glActiveTexture(GL_TEXTURE1);                    // previous monitor output
    glBindTexture(GL_TEXTURE_2D, impl->monTex[1 - impl->monIdx]);
    glActiveTexture(GL_TEXTURE0);                    // filtered current frame

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, impl->fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                              GL_TEXTURE_2D, impl->monTex[impl->monIdx], 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0, 0, pw, ph);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    impl->monShader->Apply();
    impl->monShader->SetInt("tex", 0);
    impl->monShader->SetInt("prevTex", 1);
    impl->monShader->SetFloat("sat", p.monochrome ? 0.0f : (float) p.saturation / 10.0f);
    impl->monShader->SetFloat("con", (float) p.contrast / 180.0f);
    impl->monShader->SetFloat("bri", (float) p.brightness / 255.0f);
    impl->monShader->SetFloat("scan", p.scanlines ? 1.0f : 0.0f);
    impl->monShader->SetFloat("blendAmt", (p.blend && impl->monPrimed) ? 0.5f : 0.0f);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
      glTexCoord2f(0, 0); glVertex2f(-1, -1);
      glTexCoord2f(1, 0); glVertex2f( 1, -1);
      glTexCoord2f(1, 1); glVertex2f( 1,  1);
      glTexCoord2f(0, 1); glVertex2f(-1,  1);
    glEnd();
    impl->monShader->Unapply();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    glDrawBuffer(GL_BACK);

    blitSrc = impl->monTex[impl->monIdx];
    impl->monIdx ^= 1;
    impl->monPrimed = true;
  }

  // 5. blit the small result back over the full screen through the passthrough
  //    shader (LINEAR upscale). Select unit 0 FIRST — the shader samples `tex`
  //    on unit 0, and the renderer may leave a different unit active.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, blitSrc);

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
  glActiveTexture(GL_TEXTURE1);            // don't leak the prev-frame binding
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
}
