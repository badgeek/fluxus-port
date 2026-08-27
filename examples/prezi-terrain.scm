; prezi-terrain.scm
;
; a different look from the typewriter deck: a STATIC low-poly isometric terrain
; (orthographic camera, height-shaded, generated once and never animated) with a
; Prezi-style camera that swoops, rotates and zooms from one labelled point to the
; next. Labels lie flat on the ground like map markers; the camera flies to each.
;
; nothing in the terrain moves — only the camera transforms.

(background (vector 0.06 0.07 0.10))
(fog (vector 0.06 0.07 0.10) 0.02 40 240)     ; soft depth into the haze

(define DEG (/ 3.141592653589793 180.0))
(define CW  0.44)                              ; build-text advance (matches engine)

;; ---- vector + easing helpers ----------------------------------------------
(define (clamp01 x) (max 0.0 (min 1.0 x)))
(define (ease x) (let ((u (clamp01 x))) (- 1.0 (* (- 1.0 u) (- 1.0 u) (- 1.0 u)))))
(define (lerp a b t) (+ a (* (- b a) t)))
(define (v3lerp a b t)
  (vector (lerp (vx a) (vx b) t) (lerp (vy a) (vy b) t) (lerp (vz a) (vz b) t)))
(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))
(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))

;; ---- the static heightfield -----------------------------------------------
; deterministic (no time term) so the terrain is generated once and stays put.
(define (hf x z)
  (+ (* 5.0 (sin (* 0.11 x)))
     (* 4.0 (cos (* 0.09 z)))
     (* 2.0 (sin (* 0.23 (+ x (* 0.6 z)))))
     (* 1.0 (sin (* 0.5 x)) (cos (* 0.42 z)))))

(define GRES  56)        ; grid resolution
(define GSIZE 120.0)     ; world size of the plane

; height -> colour ramp (deep valley to pale ridge), an isometric map palette.
(define (height-colour h)
  (let ((t (clamp01 (/ (+ h 8.0) 20.0))))
    (vector (+ 0.10 (* 0.65 t))
            (+ 0.16 (* 0.55 t))
            (+ 0.24 (* 0.45 (- 1.0 t))))))

(define (terrain)
  (let ((g (build-seg-plane GRES GRES)))
    (with-primitive g
      (pdata-add "ori" "v")
      (pdata-copy "p" "ori")
      (identity)
      (rotate (vector -90 0 0))                 ; lay flat (local z -> world height)
      (scale (vector GSIZE GSIZE GSIZE))
      (hint-unlit)
      (line-width 1.0)
      (wire-opacity 0.35)
      (wire-colour (vector 0.5 0.6 0.75))
      (hint-wire)                                ; solid fill + faint wire = low-poly iso
      ; displace to the heightfield (world coords, so it is fixed in space)
      (pdata-index-map!
        (lambda (i v)
          (let* ((o  (pdata-ref "ori" i))
                 (wx (* GSIZE (vx o)))
                 (wz (* GSIZE (vy o))))
            (vector (vx o) (vy o) (/ (hf wx wz) GSIZE))))
        "p")
      ; colour each vertex by its height
      (pdata-index-map!
        (lambda (i c)
          (let* ((o  (pdata-ref "ori" i))
                 (wx (* GSIZE (vx o)))
                 (wz (* GSIZE (vy o))))
            (height-colour (hf wx wz))))
        "c"))))

;; ---- labels that lie flat on the terrain ----------------------------------
(define (label str x z bright)
  (let ((tp (build-text str))
        (sc 1.4))
    (with-primitive tp
      (identity)
      (hint-unlit)
      (translate (vector (- x (* 0.5 CW (string-length str) sc))
                         (+ (hf x z) 0.2) z))
      (rotate (vector -90 0 0))                  ; lie flat, readable from above
      (scale (vector sc sc sc))
      (colour (v* (vector 1.0 1.0 1.0) bright)))))

;; ---- the stops the camera flies between (focus x,z + label) ----------------
(define stops (list
  (list   0.0   0.0 "FLUXUS")
  (list  26.0  20.0 "isometric")
  (list -22.0  40.0 "worlds")
  (list  10.0  60.0 "live coded")
  (list -14.0  80.0 "since 2005")))
(define NS (length stops))

(define (stop-xz s) (vector (car s) (cadr s)))
(define (stop-txt s) (caddr s))

(define DWELL  3.2)      ; seconds parked at a stop
(define TRAVEL 2.6)      ; seconds flying to the next
(define ELEV   34.0)     ; isometric elevation angle
(define ZOOM   16.0)     ; ortho half-height when parked (smaller = closer)
(define BUMP   22.0)     ; extra zoom-out at the midpoint of a flight (prezi pull-back)

;; ---- isometric orthographic camera on a target -----------------------------
(define (iso-cam tx tz zoom azim asp)
  (let* ((a   (* azim DEG)) (e (* ELEV DEG))
         (dist 300.0)
         (tgt (vector tx (hf tx tz) tz))
         (dir (vector (* (cos e) (sin a)) (sin e) (* (cos e) (cos a))))
         (eye (vadd tgt (v* dir dist))))
    (ortho #t)
    (frustum (- asp) asp -1.0 1.0)
    (set-ortho-zoom zoom)
    (set-camera-transform (look-at eye tgt (vector 0 1 0)))))

;; ---- clean prezi grade: vertical sky gradient + vignette (no grain) --------
(define post "
uniform sampler2D tex; uniform float time; uniform vec2 resolution;
varying vec2 uv;
void main(){
  vec3 scene = texture2D(tex, uv).rgb;
  vec3 sky = mix(vec3(0.05,0.06,0.09), vec3(0.10,0.13,0.20), uv.y);
  float lum = max(scene.r, max(scene.g, scene.b));
  vec3 col = mix(sky, scene, smoothstep(0.02, 0.12, lum));
  float vig = 16.0*uv.x*uv.y*(1.0-uv.x)*(1.0-uv.y);
  col *= pow(vig, 0.22);
  gl_FragColor = vec4(col, 1.0);
}")

;; ---- the loop: only the camera transforms ----------------------------------
(every-frame
  (terrain)

  (let* ((sz    (get-screen-size))
         (asp   (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 1.3333))
         (period (+ DWELL TRAVEL))
         (loop-t (* period NS))
         (tt    (- (time) (* loop-t (floor (/ (time) loop-t)))))
         (i     (inexact->exact (floor (/ tt period))))
         (ph    (- tt (* i period)))
         (trav  (> ph DWELL))
         (te    (if trav (ease (/ (- ph DWELL) TRAVEL)) 0.0))
         (a     (stop-xz (list-ref stops (modulo i NS))))
         (b     (stop-xz (list-ref stops (modulo (+ i 1) NS))))
         (p     (v3lerp (vector (vx a) 0 (vy a)) (vector (vx b) 0 (vy b)) te))
         (zoom  (+ ZOOM (* BUMP (sin (* 3.14159265 te)))))          ; pull back mid-flight
         (azim  (+ 45.0 (* 10.0 (sin (* 0.7 (+ i te)))))))          ; gentle rotate
    ; labels: the focused stop is bright, the rest dim
    (let loop ((j 0))
      (when (< j NS)
        (let* ((s   (list-ref stops j))
               (foc (if trav (if (> te 0.5) (modulo (+ i 1) NS) (modulo i NS)) (modulo i NS)))
               (br  (if (= j foc) 1.0 0.28)))
          (label (stop-txt s) (car s) (cadr s) br))
        (loop (+ j 1))))
    (iso-cam (vx p) (vz p) zoom azim asp))
  (post-shader post))
