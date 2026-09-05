// SPDX-License-Identifier: AGPL-3.0-or-later
// pdata domain of the fluxus command layer: the FFI channel cache, the per-
// element / bulk / whole-channel accessors, pdata-op, the native deformers and
// the primitive-function (pfunc) instances. See FluxusCommandsCore.cpp for the
// build context these all read through.
#include "FluxusCommands.h"
#include "FluxusCommandsInternal.h"

// engine only (no GL in this TU)
#include "Primitive.h"
#include "PData.h"
#include "dada.h"
#include "PrimitiveFunction.h"
#include "ArithmeticPrimFunc.h"
#include "GenSkinWeightsPrimFunc.h"
#include "SkinWeightsToVertColsPrimFunc.h"
#include "SkinningPrimFunc.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace Fluxus;

// --- pdata FFI channel cache -------------------------------------------------
// See FluxusCommandsInternal.h for why this exists; core drops it on
// grab/ungrab/frame-begin/destroy/scene-clear, and the structural channel
// commands below drop it on pdata-add / pdata-copy.
namespace {
PDataCacheEntry g_pdCache[4];
int g_pdCacheN = 0;
}

void pdataCacheClear() {
  g_pdCacheN = 0;
  for (auto& e : g_pdCache) { e.pd = nullptr; e.name.clear(); }
}

PDataCacheEntry* pdataResolve(const char* name) {
  Primitive* p = g_ctx.grabbed;
  if (!p || !name) return nullptr;
  for (int i = 0; i < g_pdCacheN; ++i)
    if (g_pdCache[i].name == name) return &g_pdCache[i];
  std::string n(name);
  char type = 'v'; unsigned size = 0;
  if (!p->GetDataInfo(n, type, size)) return nullptr;
  PData* pd = p->GetDataRaw(n);
  if (!pd) return nullptr;
  PDataCacheEntry& e = g_pdCache[g_pdCacheN < 4 ? g_pdCacheN++ : 0];
  e.name = std::move(n); e.pd = pd; e.type = type;
  return &e;
}

extern "C" {

int flux_pdata_size(void) {
  if (!g_ctx.grabbed) return 0;
  // a pixels prim's pdata "c" is its w*h pixel buffer, not the vertex count
  if (int npix = pixelsCount(g_ctx.grabbed)) return npix;
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

  const std::vector<float> bands = audioBands();
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

} // extern "C"

namespace {
struct CachedShape { std::vector<dVector> pos, nrm; };
std::map<std::string, CachedShape> g_shapeCache;
}

extern "C" {

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

  const std::vector<float> bands = audioBands();
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
  pdataCacheClear();
}
void flux_pdata_copy(const char* src, const char* dst) {
  if (g_ctx.grabbed && src && dst) { g_ctx.grabbed->CopyData(src, dst); pdataCacheClear(); }
}

double flux_pdata_get(const char* name, int i, int comp) {
  PDataCacheEntry* c = pdataResolve(name);
  if (!c || i < 0 || comp < 0 || comp > 3) return 0.0;
  return withChannel(c, [&](auto& d, int nc) -> double {
    const unsigned ui = (unsigned) i;
    if (ui >= d.size()) return 0.0;
    return elemArr(d, ui)[nc == 1 ? 0 : comp];   // float channel ignores comp
  });
}

void flux_pdata_set(const char* name, int i, int comp, double val) {
  PDataCacheEntry* c = pdataResolve(name);
  if (!c || i < 0 || comp < 0 || comp > 3) return;
  withChannel(c, [&](auto& d, int nc) {
    const unsigned ui = (unsigned) i;
    if (ui >= d.size()) return;
    elemArr(d, ui)[nc == 1 ? 0 : comp] = (float) val;   // float channel ignores comp
  });
  g_ctx.grabbed->BumpPDataVersion();   // keep the VBO re-upload invalidation SetData did
}

// Bulk per-vertex access: ONE FFI crossing per element instead of one per
// COMPONENT. get3 fills out[0..2] and returns the component count — 1 for a
// float channel (scalar: ribbon width "w", particle size "s"), 3 for
// vec/colour — so the Scheme wrapper needs no separate type query per element.
int flux_pdata_get3(const char* name, int i, double* out) {
  PDataCacheEntry* c = pdataResolve(name);
  if (!c || i < 0 || !out) return 0;
  return withChannel(c, [&](auto& d, int nc) -> int {
    const unsigned ui = (unsigned) i;
    if (ui >= d.size()) return 0;
    const float* a = elemArr(d, ui);
    out[0] = a[0];
    if (nc == 3) { out[1] = a[1]; out[2] = a[2]; } else { out[1] = out[2] = 0.0; }
    return nc;
  });
}

void flux_pdata_set3(const char* name, int i, double x, double y, double z) {
  PDataCacheEntry* c = pdataResolve(name);
  if (!c || i < 0) return;
  withChannel(c, [&](auto& d, int nc) {
    const unsigned ui = (unsigned) i;
    if (ui >= d.size()) return;
    float* a = elemArr(d, ui);
    // scalar channel mirrors the old per-component path: comps 0/1/2 all
    // landed on the same slot, so the last write (z) wins
    if (nc == 1) a[0] = (float) z;
    else { a[0] = (float) x; a[1] = (float) y; a[2] = (float) z; }
  });
  g_ctx.grabbed->BumpPDataVersion();
}

// Whole-channel copy for the Scheme pdata-map! fast path: TWO FFI crossings
// per map (read_all + write_all) instead of read+write per element. Layout is
// ncomp doubles per element, packed (ncomp = 1 for a float channel, 3 for
// vec/colour). read_all returns ncomp, 0 on error/too-small cap (cap in
// doubles). write_all ignores a ncomp mismatch (channel changed underneath).
int flux_pdata_read_all(const char* name, double* out, int cap) {
  PDataCacheEntry* c = pdataResolve(name);
  if (!c || !out) return 0;
  return withChannel(c, [&](auto& d, int nc) -> int {
    if ((int) (d.size() * (size_t) nc) > cap) return 0;
    for (size_t i = 0; i < d.size(); ++i) {
      const float* a = elemArr(d, i);
      for (int k = 0; k < nc; ++k) out[i * nc + k] = a[k];
    }
    return nc;
  });
}

void flux_pdata_write_all(const char* name, const double* in, int n, int ncomp) {
  PDataCacheEntry* c = pdataResolve(name);
  if (!c || !in || n < 0) return;
  withChannel(c, [&](auto& d, int nc) {
    if (ncomp != nc) return;
    const size_t lim = std::min((size_t) n, d.size());
    for (size_t i = 0; i < lim; ++i) {
      float* a = elemArr(d, i);
      for (int k = 0; k < nc; ++k) a[k] = (float) in[i * nc + k];
    }
  });
  g_ctx.grabbed->BumpPDataVersion();
}

} // extern "C"

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
