; generative walkthrough — a slow forward walk through a field of generative
; (recursive fractal) trees that sway with the audio, fading into fog. Inspired by
; Radiohead's PolyFauna: organic, ambient, endless. Landscape.
;
; Immediate mode: the scene is rebuilt each frame (trees animate + the camera
; walks). Tree positions are a deterministic hash of their row so they stay put
; as you approach, and new rows appear ahead as you advance.

(start-audio "system:capture_1" 512 44100)
(background (vector 0.0 0.0 0.0))            ; black; the post paints a violet sky
(fog (vector 0.16 0.10 0.42) 0.05 6 40)      ; distance fades into violet haze

;; ---- tweakables ------------------------------------------------------------
(define WALK-SPEED 2.2)      ; forward metres / sec
(define SPACING    6.0)      ; distance between tree rows
(define ROWS       7)        ; rows drawn ahead
(define DEPTH      4)        ; tree recursion depth
(define SWAY       10.0)     ; branch sway amount
(define GX         13)       ; grass patch cells (x,z)
(define GZ         13)
(define GSTEP      1.7)
(define NFIRE      10)       ; fireflies

(define (frac q) (- q (floor q)))
(define (h i) (- (* 2 (frac (* (sin (* (+ i 1) 91.73)) 3758.53))) 1))   ; [-1,1]
(define (h01 i) (abs (h i)))

; value noise (perlin-ish): smoothstep-interpolated hash lattice, returns [0,1]
(define (vnoise x y)
  (let* ((xi (floor x)) (yi (floor y)) (xf (- x xi)) (yf (- y yi))
         (u (* xf xf (- 3.0 (* 2.0 xf)))) (v (* yf yf (- 3.0 (* 2.0 yf))))
         (nn (lambda (a b) (frac (* (sin (+ (* a 12.9898) (* b 78.233))) 43758.5453))))
         (a (nn xi yi)) (b (nn (+ xi 1) yi)) (c (nn xi (+ yi 1))) (d (nn (+ xi 1) (+ yi 1))))
    (+ (* a (- 1 u) (- 1 v)) (* b u (- 1 v)) (* c (- 1 u) v) (* d u v))))

(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))
(define (vlerp a b s) (vadd a (v* (vsub b a) s)))
(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))

;; recursive tree: a segment cube, then 3 child branches at the tip
(define (tree-colour d)
  (let ((f (/ d DEPTH)))                       ; 1 trunk -> ~0.2 twig
    (vector (+ 0.25 (* 0.55 f)) (+ 0.35 (* 0.3 (- 1 f))) (+ 0.35 (* 0.5 (- 1 f))))))

(define (tree depth len)
  (when (> depth 0)
    (with-state
      (translate (vector 0 (* 0.5 len) 0))
      (scale (vector (* 0.07 len) len (* 0.07 len)))
      (colour (tree-colour depth))
      (build-cube))
    (translate (vector 0 len 0))
    (let ((sway (+ 22 (* SWAY (sin (+ (* 0.8 (time)) (* depth 0.6)))) (* 18 (gain)))))
      (with-state (rotate (vector 0 0 sway))     (rotate (vector 0 35 0))  (tree (- depth 1) (* len 0.72)))
      (with-state (rotate (vector 0 0 (- sway))) (rotate (vector 0 -35 0)) (tree (- depth 1) (* len 0.72)))
      (with-state (rotate (vector sway 0 0))     (rotate (vector 0 20 0))  (tree (- depth 1) (* len 0.66))))))

; rolling-hills height at world (x,z): multi-octave, world-fixed so it flows past
(define (terrain wx wz)
  (+ (* 3.2 (sin (* 0.11 wx)))
     (* 2.4 (cos (* 0.09 wz)))
     (* 1.2 (sin (* 0.26 (+ wx (* 0.5 wz)))))
     (* 0.6 (sin (* 0.5 wx)) (cos (* 0.45 wz)))))

(define (draw-trees wz)
  (let ((base (inexact->exact (floor (/ wz SPACING)))))
    (let loop ((i -1))
      (when (< i ROWS)
        (let* ((row (+ base i))
               (tz  (* row SPACING))
               (tx  (* 8 (h row)))
               (ty  (terrain tx tz))            ; plant on the terrain
               (len (+ 1.3 (* 0.9 (h01 (* row 3))))))
          (with-state
            (translate (vector tx ty tz))
            (rotate (vector 0 (* 180 (h (+ row 1))) 0))
            (tree DEPTH len)))
        (loop (+ i 1))))))

; contoured wireframe terrain, centred on the walker; height uses WORLD coords so
; the hills flow past as you advance (grid follows in z, contours stay put).
; noise-placed swaying grass blades on the terrain, in a patch around the walker
(define (grass wz)
  (let ((bz (inexact->exact (floor (/ wz GSTEP)))))
    (let zloop ((iz 0))
      (when (< iz GZ)
        (let xloop ((ix 0))
          (when (< ix GX)
            (let* ((cz (* (+ bz iz) GSTEP))
                   (cx (* (- ix (quotient GX 2)) GSTEP))
                   (nx (+ cx (* GSTEP 0.7 (h (+ (* cx 1.3) (* cz 2.7))))))
                   (nz (+ cz (* GSTEP 0.7 (h (+ (* cx 3.1) (* cz 1.9))))))
                   (dens (vnoise (* nx 0.35) (* nz 0.35))))
              (when (> dens 0.52)
                (let* ((gy   (terrain nx nz))
                       (bh   (+ 0.3 (* 0.7 dens)))
                       (wind (+ (* 9 (sin (+ (* 1.4 (time)) (* 0.3 nx)))) (* 22 (gain)))))
                  (with-state
                    (translate (vector nx gy nz))
                    (rotate (vector 0 (* 180 (h nx)) 0))
                    (rotate (vector wind 0 0))
                    (translate (vector 0 (* 0.5 bh) 0))
                    (scale (vector 0.05 bh 0.05))
                    (colour (vector (* 0.25 dens) (+ 0.35 (* 0.55 dens)) (* 0.2 dens)))
                    (build-cube)))))
            (xloop (+ ix 1))))
        (zloop (+ iz 1))))))

; firefly position at time t (deterministic path, so we can trace a real trail)
(define TRAIL 10)
(define (firefly-pos j t)
  (let* ((wz (* WALK-SPEED t))
         (fx (+ (* 11 (h (* j 3.1))) (* 2.4 (sin (+ (* 0.5 t) (* j 2.0))))))
         (fz (+ wz 6 (* 14 (frac (* j 0.317))) (* 2.4 (cos (+ (* 0.4 t) j)))))
         (fy (+ (terrain fx fz) 1.6 (* 1.5 (sin (+ (* 0.7 t) (* j 1.3)))))))
    (vector fx fy fz)))

; occasional blinking fireflies, each a camera-facing RIBBON trailing its path
(define (fireflies wz)
  (let loop ((j 0))
    (when (< j NFIRE)
      (let ((bl (max 0.0 (sin (+ (* 2.3 (time)) (* j 5.0))))))
        (when (> bl 0.1)
          (let ((rb (build-ribbon TRAIL)))
            (with-primitive rb
              (identity)
              (blend-mode 'one 'one)
              (hint-unlit)
              (colour (v* (vector 1.0 0.85 0.4) (* bl (+ 0.5 (* 2.2 (gain))))))
              ; ribbon points step back along the firefly's path
              (pdata-index-map!
                (lambda (i v) (firefly-pos j (- (time) (* i 0.07))))
                "p")
              ; width tapers from head (wide) to tail (thin)
              (pdata-index-map!
                (lambda (i w)
                  (let ((f (- 1.0 (/ i (exact->inexact TRAIL)))))
                    (vector (* 0.14 f) (* 0.14 f) (* 0.14 f))))
                "w")))))
      (loop (+ j 1)))))

; occasional SPARKS: fast ember streaks that burst, arc, and fade — short bright
; ribbon trails (distinct from the slow drifting fireflies).
(define NSPARK 5)
(define SPARK-TRAIL 14)
(define (spark-period j) (+ 2.5 (* 2.5 (h01 (* j 7.3)))))
(define (spark-pos j t)
  (let* ((per (spark-period j))
         (k   (floor (/ t per)))
         (ph  (- t (* per k)))
         (wz  (* WALK-SPEED t))
         (ox  (* 9 (h (+ (* j 3) (* k 5)))))
         (oz  (+ wz 4 (* 12 (frac (* (+ j (* k 2)) 0.37)))))
         (oy  (+ (terrain ox oz) 0.5))
         (dir (* 6.2832 (h (+ (* j 2) (* k 3)))))
         (spd 7.5))
    (vector (+ ox (* spd ph (cos dir)))
            (+ oy (* 5.5 ph) (* -4.5 ph ph))          ; arc up then fall
            (+ oz (* spd ph (sin dir))))))
(define (spark-active j t)
  (< (- t (* (spark-period j) (floor (/ t (spark-period j))))) 0.7))
(define (sparks wz)
  (let ((t (time)))
    (let loop ((j 0))
      (when (< j NSPARK)
        (when (spark-active j t)
          (let ((rb (build-ribbon SPARK-TRAIL)))
            (with-primitive rb
              (identity)
              (blend-mode 'one 'one)
              (hint-unlit)
              (colour (vector 1.0 0.7 0.35))
              (pdata-index-map! (lambda (i v) (spark-pos j (- t (* i 0.02)))) "p")
              (pdata-index-map!
                (lambda (i w) (let ((f (- 1.0 (/ i (exact->inexact SPARK-TRAIL)))))
                                (vector (* 0.06 f) (* 0.06 f) (* 0.06 f))))
                "w"))))
        (loop (+ j 1))))))

; a single wildfire line, revealed one word at a time then looping
(define words (list "the" "wildfire" "walks" "the" "black" "hills"
                    "breathing" "embers" "into" "the" "sleeping" "night"))
(define WORD-DUR 0.55)              ; seconds per word
(define (poem-text)
  (let* ((n     (length words))
         (total (* WORD-DUR (+ n 4)))          ; extra beats = pause before loop
         (t     (- (time) (* total (floor (/ (time) total)))))
         (shown (min n (inexact->exact (floor (/ t WORD-DUR))))))
    (let loop ((i 0) (acc ""))
      (if (< i shown)
          (loop (+ i 1) (string-append acc (if (> i 0) " " "") (list-ref words i)))
          acc))))

(define GRES 44)
(define GSIZE 64.0)
(define (ground wz)
  (let ((g (build-seg-plane GRES GRES)))
    (with-primitive g
      (pdata-add "ori" "v")
      (pdata-copy "p" "ori")
      (translate (vector 0 0 wz))            ; world: follow the walker in z
      (rotate (vector -90 0 0))              ; lay flat (local +Z normal -> world +Y)
      (scale (vector GSIZE GSIZE GSIZE))
      (hint-solid #f)
      (hint-wire)
      (line-width 1.6)
      (wire-opacity 1)
      (wire-colour (vector 0.4 0.7 0.85))       ; brighter cyan contour lines
      (pdata-index-map!
        (lambda (i v)
          (let* ((o  (pdata-ref "ori" i))
                 (ox (vx o)) (oy (vy o))
                 (wx (* GSIZE ox))
                 (wzz (- wz (* GSIZE oy)))     ; -90 X rot maps local y -> world -z
                 (hh (terrain wx wzz)))
            (vector ox oy (/ hh GSIZE))))     ; local-z displace -> world height hh
        "p"))))

; PolyFauna-style colour grade: violet gradient sky, geometry as dark-purple
; silhouettes, bright things (fireflies) glow through, soft trails + grain.
(define post "
uniform sampler2D tex; uniform sampler2D prev; uniform sampler2D depthTex;
uniform mat4 uVPinv; uniform mat4 uVPprev;
uniform float time; uniform float audio; uniform vec2 resolution; uniform float dt;
varying vec2 uv;
// the violet-silhouette colour grade
vec3 grade(vec3 scene, vec2 p){
  float lum = max(scene.r, max(scene.g, scene.b));
  vec3 sky = mix(vec3(0.34,0.13,0.86), vec3(0.30,0.36,0.98), pow(p.y, 0.7));
  vec3 geo = sky * 0.14 + scene * vec3(0.5,0.25,0.7);
  vec3 col = mix(sky, geo, smoothstep(0.015, 0.14, lum));
  col += scene * smoothstep(0.5, 0.9, lum) * vec3(1.0,0.8,0.5);
  float vig = 16.0*p.x*p.y*(1.0-p.x)*(1.0-p.y);
  col *= pow(vig, 0.42);
  float l = dot(col, vec3(0.299,0.587,0.114));
  return mix(col, vec3(l), 0.32);                       // desaturate toward noir
}
void main(){
  vec3 col = grade(texture2D(tex, uv).rgb, uv);
  // --- reprojection (camera-motion) blur: reconstruct world pos from depth,
  //     reproject through last frame's view-proj, blur prev along that velocity.
  float STRENGTH = 14.0;                                // <-- manual blur amount (try 2..25)
  float d = texture2D(depthTex, uv).r;
  if (d < 0.9999){
    vec4 clip = vec4(uv*2.0-1.0, d*2.0-1.0, 1.0);
    vec4 world = uVPinv * clip; world /= world.w;
    vec4 pp = uVPprev * world; pp /= pp.w;
    vec2 vel = clamp(((pp.xy*0.5+0.5) - uv) * STRENGTH, vec2(-0.2), vec2(0.2));
    vec3 p = vec3(0.0);
    for (int i = 1; i <= 8; i++){                       // 8 taps along the velocity
      p += texture2D(prev, uv + vel * (float(i)/8.0)).rgb;
    }
    p *= 0.125;
    col = mix(p, col, clamp(dt*8.0, 0.18, 1.0));        // lower floor = longer trail
  }
  // --- crisp cinematic noir grain on top ---
  float grain = fract(sin(dot(uv*resolution + time*80.0, vec2(12.9898,78.233)))*43758.5453);
  col += (grain - 0.5) * 0.11;
  float dust = step(0.9975, fract(sin(floor(uv.x*resolution.x*0.5)+floor(time*10.0))*91.3));
  col += dust * 0.16;
  col *= 0.96 + 0.04*fract(sin(floor(time*20.0))*57.31);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}")

(every-frame
  (let* ((t   (time))
         (wz  (* WALK-SPEED t))
         (sx  (* 0.6 (sin (* 0.35 t))))
         (bob (+ 3.2 (* 0.12 (sin (* 2.2 t)))))
         (eye (vector sx (+ (terrain sx wz) bob) wz))                 ; ride above the hills
         (tgt (vector (* 0.4 (sin (* 0.3 t)))
                      (+ (terrain sx (+ wz 8)) (- bob 1.0)) (+ wz 8))))
    (set-fov 58)
    (set-camera-transform (look-at eye tgt (vector 0 1 0)))
    (ground wz)
    (grass wz)
    (draw-trees wz)
    (fireflies wz)
    (sparks wz))
  (post-shader post))
