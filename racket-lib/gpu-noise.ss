#lang racket/base
;; GLSL noise sources for the GPU particle system — plain strings, no FFI, so this
;; file also loads standalone on the racket CLI. `string-append` a block into your
;; update/draw fragment shader between the uniforms and main(); see
;; examples/particle-spawn-curl.scm and examples/particle-image-spawn.scm.
;;
;; The simplex block is a port of NoiseWorkshop's SimplexNoiseDerivatives4D.glslinc
;; (Andreas Müller, after Ian McEwan / Ashima Arts' webgl-noise), with the mod289 /
;; permute / taylorInvSqrt helpers it relies on folded in so the string is
;; self-contained. It is 4D: the 4th axis is time, so the field ANIMATES coherently
;; instead of the field being translated past the particles.
;;
;; Why analytic derivatives: curl needs the gradient of a noise potential. Finite
;; differencing costs 6 noise evaluations per potential component (18 for a 3D curl)
;; AND the resulting field is only approximately divergence-free, so particles slowly
;; pile up in sinks. simplexNoiseDerivatives returns the exact gradient alongside the
;; value, so a curl is 3 evaluations and is divergence-free to float precision.

(provide simplex-curl-glsl value-curl-glsl)

;; ---------------------------------------------------------------------------
;; 4D simplex noise with analytic derivatives + curl.
;;
;; Defines:
;;   vec4 simplexNoiseDerivatives(vec4 v)   -> (d/dx, d/dy, d/dz, d/dw)
;;   vec3 curlNoise(vec3 p, float t)                       -- 1 octave
;;   vec3 curlNoise(vec3 p, float t, int oct, float persist) -- fBm, persist 0.5
;;
;; Cost: each curl is 3 simplex evaluations (5 corners each), so the fBm form at 3
;; octaves is 9. At 65536 particles that is fine on the Metal-backed 2.1 context, but
;; it is a lot more ALU than value-curl-glsl below — prefer 1–2 octaves unless the
;; extra detail is visible.
(define simplex-curl-glsl "
// ---- 4D simplex noise with analytic derivatives (Ashima / McEwan; curl by A. Müller)
vec4 sn_mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
float sn_mod289(float x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 sn_permute(vec4 x) { return sn_mod289(((x*34.0)+1.0)*x); }
float sn_permute(float x) { return sn_mod289(((x*34.0)+1.0)*x); }
vec4 sn_taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }
float sn_taylorInvSqrt(float r) { return 1.79284291400159 - 0.85373472095314 * r; }

vec4 sn_grad4(float j, vec4 ip) {
  const vec4 ones = vec4(1.0, 1.0, 1.0, -1.0);
  vec4 p, s;
  p.xyz = floor(fract(vec3(j) * ip.xyz) * 7.0) * ip.z - 1.0;
  p.w = 1.5 - dot(abs(p.xyz), ones.xyz);
  s = vec4(lessThan(p, vec4(0.0)));
  p.xyz = p.xyz + (s.xyz*2.0 - 1.0) * s.www;
  return p;
}

// gradient of 4D simplex noise: the kernel sum is a polynomial in the corner
// offsets, so d/dv is exact in closed form (product rule on m^3 * dot(p,x)).
vec4 simplexNoiseDerivatives(vec4 v) {
  const float F4 = 0.309016994374947451;                  // (sqrt(5)-1)/4
  const vec4 C = vec4(0.138196601125011, 0.276393202250021,
                      0.414589803375032, -0.447213595499958);
  vec4 i  = floor(v + dot(v, vec4(F4)));
  vec4 x0 = v - i + dot(i, C.xxxx);

  vec4 i0;                                                 // rank the coords
  vec3 isX  = step(x0.yzw, x0.xxx);
  vec3 isYZ = step(x0.zww, x0.yyz);
  i0.x = isX.x + isX.y + isX.z;
  i0.yzw = 1.0 - isX;
  i0.y += isYZ.x + isYZ.y;
  i0.zw += 1.0 - isYZ.xy;
  i0.z += isYZ.z;
  i0.w += 1.0 - isYZ.z;

  vec4 i3 = clamp(i0,       0.0, 1.0);
  vec4 i2 = clamp(i0 - 1.0, 0.0, 1.0);
  vec4 i1 = clamp(i0 - 2.0, 0.0, 1.0);

  vec4 x1 = x0 - i1 + C.xxxx;
  vec4 x2 = x0 - i2 + C.yyyy;
  vec4 x3 = x0 - i3 + C.zzzz;
  vec4 x4 = x0 + C.wwww;

  i = sn_mod289(i);
  float j0 = sn_permute(sn_permute(sn_permute(sn_permute(i.w) + i.z) + i.y) + i.x);
  vec4 j1 = sn_permute(sn_permute(sn_permute(sn_permute(
              i.w + vec4(i1.w, i2.w, i3.w, 1.0))
            + i.z + vec4(i1.z, i2.z, i3.z, 1.0))
            + i.y + vec4(i1.y, i2.y, i3.y, 1.0))
            + i.x + vec4(i1.x, i2.x, i3.x, 1.0));

  vec4 ip = vec4(1.0/294.0, 1.0/49.0, 1.0/7.0, 0.0);
  vec4 p0 = sn_grad4(j0,   ip);
  vec4 p1 = sn_grad4(j1.x, ip);
  vec4 p2 = sn_grad4(j1.y, ip);
  vec4 p3 = sn_grad4(j1.z, ip);
  vec4 p4 = sn_grad4(j1.w, ip);

  vec4 norm = sn_taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
  p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
  p4 *= sn_taylorInvSqrt(dot(p4,p4));

  vec3 values0 = vec3(dot(p0,x0), dot(p1,x1), dot(p2,x2));   // corner contributions
  vec2 values1 = vec2(dot(p3,x3), dot(p4,x4));
  vec3 m0 = max(0.5 - vec3(dot(x0,x0), dot(x1,x1), dot(x2,x2)), 0.0);
  vec2 m1 = max(0.5 - vec2(dot(x3,x3), dot(x4,x4)), 0.0);
  vec3 temp0 = -6.0 * m0 * m0 * values0;                     // d(m^3)/dx * value
  vec2 temp1 = -6.0 * m1 * m1 * values1;
  vec3 mmm0 = m0 * m0 * m0;
  vec2 mmm1 = m1 * m1 * m1;

  vec4 d = temp0[0]*x0 + temp0[1]*x1 + temp0[2]*x2 + temp1[0]*x3 + temp1[1]*x4
         + mmm0[0]*p0 + mmm0[1]*p1 + mmm0[2]*p2 + mmm1[0]*p3 + mmm1[1]*p4;
  return d * 49.0;
}

// curl of a 3-component noise potential, sampled at 3 well-separated offsets.
// curl = (dP3/dy - dP2/dz, dP1/dz - dP3/dx, dP2/dx - dP1/dy)
vec3 curlNoise(vec3 p, float t) {
  vec4 dx = simplexNoiseDerivatives(vec4(p, t));
  vec4 dy = simplexNoiseDerivatives(vec4(p + vec3(123.4, 129845.6, -1239.1), t));
  vec4 dz = simplexNoiseDerivatives(vec4(p + vec3(-9519.0, 9051.0, -123.0), t));
  return vec3(dz.y - dy.z, dx.z - dz.x, dy.x - dx.y);
}

// fBm curl: octaves of the above at doubling frequency, `persist` amplitude falloff
// (0.5 is the usual). Faithful to the original — each octave scales BOTH the
// sampling frequency and the derivative amplitude, so the curl stays consistent.
vec3 curlNoise(vec3 p, float t, int oct, float persist) {
  vec4 dx = vec4(0.0), dy = vec4(0.0), dz = vec4(0.0);
  for (int i = 0; i < oct; ++i) {
    float freq  = pow(2.0, float(i));
    float amp   = (i == 0) ? 0.5 : pow(persist, float(i)) * 0.5 * freq;
    dx += simplexNoiseDerivatives(vec4( p                                  * freq, t)) * amp;
    dy += simplexNoiseDerivatives(vec4((p + vec3(123.4, 129845.6, -1239.1)) * freq, t)) * amp;
    dz += simplexNoiseDerivatives(vec4((p + vec3(-9519.0, 9051.0, -123.0))  * freq, t)) * amp;
  }
  return vec3(dz.y - dy.z, dx.z - dz.x, dy.x - dx.y);
}
")

;; ---------------------------------------------------------------------------
;; The cheap alternative: curl of a trilinear VALUE-noise potential, also with
;; analytic derivatives. ~5x less ALU than the simplex block and adequate when the
;; flow is heavily blurred by motion (smoke, dissolve), but the grid axes are
;; faintly visible in a slow, sparse field — that is what simplex fixes.
;;
;; Defines: vec4 valueNoiseDerivatives(vec3 p) -> (value, d/dx, d/dy, d/dz)
;;          vec3 curlValueNoise(vec3 p)
(define value-curl-glsl "
float vn_h3(vec3 p){ return fract(sin(dot(p, vec3(127.1,311.7,74.7)))*43758.5453); }
// trilinear blend with fade u = f*f*(3-2f) is a polynomial -> exact gradient (du = 6f(1-f))
vec4 valueNoiseDerivatives(vec3 p){
  vec3 i=floor(p), f=fract(p);
  vec3 u=f*f*(3.0-2.0*f), du=6.0*f*(1.0-f);
  float a=vn_h3(i), b=vn_h3(i+vec3(1,0,0)), c=vn_h3(i+vec3(0,1,0)), d=vn_h3(i+vec3(1,1,0));
  float e=vn_h3(i+vec3(0,0,1)), f1=vn_h3(i+vec3(1,0,1)), g=vn_h3(i+vec3(0,1,1)), hh=vn_h3(i+vec3(1,1,1));
  float k1=b-a, k2=c-a, k3=e-a, k4=a-b-c+d, k5=a-b-e+f1, k6=a-c-e+g, k7=-a+b+c-d+e-f1-g+hh;
  float n = a + k1*u.x + k2*u.y + k3*u.z + k4*u.x*u.y + k5*u.x*u.z + k6*u.y*u.z + k7*u.x*u.y*u.z;
  return vec4(n,
    du.x*(k1 + k4*u.y + k5*u.z + k7*u.y*u.z),
    du.y*(k2 + k4*u.x + k6*u.z + k7*u.x*u.z),
    du.z*(k3 + k5*u.x + k6*u.y + k7*u.x*u.y));
}
vec3 curlValueNoise(vec3 p){
  vec4 n1 = valueNoiseDerivatives(p);
  vec4 n2 = valueNoiseDerivatives(p + vec3(31.4,7.2,12.9));
  vec4 n3 = valueNoiseDerivatives(p + vec3(-9.1,4.3,21.7));
  return vec3(n3.z - n2.w, n1.w - n3.y, n2.y - n1.z);
}
")
