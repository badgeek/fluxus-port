;; particle-image-spawn.scm — the SPAWN-TEXTURE idea from ParticleSystemSpawnTexture:
;; particle rebirth positions are read from an IMAGE. Here a heart is drawn into a
;; (build-pixels) texture; each reborn particle samples that mask at its own slot and
;; is placed where the image is bright, so the swarm forms the picture — then curl
;; noise drifts them off and they re-form it. 65536 particles, all on the GPU.

(retained)
(hide-editor)
(set-window-size 900 640)
(background (vector 0.02 0.02 0.04))

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

;; ---- spawn image: draw a HEART into a pixels texture ------------------------
(define RES 256)
(define mask (build-pixels RES RES))
(with-primitive mask
  (pdata-index-map!
    (lambda (i c)
      (let* ((x (- (/ (exact->inexact (modulo i RES)) RES) 0.5))
             (yy (- (/ (exact->inexact (quotient i RES)) RES) 0.5))
             (X (* x 2.6)) (Y (* yy -2.6))                 ; centre + flip y up
             (a (- (+ (* X X) (* Y Y)) 1.0))
             (f (- (* a a a) (* X X Y Y Y))))              ; heart implicit: f<0 inside
        (if (< f 0.0) (vector 1 1 1 1) (vector 0 0 0 1))))
    "c")
  (pixels-upload)
  (scale (vector 0 0 0)))                                  ; texture only — don't draw the quad

;; ---- GPU particles ----------------------------------------------------------
(define init-frag "
#version 120
varying vec2 vUV;
float h(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
void main(){ gl_FragColor = vec4((h(vUV)-0.5)*6.0, (h(vUV+0.7)-0.5)*6.0, (h(vUV+3.1)-0.5)*6.0, h(vUV+9.2)*3.0); }")

(define update-frag "
#version 120
#extension GL_ARB_draw_buffers : enable
varying vec2 vUV;
uniform sampler2D u_state, u_spawnTex;
uniform float u_time, u_dt, u_maxAge, u_mag, u_scale, u_img;
float h3(vec3 p){ return fract(sin(dot(p, vec3(127.1,311.7,74.7)))*43758.5453); }
// value noise with ANALYTIC derivatives -> vec4(value, d/dx, d/dy, d/dz): the
// trilinear blend with fade u=f*f*(3-2f) is a polynomial, so its gradient is exact.
vec4 vnd(vec3 p){
  vec3 i=floor(p), f=fract(p);
  vec3 u=f*f*(3.0-2.0*f), du=6.0*f*(1.0-f);
  float a=h3(i), b=h3(i+vec3(1,0,0)), c=h3(i+vec3(0,1,0)), d=h3(i+vec3(1,1,0));
  float e=h3(i+vec3(0,0,1)), f1=h3(i+vec3(1,0,1)), g=h3(i+vec3(0,1,1)), hh=h3(i+vec3(1,1,1));
  float k1=b-a, k2=c-a, k3=e-a, k4=a-b-c+d, k5=a-b-e+f1, k6=a-c-e+g, k7=-a+b+c-d+e-f1-g+hh;
  float n = a + k1*u.x + k2*u.y + k3*u.z + k4*u.x*u.y + k5*u.x*u.z + k6*u.y*u.z + k7*u.x*u.y*u.z;
  return vec4(n,
    du.x*(k1 + k4*u.y + k5*u.z + k7*u.y*u.z),
    du.y*(k2 + k4*u.x + k6*u.z + k7*u.x*u.z),
    du.z*(k3 + k5*u.x + k6*u.y + k7*u.x*u.y));
}
// analytic curl: 3 noise evaluations (vs 18 for finite differences), exactly
// divergence-free so the dissolve swirls without sources or sinks.
vec3 curl(vec3 p){
  vec4 n1 = vnd(p), n2 = vnd(p + vec3(31.4,7.2,12.9)), n3 = vnd(p + vec3(-9.1,4.3,21.7));
  return vec3(n3.z - n2.w, n1.w - n3.y, n2.y - n1.z);
}
void main(){
  vec4 st = texture2D(u_state, vUV); vec3 p = st.xyz; float age = st.w + u_dt;
  if(age > u_maxAge){
    age = 0.0;
    float m = texture2D(u_spawnTex, vUV).r;               // this particle's pixel of the image
    if(m > 0.4) p = vec3((vUV - 0.5) * u_img, 0.0);        // bright -> form the picture
    else        p = vec3(0.0, 0.0, 9999.0);               // dark  -> parked offscreen
  }
  vec3 vel = curl(p*u_scale + vec3(0.0,0.0,u_time*0.1)) * u_mag;
  p += vel * u_dt;
  gl_FragData[0] = vec4(p, age);
  gl_FragData[1] = vec4(vel, 1.0);
}")

(define draw-vert "
#version 120
void main(){ gl_FrontColor = gl_Color; gl_PointSize = 2.0;
  gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xyz, 1.0); }")
(define draw-frag "
#version 120
void main(){ gl_FragColor = gl_Color; }")

(define gpu (build-gpu-particles RES RES init-frag))     ; points, 65536
(with-primitive gpu (hint-nozwrite) (blend-mode 'src-alpha 'one))
(gpu-draw-shaders draw-vert draw-frag)
(gpu-spawn-from-pixels mask)                              ; <- spawn from the heart image
(gpu-uniform! "u_youngR" 1.0) (gpu-uniform! "u_youngG" 0.4) (gpu-uniform! "u_youngB" 0.55)
(gpu-uniform! "u_oldR"   0.9) (gpu-uniform! "u_oldG"   0.1) (gpu-uniform! "u_oldB"   0.3)
(gpu-uniform! "u_alpha"  0.06)

;; ---- per-frame --------------------------------------------------------------
(every-frame
  (let* ((T (time)) (ang (* 0.35 (sin (* T 0.25)))))
    (set-camera-transform (look-at (vector (* 7.0 (sin ang)) 0.6 (* 7.0 (cos ang)))
                                   (vector 0.0 0.0 0.0) (vector 0.0 1.0 0.0)))
    (gpu-uniform! "u_time"   T)
    (gpu-uniform! "u_dt"     0.045)
    (gpu-uniform! "u_img"    5.5)                          ; world size of the picture
    (gpu-uniform! "u_maxAge" (tweak "reform time" 2.2 1.0 8.0))
    (gpu-uniform! "u_mag"    (tweak "dissolve"    0.12 0.0 2.0))
    (gpu-uniform! "u_scale"  (tweak "swirl scale" 0.7 0.2 2.5))
    (gpu-update! update-frag)))
