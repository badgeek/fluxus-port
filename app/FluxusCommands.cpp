#include "FluxusCommands.h"

// engine + system GL only
#include "Renderer.h"
#include "PolyPrimitive.h"
#include "RibbonPrimitive.h"
#include "ParticlePrimitive.h"
#include "GraphicsUtils.h"
#include "Camera.h"
#include "State.h"
#include "GLSLShader.h"
#include "dada.h"

#include <vector>
#include <mutex>
#include <cmath>
#include <map>
#include <string>

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
  // poly approximation (real NURBS CVs don't carry per-vertex normals for deform)
  PolyPrimitive* p = new PolyPrimitive(PolyPrimitive::TRILIST);
  MakeSphere(p, 1.0f, hseg > 0 ? hseg : 10, rseg > 0 ? rseg : 10);
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

// ---- pdata (grabbed primitive) --------------------------------------------
void flux_grab(int id)   { g_ctx.grabbed = g_ctx.r ? g_ctx.r->GetPrimitive(id) : nullptr; }
void flux_ungrab(void)   { g_ctx.grabbed = nullptr; }

int flux_pdata_size(void) { return g_ctx.grabbed ? (int) g_ctx.grabbed->Size() : 0; }

void flux_recalc_normals(void) { if (g_ctx.grabbed) g_ctx.grabbed->RecalculateNormals(false); }

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

void flux_set_fov(double vfovDeg) {
  Camera* c = cam0();
  if (!c) return;
  const double front  = 1.0;                                  // near clip
  const double t      = front * std::tan(vfovDeg * 0.5 * 3.14159265358979323846 / 180.0);
  const double aspect = (g_screenH > 0) ? (double) g_screenW / g_screenH : 4.0 / 3.0;
  const double r      = t * aspect;
  c->SetFrustum((float) -r, (float) r, (float) -t, (float) t);
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
void flux_set_resolution(int w, int h) { g_screenW = w; g_screenH = h; }
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

void flux_report_error(const char* msg) {
  std::lock_guard<std::mutex> lk(g_errMutex);
  g_err = msg ? msg : "";
}

} // extern "C"

std::string flux_last_error() {
  std::lock_guard<std::mutex> lk(g_errMutex);
  return g_err;
}
