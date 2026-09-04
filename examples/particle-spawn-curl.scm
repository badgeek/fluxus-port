;; particle-spawn-curl.scm — port of NoiseWorkshop's ParticleSystemSpawnTexture.
;; Two ideas from that sketch: (1) CURL noise — a divergence-free flow field (the curl
;; of a noise potential) that swirls like a fluid instead of just pushing particles
;; around; (2) a SPAWN emitter — particles are (re)born at an emitter and stream off
;; it. Here it makes a FIRE/SMOKE PLUME: particles spawn at a low emitter, rise with a
;; buoyancy drift, and the curl turbulence makes the column billow and lick like flame.
;;
;; Runs on the engine's GPU particle system (build-gpu-particles): 65536 particles,
;; curl-advected in a fragment shader, state ping-ponged in a float FBO. The original
;; feeds spawn positions from a texture (a moving sphere mesh / an image mask); here
;; the emitter is a uniform (a small wander at the base). Tweaks: plume height / rise /
;; turbulence / turb scale.

(retained)
(hide-editor)
(set-window-size 920 620)
(background (vector 0.02 0.015 0.02))

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
(define init-frag "
#version 120
varying vec2 vUV;
float h(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
void main(){ gl_FragColor = vec4((h(vUV)-0.5)*0.6, -1.0+h(vUV+0.7)*3.5, (h(vUV+3.1)-0.5)*0.6, h(vUV+9.2)*4.0); }")

;; curl of a 4D-simplex noise potential -> a swirling, divergence-free velocity
;; field. simplex-curl-glsl comes from racket-lib/gpu-noise.ss (the port of
;; NoiseWorkshop's SimplexNoiseDerivatives4D.glslinc): the noise returns its exact
;; analytic gradient, so the curl is 3 evaluations and is divergence-free to float
;; precision. The 4th axis is TIME, so the turbulence evolves in place rather than
;; being scrolled past the plume.
(define update-frag (string-append "
#version 120
#extension GL_ARB_draw_buffers : enable
varying vec2 vUV;
uniform sampler2D u_state;
uniform float u_time, u_dt, u_maxAge, u_mag, u_scale, u_rise, u_spawnX, u_spawnY, u_spawnZ;
"
simplex-curl-glsl
"
float rnd(vec2 c){ return fract(sin(dot(c, vec2(12.9898,78.233)))*43758.5453); }
void main(){
  vec4 st = texture2D(u_state, vUV); vec3 p = st.xyz; float age = st.w + u_dt;
  if(age > u_maxAge){
    age = 0.0;
    float u = rnd(vUV+p.xy)*2.0-1.0; float th = rnd(vUV.yx+p.yz)*6.2831853; float s = sqrt(max(0.0,1.0-u*u));
    p = vec3(u_spawnX,u_spawnY,u_spawnZ) + vec3(s*cos(th), s*sin(th), u) * 0.42;   // born at the emitter
  }
  // rising drift (buoyancy) + curl turbulence -> a fire/smoke plume
  vec3 vel = vec3(0.0, u_rise, 0.0) + curlNoise(p*u_scale, u_time*0.15, 3, 0.5) * u_mag;
  p += vel * u_dt;
  gl_FragData[0] = vec4(p, age);       // pos + age
  gl_FragData[1] = vec4(vel, 1.0);     // velocity (for the streak tail)
}"))

;; ---- build + configure ------------------------------------------------------
;; 'streaks: each particle draws a short LINE from its position back along its
;; velocity (MRT vel target), so the plume shows flow direction — no draw shader
;; needed, the LINES prim draws its per-vertex colours directly (fixed function).
(define gpu (build-gpu-particles 128 128 init-frag (quote streaks)))
(with-primitive gpu (hint-nozwrite) (blend-mode 'src-alpha 'one))
;; fire palette (young yellow-white base -> old red ember, fading out)
(gpu-uniform! "u_youngR" 1.0) (gpu-uniform! "u_youngG" 0.85) (gpu-uniform! "u_youngB" 0.35)
(gpu-uniform! "u_oldR"   0.7) (gpu-uniform! "u_oldG"   0.06) (gpu-uniform! "u_oldB"   0.02)
(gpu-uniform! "u_alpha"  0.03)

;; ---- per-frame: emitter at the base, plume rises on the GPU -----------------
(every-frame
  (let* ((T (time)) (ang (* T 0.08)))
    (set-camera-transform (look-at (vector (* 5.5 (sin ang)) 0.6 (* 5.5 (cos ang)))
                                   (vector 0.0 0.7 0.0) (vector 0.0 1.0 0.0)))
    ;; emitter sits low and wanders a little; particles spawn here and rise
    (gpu-uniform! "u_spawnX" (* 0.25 (sin (* T 1.3))))
    (gpu-uniform! "u_spawnY" -1.1)
    (gpu-uniform! "u_spawnZ" (* 0.25 (cos (* T 1.1))))
    (gpu-uniform! "u_time"   T)
    (gpu-uniform! "u_dt"     0.05)
    (gpu-uniform! "u_maxAge" (tweak "plume height" 4.0 1.0 9.0))
    (gpu-uniform! "u_rise"   (tweak "rise speed"   1.3 0.0 3.0))
    (gpu-uniform! "u_mag"    (tweak "turbulence"   0.8 0.0 3.0))
    (gpu-uniform! "u_scale"  (tweak "turb scale"   1.2 0.2 2.5))
    (gpu-uniform! "u_streak" (tweak "streak len"   0.06 0.0 0.4))
    (gpu-update! update-frag)))
