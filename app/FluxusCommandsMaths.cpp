// SPDX-License-Identifier: AGPL-3.0-or-later
// Maths domain of the fluxus command layer: the pure vector/matrix/quaternion
// helpers, noise, and the node aim/orbit ops built on the same lookMat.
#include "FluxusCommands.h"
#include "FluxusCommandsInternal.h"

// engine only (no GL in this TU)
#include "Renderer.h"
#include "Primitive.h"
#include "State.h"
#include "dada.h"
#include "Noise.h"
#include "SimplexNoise.h"

#include <cmath>

using namespace Fluxus;

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

// ---- quaternions + node orientation (correct math; engine dQuat::dot/renorm/
// slerp are buggy, so we only reuse the SAFE dQuat::toMatrix / from-matrix, which
// agree with scheme qtomatrix — both m[row][col], row-vector v' = v*M) -----------
namespace {
struct Quat { double x, y, z, w; };
static Quat qn(Quat q) {
  double m = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
  return (m < 1e-12) ? Quat{0,0,0,1} : Quat{q.x/m, q.y/m, q.z/m, q.w/m};
}
// v' = v * toMatrix(q) — row-vector rotate, consistent with node transforms
static void qrot(const Quat& q, const double v[3], double out[3]) {
  dMatrix M = dQuat((float)q.x,(float)q.y,(float)q.z,(float)q.w).toMatrix();
  const float* a = M.arr();
  out[0] = v[0]*a[0] + v[1]*a[4] + v[2]*a[8];
  out[1] = v[0]*a[1] + v[1]*a[5] + v[2]*a[9];
  out[2] = v[0]*a[2] + v[1]*a[6] + v[2]*a[10];
}
// orientation matrix: local X->right, Y->up, Z->forward (row-vector, + translation).
static dMatrix lookMat(dVector fwd, dVector up, dVector pos) {
  fwd.normalise();
  dVector right = up.cross(fwd);
  if (right.mag() < 1e-6f) { up = dVector(0,0,1); right = up.cross(fwd);
    if (right.mag() < 1e-6f) { up = dVector(1,0,0); right = up.cross(fwd); } }
  right.normalise();
  dVector u = fwd.cross(right); u.normalise();
  return dMatrix((float)right.x,(float)u.x,(float)fwd.x,(float)pos.x,
                 (float)right.y,(float)u.y,(float)fwd.y,(float)pos.y,
                 (float)right.z,(float)u.z,(float)fwd.z,(float)pos.z,
                 0,0,0,1);
}
static void writeNodeTransform(int id, const dMatrix& m) {
  if (!g_ctx.r) return;
  if (Primitive* p = g_ctx.r->GetPrimitive(id)) p->GetState()->Transform = m;
}
} // namespace

void flux_q_slerp(const double a[4], const double b[4], double t, double out[4]) {
  Quat qa = qn({a[0],a[1],a[2],a[3]}), qb = qn({b[0],b[1],b[2],b[3]});
  double d = qa.x*qb.x + qa.y*qb.y + qa.z*qb.z + qa.w*qb.w;   // correct dot
  if (d < 0) { qb = {-qb.x,-qb.y,-qb.z,-qb.w}; d = -d; }
  Quat r;
  if (d > 0.9995) {                                          // nearly parallel -> nlerp
    r = qn({qa.x + t*(qb.x-qa.x), qa.y + t*(qb.y-qa.y),
            qa.z + t*(qb.z-qa.z), qa.w + t*(qb.w-qa.w)});
  } else {
    double ang = std::acos(d), s = std::sin(ang);
    double wa = std::sin((1-t)*ang)/s, wb = std::sin(t*ang)/s;
    r = {wa*qa.x + wb*qb.x, wa*qa.y + wb*qb.y, wa*qa.z + wb*qb.z, wa*qa.w + wb*qb.w};
  }
  out[0]=r.x; out[1]=r.y; out[2]=r.z; out[3]=r.w;
}
void flux_q_rotate_vec(const double q[4], const double v[3], double out[3]) {
  qrot(qn({q[0],q[1],q[2],q[3]}), v, out);
}
void flux_q_look_at(const double dir[3], const double up[3], double out[4]) {
  dMatrix M = lookMat(dVector((float)dir[0],(float)dir[1],(float)dir[2]),
                      dVector((float)up[0],(float)up[1],(float)up[2]), dVector(0,0,0));
  dQuat q(M); out[0]=q.x; out[1]=q.y; out[2]=q.z; out[3]=q.w;
}
void flux_q_from_matrix(const double m16[16], double out[4]) {
  dMatrix m; float* a = m.arr(); for (int i = 0; i < 16; ++i) a[i] = (float) m16[i];
  dQuat q(m); out[0]=q.x; out[1]=q.y; out[2]=q.z; out[3]=q.w;
}
void flux_node_look_at(int id, const double target[3], const double up[3]) {
  if (!g_ctx.r) return;
  Primitive* p = g_ctx.r->GetPrimitive(id); if (!p) return;
  dVector pos = p->GetState()->Transform.gettranslate();   // aim from current position
  dVector fwd = dVector((float)target[0],(float)target[1],(float)target[2]) - pos;
  writeNodeTransform(id, lookMat(fwd, dVector((float)up[0],(float)up[1],(float)up[2]), pos));
}
void flux_node_orbit(int id, double lonDeg, double latDeg, double radius, const double center[3]) {
  if (!g_ctx.r || !g_ctx.r->GetPrimitive(id)) return;
  const double d2r = 3.14159265358979323846 / 180.0;
  double lon = lonDeg * d2r, lat = latDeg * d2r;
  dVector c((float)center[0], (float)center[1], (float)center[2]);
  dVector pos((float)(c.x + radius*std::cos(lat)*std::cos(lon)),
              (float)(c.y + radius*std::sin(lat)),
              (float)(c.z + radius*std::cos(lat)*std::sin(lon)));
  writeNodeTransform(id, lookMat(c - pos, dVector(0,1,0), pos));
}
