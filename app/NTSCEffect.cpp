// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NTSCEffect.h"
#include "FluxusCommands.h"    // NtscParams

#include "GLSLShader.h"
#include "dada.h"

#include <OpenGL/gl.h>

extern "C" {
#include "crt_core.h"          // LMP88959/NTSC-CRT (CRT_SYSTEM_NTSC)
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

// The two C structs kept out of the header so the NTSC-CRT lib doesn't leak into
// GL-free translation units. CRT::analog/inp are ~0.5 MB, so heap-allocate once.
struct NTSCEffect::Impl {
  CRT           crt;
  NTSC_SETTINGS ntsc;    // zeroed here; the lib maintains iirs_initialized in it
  GLSLShader*   shader = nullptr;
};

NTSCEffect::~NTSCEffect() { release(); }

void NTSCEffect::release() {
  if (tex) { glDeleteTextures(1, &tex); tex = 0; }
  if (impl) {
    if (impl->shader && impl->shader->DecRef()) delete impl->shader;
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
  in.assign((size_t) w * h * 4, 0);
  out.assign((size_t) w * h * 4, 0);

  if (!tex) glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!impl) impl = new Impl();
  if (!impl->shader) {
    GLSLShader::Init();                              // enable GLSL before compiling
    GLSLShaderPair pair(false, kVert, kFrag);        // compile from source
    impl->shader = new GLSLShader(pair);
  }
  // crt_init memsets the CRT, resizes it, resets the monitor knobs and seeds
  // noise. `out.data()` is stable until the next ensure(), so this repoints the
  // CRT output at the fresh buffer on every resolution change.
  crt_init(&impl->crt, w, h, CRT_PIX_FORMAT_RGBA, out.data());
  return true;
}

void NTSCEffect::apply(int W, int H, const NtscParams& p) {
  if (!ensure(W, H)) return;
  if (!impl->shader || !impl->shader->IsValid()) return;  // no blit path -> leave scene as-is

  // 1. pull the finished frame off the default framebuffer. glReadPixels is
  //    bottom-up; we keep that all the way through and draw it back bottom-up,
  //    so the orientation round-trips (row 0 == screen bottom the whole time).
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, in.data());

  // 2. monitor knobs on the CRT; signal hue/colour on the field settings.
  CRT& crt = impl->crt;
  crt.saturation = p.saturation;
  crt.brightness = p.brightness;
  crt.contrast   = p.contrast;
  crt.scanlines  = p.scanlines ? 1 : 0;
  crt.blend      = p.blend ? 1 : 0;

  NTSC_SETTINGS& ntsc = impl->ntsc;
  ntsc.data     = in.data();
  ntsc.format   = CRT_PIX_FORMAT_RGBA;
  ntsc.w        = w;
  ntsc.h        = h;
  ntsc.raw      = 0;                       // scale the image to fit the raster
  ntsc.as_color = p.monochrome ? 0 : 1;
  ntsc.hue      = p.hue;
  ntsc.field    = field & 1;
  if (ntsc.field == 0) ntsc.frame ^= 1;    // advance the frame on the even field

  crt_modulate(&crt, &ntsc);               // RGB -> analog composite signal
  crt_demodulate(&crt, p.noise);           // signal -> RGB in `out`
  field ^= 1;

  // 3. blit `out` back over the screen through the passthrough shader.
  //    Select unit 0 FIRST — the shader samples `tex` on unit 0, and the renderer
  //    may leave a different unit active, so bind + upload must target unit 0 too.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());

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
