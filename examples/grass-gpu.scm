;; grass-gpu.scm — the SAME grass as grass.scm, but grown on the GPU by a real
;; GEOMETRY SHADER (GL_EXT_geometry_shader4), a faithful port of Andreas Müller's
;; NoiseWorkshop Grass.geom. One GL_POINTS vertex per blade is uploaded ONCE; the
;; geometry shader expands each point into a tapered, swaying triangle-strip blade
;; every frame on the GPU — so this scales to tens of thousands of blades where the
;; CPU version (grass.scm) tops out at a few hundred.
;;
;; Requires the (shader-source-geom …) command added to the engine.

(retained)
(hide-editor)
(set-window-size 900 600)
(background (vector 0.05 0.07 0.10))

;; ---- orbit camera (gluLookAt view matrix) -----------------------------------
(define (v- a b) (vector (- (vx a) (vx b)) (- (vy a) (vy b)) (- (vz a) (vz b))))
(define (vdot a b) (+ (* (vx a) (vx b)) (* (vy a) (vy b)) (* (vz a) (vz b))))
(define (vcross a b)
  (vector (- (* (vy a) (vz b)) (* (vz a) (vy b)))
          (- (* (vz a) (vx b)) (* (vx a) (vz b)))
          (- (* (vx a) (vy b)) (* (vy a) (vx b)))))
(define (vnorm a)
  (let ((l (sqrt (vdot a a)))) (if (> l 1e-6) (vector (/ (vx a) l) (/ (vy a) l) (/ (vz a) l)) a)))
(define (look-at eye tgt up)
  (let* ((f (vnorm (v- tgt eye))) (s (vnorm (vcross f up))) (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0.0
            (vy s) (vy u) (- (vy f)) 0.0
            (vz s) (vz u) (- (vz f)) 0.0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1.0)))

;; ---- terrain (blades + ground share this height field) ----------------------
(define (snoise x z) (- (* 2.0 (noise x z)) 1.0))
(define (fbm x z) (+ (* 1.0 (snoise (* x 0.35) (* z 0.35))) (* 0.45 (snoise (* x 0.8) (* z 0.8)))))
(define AMPT 0.55)
(define (terrain-h x z) (* AMPT (fbm x z)))
(define E 4.0)
(define (h01 i) (let ((s (* (sin (* i 12.9898)) 43758.5453))) (- s (floor s))))  ; hash -> [0,1)

;; ---- ground plane (built once) ----------------------------------------------
(define GN 40)
(define ground (build-seg-plane GN GN))
(with-primitive ground
  (hint-unlit) (hint-vertcols) (backfacecull #f)
  (let ((us (/ 1.0 GN)) (vs (/ 1.0 GN)))
    (pdata-index-map!
      (lambda (i p)
        (let* ((q (quotient i 4)) (c (modulo i 4))
               (gx (quotient q GN)) (gy (modulo q GN))
               (u (+ (/ (exact->inexact gx) GN) (if (or (= c 1) (= c 2)) us 0.0)))
               (v (+ (/ (exact->inexact gy) GN) (if (or (= c 2) (= c 3)) vs 0.0)))
               (wx (* (- u 0.5) 2.0 E)) (wz (* (- v 0.5) 2.0 E)))
          (vector wx (terrain-h wx wz) wz)))
      "p")
    (pdata-index-map!
      (lambda (i c) (vector 0.03 0.11 0.04)) "c")))

;; ---- GPU grass: a cloud of GL_POINTS, one per blade -------------------------
(define NB 20000)          ; twenty THOUSAND blades — the whole point of the GPU path
(define blades (build-particles NB))
(with-primitive blades
  (hint-points) (hint-solid #f) (hint-unlit)
  ;; scatter each point onto the terrain; tint gives per-blade colour variation
  (pdata-index-map!
    (lambda (i p)
      (let* ((x (* (- (h01 (+ i 1)) 0.5) 2.0 E))
             (z (* (- (h01 (+ (* i 3) 7)) 0.5) 2.0 E)))
        (vector x (terrain-h x z) z)))
    "p")
  (pdata-index-map!
    (lambda (i c)
      (let ((g (+ 0.35 (* 0.35 (h01 (+ (* i 5) 2))))))
        (vector (* 0.18 g) g (* 0.16 g))))
    "c"))

;; ---- the geometry-shader blade (ported from Grass.geom) ---------------------
(define grass-vert "
#version 120
void main(){ gl_FrontColor = gl_Color; gl_Position = gl_Vertex; }")

(define grass-geom "
#version 120
#extension GL_EXT_geometry_shader4 : enable
uniform float timeSecs, stalkHeight, stalkHalfWidth, swayMaxAngle, swayFreq, swayTimeScale, timeMaxDiff;
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }
float snoise2(vec2 p){
  vec2 i=floor(p), f=fract(p); vec2 u=f*f*(3.0-2.0*f);
  float a=hash(i), b=hash(i+vec2(1.0,0.0)), c=hash(i+vec2(0.0,1.0)), d=hash(i+vec2(1.0,1.0));
  return (mix(mix(a,b,u.x), mix(c,d,u.x), u.y)) * 2.0 - 1.0;
}
mat3 rotX(float a){ float s=sin(a),c=cos(a); return mat3(1.0,0.0,0.0, 0.0,c,-s, 0.0,s,c); }
mat3 rotZ(float a){ float s=sin(a),c=cos(a); return mat3(c,-s,0.0, s,c,0.0, 0.0,0.0,1.0); }
void main(){
  vec4 base  = gl_PositionIn[0];
  vec4 color = gl_FrontColorIn[0];
  vec3 right = normalize(vec3(gl_ModelViewMatrix[0][0], gl_ModelViewMatrix[1][0], gl_ModelViewMatrix[2][0]));
  float st = timeSecs * swayTimeScale;
  float n1 = snoise2(base.xz * swayFreq);
  float n2 = snoise2((base.zx + vec2(123.4,567.8)) * swayFreq);
  mat3 sway = rotX(sin(st + n1*timeMaxDiff) * swayMaxAngle) * rotZ(sin(st + n2*timeMaxDiff) * swayMaxAngle);
  vec3 side = right * stalkHalfWidth;
  vec3 pos = base.xyz;
  vec3 dir = vec3(0.0,1.0,0.0);
  float stepM = stalkHeight / 6.0;
  for(int i=0;i<7;i++){
    float t = float(i)/6.0;
    float dk = mix(0.35, 1.0, smoothstep(0.3,1.0,t));
    vec3 hs = side * (1.0-t);
    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos - hs, 1.0); gl_FrontColor = color*vec4(dk,dk,dk,1.0); EmitVertex();
    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos + hs, 1.0); gl_FrontColor = color*vec4(dk,dk,dk,1.0); EmitVertex();
    dir = sway * dir;
    pos += dir * stepM;
  }
  EndPrimitive();
}")

(define grass-frag "
#version 120
void main(){ gl_FragColor = gl_Color; }")

;; bind the shader ONCE (in: points, out: triangle_strip, 14 verts = 7 levels x 2)
(with-primitive blades
  (shader-source-geom grass-vert grass-geom grass-frag gl-points gl-triangle-strip 14))

;; ---- per-frame: orbit + push wind uniforms (geometry runs on the GPU) --------
(every-frame
  (let* ((T (time))
         (ang (* T 0.12))
         (eye (vector (* 8.5 (sin ang)) 2.4 (* 8.5 (cos ang))))
         (tgt (vector 0.0 0.4 0.0)))
    (set-camera-transform (look-at eye tgt (vector 0.0 1.0 0.0)))
    (with-primitive blades
      (shader-set-float! "timeSecs"      T)
      (shader-set-float! "stalkHeight"   (tweak "blade h"   0.55  0.1 1.4))
      (shader-set-float! "stalkHalfWidth"(tweak "blade w"   0.018 0.004 0.06))
      (shader-set-float! "swayMaxAngle"  (tweak "wind"      0.22  0.0 0.8))
      (shader-set-float! "swayFreq"      (tweak "wind freq" 0.5   0.05 2.0))
      (shader-set-float! "swayTimeScale" (tweak "wind speed" 1.6  0.0 5.0))
      (shader-set-float! "timeMaxDiff"   (tweak "phase"     3.0   0.0 8.0)))))
