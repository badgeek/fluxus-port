// SPDX-License-Identifier: AGPL-3.0-or-later
// GPU domain of the fluxus command layer: GLSL shader binding (vert/geom/frag +
// uniforms), the ping-pong FBO GPU-particle system, and the (build-pixels)
// primitive with its writable texture. The pixels table lives here; core reaches
// it through pixelsErase (on destroy) and pdata through pixelsCount.
#include "FluxusCommands.h"
#include "FluxusCommandsInternal.h"

// engine + system GL only
#include "PolyPrimitive.h"
#include "ParticlePrimitive.h"
#include "GraphicsUtils.h"
#include "State.h"
#include "GLSLShader.h"
#include "dada.h"
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>   // EXT_framebuffer_object + RGBA32F for GPU particle ping-pong

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace Fluxus;

namespace {
// source->shader cache: compile a GLSL program once, reuse it across the per-frame
// re-evals (immediate-mode would otherwise recompile every frame). Keyed by
// vertex+fragment source; each cached shader holds one ref for the session.
std::map<std::string, GLSLShader*> g_shaderCache;

// the shader a script command currently targets: grabbed prim's, else build ctx.
GLSLShader* currentShader() {
  if (g_ctx.grabbed) return g_ctx.grabbed->GetState()->Shader;
  return g_ctx.shader;
}

// pixels primitives: plane id -> {GL texture, dims} for (build-pixels)/(pixels-upload)
struct PixBuf { unsigned tex = 0; int w = 0, h = 0; };
std::map<Primitive*, PixBuf> g_pixels;
} // namespace

// (destroy id) on a pixels prim: drop the entry. Its GL texture is left to GL
// teardown, as it always has been.
void pixelsErase(Primitive* p) { g_pixels.erase(p); }
// a pixels prim's pdata "c" is its w*h pixel buffer, not its vertex count.
int pixelsCount(Primitive* p) {
  auto it = g_pixels.find(p);
  return it == g_pixels.end() ? 0 : it->second.w * it->second.h;
}

extern "C" {

int flux_build_pixels(int w, int h) {
  if (w < 1) w = 1; if (h < 1) h = 1;
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::QUADS);
  MakePlane(p, 1, 1);                                   // unit quad with 0..1 texcoords
  // MakePlane already made a per-vertex "c" (size 4); repurpose it as the w*h pixel
  // buffer (it isn't used as vertex colour — the texture provides the colour).
  if (PData* cd = p->GetDataRaw("c")) cd->Resize((unsigned) (w * h));
  int id = addPrim(p);

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  g_pixels[p] = PixBuf{ tex, w, h };
  State* s = p->GetState();
  s->Textures[0] = tex;
  setStateShader(s, builtinTexShader());
  return id;
}
void flux_pixels_upload(void) {
  Primitive* p = g_ctx.grabbed;
  if (!p) return;
  auto it = g_pixels.find(p);
  if (it == g_pixels.end()) return;
  const PixBuf& pb = it->second;
  const unsigned n = (unsigned) (pb.w * pb.h);
  std::vector<unsigned char> buf((size_t) n * 4);
  for (unsigned i = 0; i < n; ++i) {
    const dColour c = p->GetData<dColour>("c", i);
    unsigned char* px = &buf[(size_t) i * 4];
    auto b = [](float v){ return (unsigned char) (v < 0 ? 0 : v > 1 ? 255 : (int) (v * 255.0f + 0.5f)); };
    px[0] = b(c.r); px[1] = b(c.g); px[2] = b(c.b); px[3] = b(c.a);
  }
  glBindTexture(GL_TEXTURE_2D, pb.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pb.w, pb.h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}
int flux_pixels_width(void)  { auto it = g_pixels.find(g_ctx.grabbed); return it == g_pixels.end() ? 0 : it->second.w; }
int flux_pixels_height(void) { auto it = g_pixels.find(g_ctx.grabbed); return it == g_pixels.end() ? 0 : it->second.h; }

// ---- GLSL shaders ----------------------------------------------------------
void flux_shader_source(const char* vert, const char* frag) {
  if (!vert || !frag) return;
  GLSLShader::Init();   // set m_Enabled BEFORE compiling (render normally does this later)
  std::string key = std::string(vert) + "\n---\n" + frag;
  GLSLShader* sh;
  auto it = g_shaderCache.find(key);
  if (it != g_shaderCache.end()) sh = it->second;
  else {
    GLSLShaderPair pair(false, vert, frag);   // compile from source
    sh = new GLSLShader(pair);                // shares the compiled program
    g_shaderCache[key] = sh;                  // one session-held ref
  }
  if (State* s = grabbedState()) setStateShader(s, sh);   // grabbed prim
  else                           g_ctx.shader = sh;       // next-built prims
}

// fluxus->JUCE port: vertex + GEOMETRY + fragment shader on the grabbed prim.
// gin/gout are GL primitive enums (in: 1=GL_LINES, 4=GL_TRIANGLES, 0=GL_POINTS;
// out: 3=GL_LINE_STRIP, 5=GL_TRIANGLE_STRIP, 0=GL_POINTS), gverts = max emitted.
void flux_shader_source_geom(const char* vert, const char* geom, const char* frag,
                             int gin, int gout, int gverts) {
  if (!vert || !geom || !frag) return;
  GLSLShader::Init();
  std::string key = "GEOM\n" + std::string(vert) + "\n-g-\n" + geom + "\n-f-\n" + frag +
                    "\n" + std::to_string(gin) + "," + std::to_string(gout) + "," + std::to_string(gverts);
  GLSLShader* sh;
  auto it = g_shaderCache.find(key);
  if (it != g_shaderCache.end()) sh = it->second;
  else {
    GLSLShaderPair pair(false, vert, geom, frag, gin, gout, gverts);
    sh = new GLSLShader(pair);
    g_shaderCache[key] = sh;
  }
  if (State* s = grabbedState()) setStateShader(s, sh);
  else                           g_ctx.shader = sh;
}

} // extern "C"

// ============================================================================
// GPU particle system: ping-pong RGBA32F FBO state + vertex-texture-fetch draw.
// State (pos.xyz, age) lives in float textures; an update fragment shader advances
// ALL particles in parallel each frame (render into the back texture reading the
// front, then swap). A W*H GL_POINTS prim draws them — each vertex stores its texel
// coord in gl_Vertex.xy and the DRAW vertex shader FETCHES its state from the front
// texture (VTF), so W*H particles (e.g. 256*256 = 65536) live entirely on the GPU.
namespace {
struct GPUParticles {
  int w = 0, h = 0; int mode = 0;   // mode 0 = points, 1 = velocity streaks (MRT pos+vel)
  GLuint pos[2] = {0,0}; GLuint vel[2] = {0,0}; GLuint fbo = 0; int front = 0;
  GLuint spawnTex = 0;   // optional spawn-position/mask texture (unit 2 in the update)
  GLSLShader* updateProg = nullptr; std::string updateSrc;
  GLSLShader* drawProg = nullptr;
  Primitive* draw = nullptr; int drawId = -1;
  std::map<std::string,float> uniforms;
};
GPUParticles* g_gpu = nullptr;
} // namespace

#ifndef GL_COLOR_ATTACHMENT1_EXT
#define GL_COLOR_ATTACHMENT1_EXT 0x8CE1
#endif

namespace {
const char* kGpuQuadVS =
  "varying vec2 vUV;\n"
  "void main(){ vUV = gl_MultiTexCoord0.xy; gl_Position = gl_Vertex; }\n";

GLuint gpuMakeStateTex(int w, int h) {
  GLuint t = 0; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

// render a fullscreen quad through `prog` into targetTex, inputTex bound to unit 0.
void gpuRunPass(GPUParticles* g, GLuint targetTex, GLuint inputTex, GLSLShader* prog) {
  GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g->fbo);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, targetTex, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);   // route fragment output to the attachment
  glViewport(0, 0, g->w, g->h);
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_LIGHTING);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, inputTex);
  prog->Apply();
  prog->SetInt("u_state", 0);
  for (auto& kv : g->uniforms) prog->SetFloat(kv.first, kv.second);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
  glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(-1,-1); glTexCoord2f(1,0); glVertex2f(1,-1);
    glTexCoord2f(1,1); glVertex2f( 1, 1); glTexCoord2f(0,1); glVertex2f(-1,1);
  glEnd();
  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  glPopMatrix();
  GLSLShader::Unapply();
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
  glDrawBuffer(GL_BACK);                     // restore the default framebuffer draw target
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glEnable(GL_DEPTH_TEST);
}
} // namespace

extern "C" {

int flux_gpu_build(int w, int h, const char* initFrag, int mode) {
  if (w < 1) w = 1; if (h < 1) h = 1;
  GLSLShader::Init();
  GPUParticles* g = new GPUParticles();
  g->w = w; g->h = h; g->mode = mode;
  g->pos[0] = gpuMakeStateTex(w, h); g->pos[1] = gpuMakeStateTex(w, h);
  g->vel[0] = gpuMakeStateTex(w, h); g->vel[1] = gpuMakeStateTex(w, h);   // MRT velocity target
  glGenFramebuffersEXT(1, &g->fbo);
  int id;
  if (mode == 1) {   // velocity STREAKS: a LINES prim, 2 verts (head + tail) per particle
    PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::LINES);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
      dVector tc((x + 0.5f) / (float) w, (y + 0.5f) / (float) h, 0);
      p->AddVertex(dVertex(tc, dVector(0,0,1), 0, 0));
      p->AddVertex(dVertex(tc, dVector(0,0,1), 0, 0));
    }
    id = addPrim(p); g->draw = p;
    // AFTER addPrim — it copies the renderer's build state over the prim's, which
    // would otherwise drop VERTCOLS/UNLIT and leave the default HINT_SOLID. The
    // segments ARE the solid pass here (PolyPrimitive maps LINES -> GL_LINES), so
    // keep HINT_SOLID and let the per-vertex colours through it.
    State* s = p->GetState();
    s->Hints = HINT_SOLID | HINT_UNLIT | HINT_VERTCOLS;
    s->LineWidth = 1.6f;
  } else {           // POINTS: one GL_POINTS vertex per texel
    ParticlePrimitive* p = new ParticlePrimitive();
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
      p->AddParticle(dVector((x + 0.5f) / (float) w, (y + 0.5f) / (float) h, 0),
                     dColour(1,1,1,1), dVector(0.1f, 0.1f, 0));
    id = addPrim(p); g->draw = p;
    State* s = p->GetState(); s->Hints = HINT_POINTS | HINT_UNLIT;   // after addPrim, as above
  }
  g->drawId = id;
  if (initFrag && *initFrag) {   // seed the initial position/age into pos[0]
    GLSLShaderPair pair(false, kGpuQuadVS, initFrag);
    GLSLShader* initProg = new GLSLShader(pair);
    gpuRunPass(g, g->pos[0], g->pos[1], initProg);
    delete initProg;
  }
  g->front = 0;
  g_gpu = g;   // one system per session (previous, if any, leaks — session-lived)
  return id;
}

void flux_gpu_uniform(const char* name, double v) {
  if (g_gpu && name) g_gpu->uniforms[name] = (float) v;
}

// use a (build-pixels) primitive's texture as the spawn source: the update shader
// samples u_spawnTex (unit 2) to decide where reborn particles appear (image/mask).
void flux_gpu_spawn_from_pixels(int pixId) {
  if (!g_gpu || !g_ctx.r) return;
  Primitive* p = g_ctx.r->GetPrimitive(pixId);
  auto it = g_pixels.find(p);
  g_gpu->spawnTex = (it != g_pixels.end()) ? it->second.tex : 0;
}

void flux_gpu_update(const char* updateFrag) {
  if (!g_gpu || !updateFrag) return;
  GPUParticles* g = g_gpu;
  if (!g->updateProg || g->updateSrc != updateFrag) {
    GLSLShaderPair pair(false, kGpuQuadVS, updateFrag);
    g->updateProg = new GLSLShader(pair);   // old leaks (source rarely changes)
    g->updateSrc = updateFrag;
  }
  const int back = 1 - g->front;
  // --- MRT update pass: advance ALL particles, writing pos+age AND velocity -----
  GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g->fbo);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, g->pos[back], 0);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, g->vel[back], 0);
  GLenum bufs[2] = { GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT };
  glDrawBuffers(2, bufs);
  glViewport(0, 0, g->w, g->h);
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_LIGHTING);
  if (g->spawnTex) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g->spawnTex); }
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g->vel[g->front]);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g->pos[g->front]);
  g->updateProg->Apply();
  g->updateProg->SetInt("u_state", 0); g->updateProg->SetInt("u_vel", 1); g->updateProg->SetInt("u_spawnTex", 2);
  for (auto& kv : g->uniforms) g->updateProg->SetFloat(kv.first, kv.second);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
  glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(-1,-1); glTexCoord2f(1,0); glVertex2f(1,-1);
    glTexCoord2f(1,1); glVertex2f( 1, 1); glTexCoord2f(0,1); glVertex2f(-1,1);
  glEnd();
  glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
  GLSLShader::Unapply();
  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
  g->front = back;

  // --- read the updated state back (Apple can't VTF a float texture) ------------
  const int n = g->w * g->h;
  static std::vector<float> pbuf, vbuf; pbuf.resize((size_t) n * 4); vbuf.resize((size_t) n * 4);
  glReadBuffer(GL_COLOR_ATTACHMENT0_EXT); glReadPixels(0, 0, g->w, g->h, GL_RGBA, GL_FLOAT, pbuf.data());
  if (g->mode == 1) { glReadBuffer(GL_COLOR_ATTACHMENT1_EXT); glReadPixels(0, 0, g->w, g->h, GL_RGBA, GL_FLOAT, vbuf.data()); }
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
  glDrawBuffer(GL_BACK);
  glViewport(vp[0], vp[1], vp[2], vp[3]); glEnable(GL_DEPTH_TEST);

  // --- fill the draw prim's pdata -----------------------------------------------
  auto uni = [&](const char* k, float d) { auto it = g->uniforms.find(k); return it != g->uniforms.end() ? it->second : d; };
  const float maxAge = uni("u_maxAge", 9.0f);
  const float yr = uni("u_youngR", 0.45f), yg = uni("u_youngG", 1.0f), yb = uni("u_youngB", 1.0f);
  const float orr = uni("u_oldR", 0.10f),  og = uni("u_oldG", 0.45f), ob = uni("u_oldB", 1.0f);
  const float alpha = uni("u_alpha", 0.08f);
  const float streak = uni("u_streak", 0.06f);   // streak length as a fraction of velocity
  for (int i = 0; i < n; ++i) {
    const float* q = &pbuf[(size_t) i * 4];
    float f = 1.0f - std::min(1.0f, std::max(0.0f, q[3] / maxAge));   // 1 young -> 0 old
    dColour col(orr + (yr - orr) * f, og + (yg - og) * f, ob + (yb - ob) * f, alpha * (0.15f + 0.85f * f));
    if (g->mode == 1) {
      const float* vv = &vbuf[(size_t) i * 4];
      dVector head(q[0], q[1], q[2]);
      dVector tail(q[0] - vv[0] * streak, q[1] - vv[1] * streak, q[2] - vv[2] * streak);
      g->draw->SetData<dVector>("p", 2 * i,     head);
      g->draw->SetData<dVector>("p", 2 * i + 1, tail);
      g->draw->SetData<dColour>("c", 2 * i,     col);
      g->draw->SetData<dColour>("c", 2 * i + 1, dColour(col.r, col.g, col.b, 0.0f));   // fade to the tail
    } else {
      g->draw->SetData<dVector>("p", i, dVector(q[0], q[1], q[2]));
      g->draw->SetData<dColour>("c", i, col);
    }
  }
  g->draw->BumpPDataVersion();
  if (g->drawProg) {   // keep the draw shader's uniforms (uSize, …) current
    g->drawProg->Apply();
    for (auto& kv : g->uniforms) g->drawProg->SetFloat(kv.first, kv.second);
    GLSLShader::Unapply();
  }
}

void flux_gpu_draw_shaders(const char* vert, const char* geom, const char* frag,
                           int gin, int gout, int gverts) {
  if (!g_gpu || !vert || !frag) return;
  GLSLShader* prog;
  if (geom && *geom) { GLSLShaderPair pr(false, vert, geom, frag, gin, gout, gverts); prog = new GLSLShader(pr); }
  else               { GLSLShaderPair pr(false, vert, frag); prog = new GLSLShader(pr); }
  prog->Apply(); prog->SetInt("u_state", 0); GLSLShader::Unapply();
  g_gpu->drawProg = prog;
  setStateShader(g_gpu->draw->GetState(), prog);
}

void flux_shader_clear(void) {
  if (State* s = grabbedState()) setStateShader(s, nullptr);
  else                           g_ctx.shader = nullptr;
}
void flux_shader_set_float(const char* name, double v) {
  GLSLShader* sh = currentShader();
  if (sh && name) { sh->Apply(); sh->SetFloat(name, (float) v); }
}
void flux_shader_set_vec(const char* name, double x, double y, double z) {
  GLSLShader* sh = currentShader();
  if (sh && name) { sh->Apply(); sh->SetVector(name, dVector((float) x, (float) y, (float) z), 3); }
}
void flux_shader_set_int(const char* name, int v) {
  GLSLShader* sh = currentShader();
  if (sh && name) { sh->Apply(); sh->SetInt(name, v); }
}
void flux_blend_mode(int src, int dst) {
  if (State* s = grabbedState()) { s->SourceBlend = src; s->DestinationBlend = dst; }
  else { g_ctx.srcBlend = src; g_ctx.dstBlend = dst; }
}
void flux_multitexture(int unit, int id) {
  if (unit < 0 || unit >= 8) return;
  if (State* s = grabbedState()) s->Textures[(unsigned) unit] = (unsigned) id;
}

} // extern "C"
