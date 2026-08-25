#include "FluxusCommands.h"

// engine + system GL only
#include "Renderer.h"
#include "PolyPrimitive.h"
#include "RibbonPrimitive.h"
#include "ParticlePrimitive.h"
#include "GraphicsUtils.h"
#include "Camera.h"
#include "State.h"
#include "dada.h"

#include <vector>
#include <mutex>

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
};
BuildCtx g_ctx;

std::mutex  g_errMutex;
std::string g_err;

std::mutex         g_audioMutex;
std::vector<float> g_bands;
double             g_gain = 0.0;

// mouse + orbit camera state (persists across frames)
struct CamState { double yaw = 0.3, pitch = 0.3, dist = 10.0; };
CamState g_cam;
double   g_mouseX = 0, g_mouseY = 0;
int      g_mouseButton = 0;

void applyCamera() {
  if (!g_ctx.r) return;
  auto& cams = g_ctx.r->GetCameraVec();
  if (cams.empty()) return;
  dMatrix rot;  rot.rotxyz((float) g_cam.pitch, (float) g_cam.yaw, 0);
  dMatrix back; back.translate(0, 0, (float) -g_cam.dist);
  dMatrix view = back * rot;          // rotate world, then push back from eye
  cams[0].SetMatrix(view);
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

void flux_report_error(const char* msg) {
  std::lock_guard<std::mutex> lk(g_errMutex);
  g_err = msg ? msg : "";
}

} // extern "C"

std::string flux_last_error() {
  std::lock_guard<std::mutex> lk(g_errMutex);
  return g_err;
}
