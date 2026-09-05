// SPDX-License-Identifier: AGPL-3.0-or-later
// Regression tests for the vendored dada math fixes (dMatrix::inverse,
// dQuat::dot/renorm/slerp). Five ~20-year-old upstream bugs were found here in
// one session — run this after ANY change to vendor/fluxus/libfluxus/src/dada.*
// Exits non-zero on failure.
//
//   cmake --build build --target math_test && ./build/math_test
#include "dada.h"
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

int main() {
  testMatrix();
  testQuat();
  std::printf(fails ? "RESULT: FAIL\n" : "RESULT: OK\n");
  return fails;
}
