;; noise-grid-3d.scm — a 3D noise FLOW FIELD, port of NoiseWorkshop's NoiseGrid
;; (the "NoiseGrid3D" scratchpad step). A res^3 grid of points fills a box; at each
;; point a 4D noise (x,y,z,time) gives a velocity, drawn as a short line coloured by
;; its direction and fading to transparent at the tip. It flows because the noise's
;; 4th dimension is time.
;;
;; GPU port: the grid points are uploaded ONCE as GL_POINTS; a GEOMETRY SHADER
;; samples the noise and emits the coloured velocity line for every point each frame
;; (uses the (shader-source-geom …) stage added to the engine). res^3 = 10648 lines,
;; all on the GPU. Sliders: frequency / time-frequency / magnitude.

(retained)
(hide-editor)
(set-window-size 900 640)
(background (vector 0.02 0.02 0.03))

;; ---- orbit camera (gluLookAt view matrix) -----------------------------------
(define (v- a b) (vector (- (vx a) (vx b)) (- (vy a) (vy b)) (- (vz a) (vz b))))
(define (vdot a b) (+ (* (vx a) (vx b)) (* (vy a) (vy b)) (* (vz a) (vz b))))
(define (vcross a b)
  (vector (- (* (vy a) (vz b)) (* (vz a) (vy b)))
          (- (* (vz a) (vx b)) (* (vx a) (vz b)))
          (- (* (vx a) (vy b)) (* (vy a) (vx b)))))
(define (vnorm a) (let ((l (sqrt (vdot a a)))) (if (> l 1e-6) (vector (/ (vx a) l) (/ (vy a) l) (/ (vz a) l)) a)))
(define (look-at eye tgt up)
  (let* ((f (vnorm (v- tgt eye))) (s (vnorm (vcross f up))) (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0.0 (vy s) (vy u) (- (vy f)) 0.0
            (vz s) (vz u) (- (vz f)) 0.0 (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1.0)))

;; ---- grid (built once; gridSize / resolution are build-time constants) -------
(define GS 6.0) (define RES 22)
(define NP (* RES RES RES))

;; a subtle floor grid at y=0
(define floor-grid (build-seg-plane 12 12))
(with-primitive floor-grid
  (hint-unlit) (hint-wire) (hint-solid #f) (backfacecull #f)
  (wire-colour (vector 0.16 0.18 0.22)) (wire-opacity 0.5)
  (rotate (vector -90 0 0)) (scale (vector (* GS 1.6) (* GS 1.6) 1)))

;; the flow-field points: one GL_POINTS vertex per grid node, placed ONCE
(define grid (build-particles NP))
(with-primitive grid
  (hint-points) (hint-solid #f) (hint-unlit)
  (pdata-index-map!
    (lambda (i p)
      (let* ((gy (quotient i (* RES RES)))
             (r  (modulo i (* RES RES)))
             (gz (quotient r RES))
             (gx (modulo r RES))
             (m  (lambda (k) (- (* (/ (exact->inexact k) (- RES 1)) GS) (* GS 0.5)))))
        (vector (m gx) (* (/ (exact->inexact gy) (- RES 1)) GS) (m gz))))   ; x,z centred; y in 0..GS
    "p")
  (pdata-index-map! (lambda (i c) (vector 1 1 1 1)) "c"))

;; ---- geometry shader: point -> 4D-noise velocity line -----------------------
(define ng-vert "
#version 120
void main(){ gl_FrontColor = gl_Color; gl_Position = gl_Vertex; }")

(define ng-geom "
#version 120
#extension GL_EXT_geometry_shader4 : enable
uniform float uTime, uFreq, uTimeFreq, uMag;
float hash(vec3 p){ return fract(sin(dot(p, vec3(127.1,311.7,74.7))) * 43758.5453); }
float vnoise(vec3 p){
  vec3 i=floor(p), f=fract(p); vec3 u=f*f*(3.0-2.0*f);
  float n = mix(mix(mix(hash(i+vec3(0,0,0)), hash(i+vec3(1,0,0)), u.x),
                    mix(hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)), u.x), u.y),
                mix(mix(hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)), u.x),
                    mix(hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)), u.x), u.y), u.z);
  return n*2.0-1.0;
}
void main(){
  vec3 P  = gl_PositionIn[0].xyz;
  float nt = uTime * uTimeFreq;
  vec3 np = P * uFreq;
  vec3 vel = vec3( vnoise(np      + vec3(0.0,0.0,nt)),
                   vnoise(np.zyx  + vec3(nt,0.0,0.0)),
                   vnoise(np.zxy  + vec3(0.0,nt,0.0)) );
  vec3 col = (normalize(vel) + 1.0) * 0.5;                 // colour by direction
  gl_Position = gl_ModelViewProjectionMatrix * vec4(P, 1.0);            gl_FrontColor = vec4(col, 1.0); EmitVertex();
  gl_Position = gl_ModelViewProjectionMatrix * vec4(P + vel*uMag, 1.0); gl_FrontColor = vec4(col, 0.0); EmitVertex();
  EndPrimitive();
}")

(define ng-frag "
#version 120
void main(){ gl_FragColor = gl_Color; }")

(with-primitive grid
  (shader-source-geom ng-vert ng-geom ng-frag gl-points gl-line-strip 2))

;; ---- per-frame: orbit + push noise uniforms (lines built on the GPU) --------
(every-frame
  (let* ((T (time)) (ang (* T 0.15))
         (eye (vector (* GS 1.7 (sin ang)) (* GS 0.7) (* GS 1.7 (cos ang))))
         (tgt (vector 0.0 (* GS 0.42) 0.0)))
    (set-camera-transform (look-at eye tgt (vector 0.0 1.0 0.0)))
    (with-primitive grid
      (shader-set-float! "uTime"     T)
      (shader-set-float! "uFreq"     (tweak "frequency"      1.0  0.05 2.0))
      (shader-set-float! "uTimeFreq" (tweak "time frequency" 0.6  0.0  3.0))
      (shader-set-float! "uMag"      (tweak "magnitude"      0.35 0.0  1.5)))))
