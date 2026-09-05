// SPDX-License-Identifier: AGPL-3.0-or-later
// Regression tests for the vendored dada math fixes (dMatrix::inverse,
// dQuat::dot/renorm/slerp). Five ~20-year-old upstream bugs were found here in
// one session — run this after ANY change to vendor/fluxus/libfluxus/src/dada.*
// Exits non-zero on failure.
//
//   cmake --build build --target math_test && ./build/math_test
#include "dada.h"
#include "Geometry.h"
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace Fluxus;

static int fails = 0;
static void expect(const char* what, float diff, float tol) {
  std::printf("%-46s max diff %.3e %s\n", what, diff, diff <= tol ? "OK" : "FAIL");
  if (diff > tol) fails = 1;
}
static float maxdiff(const float* a, const float* b) {
  float md = 0; for (int i = 0; i < 16; ++i) md = std::max(md, std::fabs(a[i]-b[i])); return md;
}

// ---- ground truth: MESA gluInvertMatrix + naive column-major multiply -------
static void mul4(float* out, const float* a, const float* b) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k*4+r] * b[c*4+k];
      out[c*4+r] = s;
    }
}
static bool invert4(float* o, const float* m) {
  float inv[16];
  inv[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
  inv[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
  inv[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
  inv[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
  inv[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
  inv[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
  inv[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
  inv[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
  inv[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
  inv[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
  inv[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
  inv[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
  inv[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
  inv[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
  inv[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
  inv[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
  float det = m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
  if (det == 0.0f) return false;
  det = 1.0f/det;
  for (int i = 0; i < 16; ++i) o[i] = inv[i]*det;
  return true;
}

static void testMatrix() {
  // GL column-major view (rigid) + 12deg-fov projection + scaled transform
  const float fov = 12.0f * 3.14159265f/180.0f, aspect = 1.64f, zn = 0.1f, zf = 100.0f;
  const float f = 1.0f/std::tan(fov/2);
  float pr[16] = { f/aspect,0,0,0,  0,f,0,0,  0,0,(zf+zn)/(zn-zf),-1,  0,0,2*zf*zn/(zn-zf),0 };
  const float a = 0.7f, ca = std::cos(a), sa = std::sin(a);
  float mv[16] = { ca,0,-sa,0,  0,1,0,0,  sa,0,ca,0,  0.3f,-1.2f,-8.0f,1 };
  float sc[16] = { 2*ca,0,-2*sa,0,  0,3,0,0,  0.5f*sa,0,0.5f*ca,0,  1,2,3,1 };

  // self-inverse: the old inverse() was off by ~det (row 4 undivided, det^3)
  for (auto [name, src] : { std::pair<const char*, float*>{"proj",pr}, {"vp",nullptr}, {"scaled",sc} }) {
    float vp[16];
    const float* mm = src;
    if (!mm) { mul4(vp, pr, mv); mm = vp; }
    dMatrix d; std::memcpy(&d.m[0][0], mm, 64);
    dMatrix prod = d * d.inverse();
    float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    char buf[64]; std::snprintf(buf, sizeof buf, "M*M.inverse() == I  (%s)", name);
    // dada a*b is the storage product b.a, so prod storage IS inv.M — identity either way
    expect(buf, maxdiff(&prod.m[0][0], ident), 1e-4f);
  }

  // FluxusScene reprojection recipe: memcpy GL arrays in, dPr*dMv == mul4(pr,mv),
  // memcpy'd-out inverse == invert4 (inverse-of-transpose = transpose-of-inverse)
  float vp[16], vpInvA[16];
  mul4(vp, pr, mv);
  invert4(vpInvA, vp);
  dMatrix dPr, dMv;
  std::memcpy(&dPr.m[0][0], pr, sizeof pr);
  std::memcpy(&dMv.m[0][0], mv, sizeof mv);
  dMatrix dVP = dPr * dMv;
  expect("mul4(pr,mv) == memcpy (dPr*dMv)", maxdiff(vp, &dVP.m[0][0]), 1e-4f);
  dMatrix dInv = dVP.inverse();
  expect("invert4(vp) == memcpy dVP.inverse()", maxdiff(vpInvA, &dInv.m[0][0]), 1e-4f);
}

static void testQuat() {
  // dot: was z*q.x (typo)
  dQuat a(0.1f, -0.4f, 0.7f, 0.2f), b(0.3f, 0.5f, -0.2f, 0.9f);
  float manual = 0.1f*0.3f + -0.4f*0.5f + 0.7f*-0.2f + 0.2f*0.9f;
  expect("dQuat::dot == manual", std::fabs(a.dot(b) - manual), 1e-6f);
  // renorm: was divide by norm^2 (missing sqrt)
  dQuat c(2, 0, 0, 2); c.renorm();
  expect("renorm |q| == 1", std::fabs(std::sqrt(c.x*c.x+c.y*c.y+c.z*c.z+c.w*c.w) - 1.0f), 1e-6f);
  expect("renorm direction", std::fabs(c.x - 0.70710678f) + std::fabs(c.w - 0.70710678f), 1e-5f);
  // slerp: was from*cos + TO*sin (orthonormalised q computed then unused)
  dQuat id, y90; y90.setAxisAngle(dVector(0,1,0), 90.0f);
  dQuat mid = slerp(id, y90, 0.5f);
  const float s225 = std::sin(22.5f*3.14159265f/180.0f), c225 = std::cos(22.5f*3.14159265f/180.0f);
  expect("slerp t=0.5 == 45deg about Y",
         std::fabs(mid.x) + std::fabs(mid.y - s225) + std::fabs(mid.z) + std::fabs(mid.w - c225), 1e-4f);
  expect("slerp result unit",
         std::fabs(std::sqrt(mid.x*mid.x+mid.y*mid.y+mid.z*mid.z+mid.w*mid.w) - 1.0f), 1e-5f);
  dQuat e0 = slerp(id, y90, 0.0f), e1 = slerp(id, y90, 1.0f);
  expect("slerp t=0 == from", std::fabs(e0.w-1.0f)+std::fabs(e0.x)+std::fabs(e0.y)+std::fabs(e0.z), 1e-4f);
  expect("slerp t=1 == to", std::fabs(e1.y-y90.y)+std::fabs(e1.w-y90.w)+std::fabs(e1.x)+std::fabs(e1.z), 1e-4f);
}

// ---- independent reference: Rodrigues rotation in dada's row-vector layout --
// dada stores m[i][j] with i = INPUT axis and j = OUTPUT axis (v' = v*M), so a
// rotation about a unit axis is the TRANSPOSE of the textbook column-vector
// Rodrigues matrix. Built from the formula, not from dada's own sign patterns.
static void refAxisAngle(float M[4][4], float nx, float ny, float nz, float rad) {
  const float l = std::sqrt(nx*nx + ny*ny + nz*nz); nx/=l; ny/=l; nz/=l;
  const float c = std::cos(rad), s = std::sin(rad), t = 1 - c;
  const float R[3][3] = {                       // column-vector R = Ic + s[n]x + t nn^T
    { t*nx*nx + c,    t*nx*ny - s*nz, t*nx*nz + s*ny },
    { t*nx*ny + s*nz, t*ny*ny + c,    t*ny*nz - s*nx },
    { t*nx*nz - s*ny, t*ny*nz + s*nx, t*nz*nz + c    }};
  std::memset(M, 0, sizeof(float)*16);
  for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) M[i][j] = R[j][i];
  M[3][3] = 1;
}
static void refMul(float o[4][4], const float a[4][4], const float b[4][4]) {
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0; for (int k = 0; k < 4; ++k) s += a[i][k]*b[k][j];
      o[i][j] = s;
    }
}
static float vdiff(const dVector& a, float x, float y, float z) {
  return std::max(std::max(std::fabs(a.x-x), std::fabs(a.y-y)), std::fabs(a.z-z));
}
static const float D2R = 3.14159265f/180.0f;

// ---- dVector ---------------------------------------------------------------
static void testVector() {
  dVector X(1,0,0), Y(0,1,0), Z(0,0,1);
  expect("cross: X*Y == Z (right handed)", vdiff(X.cross(Y),0,0,1), 0);
  expect("cross: Y*Z == X", vdiff(Y.cross(Z),1,0,0), 0);
  expect("cross: Z*X == Y", vdiff(Z.cross(X),0,1,0), 0);
  dVector a(0.3f,-1.7f,2.2f), b(-4.0f,0.5f,1.25f);
  expect("cross anticommutes", vdiff(a.cross(b)+b.cross(a),0,0,0), 1e-6f);
  expect("cross perpendicular to both",
         std::fabs(a.cross(b).dot(a)) + std::fabs(a.cross(b).dot(b)), 1e-5f);
  // |a x b|^2 == |a|^2|b|^2 - (a.b)^2  (Lagrange)
  expect("|a x b| == Lagrange identity",
         std::fabs(a.cross(b).magsq() - (a.magsq()*b.magsq() - a.dot(b)*a.dot(b))), 1e-4f);
  expect("dot == manual",
         std::fabs(a.dot(b) - (0.3f*-4.0f + -1.7f*0.5f + 2.2f*1.25f)), 1e-6f);

  dVector n = a; n.normalise();
  expect("normalise: unit length", std::fabs(n.mag()-1.0f), 1e-6f);
  expect("normalise: same direction", vdiff(n*a.mag(), a.x, a.y, a.z), 1e-5f);
  dVector zero(0,0,0); zero.normalise();
  expect("normalise(0) stays finite",
         (std::isfinite(zero.x) && std::isfinite(zero.y) && std::isfinite(zero.z)) ? 0.0f : 1.0f, 0);

  // reflect(n): mirror about the plane with normal n
  dVector inc(1,-1,0);
  expect("reflect (1,-1,0) about +Y == (1,1,0)", vdiff(inc.reflect(Y),1,1,0), 1e-6f);
  dVector nn = b; nn.normalise();
  dVector r1 = a.reflect(nn), r2 = r1.reflect(nn);
  expect("reflect preserves length", std::fabs(r1.mag() - a.mag()), 1e-5f);
  expect("reflect twice == identity", vdiff(r2, a.x, a.y, a.z), 1e-5f);

  expect("dist == |a-b|", std::fabs(a.dist(b) - (a-b).mag()), 1e-5f);
  expect("distsq == dist^2", std::fabs(a.distsq(b) - a.dist(b)*a.dist(b)), 1e-4f);
  expect("lerp t=0.25", vdiff(lerp(a,b,0.25f), a.x+(b.x-a.x)*0.25f,
                              a.y+(b.y-a.y)*0.25f, a.z+(b.z-a.z)*0.25f), 1e-6f);
}

// ---- dMatrix: rotate / translate / scale / transform / transpose ------------
static void testMatrixOps() {
  // closed form: rotxyz is right-handed CCW about each axis (row-vector v' = v*M)
  { dMatrix m; m.rotxyz(0,0,90);
    expect("rotxyz z=90: X -> Y", vdiff(m.transform(dVector(1,0,0)),0,1,0), 1e-6f); }
  { dMatrix m; m.rotxyz(90,0,0);
    expect("rotxyz x=90: Y -> Z", vdiff(m.transform(dVector(0,1,0)),0,0,1), 1e-6f); }
  { dMatrix m; m.rotxyz(0,90,0);
    expect("rotxyz y=90: Z -> X", vdiff(m.transform(dVector(0,0,1)),1,0,0), 1e-6f); }

  // full 3-axis composition against Rz*Ry*Rx built from Rodrigues
  { dMatrix m; m.rotxyz(23,-41,67);
    float rx[4][4], ry[4][4], rz[4][4], t[4][4], ref[4][4];
    refAxisAngle(rx,1,0,0, 23*D2R); refAxisAngle(ry,0,1,0,-41*D2R); refAxisAngle(rz,0,0,1, 67*D2R);
    refMul(t, rz, ry); refMul(ref, t, rx);
    expect("rotxyz(23,-41,67) == Rz*Ry*Rx", maxdiff(m.arr(), &ref[0][0]), 1e-5f); }

  // the single-axis helpers must agree with the matching rotxyz branch
  { dMatrix a,b; a.rotx(37); b.rotxyz(37,0,0);
    expect("rotx(a) == rotxyz(a,0,0)", maxdiff(a.arr(), b.arr()), 1e-6f); }
  { dMatrix a,b; a.roty(37); b.rotxyz(0,37,0);
    expect("roty(a) == rotxyz(0,a,0)", maxdiff(a.arr(), b.arr()), 1e-6f); }
  { dMatrix a,b; a.rotz(37); b.rotxyz(0,0,37);
    expect("rotz(a) == rotxyz(0,0,a)", maxdiff(a.arr(), b.arr()), 1e-6f); }

  // translate / scale / no_trans
  { dMatrix m; m.translate(1,2,3);
    expect("translate: origin -> t", vdiff(m.transform(dVector(0,0,0)),1,2,3), 0);
    expect("translate: p -> p+t", vdiff(m.transform(dVector(-1,5,0.5f)),0,7,3.5f), 1e-6f);
    expect("transform_no_trans ignores translation",
           vdiff(m.transform_no_trans(dVector(-1,5,0.5f)),-1,5,0.5f), 0);
    expect("gettranslate round-trips settranslate", vdiff(m.gettranslate(),1,2,3), 0); }
  { dMatrix m; m.scale(2,3,4);
    expect("scale: (1,1,1) -> (2,3,4)", vdiff(m.transform(dVector(1,1,1)),2,3,4), 0); }
  // script order (translate ...)(rotate ...) = rotate applied first, then offset
  { dMatrix m; m.translate(10,0,0); m.rotxyz(0,0,90);
    expect("translate then rotxyz: X -> (10,1,0)", vdiff(m.transform(dVector(1,0,0)),10,1,0), 1e-5f); }

  // transform_persp: GL projection, near plane -> NDC z=-1, far plane -> +1
  { const float zn = 0.1f, zf = 100.0f, f = 1.0f/std::tan(30*D2R);
    dMatrix p; p.zero();
    p.m[0][0] = f/1.5f; p.m[1][1] = f;
    p.m[2][2] = (zf+zn)/(zn-zf); p.m[2][3] = -1; p.m[3][2] = 2*zf*zn/(zn-zf);
    expect("transform_persp: near plane -> ndc z=-1",
           std::fabs(p.transform_persp(dVector(0,0,-zn)).z + 1.0f), 1e-4f);
    expect("transform_persp: far plane -> ndc z=+1",
           std::fabs(p.transform_persp(dVector(0,0,-zf)).z - 1.0f), 1e-4f);
    dVector clip = p.transform(dVector(0.4f,-0.2f,-3.0f));
    dVector ndc  = p.transform_persp(dVector(0.4f,-0.2f,-3.0f));
    expect("transform_persp == transform + homog",
           vdiff(ndc, clip.x/clip.w, clip.y/clip.w, clip.z/clip.w), 1e-5f); }

  // transpose
  { dMatrix m; m.rotxyz(11,22,33); m.settranslate(dVector(4,5,6));
    dMatrix t = m.getTranspose();
    float md = 0;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) md = std::max(md, std::fabs(t.m[i][j]-m.m[j][i]));
    expect("getTranspose == m[j][i]", md, 0);
    dMatrix tt = t; tt.transpose();
    expect("transpose twice == original", maxdiff(tt.arr(), m.arr()), 0); }
  { dMatrix r; r.rotxyz(11,22,33);
    dMatrix rt = r.getTranspose(), prod = r*rt;
    const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    expect("R * R^T == I (rotation orthonormal)", maxdiff(prod.arr(), ident), 1e-5f); }

  // GL array round-trip (the port memcpys GL float[16] straight into dMatrix)
  { dMatrix m; m.rotxyz(11,22,33); m.settranslate(dVector(4,5,6));
    float gl[16]; m.load_glmatrix(gl);
    expect("load_glmatrix == arr() (no transpose)", maxdiff(gl, m.arr()), 0);
    dMatrix back; back.load_dMatrix(gl);
    expect("load_dMatrix(load_glmatrix) round-trip", maxdiff(back.arr(), m.arr()), 0); }

  // determinant
  { dMatrix r; r.rotxyz(11,22,33);
    expect("det(rotation) == 1", std::fabs(r.determinant()-1.0f), 1e-5f);
    dMatrix s; s.scale(2,3,4);
    expect("det(scale 2,3,4) == 24", std::fabs(s.determinant()-24.0f), 1e-4f);
    dMatrix rs; rs.rotxyz(11,22,33); rs.scale(2,3,4); rs.settranslate(dVector(4,5,6));
    expect("det(rot*scale+trans) == 24", std::fabs(rs.determinant()-24.0f), 1e-3f); }

  // get_scale / remove_scale: script order (rotate)(scale) -> storage S*R, so
  // row i of the matrix is s_i * (unit basis row i)
  { dMatrix m; m.rotxyz(0,35,0); m.scale(2,3,4);
    expect("get_scale of rotate+scale == (2,3,4)", vdiff(m.get_scale(),2,3,4), 1e-4f);
    dMatrix r = m; r.remove_scale();
    dMatrix pure; pure.rotxyz(0,35,0);
    expect("remove_scale recovers the rotation", maxdiff(r.arr(), pure.arr()), 1e-4f); }

  // extract_euler must invert rotxyz (angles inside the atan/asin principal range)
  { dMatrix m; m.rotxyz(20,-35,50);
    float ex, ey, ez; m.extract_euler(ex, ey, ez);
    expect("extract_euler inverts rotxyz(20,-35,50)",
           std::fabs(ex-20.0f) + std::fabs(ey+35.0f) + std::fabs(ez-50.0f), 1e-2f); }

  // aim: orthonormal right-handed frame, local X down dir, local Z towards up
  { dMatrix m; m.aim(dVector(2,0,0), dVector(0,1,0));
    dVector ax = m.get_vert_i(), ay = m.get_vert_j(), az = m.get_vert_k();
    expect("aim: local X == normalised dir", vdiff(ax,1,0,0), 1e-5f);
    expect("aim: rows orthonormal",
           std::fabs(ax.dot(ay)) + std::fabs(ax.dot(az)) + std::fabs(ay.dot(az)) +
           std::fabs(ax.mag()-1) + std::fabs(ay.mag()-1) + std::fabs(az.mag()-1), 1e-5f);
    expect("aim: right handed (X cross Y == Z)", vdiff(ax.cross(ay), az.x, az.y, az.z), 1e-5f);
    expect("aim: local Z points towards up", vdiff(az,0,1,0), 1e-5f); }
  { dMatrix m; m.aim(dVector(0,0,-3), dVector(0,1,0));
    dVector ax = m.get_vert_i(), az = m.get_vert_k();
    expect("aim(-Z,up): local X == -Z", vdiff(ax,0,0,-1), 1e-5f);
    expect("aim(-Z,up): local Z still up", vdiff(az,0,1,0), 1e-5f); }
}

// ---- dQuat: axis/angle, matrix conversions, products -----------------------
static void testQuatConv() {
  // toMatrix must agree with the rotxyz of the same rotation
  { dQuat q; q.setAxisAngle(dVector(0,1,0), 30.0f);
    dMatrix qm = q.toMatrix(), rm; rm.rotxyz(0,30,0);
    expect("q(Y,30).toMatrix() == rotxyz(0,30,0)", maxdiff(qm.arr(), rm.arr()), 1e-5f); }
  { dQuat q; q.setAxisAngle(dVector(1,1,1), 137.0f);
    dMatrix qm = q.toMatrix();
    float ref[4][4]; refAxisAngle(ref,1,1,1,137*D2R);
    expect("q(diag,137).toMatrix() == Rodrigues", maxdiff(qm.arr(), &ref[0][0]), 1e-5f); }

  // setAxisAngle takes DEGREES; toAxisAngle must give the same units back
  { dQuat q; q.setAxisAngle(dVector(0,0,1), 40.0f);
    dVector axis; float angle = 0; q.toAxisAngle(axis, angle);
    expect("toAxisAngle: axis round-trip", vdiff(axis,0,0,1), 1e-4f);
    expect("toAxisAngle: angle round-trip (degrees)", std::fabs(angle-40.0f), 1e-2f); }

  // from-matrix ctor must invert toMatrix
  { dQuat q; q.setAxisAngle(dVector(0.3f,-0.5f,0.81f), 62.0f); q.renorm();
    dQuat back(q.toMatrix());
    expect("dQuat(toMatrix(q)) == q",
           std::fabs(back.x-q.x)+std::fabs(back.y-q.y)+std::fabs(back.z-q.z)+std::fabs(back.w-q.w), 1e-4f); }
  { // past 180deg the ctor returns the equivalent -q, so compare via the matrix
    dQuat q; q.setAxisAngle(dVector(1,0,0), 200.0f); q.renorm();
    dMatrix m = q.toMatrix();
    expect("dQuat(m).toMatrix() == m (200deg)", maxdiff(dQuat(m).toMatrix().arr(), m.arr()), 1e-4f); }
  { // trace+1 == 0 only at exactly 180deg — the ctor's second branch
    dQuat q; q.setAxisAngle(dVector(0,1,0), 180.0f);
    dMatrix m = q.toMatrix();
    expect("dQuat(m).toMatrix() == m (180deg, 2nd branch)",
           maxdiff(dQuat(m).toMatrix().arr(), m.arr()), 1e-4f); }

  // Hamilton product: R(a*b) = R(a)R(b) for column vectors, and dada's
  // storage-transposed a*b is exactly toMatrix(a)*toMatrix(b) in dada's operator
  { dQuat a, b; a.setAxisAngle(dVector(0,1,0), 35.0f); b.setAxisAngle(dVector(1,0,0), -20.0f);
    dMatrix prod = a.toMatrix() * b.toMatrix();
    expect("toMatrix(a*b) == toMatrix(a)*toMatrix(b)", maxdiff((a*b).toMatrix().arr(), prod.arr()), 1e-5f); }
  { dQuat a; a.setAxisAngle(dVector(0.2f,0.9f,-0.4f), 77.0f);
    dQuat i = a * a.conjugate();
    expect("q * conjugate(q) == identity",
           std::fabs(i.x)+std::fabs(i.y)+std::fabs(i.z)+std::fabs(i.w-1.0f), 1e-5f); }
}

// ---- Geometry.cpp: ray/triangle + point/line -------------------------------
static void testGeometry() {
  const dVector ta(0,0,0), tb(1,0,0), tc(0,1,0);
  dVector bary;
  // centre-ish hit: I=(0.2,0.2,0) => tc + 0.6*(ta-tc) + 0.2*(tb-tc)
  { float r = IntersectLineTriangle(dVector(0.2f,0.2f,3), dVector(0.2f,0.2f,-1), ta,tb,tc, bary);
    expect("tri hit: parametric r == 0.75", std::fabs(r-0.75f), 1e-5f);
    expect("tri hit: barycentric (a,b,c)", vdiff(bary,0.6f,0.2f,0.2f), 1e-5f);
    dVector p = ta*bary.x + tb*bary.y + tc*bary.z;
    expect("tri hit: bary reconstructs the point", vdiff(p,0.2f,0.2f,0), 1e-5f); }
  // reversed winding still hits (selection must not depend on facing)
  { float r = IntersectLineTriangle(dVector(0.2f,0.2f,3), dVector(0.2f,0.2f,-1), ta,tc,tb, bary);
    expect("tri hit: reversed winding still hits", std::fabs(r-0.75f), 1e-5f); }
  // miss outside the hypotenuse
  { float r = IntersectLineTriangle(dVector(0.8f,0.8f,1), dVector(0.8f,0.8f,-1), ta,tb,tc, bary);
    expect("tri miss: outside hypotenuse -> -1", std::fabs(r+1.0f), 0); }
  // graze the ta-tb edge exactly
  { float r = IntersectLineTriangle(dVector(0.5f,0,1), dVector(0.5f,0,-1), ta,tb,tc, bary);
    expect("tri graze: on edge still hits", std::fabs(r-0.5f), 1e-5f); }
  // plane behind the segment end
  { float r = IntersectLineTriangle(dVector(0.2f,0.2f,1), dVector(0.2f,0.2f,0.5f), ta,tb,tc, bary);
    expect("tri: plane past segment end -> -1", std::fabs(r+1.0f), 0); }
  // ray parallel to the triangle plane
  { float r = IntersectLineTriangle(dVector(0.2f,0.2f,1), dVector(0.7f,0.7f,1), ta,tb,tc, bary);
    expect("tri: parallel ray -> -1", std::fabs(r+1.0f), 0); }
  // degenerate triangle
  { float r = IntersectLineTriangle(dVector(0,0,1), dVector(0,0,-1), ta,tb,tb, bary);
    expect("tri: degenerate -> -1", std::fabs(r+1.0f), 0); }

  expect("PointLineDist: perpendicular",
         std::fabs(PointLineDist(dVector(0,1,0), dVector(-1,0,0), dVector(1,0,0)) - 1.0f), 1e-5f);
  expect("PointLineDist: off the start end",
         std::fabs(PointLineDist(dVector(-3,0,0), dVector(-1,0,0), dVector(1,0,0)) - 2.0f), 1e-5f);
  expect("PointLineDist: off the finish end",
         std::fabs(PointLineDist(dVector(3,0,0), dVector(-1,0,0), dVector(1,0,0)) - 2.0f), 1e-5f);
}

// ---- dBoundingBox ----------------------------------------------------------
static void testBoundingBox() {
  dBoundingBox b;
  expect("bbox: default is empty", b.empty() ? 0.0f : 1.0f, 0);
  b.expand(dVector(1,2,3));
  expect("bbox: first expand seeds min", vdiff(b.min,1,2,3), 0);
  expect("bbox: first expand seeds max", vdiff(b.max,1,2,3), 0);
  b.expand(dVector(-1,5,0));
  expect("bbox: min after 2nd expand", vdiff(b.min,-1,2,0), 0);
  expect("bbox: max after 2nd expand", vdiff(b.max,1,5,3), 0);
  expect("bbox: inside", b.inside(dVector(0,3,1)) ? 0.0f : 1.0f, 0);
  expect("bbox: outside", b.inside(dVector(0,9,1)) ? 1.0f : 0.0f, 0);
  b.expandby(1);
  expect("bbox: expandby", vdiff(b.min,-2,1,-1) + vdiff(b.max,2,6,4), 0);

  // the (min,max) ctor must produce a NON-empty box, or the first expand()
  // silently throws the constructed bounds away
  dBoundingBox c(dVector(-1,-1,-1), dVector(1,1,1));
  expect("bbox: (min,max) ctor is not empty", c.empty() ? 1.0f : 0.0f, 0);
  c.expand(dVector(2,0,0));
  expect("bbox: (min,max) ctor keeps its bounds on expand",
         vdiff(c.min,-1,-1,-1) + vdiff(c.max,2,1,1), 0);

  dBoundingBox u; u.expand(dVector(0,0,0)); u.expand(dVector(1,1,1));
  dBoundingBox v; v.expand(dVector(2,2,2)); v.expand(dVector(3,3,3));
  expect("bbox: disjoint boxes not inside", u.inside(v) ? 1.0f : 0.0f, 0);
  u.expand(v);
  expect("bbox: expand(box) unions", vdiff(u.min,0,0,0) + vdiff(u.max,3,3,3), 0);
}

int main() {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);   // keep the log if a check traps
  testMatrix();
  testQuat();
  testVector();
  testMatrixOps();
  testQuatConv();
  testGeometry();
  testBoundingBox();
  std::printf(fails ? "RESULT: FAIL\n" : "RESULT: OK\n");
  return fails;
}
