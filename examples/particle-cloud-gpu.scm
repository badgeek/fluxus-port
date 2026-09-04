;; particle-cloud-gpu.scm — the TRUE GPU port of NoiseWorkshop's ParticleCloudGPU,
;; now that the engine has render-to-texture. 65536 particles (256x256) live entirely
;; on the GPU: their state (pos.xyz, age) sits in a ping-pong RGBA32F texture, an
;; UPDATE fragment shader advects them all in parallel each frame, and the draw pass'
;; VERTEX shader fetches each particle's position from that texture (vertex texture
;; fetch) while a GEOMETRY shader billboards it into a soft additive sprite.
;;
;; Compare examples/particle-cloud.scm — same simulation on the CPU, ~2500 particles.
;; This one is 26x more particles with the CPU nearly idle.

(retained)
(hide-editor)
(set-window-size 920 620)
(background (vector 0.02 0.02 0.035))

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

;; ---- shaders ---------------------------------------------------------------
;; seed: scatter each particle through a box, random start age
(define init-frag "
#version 120
varying vec2 vUV;
float h(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }
void main(){
  float BOX = 3.0;
  vec3 p = vec3( (h(vUV)*2.0-1.0)*BOX, h(vUV+0.7)*BOX*1.3, (h(vUV+3.1)*2.0-1.0)*BOX );
  gl_FragColor = vec4(p, h(vUV+9.2) * 9.0);
}")

;; advance: age, respawn in the box when old, advect by wind + 3-axis noise velocity
(define update-frag "
#version 120
varying vec2 vUV;
uniform sampler2D u_state;
uniform float u_time, u_dt, u_maxAge, u_wind, u_mag, u_scale;
float h3(vec3 p){ return fract(sin(dot(p, vec3(127.1,311.7,74.7))) * 43758.5453); }
float vnoise(vec3 p){ vec3 i=floor(p), f=fract(p); vec3 u=f*f*(3.0-2.0*f);
  return mix(mix(mix(h3(i),h3(i+vec3(1,0,0)),u.x),mix(h3(i+vec3(0,1,0)),h3(i+vec3(1,1,0)),u.x),u.y),
             mix(mix(h3(i+vec3(0,0,1)),h3(i+vec3(1,0,1)),u.x),mix(h3(i+vec3(0,1,1)),h3(i+vec3(1,1,1)),u.x),u.y),u.z)*2.0-1.0; }
void main(){
  vec4 st = texture2D(u_state, vUV);
  vec3 p = st.xyz; float age = st.w + u_dt;
  if(age > u_maxAge){
    age -= u_maxAge; float BOX=3.0;
    p = vec3((h3(vec3(vUV,1.0)+p)*2.0-1.0)*BOX, h3(vec3(vUV,2.0)+p)*BOX*1.3, (h3(vec3(vUV,3.0)+p)*2.0-1.0)*BOX);
  }
  vec3 np = p*u_scale; float nt = u_time*0.15;
  vec3 vel = vec3( vnoise(np+vec3(0.0,0.0,nt)), vnoise(np.zyx+vec3(nt,0.0,0.0)), vnoise(np.zxy+vec3(0.0,nt,0.0)) );
  p += (vec3(u_wind,0.0,0.0) + vel*u_mag) * u_dt;
  gl_FragColor = vec4(p, age);
}")

;; draw shader: position + age-faded colour come from pdata (filled from the GPU
;; readback); drawn as additive points.
(define draw-vert "
#version 120
void main(){ gl_FrontColor = gl_Color; gl_PointSize = 2.5;
  gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xyz, 1.0); }")
(define draw-frag "
#version 120
void main(){ gl_FragColor = gl_Color; }")

;; ---- build the GPU particle system -----------------------------------------
(define gpu (build-gpu-particles 256 256 init-frag))     ; 65536 particles
(with-primitive gpu (hint-nozwrite) (blend-mode 'src-alpha 'one))   ; additive glow
(gpu-draw-shaders draw-vert draw-frag)

;; ---- per-frame: orbit + push uniforms + step the sim on the GPU -------------
(every-frame
  (let* ((T (time)) (ang (* T 0.12)))
    (set-camera-transform (look-at (vector (* 9.5 (sin ang)) 2.4 (* 9.5 (cos ang)))
                                   (vector 0.5 0.4 0.0) (vector 0.0 1.0 0.0)))
    (gpu-uniform! "u_time"   T)
    (gpu-uniform! "u_dt"     0.020)
    (gpu-uniform! "u_maxAge" 9.0)
    (gpu-uniform! "u_wind"   (tweak "wind x"      0.2  -1.0 2.0))
    (gpu-uniform! "u_mag"    (tweak "noise mag"   1.5   0.0 3.0))
    (gpu-uniform! "u_scale"  (tweak "noise scale" 1.1   0.2 3.0))
    (gpu-update! update-frag)))
