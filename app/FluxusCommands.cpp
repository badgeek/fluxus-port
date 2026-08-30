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
#include "Noise.h"
#include "SimplexNoise.h"
#include "VoxelPrimitive.h"
#include "BlobbyPrimitive.h"
#include "PrimitiveIO.h"
#include "Tree.h"
#include "PrimitiveFunction.h"
#include "ArithmeticPrimFunc.h"
#include "GenSkinWeightsPrimFunc.h"
#include "SkinWeightsToVertColsPrimFunc.h"
#include "SkinningPrimFunc.h"

#include <vector>
#include <deque>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <set>
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
  int        grabbedId = -1;      // its scene-graph id (for scene-graph queries / save)
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
// script-requested window size (scheme (set-window-size w h)); the message-thread
// component polls flux_take_window_request and resizes its window content. Defs
// live in the extern "C" block below so they export C symbols.
static std::mutex g_winMutex;
static int  g_winReqW = 0, g_winReqH = 0;
static bool g_winReqPending = false;
// script-requested code-editor visibility ((show-editor)/(hide-editor)/(editor-
// full-width b)). The message-thread component polls flux_get_editor and toggles
// the overlay editor. g_edSet stays false until a script speaks, so sketches that
// never call these keep the default (editor shown, left half).
static std::mutex g_edMutex;
static bool g_edSet = false;
static int  g_edVisible = 1, g_edFull = 0;
// frame-recording state: while on, the scene grabs each rendered frame to
// g_recDir/fNNNNN.png (g_recFrame auto-increments on the GL thread).
static std::mutex g_recMutex;
static bool g_recOn = false;
static std::string g_recDir;
static long g_recFrame = 0;
// offline export desired-state (the scene owns the ffmpeg pipe on the GL thread)
static std::mutex g_expMutex;
static bool g_expOn = false;
static std::string g_expPath;
static int g_expFps = 60;
// audio-reactive export: mux path + pre-analysed per-frame feature table
static std::mutex g_expAudMutex;
static std::string g_expAudPath;
static std::vector<float> g_expAudGains;
static std::vector<float> g_expAudBands;
static int  g_expAudNBands = 0;
static long g_expAudFrames = 0;
// one-shot screenshot state
static std::mutex g_shotMutex;
static std::string g_shotPending;
static std::set<std::string> g_shotDone;
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

// (clear): wipe the scene graph. In immediate mode the host already clears each
// frame (so a top-of-script (clear) is a harmless redundant wipe); in RETAINED
// mode the host does NOT clear, so a sketch that rebuilds every frame calls
// (clear) itself at the top of its every-frame thunk. Retained + (clear) lets a
// heavy sketch compile once and only re-run its thunk (no per-frame re-parse of
// the whole program), which is far cheaper than immediate mode.
void flux_scene_clear(void) { if (g_ctx.r) g_ctx.r->Clear(); }

// (destroy id): remove one primitive by id. Lets a RETAINED sketch keep static
// geometry alive across frames while destroying + rebuilding only its animated
// prims each frame (persistent scene) — far cheaper than clearing + rebuilding
// everything. Resets our build-context grab if it pointed at the removed prim
// (the renderer clears its own m_Grabbed inside RemovePrimitive).
void flux_destroy(int id) {
  if (!g_ctx.r) return;
  if (g_ctx.grabbed && g_ctx.r->GetPrimitive(id) == g_ctx.grabbed) g_ctx.grabbed = nullptr;
  g_ctx.r->RemovePrimitive(id);
}

void flux_frame_begin(double t, int frame) {
  g_ctx.time  = t;
  g_ctx.frame = frame;
  g_ctx.tx    = dMatrix();
  g_ctx.col   = dColour(1, 1, 1, 1);
  g_ctx.stack.clear();
  g_ctx.hints = 0;
  g_ctx.lineWidth = 2.0f;
  g_ctx.grabbed = nullptr;
  g_ctx.grabbedId = -1;
  g_ctx.shader  = nullptr;
  g_ctx.parent  = -1;
  g_ctx.texture = 0;
  g_ctx.srcBlend = GL_SRC_ALPHA;
  g_ctx.dstBlend = GL_ONE_MINUS_SRC_ALPHA;
  // immediate mode re-makes pfuncs every eval — free last frame's set so they
  // don't leak. Retained mode makes them once in setup, so keep them there.
  if (!flux_retained_on()) flux_pfunc_clear();
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
  const float W = 0.6f, H = 0.9f;                       // glyph quad size
  const float ADV = 0.44f;                              // pen advance per char
  // (< W so cells overlap a little -> tighter letter + word spacing). Layout
  // code (scheme CW) must match ADV to centre text correctly.
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
    x += ADV;
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
void flux_grab(int id)   { g_ctx.grabbed = g_ctx.r ? g_ctx.r->GetPrimitive(id) : nullptr; g_ctx.grabbedId = g_ctx.grabbed ? id : -1; }
void flux_ungrab(void)   { g_ctx.grabbed = nullptr; g_ctx.grabbedId = -1; }

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

// key channel: the app pushes the last-pressed char; a script polls it once
// ((key-poll) consumes it, returning 0 when nothing new). Feeds simple hotkeys.
static std::atomic<int> g_key{0};
void flux_set_key(int c) { g_key = c; }
int  flux_get_key(void)  { return g_key.exchange(0); }
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

void flux_request_window_size(int w, int h) {
  std::lock_guard<std::mutex> lk(g_winMutex);
  g_winReqW = w; g_winReqH = h; g_winReqPending = true;
}
int flux_take_window_request(int* w, int* h) {
  std::lock_guard<std::mutex> lk(g_winMutex);
  if (!g_winReqPending) return 0;
  if (w) *w = g_winReqW; if (h) *h = g_winReqH;
  g_winReqPending = false; return 1;
}

// editor visibility: the script sets a desired state; the component reads it every
// tick and only re-lays-out when it actually changed (so calling this every frame
// in an every-frame thunk is cheap).
void flux_set_editor_visible(int visible) {
  std::lock_guard<std::mutex> lk(g_edMutex);
  g_edVisible = visible ? 1 : 0; g_edSet = true;
}
void flux_set_editor_full_width(int full) {
  std::lock_guard<std::mutex> lk(g_edMutex);
  g_edFull = full ? 1 : 0; g_edSet = true;
}
int flux_get_editor(int* visible, int* full) {
  std::lock_guard<std::mutex> lk(g_edMutex);
  if (!g_edSet) return 0;
  if (visible) *visible = g_edVisible;
  if (full)    *full    = g_edFull;
  return 1;
}

void flux_set_recording(int on, const char* dir) {
  std::lock_guard<std::mutex> lk(g_recMutex);
  if (on) { g_recDir = dir ? dir : "."; g_recFrame = 0; g_recOn = true; }
  else    { g_recOn = false; }
}
int flux_recording_next(char* out, int cap) {
  std::lock_guard<std::mutex> lk(g_recMutex);
  if (!g_recOn || !out || cap <= 0) return 0;
  std::snprintf(out, (size_t) cap, "%s/f%05ld.png", g_recDir.c_str(), g_recFrame++);
  return 1;
}

void flux_set_export(int on, const char* path, int fps) {
  std::lock_guard<std::mutex> lk(g_expMutex);
  if (on) { g_expPath = path ? path : "export.mp4"; g_expFps = fps > 0 ? fps : 60; g_expOn = true; }
  else    { g_expOn = false; }
}
int flux_export_state(char* pathOut, int cap, int* fps) {
  std::lock_guard<std::mutex> lk(g_expMutex);
  if (!g_expOn) return 0;
  if (pathOut && cap > 0) std::snprintf(pathOut, (size_t) cap, "%s", g_expPath.c_str());
  if (fps) *fps = g_expFps;
  return 1;
}

void flux_set_export_audio(const char* wavPath) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  g_expAudPath = wavPath ? wavPath : "";
}
int flux_export_audio_path(char* out, int cap) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  if (g_expAudPath.empty() || !out || cap <= 0) return 0;
  std::snprintf(out, (size_t) cap, "%s", g_expAudPath.c_str());
  return 1;
}
void flux_export_audio_load(const float* gains, const float* bands, int nFrames, int nBands) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  if (!gains || !bands || nFrames <= 0 || nBands <= 0) { g_expAudFrames = 0; return; }
  g_expAudGains.assign(gains, gains + nFrames);
  g_expAudBands.assign(bands, bands + (size_t) nFrames * nBands);
  g_expAudNBands = nBands; g_expAudFrames = nFrames;
}
void flux_export_audio_apply(long frame) {
  float gain; const float* bands; int n;
  {
    std::lock_guard<std::mutex> lk(g_expAudMutex);
    if (g_expAudFrames <= 0) return;
    long f = frame; if (f < 0) f = 0; if (f >= g_expAudFrames) f = g_expAudFrames - 1;  // clamp
    gain = g_expAudGains[(size_t) f];
    bands = &g_expAudBands[(size_t) f * g_expAudNBands];
    n = g_expAudNBands;
  }
  flux_set_audio(bands, n, gain);   // overwrite this frame's audio state
}
void flux_export_audio_clear(void) {
  std::lock_guard<std::mutex> lk(g_expAudMutex);
  g_expAudGains.clear(); g_expAudBands.clear(); g_expAudFrames = 0; g_expAudNBands = 0;
  g_expAudPath.clear();
}

// one-shot screenshot request. Captured once per unique path, so a script may
// call (screenshot p) unconditionally every frame — only the first is written.
void flux_screenshot(const char* path) {
  if (!path) return;
  std::lock_guard<std::mutex> lk(g_shotMutex);
  if (g_shotDone.count(path)) return;          // already captured this path
  g_shotPending = path;
}
int flux_take_screenshot(char* out, int cap) {
  std::lock_guard<std::mutex> lk(g_shotMutex);
  if (g_shotPending.empty() || !out || cap <= 0) return 0;
  std::snprintf(out, (size_t) cap, "%s", g_shotPending.c_str());
  g_shotDone.insert(g_shotPending);
  g_shotPending.clear();
  return 1;
}
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

// ---- maths primitives -------------------------------------------------------
// Pure functions on the engine's dVector/dMatrix/dQuat. Matrices marshal through
// dMatrix::arr() (float[16], m[row][col] order) so results match the engine's own
// transform stack (flux_rotate/scale/translate use the same rotxyz/scale/translate).
namespace {
  inline dVector V(const double a[3]) { return dVector((float) a[0], (float) a[1], (float) a[2]); }
  inline void    outV(const dVector& v, double o[3]) { o[0] = v.x; o[1] = v.y; o[2] = v.z; }
  inline dMatrix M(const double a[16]) { dMatrix m; float* p = m.arr(); for (int i = 0; i < 16; ++i) p[i] = (float) a[i]; return m; }
  inline void    outM(dMatrix m, double o[16]) { const float* p = m.arr(); for (int i = 0; i < 16; ++i) o[i] = p[i]; }
  inline dQuat   Q(const double a[4]) { return dQuat((float) a[0], (float) a[1], (float) a[2], (float) a[3]); }
  inline void    outQ(const dQuat& q, double o[4]) { o[0] = q.x; o[1] = q.y; o[2] = q.z; o[3] = q.w; }
}

void   flux_vadd(const double a[3], const double b[3], double o[3]) { outV(V(a) + V(b), o); }
void   flux_vsub(const double a[3], const double b[3], double o[3]) { outV(V(a) - V(b), o); }
void   flux_vmul(const double a[3], double s, double o[3])          { outV(V(a) * (float) s, o); }
void   flux_vdiv(const double a[3], double s, double o[3])          { outV(V(a) / (float) s, o); }
double flux_vdot(const double a[3], const double b[3])              { dVector x = V(a); return x.dot(V(b)); }
void   flux_vcross(const double a[3], const double b[3], double o[3]) { outV(V(a).cross(V(b)), o); }
double flux_vmag(const double a[3])                                 { dVector x = V(a); return x.mag(); }
double flux_vdist(const double a[3], const double b[3])             { dVector d = V(a) - V(b); return d.mag(); }
double flux_vdist_sq(const double a[3], const double b[3])          { dVector d = V(a) - V(b); return d.dot(d); }
void   flux_vnormalise(const double a[3], double o[3]) {
  dVector v = V(a); float m = v.mag();
  if (m > 0.0f) v /= m;
  outV(v, o);
}
void   flux_vreflect(const double a[3], const double n[3], double o[3]) { dVector v = V(a); outV(v.reflect(V(n)), o); }
void   flux_vtransform(const double v[3], const double m[16], double o[3])     { outV(M(m).transform(V(v)), o); }
void   flux_vtransform_rot(const double v[3], const double m[16], double o[3]) { outV(M(m).transform_no_trans(V(v)), o); }

void flux_mident(double o[16])                                   { outM(dMatrix(), o); }
void flux_mmul(const double a[16], const double b[16], double o[16]) { outM(M(a) * M(b), o); }
void flux_mtranslate(const double v[3], double o[16]) { dMatrix m; m.translate((float) v[0], (float) v[1], (float) v[2]); outM(m, o); }
void flux_mrotate(const double v[3], double o[16])    { dMatrix m; m.rotxyz((float) v[0], (float) v[1], (float) v[2]); outM(m, o); }
void flux_mscale(const double v[3], double o[16])     { dMatrix m; m.scale((float) v[0], (float) v[1], (float) v[2]); outM(m, o); }
void flux_mtranspose(const double a[16], double o[16]) { dMatrix m = M(a); m.transpose(); outM(m, o); }
void flux_minverse(const double a[16], double o[16])   { outM(M(a).inverse(), o); }
void flux_maim(const double dir[3], const double up[3], double o[16]) { dMatrix m; m.aim(V(dir), V(up)); outM(m, o); }

void flux_qaxisangle(const double axis[3], double angle, double o[4]) { dQuat q; q.setAxisAngle(V(axis), (float) angle); outQ(q, o); }
void flux_qmul(const double a[4], const double b[4], double o[4])     { outQ(Q(a) * Q(b), o); }
void flux_qnormalise(const double a[4], double o[4])                  { outQ(Q(a).getNormlised(), o); }
void flux_qconjugate(const double a[4], double o[4])                  { outQ(Q(a).conjugate(), o); }
void flux_qtomatrix(const double a[4], double o[16])                  { outM(Q(a).toMatrix(), o); }

double flux_noise(double x, double y, double z)  { return Noise::noise((float) x, (float) y, (float) z); }
double flux_snoise(double x, double y, double z) { return SimplexNoise::noise((float) x, (float) y, (float) z); }
void   flux_noise_seed(int seed)                 { Noise::noise_seed((unsigned) seed); }
void   flux_noise_detail(int octaves, double falloff) { Noise::noise_detail(octaves, (float) falloff); }

// ---- turtle builder (ported from modules/fluxus-engine/src/TurtleBuilder) ---
// A turtle carries a transform stack; move/turn drive it, vert emits a vertex
// into a build prim (or overwrites an attached prim's "p" pdata). Build hands
// the prim to the renderer through addPrim so it honours the build context.
namespace {
struct TurtleState { dVector pos = dVector(0,0,0); dVector rot = dVector(0,0,0); };
struct Turtle {
  PolyPrimitive* building = nullptr;
  TypedPData<dVector>* attached = nullptr;   // non-owning: an attached prim's "p"
  unsigned int position = 0;
  std::deque<TurtleState> state;
  Turtle() { reset(); }
  void reset() { state.clear(); state.push_front(TurtleState()); position = 0; }
  void init()  { if (building) delete building; building = nullptr; attached = nullptr; position = 0; }
  void prim(int type) {
    init();
    PolyPrimitive::Type t;
    switch (type) { case 1: t = PolyPrimitive::QUADS;   break;
                    case 2: t = PolyPrimitive::TRILIST; break;
                    case 3: t = PolyPrimitive::TRIFAN;  break;
                    case 4: t = PolyPrimitive::POLYGON; break;
                    default: t = PolyPrimitive::TRISTRIP; }
    building = new PolyPrimitive(t);
  }
  void attach(PolyPrimitive* p) {
    init();
    attached = dynamic_cast<TypedPData<dVector>*>(p->GetDataRaw("p"));
  }
  void vert() {
    if (building) building->AddVertex(dVertex(state.front().pos, dVector(0, 1, 0)));
    else if (attached && !attached->m_Data.empty())
      attached->m_Data[position % attached->m_Data.size()] = state.front().pos;
    position++;
  }
  void move(float d) {
    dVector off(d, 0, 0); dMatrix m;
    m.rotxyz(state.front().rot.x, state.front().rot.y, state.front().rot.z);
    off = m.transform(off); state.front().pos += off;
  }
  void turn(dVector a) { state.front().rot += a; }
  void push() { if (state.empty()) state.push_front(TurtleState()); else state.push_front(state.front()); }
  void pop()  { if (state.size() > 1) state.pop_front(); }
  dMatrix transform() {
    dMatrix m; m.rotxyz(state.front().rot.x, state.front().rot.y, state.front().rot.z);
    m.settranslate(state.front().pos); return m;
  }
};
Turtle g_turtle;
}
void flux_turtle_prim(int type)  { g_turtle.prim(type); }
void flux_turtle_vert(void)      { g_turtle.vert(); }
int  flux_turtle_build(void)     { if (!g_turtle.building) return -1;
                                   PolyPrimitive* p = g_turtle.building; g_turtle.building = nullptr;
                                   return addPrim(p); }
void flux_turtle_move(double d)  { g_turtle.move((float) d); }
void flux_turtle_turn(double x, double y, double z) { g_turtle.turn(dVector((float) x, (float) y, (float) z)); }
void flux_turtle_push(void)      { g_turtle.push(); }
void flux_turtle_pop(void)       { g_turtle.pop(); }
void flux_turtle_reset(void)     { g_turtle.reset(); }
void flux_turtle_attach(int id)  { if (g_ctx.r) { PolyPrimitive* p = dynamic_cast<PolyPrimitive*>(g_ctx.r->GetPrimitive(id)); if (p) g_turtle.attach(p); } }
void flux_turtle_skip(int n)     { g_turtle.position += (unsigned) n; }
int  flux_turtle_position(void)  { return (int) g_turtle.position; }
void flux_turtle_seek(int pos)   { g_turtle.position = (unsigned) pos; }
void flux_get_turtle_transform(double out[16]) { dMatrix m = g_turtle.transform(); const float* a = m.arr(); for (int i = 0; i < 16; ++i) out[i] = a[i]; }

// ---- voxels + blobby --------------------------------------------------------
// Voxel mutators/accessors act on the GRABBED prim (with-primitive); build-* and
// the ->poly / ->blobby converters act by primitive id, like the other builders.
namespace {
  inline VoxelPrimitive* grabbedVoxel() { return dynamic_cast<VoxelPrimitive*>(g_ctx.grabbed); }
}
int flux_build_voxels(int w, int h, int d) {
  return addPrim(new VoxelPrimitive(w > 0 ? w : 1, h > 0 ? h : 1, d > 0 ? d : 1));
}
int flux_voxels_width(void)  { VoxelPrimitive* v = grabbedVoxel(); return v ? (int) v->GetWidth()  : 0; }
int flux_voxels_height(void) { VoxelPrimitive* v = grabbedVoxel(); return v ? (int) v->GetHeight() : 0; }
int flux_voxels_depth(void)  { VoxelPrimitive* v = grabbedVoxel(); return v ? (int) v->GetDepth()  : 0; }
void flux_voxels_calc_gradient(void) { if (VoxelPrimitive* v = grabbedVoxel()) v->CalcGradient(); }
void flux_voxels_sphere_influence(double px, double py, double pz, double r, double g, double b, double pow) {
  if (VoxelPrimitive* v = grabbedVoxel())
    v->SphereInfluence(dVector((float) px, (float) py, (float) pz), dColour((float) r, (float) g, (float) b), (float) pow);
}
void flux_voxels_sphere_solid(double px, double py, double pz, double r, double g, double b, double radius) {
  if (VoxelPrimitive* v = grabbedVoxel())
    v->SphereSolid(dVector((float) px, (float) py, (float) pz), dColour((float) r, (float) g, (float) b), (float) radius);
}
void flux_voxels_box_solid(double tx, double ty, double tz, double bx, double by, double bz, double r, double g, double b) {
  if (VoxelPrimitive* v = grabbedVoxel())
    v->BoxSolid(dVector((float) tx, (float) ty, (float) tz), dVector((float) bx, (float) by, (float) bz), dColour((float) r, (float) g, (float) b));
}
void flux_voxels_threshold(double val) { if (VoxelPrimitive* v = grabbedVoxel()) v->Threshold((float) val); }
void flux_voxels_point_light(double px, double py, double pz, double r, double g, double b) {
  if (VoxelPrimitive* v = grabbedVoxel())
    v->PointLight(dVector((float) px, (float) py, (float) pz), dColour((float) r, (float) g, (float) b));
}
int flux_voxels_to_blobby(int id) {
  if (!g_ctx.r) return -1;
  VoxelPrimitive* v = dynamic_cast<VoxelPrimitive*>(g_ctx.r->GetPrimitive(id));
  if (!v) return -1;
  return addPrim(v->ConvertToBlobby());
}
int flux_voxels_to_poly(int id, double isolevel) {
  if (!g_ctx.r) return -1;
  VoxelPrimitive* v = dynamic_cast<VoxelPrimitive*>(g_ctx.r->GetPrimitive(id));
  if (!v) return -1;
  BlobbyPrimitive* bp = v->ConvertToBlobby();
  PolyPrimitive* np = new PolyPrimitive(PolyPrimitive::TRILIST);
  bp->ConvertToPoly(*np, (float) isolevel);
  delete bp;
  return addPrim(np);
}
int flux_build_blobby(int count, double dx, double dy, double dz, double sx, double sy, double sz) {
  BlobbyPrimitive* p = new BlobbyPrimitive((int) dx, (int) dy, (int) dz, dVector((float) sx, (float) sy, (float) sz));
  for (int i = 0; i < count; ++i) p->AddInfluence(dVector(0, 0, 0), 0);
  return addPrim(p);
}
int flux_blobby_to_poly(int id) {
  if (!g_ctx.r) return -1;
  BlobbyPrimitive* bp = dynamic_cast<BlobbyPrimitive*>(g_ctx.r->GetPrimitive(id));
  if (!bp) return -1;
  PolyPrimitive* np = new PolyPrimitive(PolyPrimitive::TRILIST);
  bp->ConvertToPoly(*np);
  return addPrim(np);
}

// ---- pdata-op ---------------------------------------------------------------
namespace {
  int pdataOpResult(PData* ret, double out[3]) {   // read a "closest"-style result, own+free it
    if (!ret) return 0;
    int n = 0;
    if (TypedPData<dVector>* v = dynamic_cast<TypedPData<dVector>*>(ret))
      if (!v->m_Data.empty()) { out[0] = v->m_Data[0].x; out[1] = v->m_Data[0].y; out[2] = v->m_Data[0].z; n = 3; }
    delete ret;
    return n;
  }
}
int flux_pdata_op_num(const char* op, const char* name, double val, double out[3]) {
  if (!g_ctx.grabbed) return 0;
  PData* ret = g_ctx.grabbed->DataOp<float>(op, name, (float) val);
  g_ctx.grabbed->BumpPDataVersion();
  return pdataOpResult(ret, out);
}
int flux_pdata_op_vec(const char* op, const char* name, const double* v, int n, double out[3]) {
  if (!g_ctx.grabbed) return 0;
  PData* ret = nullptr;
  if (n == 3)  ret = g_ctx.grabbed->DataOp<dVector>(op, name, dVector((float) v[0], (float) v[1], (float) v[2]));
  else if (n == 4)  ret = g_ctx.grabbed->DataOp<dColour>(op, name, dColour((float) v[0], (float) v[1], (float) v[2], (float) v[3]));
  else if (n == 16) { dMatrix m; float* a = m.arr(); for (int i = 0; i < 16; ++i) a[i] = (float) v[i];
                      ret = g_ctx.grabbed->DataOp<dMatrix>(op, name, m); }
  g_ctx.grabbed->BumpPDataVersion();
  return pdataOpResult(ret, out);
}
int flux_pdata_op_pdata(const char* op, const char* name, const char* other, double out[3]) {
  if (!g_ctx.grabbed) return 0;
  PData* pd = g_ctx.grabbed->GetDataRaw(other);
  PData* ret = nullptr;
  if (TypedPData<dVector>* v = dynamic_cast<TypedPData<dVector>*>(pd))
    ret = g_ctx.grabbed->DataOp<TypedPData<dVector>*>(op, name, v);
  else if (TypedPData<dColour>* c = dynamic_cast<TypedPData<dColour>*>(pd))
    ret = g_ctx.grabbed->DataOp<TypedPData<dColour>*>(op, name, c);
  else if (TypedPData<float>* f = dynamic_cast<TypedPData<float>*>(pd))
    ret = g_ctx.grabbed->DataOp<TypedPData<float>*>(op, name, f);
  g_ctx.grabbed->BumpPDataVersion();
  return pdataOpResult(ret, out);
}

// ---- poly indexing ----------------------------------------------------------
namespace { inline PolyPrimitive* grabbedPoly() { return dynamic_cast<PolyPrimitive*>(g_ctx.grabbed); } }
int flux_poly_type(void)    { PolyPrimitive* p = grabbedPoly(); return p ? (int) p->GetType() : -1; }
int flux_poly_indexed(void) { PolyPrimitive* p = grabbedPoly(); return (p && p->IsIndexed()) ? 1 : 0; }
int flux_poly_index_count(void) { PolyPrimitive* p = grabbedPoly(); return p ? (int) p->GetIndex().size() : 0; }
void flux_poly_indices(unsigned int* out, int n) {
  PolyPrimitive* p = grabbedPoly(); if (!p) return;
  std::vector<unsigned int>& idx = p->GetIndex();
  for (int i = 0; i < n && i < (int) idx.size(); ++i) out[i] = idx[i];
}
void flux_poly_set_index(const unsigned int* idx, int n) {
  PolyPrimitive* p = grabbedPoly(); if (!p) return;
  std::vector<unsigned int>& v = p->GetIndex();
  v.resize(n); for (int i = 0; i < n; ++i) v[i] = idx[i];
  p->SetIndexMode(true); p->BumpPDataVersion();
}
void flux_poly_convert_to_indexed(void) { if (PolyPrimitive* p = grabbedPoly()) p->ConvertToIndexed(); }

// ---- scene-graph queries ----------------------------------------------------
int flux_get_bb(double outmin[3], double outmax[3]) {
  if (!g_ctx.grabbed) return 0;
  dMatrix space; space.init();
  dBoundingBox bb = g_ctx.grabbed->GetBoundingBox(space);
  outmin[0] = bb.min.x; outmin[1] = bb.min.y; outmin[2] = bb.min.z;
  outmax[0] = bb.max.x; outmax[1] = bb.max.y; outmax[2] = bb.max.z;
  return 1;
}
int flux_get_parent(void) {
  if (!g_ctx.r || g_ctx.grabbedId < 0) return -1;
  Node* n = g_ctx.r->GetSceneGraph().FindNode(g_ctx.grabbedId);
  return (n && n->Parent) ? n->Parent->ID : -1;
}
namespace {
  Node* grabbedOrRoot() {
    if (!g_ctx.r) return nullptr;
    if (g_ctx.grabbedId < 0) return g_ctx.r->GetSceneGraph().Root();
    return g_ctx.r->GetSceneGraph().FindNode(g_ctx.grabbedId);
  }
}
int flux_get_children_count(void) { Node* n = grabbedOrRoot(); return n ? (int) n->Children.size() : 0; }
void flux_get_children(int* out, int n) {
  Node* node = grabbedOrRoot(); if (!node) return;
  for (int i = 0; i < n && i < (int) node->Children.size(); ++i) out[i] = node->Children[i]->ID;
}
void flux_recalc_bb(void) {
  if (!g_ctx.r || g_ctx.grabbedId < 0) return;
  SceneNode* n = (SceneNode*) g_ctx.r->GetSceneGraph().FindNode(g_ctx.grabbedId);
  if (n) g_ctx.r->GetSceneGraph().RecalcAABB(n);
}

// ---- primitive IO (OBJ) -----------------------------------------------------
int flux_load_primitive(const char* path) {
  if (!g_ctx.r || !path) return -1;
  Primitive* p = PrimitiveIO::Read(path);
  return p ? g_ctx.r->AddPrimitive(p) : -1;
}
void flux_save_primitive(const char* path) {
  if (!g_ctx.r || !g_ctx.grabbed || g_ctx.grabbedId < 0 || !path) return;
  PrimitiveIO::Write(path, g_ctx.grabbed, (unsigned) g_ctx.grabbedId, g_ctx.r->GetSceneGraph());
}

void flux_get_transform(double out[16]) {
  dMatrix m = g_ctx.grabbed ? g_ctx.grabbed->GetState()->Transform : g_ctx.tx;
  const float* a = m.arr(); for (int i = 0; i < 16; ++i) out[i] = a[i];
}
void flux_get_global_transform(double out[16]) {
  dMatrix m;   // identity if nothing grabbed / not in the graph
  if (g_ctx.r && g_ctx.grabbedId >= 0) {
    SceneNode* n = (SceneNode*) g_ctx.r->GetSceneGraph().FindNode(g_ctx.grabbedId);
    if (n) m = g_ctx.r->GetSceneGraph().GetGlobalTransform(n);
  }
  const float* a = m.arr(); for (int i = 0; i < 16; ++i) out[i] = a[i];
}

// ---- primitive functions (pfunc) + skinning ---------------------------------
// A pfunc is a named operation applied to the grabbed prim: "arithmetic" (pdata
// math), "genskinweights"/"skinning"/"skinweights->vertcols" (mesh skinning).
// We own the instances (upstream's PFuncContainer needs Engine, which the port
// lacks). make returns an int id; typed setters stash args; run applies it.
namespace { std::vector<PrimitiveFunction*> g_pfuncs;
  inline PrimitiveFunction* pf(int id) { return (id >= 0 && id < (int) g_pfuncs.size()) ? g_pfuncs[id] : nullptr; }
}
int flux_pfunc_make(const char* name) {
  std::string n = name ? name : "";
  PrimitiveFunction* p = nullptr;
  if      (n == "arithmetic")            p = new ArithmeticPrimFunc();
  else if (n == "genskinweights")        p = new GenSkinWeightsPrimFunc();
  else if (n == "skinweights->vertcols") p = new SkinWeightsToVertColsPrimFunc();
  else if (n == "skinning")              p = new SkinningPrimFunc();
  else return -1;   // upstream returns 0 (a valid id) for unknown; -1 is safer
  g_pfuncs.push_back(p);
  return (int) g_pfuncs.size() - 1;
}
void flux_pfunc_set_str(int id, const char* k, const char* v) { if (PrimitiveFunction* f = pf(id)) f->SetArg<std::string>(k, std::string(v ? v : "")); }
void flux_pfunc_set_int(int id, const char* k, int v)         { if (PrimitiveFunction* f = pf(id)) f->SetArg<int>(k, v); }
void flux_pfunc_set_float(int id, const char* k, double v)    { if (PrimitiveFunction* f = pf(id)) f->SetArg<float>(k, (float) v); }
void flux_pfunc_set_vec(int id, const char* k, double x, double y, double z)            { if (PrimitiveFunction* f = pf(id)) f->SetArg<dVector>(k, dVector((float) x, (float) y, (float) z)); }
void flux_pfunc_set_col(int id, const char* k, double r, double g, double b, double a)  { if (PrimitiveFunction* f = pf(id)) f->SetArg<dColour>(k, dColour((float) r, (float) g, (float) b, (float) a)); }
void flux_pfunc_run(int id) {
  PrimitiveFunction* f = pf(id);
  if (f && g_ctx.grabbed && g_ctx.r) f->Run(*g_ctx.grabbed, g_ctx.r->GetSceneGraph());
}
void flux_pfunc_clear(void) { for (PrimitiveFunction* p : g_pfuncs) delete p; g_pfuncs.clear(); }
