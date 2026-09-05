// SPDX-License-Identifier: AGPL-3.0-or-later
// Core of the shared fluxus command layer: the build context itself, the
// renderer/scene-graph plumbing, every build-* primitive, the state/transform/
// hint commands, grab/ungrab and the per-frame reset. The other domains live in
// FluxusCommands{Pdata,Maths,Terminal,Gpu,Input,Fx}.cpp — see
// FluxusCommandsInternal.h for what they share.
#include "FluxusCommands.h"
#include "FluxusCommandsInternal.h"

// engine + system GL only
#include "Renderer.h"
#include "PolyPrimitive.h"
#include "RibbonPrimitive.h"
#include "ParticlePrimitive.h"
#include "LocatorPrimitive.h"
#include "NURBSPrimitive.h"
#include <OpenGL/gl.h>
#include "GraphicsUtils.h"
#include "State.h"
#include "Light.h"
#include "SceneGraph.h"
#include "GLSLShader.h"
#include "dada.h"
#include "VoxelPrimitive.h"
#include "BlobbyPrimitive.h"
#include "PrimitiveIO.h"
#include "Tree.h"

#include <deque>
#include <vector>
#include <string>

using namespace Fluxus;

BuildCtx g_ctx;

namespace {
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
} // namespace

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

int addPrim(Primitive* p) {
  if (!g_ctx.r) { delete p; return -1; }
  // AddPrimitive copies the renderer's current State into the prim, so set the
  // prim's state AFTER adding it.
  int id = g_ctx.r->AddPrimitive(p);
  State* s = p->GetState();
  s->Transform = g_ctx.tx;
  s->Colour    = g_ctx.col;
  s->Hints     = (s->Hints | g_ctx.hints) & ~g_ctx.hintsOff;
  if (g_ctx.wireColSet) s->WireColour = g_ctx.wireCol;
  s->LineWidth = g_ctx.lineWidth;
  if (g_ctx.shader) setStateShader(s, g_ctx.shader);
  if (g_ctx.texture) s->Textures[0] = g_ctx.texture;
  s->SourceBlend = g_ctx.srcBlend;
  s->DestinationBlend = g_ctx.dstBlend;
  if (g_ctx.parent >= 0) g_ctx.r->GetSceneGraph().ReparentNode(id, g_ctx.parent);
  return id;
}

extern "C" {

void flux_set_renderer(void* renderer) { g_ctx.r = static_cast<Renderer*>(renderer); }

// (clear): wipe the scene graph. In immediate mode the host already clears each
// frame (so a top-of-script (clear) is a harmless redundant wipe); in RETAINED
// mode the host does NOT clear, so a sketch that rebuilds every frame calls
// (clear) itself at the top of its every-frame thunk. Retained + (clear) lets a
// heavy sketch compile once and only re-run its thunk (no per-frame re-parse of
// the whole program), which is far cheaper than immediate mode.
void flux_scene_clear(void) { flux_free_terminals(); pdataCacheClear(); if (g_ctx.r) g_ctx.r->Clear(); }

// (destroy id): remove one primitive by id. Lets a RETAINED sketch keep static
// geometry alive across frames while destroying + rebuilding only its animated
// prims each frame (persistent scene) — far cheaper than clearing + rebuilding
// everything. Resets our build-context grab if it pointed at the removed prim
// (the renderer clears its own m_Grabbed inside RemovePrimitive).
void flux_destroy(int id) {
  if (!g_ctx.r) return;
  Primitive* p = g_ctx.r->GetPrimitive(id);
  if (g_ctx.grabbed && p == g_ctx.grabbed) { g_ctx.grabbed = nullptr; pdataCacheClear(); }
  if (p) {
    // free per-prim resources we own (else they leak — the renderer only frees the
    // Primitive itself). The pixels prim's GL texture is left to GL teardown as before.
    terminalErase(p);
    pixelsErase(p);
  }
  g_ctx.r->RemovePrimitive(id);
}

void flux_frame_begin(double t, int frame) {
  g_ctx.time  = t;
  g_ctx.frame = frame;
  g_ctx.tx    = dMatrix();
  g_ctx.col   = dColour(1, 1, 1, 1);
  g_ctx.stack.clear();
  g_ctx.hints = 0;
  g_ctx.hintsOff = 0;
  g_ctx.wireColSet = false;
  g_ctx.lineWidth = 2.0f;
  g_ctx.grabbed = nullptr;
  g_ctx.grabbedId = -1;
  pdataCacheClear();
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

static void applyOp(const dMatrix& op) {
  if (State* s = grabbedState()) s->Transform = s->Transform * op;
  else                           g_ctx.tx = g_ctx.tx * op;
}

void flux_colour(double r, double g, double b) {
  const dColour c = makeCol(r, g, b);
  if (State* s = grabbedState()) s->Colour = c; else g_ctx.col = c;
}
void flux_colour_mode(int mode) {
  COLOUR_MODE m = (mode == 1) ? MODE_HSV : MODE_RGB;
  if (State* s = grabbedState()) s->ColourMode = m; else g_ctx.colourMode = m;
}
void flux_hsv_to_rgb(const double hsv[3], double rgb[3]) {
  float out[3]; dColour::HSVtoRGB((float) hsv[0], (float) hsv[1], (float) hsv[2], out);
  rgb[0] = out[0]; rgb[1] = out[1]; rgb[2] = out[2];
}
void flux_rgb_to_hsv(const double rgb[3], double hsv[3]) {
  float out[3]; dColour::RGBtoHSV((float) rgb[0], (float) rgb[1], (float) rgb[2], out);
  hsv[0] = out[0]; hsv[1] = out[1]; hsv[2] = out[2];
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
  else if (on) { g_ctx.hints |= bit;  g_ctx.hintsOff &= ~bit; }
  else         { g_ctx.hints &= ~bit; g_ctx.hintsOff |= bit;  }
}
void flux_hint_wire(int on)  { setHint(HINT_WIRE,  on); }
void flux_hint_solid(int on) { setHint(HINT_SOLID, on); }
void flux_line_width(double w) {
  if (State* s = grabbedState()) s->LineWidth = (float) w; else g_ctx.lineWidth = (float) w;
}
void flux_opacity(double o)      { if (State* s = grabbedState()) s->Opacity = (float) o; }
void flux_wire_opacity(double o) { if (State* s = grabbedState()) s->WireOpacity = (float) o; }
void flux_wire_colour(double r, double g, double b) {
  if (State* s = grabbedState()) s->WireColour = makeCol(r, g, b);
  else { g_ctx.wireCol = makeCol(r, g, b); g_ctx.wireColSet = true; }
}
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

// Merge N same-type poly prims into ONE — fewer draws, which is the only real
// lever on Metal-emulated GL where per-draw state dispatch dominates (see
// CLAUDE.md perf notes). Bakes each source's State.Transform into positions/
// normals and its State.Colour (× Opacity into alpha) into per-vertex colours;
// render the merged prim with (hint-vertcols) so the baked colours are used.
// Sources are left untouched — (destroy) them after. Only PolyPrimitives whose
// type matches the FIRST valid source merge (QUADS with QUADS, …); indexed
// sources are expanded through their index. Non-uniform scale slightly skews
// baked normals (no inverse-transpose) — irrelevant for unlit/wire looks.
int flux_build_merged(const int* ids, int n) {
  if (!g_ctx.r || !ids || n <= 0) return -1;
  PolyPrimitive* dst = nullptr;
  int type = -1;
  for (int k = 0; k < n; ++k) {
    PolyPrimitive* p = dynamic_cast<PolyPrimitive*>(g_ctx.r->GetPrimitive(ids[k]));
    if (!p) continue;
    if (type == -1) { type = (int) p->GetType(); dst = new PolyPrimitive((PolyPrimitive::Type) type); }
    if ((int) p->GetType() != type) continue;
    const dMatrix tx = p->GetState()->Transform;
    dColour col = p->GetState()->Colour;
    col.a *= p->GetState()->Opacity;
    auto* vp = p->GetDataVec<dVector>("p");
    auto* vn = p->GetDataVec<dVector>("n");
    auto* vt = p->GetDataVec<dVector>("t");
    if (!vp) continue;
    auto emit = [&](unsigned i) {
      dVector pos = tx.transform((*vp)[i]);
      dVector nrm(0, 1, 0);
      if (vn && i < vn->size()) { nrm = tx.transform_no_trans((*vn)[i]); nrm.normalise(); }
      const dVector uv = (vt && i < vt->size()) ? (*vt)[i] : dVector(0, 0, 0);
      dst->AddVertex(dVertex(pos, nrm, col, uv.x, uv.y));
    };
    if (p->IsIndexed()) { for (unsigned idx : p->GetIndexConst()) if (idx < vp->size()) emit(idx); }
    else                { for (unsigned i = 0; i < vp->size(); ++i) emit(i); }
  }
  return dst ? addPrim(dst) : -1;
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

// ---- material (grabbed primitive) ------------------------------------------
void flux_specular(double r, double g, double b)      { if (State* s = grabbedState()) s->Specular = dColour((float) r, (float) g, (float) b, 1); }
void flux_ambient(double r, double g, double b)       { if (State* s = grabbedState()) s->Ambient  = dColour((float) r, (float) g, (float) b, 1); }
void flux_emissive(double r, double g, double b)      { if (State* s = grabbedState()) s->Emissive = dColour((float) r, (float) g, (float) b, 1); }
void flux_shinyness(double v)                         { if (State* s = grabbedState()) s->Shinyness = (float) v; }
void flux_normal_colour(double r, double g, double b) { if (State* s = grabbedState()) s->NormalColour = dColour((float) r, (float) g, (float) b, 1); }
void flux_point_width(double w)                       { if (State* s = grabbedState()) s->PointWidth = (float) w; }

// ---- render hints ----------------------------------------------------------
void flux_hint_none(void)          { if (State* s = grabbedState()) s->Hints = 0; else { g_ctx.hints = 0; g_ctx.hintsOff = ~0; } }
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
void flux_light_spot_exponent(int id, double e)                { if (Light* l = light(id)) l->SetSpotExponent((float) e); }
void flux_light_attenuation(int id, int type, double v)        { if (Light* l = light(id)) l->SetAttenuation(type, (float) v); } // 0 const,1 linear,2 quad

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

// ---- grab (the pdata / state target) ---------------------------------------
void flux_grab(int id)   { g_ctx.grabbed = g_ctx.r ? g_ctx.r->GetPrimitive(id) : nullptr; g_ctx.grabbedId = g_ctx.grabbed ? id : -1; pdataCacheClear(); }
void flux_ungrab(void)   { g_ctx.grabbed = nullptr; g_ctx.grabbedId = -1; pdataCacheClear(); }

double flux_time(void)  { return g_ctx.time; }
int    flux_frame(void) { return g_ctx.frame; }
double flux_delta(void) { return g_ctx.r ? g_ctx.r->GetDelta() : 0.0; }

} // extern "C"

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
void flux_set_transform(const double m16[16]) {
  if (!m16) return;
  dMatrix m; float* a = m.arr();
  for (int i = 0; i < 16; ++i) a[i] = (float) m16[i];
  if (State* s = grabbedState()) s->Transform = m; else g_ctx.tx = m;
}
