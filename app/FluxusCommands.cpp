#include "FluxusCommands.h"

// engine + system GL only
#include "Renderer.h"
#include "PolyPrimitive.h"
#include "RibbonPrimitive.h"
#include "ParticlePrimitive.h"
#include "LocatorPrimitive.h"
#include "TextPrimitive.h"
#include "NURBSPrimitive.h"
#include <OpenGL/gl.h>
#include "GraphicsUtils.h"
#include "Camera.h"
#include "State.h"
#include "Light.h"
#include "SceneGraph.h"
#include "GLSLShader.h"
#include "dada.h"

#include <vector>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <atomic>

using namespace Fluxus;

namespace {
struct BuildCtx {
  Renderer* r = nullptr;
  dMatrix   tx;
  dColour   col{1, 1, 1, 1};
  std::vector<std::pair<dMatrix, dColour>> stack;
  double    time  = 0.0;
  int       frame = 0;
  int       hints = 0;      // extra State hints OR'd onto built prims
  float     lineWidth = 2.0f;
  Primitive* grabbed = nullptr;   // current pdata target
  GLSLShader* shader = nullptr;   // current shader for newly built prims (not owned)
  int         parent = -1;        // parent id for newly built prims (-1 = root)
  unsigned    texture = 0;        // GL texture id for newly built prims (0 = none)
  int         srcBlend = GL_SRC_ALPHA;           // blend factors for newly built prims
  int         dstBlend = GL_ONE_MINUS_SRC_ALPHA;
};
BuildCtx g_ctx;

// source->shader cache: compile a GLSL program once, reuse it across the per-frame
// re-evals (immediate-mode would otherwise recompile every frame). Keyed by
// vertex+fragment source; each cached shader holds one ref for the session.
std::map<std::string, GLSLShader*> g_shaderCache;

// swap a State's shader with correct refcounting (State DecRefs/deletes at 0).
void setStateShader(State* s, GLSLShader* sh) {
  if (!s) return;
  if (s->Shader && s->Shader->DecRef()) delete s->Shader;
  s->Shader = sh;
  if (sh) sh->IncRef();
}
// the shader a script command currently targets: grabbed prim's, else build ctx.
GLSLShader* currentShader() {
  if (g_ctx.grabbed) return g_ctx.grabbed->GetState()->Shader;
  return g_ctx.shader;
}

// Built-in texturing shader. Apple's OpenGL-on-Metal layer ignores fixed-function
// texturing (glEnable(GL_TEXTURE_2D)), but GLSL sampler2D works — so a textured
// prim is drawn with this shader, sampling texture unit 0 (where the engine binds
// State.Textures[0]) with the mesh's texcoords, modulated by the vertex colour.
GLSLShader* builtinTexShader() {
  static GLSLShader* sh = nullptr;
  if (!sh) {
    GLSLShader::Init();
    const char* v =
      "varying vec2 uv;\n"
      "void main(){ uv = gl_MultiTexCoord0.xy; gl_FrontColor = gl_Color;\n"
      "  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n";
    const char* f =
      "uniform sampler2D tex;\n"
      "varying vec2 uv;\n"
      "void main(){ gl_FragColor = texture2D(tex, uv) * gl_Color; }\n";
    GLSLShaderPair pair(false, v, f);
    sh = new GLSLShader(pair);
    sh->Apply(); sh->SetInt("tex", 0); GLSLShader::Unapply();   // pin sampler to unit 0
  }
  return sh;
}

// Text shader: like the texturing shader but alpha-tests the glyph atlas so only
// the glyph coverage shows (no quad background), coloured by the vertex colour.
GLSLShader* builtinTextShader() {
  static GLSLShader* sh = nullptr;
  if (!sh) {
    GLSLShader::Init();
    const char* v =
      "varying vec2 uv;\n"
      "void main(){ uv = gl_MultiTexCoord0.xy; gl_FrontColor = gl_Color;\n"
      "  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n";
    const char* f =
      "uniform sampler2D tex;\n"
      "varying vec2 uv;\n"
      "void main(){ float a = texture2D(tex, uv).a;\n"
      "  if (a < 0.4) discard;\n"                       // glyph coverage only
      "  gl_FragColor = vec4(gl_Color.rgb, 1.0); }\n";
    GLSLShaderPair pair(false, v, f);
    sh = new GLSLShader(pair);
    sh->Apply(); sh->SetInt("tex", 0); GLSLShader::Unapply();
  }
  return sh;
}

// pixels primitives: plane id -> {GL texture, dims} for (build-pixels)/(pixels-upload)
struct PixBuf { unsigned tex = 0; int w = 0, h = 0; };
std::map<Primitive*, PixBuf> g_pixels;

std::mutex  g_errMutex;
std::string g_err;

std::mutex         g_audioMutex;
std::vector<float> g_bands;
double             g_gain = 0.0;

// persistent script state (GL thread only, but guard anyway)
std::mutex                                  g_stateMutex;
std::map<std::string, std::vector<double>>  g_state;

// mouse + orbit camera state (persists across frames)
struct CamState { double yaw = 0.3, pitch = 0.3, dist = 10.0; };
CamState g_cam;
double   g_mouseX = 0, g_mouseY = 0;
int      g_mouseButton = 0;

// script-driven camera: an optional transform override (scripts set it via
// set-camera-transform) that suppresses the mouse orbit, plus the last matrix
// actually applied (returned by get-camera-transform) and the pixel resolution
// the host feeds each frame (get-screen-size).
bool    g_camOverride = false;
dMatrix g_camOverrideMat;
dMatrix g_camAppliedMat;
int     g_screenW = 720, g_screenH = 576;
// aspect-ratio lock (0 = auto/off). When >0 the frustum is built for this w/h
// and the viewport is letterboxed (bars) so content keeps the AR as the window
// resizes. g_lastVfov remembers the vertical fov so a resize can rebuild the
// frustum even for scripts that don't call (set-fov) every frame.
double  g_aspectLock = 0.0, g_prevAspectLock = 0.0;
double  g_lastVfov   = 73.7397;   // = 2*atan(0.75) deg, the default frustum fov

Camera* cam0() {
  if (!g_ctx.r) return nullptr;
  auto& cams = g_ctx.r->GetCameraVec();
  return cams.empty() ? nullptr : &cams[0];
}

void applyCamera() {
  Camera* c = cam0();
  if (!c) return;
  dMatrix view;
  if (g_camOverride) {
    view = g_camOverrideMat;
  } else {
    dMatrix rot;  rot.rotxyz((float) g_cam.pitch, (float) g_cam.yaw, 0);
    dMatrix back; back.translate(0, 0, (float) -g_cam.dist);
    view = back * rot;                // rotate world, then push back from eye
  }
  c->SetMatrix(view);
  g_camAppliedMat = view;
}

int addPrim(Primitive* p) {
  if (!g_ctx.r) { delete p; return -1; }
  // AddPrimitive copies the renderer's current State into the prim, so set the
  // prim's state AFTER adding it.
  int id = g_ctx.r->AddPrimitive(p);
  State* s = p->GetState();
  s->Transform = g_ctx.tx;
  s->Colour    = g_ctx.col;
  s->Hints    |= g_ctx.hints;
  s->LineWidth = g_ctx.lineWidth;
  if (g_ctx.shader) setStateShader(s, g_ctx.shader);
  if (g_ctx.texture) s->Textures[0] = g_ctx.texture;
  s->SourceBlend = g_ctx.srcBlend;
  s->DestinationBlend = g_ctx.dstBlend;
  if (g_ctx.parent >= 0) g_ctx.r->GetSceneGraph().ReparentNode(id, g_ctx.parent);
  return id;
}
} // namespace

extern "C" {

void flux_set_renderer(void* renderer) { g_ctx.r = static_cast<Renderer*>(renderer); }

void flux_frame_begin(double t, int frame) {
  g_ctx.time  = t;
  g_ctx.frame = frame;
  g_ctx.tx    = dMatrix();
  g_ctx.col   = dColour(1, 1, 1, 1);
  g_ctx.stack.clear();
  g_ctx.hints = 0;
  g_ctx.lineWidth = 2.0f;
  g_ctx.grabbed = nullptr;
  g_ctx.shader  = nullptr;
  g_ctx.parent  = -1;
  g_ctx.texture = 0;
  g_ctx.srcBlend = GL_SRC_ALPHA;
  g_ctx.dstBlend = GL_ONE_MINUS_SRC_ALPHA;
  applyCamera();   // orbit camera survives the per-frame scene Clear()
}

void flux_background(double r, double g, double b) {
  if (g_ctx.r) g_ctx.r->SetBGColour(dColour((float) r, (float) g, (float) b, 1));
}
// Inside (with-primitive id ...) the grabbed prim exists, and fluxus state
// commands modify ITS state. Otherwise they set the build context for the
// next-built primitive.
static State* grabbedState() { return g_ctx.grabbed ? g_ctx.grabbed->GetState() : nullptr; }
static void applyOp(const dMatrix& op) {
  if (State* s = grabbedState()) s->Transform = s->Transform * op;
  else                           g_ctx.tx = g_ctx.tx * op;
}

void flux_colour(double r, double g, double b) {
  const dColour c((float) r, (float) g, (float) b, 1);
  if (State* s = grabbedState()) s->Colour = c; else g_ctx.col = c;
}

void flux_translate(double x, double y, double z) { dMatrix m; m.translate((float) x, (float) y, (float) z); applyOp(m); }
void flux_rotate(double x, double y, double z)    { dMatrix m; m.rotxyz((float) x, (float) y, (float) z);    applyOp(m); }
void flux_scale(double x, double y, double z)     { dMatrix m; m.scale((float) x, (float) y, (float) z);     applyOp(m); }
void flux_identity(void) { if (State* s = grabbedState()) s->Transform = dMatrix(); else g_ctx.tx = dMatrix(); }

void flux_push(void) { g_ctx.stack.push_back({g_ctx.tx, g_ctx.col}); }
void flux_pop(void) {
  if (!g_ctx.stack.empty()) { g_ctx.tx = g_ctx.stack.back().first; g_ctx.col = g_ctx.stack.back().second; g_ctx.stack.pop_back(); }
}

int flux_build_cube(void) {
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::QUADS);
  MakeCube(p, 1.0f);
  return addPrim(p);
}
int flux_build_sphere(int sl, int st) {
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::TRILIST);
  MakeSphere(p, 1.0f, sl > 0 ? sl : 10, st > 0 ? st : 10);
  return addPrim(p);
}
int flux_build_torus(double in, double out, int sl, int st) {
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::QUADS);
  MakeTorus(p, (float) in, (float) out, sl > 0 ? sl : 12, st > 0 ? st : 12);
  return addPrim(p);
}
int flux_build_plane(void) {
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::QUADS);
  MakePlane(p);
  return addPrim(p);
}
int flux_build_seg_plane(int xsegs, int ysegs) {
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::QUADS);
  MakePlane(p, xsegs > 0 ? xsegs : 1, ysegs > 0 ? ysegs : 1);   // grid in XY, +Z normals
  return addPrim(p);
}
int flux_build_nurbs_sphere(int hseg, int rseg) {
  // real NURBS sphere (GLU). control points are pdata "p"; moving them bends the
  // smooth surface. (NURBS has no per-vertex "n" — GLU computes normals itself.)
  NURBSPrimitive* p = new NURBSPrimitive();
  MakeNURBSSphere(p, 1.0f, hseg > 0 ? hseg : 10, rseg > 0 ? rseg : 10);
  return addPrim(p);
}
int flux_build_ribbon(int n) {
  RibbonPrimitive* p = new RibbonPrimitive();
  p->Resize((unsigned) (n > 0 ? n : 1));
  return addPrim(p);
}
int flux_build_particles(int n) {
  ParticlePrimitive* p = new ParticlePrimitive();
  for (int i = 0; i < (n > 0 ? n : 1); ++i)
    p->AddParticle(dVector(0, 0, 0), dColour(0, 0, 0), dVector(0.1f, 0.1f, 0.1f));
  return addPrim(p);
}

static void setHint(int bit, int on) {
  if (State* s = grabbedState()) { if (on) s->Hints |= bit; else s->Hints &= ~bit; }
  else                           { if (on) g_ctx.hints |= bit; else g_ctx.hints &= ~bit; }
}
void flux_hint_wire(int on)  { setHint(HINT_WIRE,  on); }
void flux_hint_solid(int on) { setHint(HINT_SOLID, on); }
void flux_line_width(double w) {
  if (State* s = grabbedState()) s->LineWidth = (float) w; else g_ctx.lineWidth = (float) w;
}
void flux_opacity(double o)      { if (State* s = grabbedState()) s->Opacity = (float) o; }
void flux_wire_opacity(double o) { if (State* s = grabbedState()) s->WireOpacity = (float) o; }
void flux_wire_colour(double r, double g, double b) { if (State* s = grabbedState()) s->WireColour = dColour((float) r, (float) g, (float) b, 1); }
void flux_backfacecull(int on)   { if (State* s = grabbedState()) s->Cull = on != 0; }

// ---- more builders ---------------------------------------------------------
int flux_build_cylinder(double h, double r, int hs, int rs) {
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::TRILIST);
  MakeCylinder(p, (float) h, (float) r, hs > 0 ? hs : 10, rs > 0 ? rs : 10);
  return addPrim(p);
}
int flux_build_polygons(int type, int nverts) {
  PolyPrimitive::Type t;
  switch (type) {
    case 1:  t = PolyPrimitive::QUADS;   break;
    case 2:  t = PolyPrimitive::TRILIST; break;
    case 3:  t = PolyPrimitive::TRIFAN;  break;
    case 4:  t = PolyPrimitive::POLYGON; break;
    default: t = PolyPrimitive::TRISTRIP;
  }
  PolyPrimitive* p = new PolyPrimitive(t);
  for (int i = 0; i < (nverts > 0 ? nverts : 0); ++i)
    p->AddVertex(dVertex(dVector(0, 0, 0), dVector(0, 1, 0), 0, 0));
  return addPrim(p);
}
int flux_build_copy(int id) {
  if (!g_ctx.r) return -1;
  Primitive* src = g_ctx.r->GetPrimitive(id);
  if (!src) return -1;
  return addPrim(src->Clone());
}
int flux_build_locator(void) { return addPrim(new LocatorPrimitive()); }
int flux_build_nurbs_plane(int u, int v) {
  NURBSPrimitive* p = new NURBSPrimitive();
  MakeNURBSPlane(p, u > 0 ? u : 5, v > 0 ? v : 5);
  return addPrim(p);
}

int flux_build_text(const char* str) {
  // build glyph quads directly into a PolyPrimitive (renders via the shader path;
  // avoids TextPrimitive's forced GL_CULL_FACE). Each char -> a quad sampling its
  // cell in the 16x16 atlas (T flipped for GL's bottom-left origin).
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::QUADS);
  const float cw = 1.0f / 16.0f, ch = 1.0f / 16.0f;   // atlas cell (texcoords)
  const float W = 0.6f, H = 0.9f;                       // world size per char
  const dVector N(0, 0, 1);
  float x = 0, y = 0;
  for (const char* c = str ? str : ""; *c; ++c) {
    if (*c == '\n') { x = 0; y -= H; continue; }
    const int pos = (unsigned char) *c;
    const float s0 = (pos % 16) * cw, t0 = (pos / 16) * ch;
    const float s1 = s0 + cw, t1 = t0 + ch;
    p->AddVertex(dVertex(dVector(x,     y,     0), N, s0, 1 - t1));   // bottom-left
    p->AddVertex(dVertex(dVector(x + W, y,     0), N, s1, 1 - t1));   // bottom-right
    p->AddVertex(dVertex(dVector(x + W, y + H, 0), N, s1, 1 - t0));   // top-right
    p->AddVertex(dVertex(dVector(x,     y + H, 0), N, s0, 1 - t0));   // top-left
    x += W;
  }
  int id = addPrim(p);
  State* s = p->GetState();
  s->Textures[0] = flux_font_atlas();
  setStateShader(s, builtinTextShader());
  return id;
}

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

// ---- material (grabbed primitive) ------------------------------------------
void flux_specular(double r, double g, double b)      { if (State* s = grabbedState()) s->Specular = dColour((float) r, (float) g, (float) b, 1); }
void flux_ambient(double r, double g, double b)       { if (State* s = grabbedState()) s->Ambient  = dColour((float) r, (float) g, (float) b, 1); }
void flux_emissive(double r, double g, double b)      { if (State* s = grabbedState()) s->Emissive = dColour((float) r, (float) g, (float) b, 1); }
void flux_shinyness(double v)                         { if (State* s = grabbedState()) s->Shinyness = (float) v; }
void flux_normal_colour(double r, double g, double b) { if (State* s = grabbedState()) s->NormalColour = dColour((float) r, (float) g, (float) b, 1); }
void flux_point_width(double w)                       { if (State* s = grabbedState()) s->PointWidth = (float) w; }

// ---- render hints ----------------------------------------------------------
void flux_hint_none(void)          { if (State* s = grabbedState()) s->Hints = 0; else g_ctx.hints = 0; }
void flux_hint_normal(int on)      { setHint(HINT_NORMAL, on); }
void flux_hint_points(int on)      { setHint(HINT_POINTS, on); }
void flux_hint_unlit(int on)       { setHint(HINT_UNLIT, on); }
void flux_hint_vertcols(int on)    { setHint(HINT_VERTCOLS, on); }
void flux_hint_depth_sort(int on)  { setHint(HINT_DEPTH_SORT, on); }
void flux_hint_cull_ccw(int on)    { setHint(HINT_CULL_CCW, on); }
void flux_hint_origin(int on)      { setHint(HINT_ORIGIN, on); }
void flux_hint_cast_shadow(int on) { setHint(HINT_CAST_SHADOW, on); }
void flux_hint_ignore_depth(int on){ setHint(HINT_IGNORE_DEPTH, on); }
void flux_hint_nozwrite(int on)    { setHint(HINT_NOZWRITE, on); }
void flux_hint_sphere_map(int on)  { setHint(HINT_SPHERE_MAP, on); }

// ---- lights ----------------------------------------------------------------
int flux_make_light(int type) {
  if (!g_ctx.r) return -1;
  Light* l = new Light();
  l->SetType((Light::Type) (type >= 0 && type <= 2 ? type : 0));
  l->SetCameraLock(false);
  return g_ctx.r->AddLight(l);
}
static Light* light(int id) { return g_ctx.r ? g_ctx.r->GetLight(id) : nullptr; }
void flux_light_position(int id, double x, double y, double z) { if (Light* l = light(id)) l->SetPosition(dVector((float) x, (float) y, (float) z)); }
void flux_light_diffuse(int id, double r, double g, double b)  { if (Light* l = light(id)) l->SetDiffuse(dColour((float) r, (float) g, (float) b, 1)); }
void flux_light_ambient(int id, double r, double g, double b)  { if (Light* l = light(id)) l->SetAmbient(dColour((float) r, (float) g, (float) b, 1)); }
void flux_light_specular(int id, double r, double g, double b) { if (Light* l = light(id)) l->SetSpecular(dColour((float) r, (float) g, (float) b, 1)); }
void flux_light_direction(int id, double x, double y, double z){ if (Light* l = light(id)) l->SetDirection(dVector((float) x, (float) y, (float) z)); }
void flux_light_spot_angle(int id, double a)                   { if (Light* l = light(id)) l->SetSpotAngle((float) a); }

// ---- fog / parent / select / shadows ---------------------------------------
void flux_fog(double r, double g, double b, double d, double s, double e) {
  if (g_ctx.r) g_ctx.r->SetFog(dColour((float) r, (float) g, (float) b, 1), (float) d, (float) s, (float) e);
}
void flux_parent(int id) { g_ctx.parent = id; }
int  flux_select(int x, int y, int size) { return g_ctx.r ? g_ctx.r->Select(0, x, y, size > 0 ? size : 5) : 0; }
void flux_shadow_light(int index)  { if (g_ctx.r) g_ctx.r->ShadowLight((unsigned) (index < 0 ? 0 : index)); }
void flux_shadow_length(double len){ if (g_ctx.r) g_ctx.r->ShadowLength((float) len); }

void flux_texture(int id) {
  GLSLShader* tsh = (id != 0) ? builtinTexShader() : nullptr;
  if (State* s = grabbedState()) {
    s->Textures[0] = (unsigned) id;
    if (id && !s->Shader) setStateShader(s, tsh);   // texture via shader (Metal-GL)
  } else {
    g_ctx.texture = (unsigned) id;
    if (id && !g_ctx.shader) g_ctx.shader = tsh;     // attach to next-built prims
  }
}

// ---- pdata (grabbed primitive) --------------------------------------------
void flux_grab(int id)   { g_ctx.grabbed = g_ctx.r ? g_ctx.r->GetPrimitive(id) : nullptr; }
void flux_ungrab(void)   { g_ctx.grabbed = nullptr; }

int flux_pdata_size(void) {
  if (!g_ctx.grabbed) return 0;
  auto it = g_pixels.find(g_ctx.grabbed);      // pixels prim: pdata "c" is w*h, not vertex count
  if (it != g_pixels.end()) return it->second.w * it->second.h;
  return (int) g_ctx.grabbed->Size();
}

void flux_recalc_normals(void) { if (g_ctx.grabbed) g_ctx.grabbed->RecalculateNormals(false); }

void flux_deform_audio(double bandScale, double wobble, double freq,
                       double speed, int recalcNormals) {
  Primitive* p = g_ctx.grabbed;
  if (!p) return;
  // verify the required pdata exists
  std::string on("ori"), nn("n"), pn("p");
  char t = 'v'; unsigned so = 0, sn = 0;
  p->GetDataInfo(on, t, so);
  p->GetDataInfo(nn, t, sn);
  if (so == 0 || sn == 0) return;

  std::vector<float> bands;
  { std::lock_guard<std::mutex> lk(g_audioMutex); bands = g_bands; }
  const int nb = (int) bands.size();
  const double time = g_ctx.time;
  const unsigned sz = p->Size();
  for (unsigned i = 0; i < sz; ++i) {
    const dVector o = p->GetData<dVector>(on, i);
    const dVector n = p->GetData<dVector>(nn, i);
    const double band = nb ? (double) bands[(size_t) ((int) i % nb)] : 0.0;
    const double w = wobble * (std::sin(freq * o.x + speed * time) +
                               std::cos(freq * o.y + speed * time * 0.8));
    const float disp = (float) (band * bandScale + w);
    p->SetData<dVector>(pn, i, o + n * disp);
  }
  if (recalcNormals) p->RecalculateNormals(false);
}

namespace {
struct CachedShape { std::vector<dVector> pos, nrm; };
std::map<std::string, CachedShape> g_shapeCache;
}
void flux_cache_shape(const char* name) {
  Primitive* p = g_ctx.grabbed;
  if (!p || !name) return;
  const unsigned sz = p->Size();
  std::string pn("p"), nn("n");
  char t = 'v'; unsigned sn = 0;
  p->GetDataInfo(nn, t, sn);
  const bool hasN = sn > 0;
  CachedShape cs;
  cs.pos.resize(sz);
  cs.nrm.resize(sz);
  for (unsigned i = 0; i < sz; ++i) {
    cs.pos[i] = p->GetData<dVector>(pn, i);
    cs.nrm[i] = hasN ? p->GetData<dVector>(nn, i) : dVector(0, 0, 0);
  }
  g_shapeCache[name] = std::move(cs);
}
int flux_shape_cached(const char* name) {
  return (name && g_shapeCache.find(name) != g_shapeCache.end()) ? 1 : 0;
}
void flux_deform_cached(const char* name, double bandScale, double wobble,
                        double freq, double speed, int recalcNormals) {
  Primitive* p = g_ctx.grabbed;
  if (!p || !name) return;
  auto it = g_shapeCache.find(name);
  if (it == g_shapeCache.end()) return;
  const CachedShape& cs = it->second;
  if (cs.pos.size() != p->Size()) return;

  std::vector<float> bands;
  { std::lock_guard<std::mutex> lk(g_audioMutex); bands = g_bands; }
  const int nb = (int) bands.size();
  const double time = g_ctx.time;
  std::string pn("p");
  const unsigned sz = p->Size();
  for (unsigned i = 0; i < sz; ++i) {
    const dVector base = cs.pos[i];
    const dVector n    = cs.nrm[i];
    const double band = nb ? (double) bands[(size_t) ((int) i % nb)] : 0.0;
    const double w = wobble * (std::sin(freq * base.x + speed * time) +
                               std::cos(freq * base.y + speed * time * 0.8));
    const float disp = (float) (band * bandScale + w);
    p->SetData<dVector>(pn, i, base + n * disp);
  }
  if (recalcNormals) p->RecalculateNormals(false);
}

void flux_pdata_add(const char* name, const char* type) {
  Primitive* p = g_ctx.grabbed;
  if (!p || !name) return;
  const unsigned sz = p->Size();
  const char t = (type && type[0]) ? type[0] : 'v';
  PData* pd;
  if      (t == 'c') pd = new TypedPData<dColour>(sz);
  else if (t == 'f') pd = new TypedPData<float>(sz);
  else               pd = new TypedPData<dVector>(sz);
  p->AddData(name, pd);
}
void flux_pdata_copy(const char* src, const char* dst) {
  if (g_ctx.grabbed && src && dst) g_ctx.grabbed->CopyData(src, dst);
}

double flux_pdata_get(const char* name, int i, int comp) {
  Primitive* p = g_ctx.grabbed;
  if (!p || !name) return 0.0;
  std::string n(name);
  char type = 'v'; unsigned size = 0;
  p->GetDataInfo(n, type, size);
  if (i < 0 || (unsigned) i >= size) return 0.0;
  if (comp < 0 || comp > 3) return 0.0;
  const unsigned ui = (unsigned) i;
  if (type == 'c') return p->GetData<dColour>(n, ui).arr()[comp];
  if (type == 'f') return p->GetData<float>(n, ui);
  return p->GetData<dVector>(n, ui).arr()[comp];   // 'v' positions/normals/texcoords
}

void flux_pdata_set(const char* name, int i, int comp, double val) {
  Primitive* p = g_ctx.grabbed;
  if (!p || !name) return;
  std::string n(name);
  char type = 'v'; unsigned size = 0;
  p->GetDataInfo(n, type, size);
  if (i < 0 || (unsigned) i >= size) return;
  if (comp < 0 || comp > 3) return;
  const unsigned ui = (unsigned) i;
  if (type == 'c')      { dColour v = p->GetData<dColour>(n, ui); v.arr()[comp] = (float) val; p->SetData<dColour>(n, ui, v); }
  else if (type == 'f') { p->SetData<float>(n, ui, (float) val); }
  else                  { dVector v = p->GetData<dVector>(n, ui); v.arr()[comp] = (float) val; p->SetData<dVector>(n, ui, v); }
}

double flux_time(void)  { return g_ctx.time; }
int    flux_frame(void) { return g_ctx.frame; }
double flux_delta(void) { return g_ctx.r ? g_ctx.r->GetDelta() : 0.0; }

void flux_set_audio(const float* bands, int n, double gain) {
  std::lock_guard<std::mutex> lk(g_audioMutex);
  g_bands.assign(bands, bands + (n > 0 ? n : 0));
  g_gain = gain;
}
double flux_audio_harmonic(int n) {
  std::lock_guard<std::mutex> lk(g_audioMutex);
  if (g_bands.empty()) return 0.0;
  if (n < 0) n = -n;
  return g_bands[(size_t) (n % (int) g_bands.size())];   // fluxus: gh(h) = bars[h % numBars]
}
double flux_audio_gain(void) {
  std::lock_guard<std::mutex> lk(g_audioMutex);
  return g_gain;
}

void flux_set_mouse(double x, double y, int button) { g_mouseX = x; g_mouseY = y; g_mouseButton = button; }
double flux_mouse_x(void)     { return g_mouseX; }
double flux_mouse_y(void)     { return g_mouseY; }
int    flux_mouse_button(void){ return g_mouseButton; }
void flux_camera_drag(double dx, double dy) {
  g_cam.yaw   += dx * 0.5;
  g_cam.pitch += dy * 0.5;
  if (g_cam.pitch >  89.0) g_cam.pitch =  89.0;
  if (g_cam.pitch < -89.0) g_cam.pitch = -89.0;
}
void flux_camera_zoom(double d) {
  g_cam.dist += d;
  if (g_cam.dist < 2.0)  g_cam.dist = 2.0;
  if (g_cam.dist > 80.0) g_cam.dist = 80.0;
}
double flux_camera_dist(void)  { return g_cam.dist; }
double flux_camera_yaw(void)   { return g_cam.yaw; }
double flux_camera_pitch(void) { return g_cam.pitch; }

// ---- script-driven camera --------------------------------------------------
void flux_set_camera_transform(const double* m) {
  if (!m) return;
  float* a = g_camOverrideMat.arr();
  for (int i = 0; i < 16; ++i) a[i] = (float) m[i];
  g_camOverride = true;
  g_camAppliedMat = g_camOverrideMat;
  if (Camera* c = cam0()) c->SetMatrix(g_camOverrideMat);   // apply this frame too
}
void flux_get_camera_transform(double* out) {
  if (!out) return;
  const float* a = g_camAppliedMat.arr();
  for (int i = 0; i < 16; ++i) out[i] = a[i];
}
void flux_set_camera_position(double x, double y, double z) {
  // set the eye position: translate part of the camera (view) matrix
  dMatrix m; m.translate((float) -x, (float) -y, (float) -z);
  float* a = g_camOverrideMat.arr();
  for (int i = 0; i < 16; ++i) a[i] = m.arr()[i];
  g_camOverride = true;
  g_camAppliedMat = g_camOverrideMat;
  if (Camera* c = cam0()) c->SetMatrix(g_camOverrideMat);
}
void flux_camera_reset(void) { g_camOverride = false; }

// build the frustum for a vertical fov + aspect (w/h) on the given camera
static void applyFrustum(Camera* c, double vfovDeg, double aspect) {
  const double front = 1.0;                                   // near clip
  const double t = front * std::tan(vfovDeg * 0.5 * 3.14159265358979323846 / 180.0);
  const double r = t * aspect;
  c->SetFrustum((float) -r, (float) r, (float) -t, (float) t);
}
// centre a viewport of target AR inside a window of the current pixel size,
// adding letterbox/pillarbox bars so content isn't stretched.
static void applyLetterbox(Camera* c, double targetAR) {
  const double winAR = (g_screenH > 0) ? (double) g_screenW / g_screenH : targetAR;
  double vx = 0, vy = 0, vw = 1, vh = 1;
  if (winAR > targetAR) { vw = targetAR / winAR; vx = (1.0 - vw) * 0.5; }  // pillarbox
  else                  { vh = winAR / targetAR; vy = (1.0 - vh) * 0.5; }  // letterbox
  c->SetViewport((float) vx, (float) vy, (float) vw, (float) vh);
}

void flux_set_fov(double vfovDeg) {
  Camera* c = cam0();
  if (!c) return;
  g_lastVfov = vfovDeg;
  const double aspect = (g_aspectLock > 0.0) ? g_aspectLock
                      : (g_screenH > 0) ? (double) g_screenW / g_screenH : 4.0 / 3.0;
  applyFrustum(c, vfovDeg, aspect);
}

// lock the render aspect ratio (w/h); ratio<=0 restores auto (fill window).
void flux_set_aspect(double ratio) { g_aspectLock = (ratio > 0.0) ? ratio : 0.0; }
void flux_set_frustum(double l, double r, double b, double t) {
  if (Camera* c = cam0()) c->SetFrustum((float) l, (float) r, (float) b, (float) t);
}
void flux_set_ortho(int on)         { if (Camera* c = cam0()) c->SetOrtho(on != 0); }
void flux_set_ortho_zoom(double z)  { if (Camera* c = cam0()) c->SetOrthoZoom((float) z); }
void flux_set_clip(double f, double b) { if (Camera* c = cam0()) c->SetClip((float) f, (float) b); }
void flux_set_viewport(double x, double y, double w, double h) {
  if (Camera* c = cam0()) c->SetViewport((float) x, (float) y, (float) w, (float) h);
}
void flux_set_resolution(int w, int h) {
  g_screenW = w; g_screenH = h;
  // apply the aspect lock every frame (runs on the GL thread) so the render
  // tracks window resizes: locked -> rebuild frustum + letterbox; just-unlocked
  // -> restore the full viewport + a window-aspect frustum once.
  if (Camera* c = cam0()) {
    if (g_aspectLock > 0.0) {
      applyFrustum(c, g_lastVfov, g_aspectLock);
      applyLetterbox(c, g_aspectLock);
    } else if (g_prevAspectLock > 0.0) {
      c->SetViewport(0.0f, 0.0f, 1.0f, 1.0f);
      applyFrustum(c, g_lastVfov, (h > 0) ? (double) w / h : 4.0 / 3.0);
    }
  }
  g_prevAspectLock = g_aspectLock;
}
void flux_get_screen_size(double* out) { if (out) { out[0] = g_screenW; out[1] = g_screenH; } }

int flux_state_get(const char* key, double* out, int n) {
  if (!key || !out || n <= 0) return 0;
  std::lock_guard<std::mutex> lk(g_stateMutex);
  auto it = g_state.find(key);
  if (it == g_state.end()) return 0;
  const auto& v = it->second;
  for (int i = 0; i < n; ++i) out[i] = (i < (int) v.size()) ? v[(size_t) i] : 0.0;
  return 1;
}
void flux_state_set(const char* key, const double* v, int n) {
  if (!key || !v || n < 0) return;
  std::lock_guard<std::mutex> lk(g_stateMutex);
  g_state[key].assign(v, v + n);
}
void flux_state_clear(void) {
  std::lock_guard<std::mutex> lk(g_stateMutex);
  g_state.clear();
}

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

void flux_report_error(const char* msg) {
  std::lock_guard<std::mutex> lk(g_errMutex);
  g_err = msg ? msg : "";
}

} // extern "C"

// ---- post-FX state (read by FluxusScene's PostFX) --------------------------
namespace {
std::mutex  g_postMutex;
bool        g_postEnabled = false;
std::string g_postFrag;
double      g_postFeedback = 0.0;
bool        g_postDirty   = false;

// built-in feedback motion-blur fragment: blend the current frame over a decayed
// copy of the previous output (max keeps bright trails that fade each frame).
const char* kBlurFrag =
  "uniform sampler2D tex;\n"
  "uniform sampler2D prev;\n"
  "uniform float feedback;\n"
  "varying vec2 uv;\n"
  "void main() {\n"
  "  vec3 c = texture2D(tex, uv).rgb;\n"
  "  vec3 p = texture2D(prev, uv).rgb * feedback;\n"
  "  gl_FragColor = vec4(max(c, p), 1.0);\n"
  "}\n";
}
extern "C" void flux_post_shader(const char* frag) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  std::string f = frag ? frag : "";
  if (f != g_postFrag) { g_postFrag = f; g_postDirty = true; }
  g_postEnabled = true;
}
extern "C" void flux_post_off(void) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  g_postEnabled = false;
}

std::atomic<bool> g_antialias{false};
extern "C" void flux_set_antialias(int on) { g_antialias = (on != 0); }
bool flux_antialias_on() { return g_antialias.load(); }

std::atomic<bool> g_retained{false};
extern "C" void flux_set_retained(int on) { g_retained = (on != 0); }
bool flux_retained_on() { return g_retained.load(); }
extern "C" void flux_blur(double amount) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  if (g_postFrag != kBlurFrag) { g_postFrag = kBlurFrag; g_postDirty = true; }
  g_postFeedback = amount < 0 ? 0 : (amount > 0.97 ? 0.97 : amount);
  g_postEnabled = true;
}
bool flux_post_state(std::string& frag, double& feedback, bool& dirty) {
  std::lock_guard<std::mutex> lk(g_postMutex);
  frag = g_postFrag;
  feedback = g_postFeedback;
  dirty = g_postDirty;
  g_postDirty = false;
  return g_postEnabled;
}

std::string flux_last_error() {
  std::lock_guard<std::mutex> lk(g_errMutex);
  return g_err;
}
