;; particle-cloud.scm — a wind-blown noise particle cloud, port of NoiseWorkshop's
;; ParticleCloudGPU. Each particle advects through a 3-octave noise flow field plus a
;; constant wind, fading white -> transparent as it ages, and respawns on a small
;; sphere at the origin when too old — so a turbulent plume streams downwind.
;;
;; The original keeps 160k particles' state in ping-pong FBO textures and updates
;; them in a fragment shader. This engine has no render-to-texture, and particles
;; need persistent state (so the stateless geometry shader can't help either), so
;; the simulation runs on the CPU here: ~9k particles in Racket vectors, re-uploaded
;; to a points primitive each frame. Same motion, fewer particles.

(retained)
(hide-editor)
(set-window-size 920 620)
(background (vector 0.02 0.02 0.035))

;; ---- orbit camera -----------------------------------------------------------
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

;; ---- noise flow field -------------------------------------------------------
(define (sn x z t) (- (* 2.0 (noise x z t)) 1.0))     ; signed noise
(define (fbm x z t pers)                               ; 2 octaves (kept cheap for CPU)
  (+ (* 1.0 (sn x z t))
     (* pers (sn (* x 2.0) (* z 2.0) t))))
;; a 3-component noise velocity at (p, time) — shuffled args per component
(define (noise-vel px py pz nt pers)
  (vector (fbm px py nt pers) (fbm py pz nt pers) (fbm pz px nt pers)))

;; ---- particle state (persists across frames in the every-frame closure) -----
(define N 2500)
(define MAXAGE 9.0)
(define PI 3.141592653589793)
(define posx (make-vector N 0.0)) (define posy (make-vector N 0.0)) (define posz (make-vector N 0.0))
(define age  (make-vector N 0.0))
(define seed (make-vector N 0.0))   ; per-particle churn (embedded Racket's (random) is constant)
(define (h01 x) (let ((s (* (sin (* x 12.9898)) 43758.5453))) (- s (floor s))))   ; hash -> [0,1)

;; respawn across a VOLUME (not a point): with few particles, spawning them spread
;; through the box means each samples a different part of the noise field and flows
;; along it — a volumetric churning cloud, rather than one coherent clump.
(define BOX 3.0)
(define (respawn! i)
  (let ((k (vector-ref seed i)))
    (vector-set! seed i (+ k 1.0))   ; advance so the NEXT respawn differs
    (vector-set! posx i (* (- (* 2.0 (h01 (+ (* i 1.7) k)))      1.0) BOX))
    (vector-set! posy i (*    (h01 (+ (* i 3.3) k 5.0))              (* BOX 1.3)))
    (vector-set! posz i (* (- (* 2.0 (h01 (+ (* i 5.1) k 11.0)))  1.0) BOX))))

(do ((i 0 (+ i 1))) ((= i N))
  (respawn! i)
  (vector-set! age i (* MAXAGE (h01 (* i 2.13)))))   ; stagger initial ages

;; ---- points primitive (positions/colours re-uploaded each frame) ------------
;; a geometry shader billboards each GL_POINT into a soft additive sprite so the
;; wispy plume is actually visible (1px points spread over a big volume vanish).
(define pc-vert "
#version 120
void main(){ gl_FrontColor = gl_Color; gl_Position = gl_Vertex; }")
(define pc-geom "
#version 120
#extension GL_EXT_geometry_shader4 : enable
uniform float uSize;
void main(){
  vec4 e = gl_ModelViewMatrix * vec4(gl_PositionIn[0].xyz, 1.0);   // eye space
  vec4 col = gl_FrontColorIn[0];
  float s = uSize;
  gl_TexCoord[0]=vec4(0.0,0.0,0,0); gl_FrontColor=col; gl_Position=gl_ProjectionMatrix*(e+vec4(-s,-s,0,0)); EmitVertex();
  gl_TexCoord[0]=vec4(1.0,0.0,0,0); gl_FrontColor=col; gl_Position=gl_ProjectionMatrix*(e+vec4( s,-s,0,0)); EmitVertex();
  gl_TexCoord[0]=vec4(0.0,1.0,0,0); gl_FrontColor=col; gl_Position=gl_ProjectionMatrix*(e+vec4(-s, s,0,0)); EmitVertex();
  gl_TexCoord[0]=vec4(1.0,1.0,0,0); gl_FrontColor=col; gl_Position=gl_ProjectionMatrix*(e+vec4( s, s,0,0)); EmitVertex();
  EndPrimitive();
}")
(define pc-frag "
#version 120
void main(){
  float a = smoothstep(0.5, 0.0, length(gl_TexCoord[0].xy - vec2(0.5)));   // soft round falloff
  gl_FragColor = vec4(gl_Color.rgb, gl_Color.a * a);
}")
(define cloud (build-particles N))
(with-primitive cloud
  (hint-points) (hint-solid #f) (hint-unlit) (hint-nozwrite)
  (blend-mode 'src-alpha 'one)          ; additive: overlapping sprites glow
  (shader-source-geom pc-vert pc-geom pc-frag gl-points gl-triangle-strip 4))

;; ---- per-frame: advect + age + orbit ----------------------------------------
;; integrate by REAL elapsed time so the plume spreads at a wall-clock-correct rate
;; even when the CPU sim can't hit full framerate.
(define *lastT* (vector -1.0))
(every-frame
  (let* ((T (time)) (ang (* T 0.12))
         (DT (let ((l (vector-ref *lastT* 0))) (vector-set! *lastT* 0 T)
                  (if (< l 0.0) 0.016 (max 0.0 (min 0.25 (- T l))))))   ; allow catch-up at low fps
         (wind  (tweak "wind x"    0.2  -1.0 2.0))
         (mag   (tweak "noise mag" 1.5   0.0 3.0))
         (pscl  (tweak "noise scale" 1.1 0.2 3.0))
         (pers  (tweak "persistence" 0.4  0.02 1.0))
         (nt    (* T 0.15)))
    ;; camera orbits the churning cloud
    (let ((eye (vector (* 5.0 (sin ang)) 1.6 (* 5.0 (cos ang))))
          (tgt (vector 0.6 0.2 0.0)))
      (set-camera-transform (look-at eye tgt (vector 0.0 1.0 0.0))))
    ;; integrate every particle
    (do ((i 0 (+ i 1))) ((= i N))
      (let ((a (+ (vector-ref age i) DT)))
        (if (> a MAXAGE) (begin (respawn! i) (vector-set! age i (- a MAXAGE)))
            (let* ((px (vector-ref posx i)) (py (vector-ref posy i)) (pz (vector-ref posz i))
                   (nv (noise-vel (* px pscl) (* py pscl) (* pz pscl) nt pers)))
              (vector-set! age i a)
              (vector-set! posx i (+ px (* (+ wind (* (vx nv) mag)) DT)))
              (vector-set! posy i (+ py (* (* (vy nv) mag) DT)))
              (vector-set! posz i (+ pz (* (* (vz nv) mag) DT)))))))
    ;; upload positions + age-faded colour (white -> transparent)
    (with-primitive cloud
      (shader-set-float! "uSize" (tweak "sprite size" 0.045 0.008 0.15))
      (pdata-index-map! (lambda (i p) (vector (vector-ref posx i) (vector-ref posy i) (vector-ref posz i))) "p")
      (pdata-index-map!
        (lambda (i c)
          (let* ((f (- 1.0 (/ (vector-ref age i) MAXAGE)))    ; 1 young -> 0 old
                 (a (* 0.5 (+ 0.18 (* 0.82 f)))))             ; alpha floor so old stay visible
            (vector (* 0.45 f) (+ 0.45 (* 0.55 f)) 1.0 a)))   ; young cyan-white -> old blue
        "c"))))
