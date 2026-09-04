; TRACK RADAR — tactical multi-target tracking display, NTSC composite look.
; Pale blue-white wire chrome on black, boxed target glyphs with ID + X/Y data
; callouts, pink hazard triangles, dark-red track lines converging on a kill
; point, legend + readout panel, ticked border with axis labels. Landscape 16:9.
; Retained mode; the whole panel is rebuilt each frame into *dyn* (flat XZ
; plane, top-down camera) — same pattern as alien-ui.scm. Needs the Racket host.
; Keys: n = toggle NTSC.

(clear)
(anti-alias #t)
(set-window-size 960 540)                      ; 16:9 landscape
;; NTSC look via post-shader (fake composite): the REAL (ntsc) filter rasters the
;; frame down to ~525-line composite video, which destroys small text no matter the
;; framebuffer res. The shader fakes the look — chroma fringing, smear, scanlines,
;; noise, vignette — at FULL resolution, so the callout text stays legible.
;; Key n toggles the real (ntsc) on top for the fully-degraded VHS version.
(define ntsc-post "
uniform sampler2D tex;
uniform float time;
varying vec2 uv;
float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
void main(){
  vec2 u = uv;
  // tiny horizontal jitter per scanline (time-varying tape weave)
  u.x += 0.00018*sin(u.y*290.0 + time*6.0);
  // chroma fringing: shift R right, B left, with a soft 3-tap horizontal smear
  float ca = 0.0007;
  vec3 c;
  c.r = 0.75*texture2D(tex, u+vec2( ca,0.0)).r + 0.25*texture2D(tex, u+vec2( ca*2.0,0.0)).r;
  c.g = 0.85*texture2D(tex, u).g             + 0.15*texture2D(tex, u+vec2(0.0005,0.0)).g;
  c.b = 0.75*texture2D(tex, u-vec2( ca,0.0)).b + 0.25*texture2D(tex, u-vec2( ca*2.0,0.0)).b;
  // bloom-ish lift from a wider tap (bright lines glow)
  vec3 w = texture2D(tex, u+vec2(0.0015,0.0)).rgb + texture2D(tex, u-vec2(0.0015,0.0)).rgb;
  c += w*0.12;
  // scanlines + slow roll flicker
  c *= 0.93 + 0.07*sin(u.y*1100.0);
  c *= 0.97 + 0.03*sin(time*2.1 + u.y*9.0);
  // per-pixel noise sparkle
  c += (hash(u*vec2(1920.0,1080.0) + fract(time)*13.7) - 0.5) * 0.028;
  // mild saturation push (composite chroma ring)
  float l = dot(c, vec3(0.299,0.587,0.114));
  c = mix(vec3(l), c, 1.15);
  // vignette
  float d = length(u - 0.5);
  c *= 1.0 - d*d*0.55;
  gl_FragColor = vec4(c, 1.0);
}")

(define PI 3.14159265358979)
(define *ntsc* #f)   ; real composite filter OFF by default (post-shader carries the look)
(define *fc* 0)

;; ---- 2D helpers (XZ plane, Y up, flat panel) --------------------------------
(define (v2 x z) (vector x 0.02 z))
(define (vlen2 a b) (let ((dx (- (vx b)(vx a))) (dz (- (vz b)(vz a)))) (sqrt (+ (* dx dx)(* dz dz)))))
(define (deg r) (* r 57.2957795))
(define (pad n w) (let ((s (number->string n))) (string-append (make-string (max 0 (- w (string-length s))) #\0) s)))
(define (clampf x lo hi) (max lo (min hi x)))
(define (f1 x) (/ (round (* x 10)) 10.0))      ; one decimal

(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye))) (s (vnormalise (vcross f up))) (u (vcross s f)))
    (vector (vx s)(vx u)(- (vx f)) 0 (vy s)(vy u)(- (vy f)) 0
            (vz s)(vz u)(- (vz f)) 0 (- (vdot s eye))(- (vdot u eye))(vdot f eye) 1)))

;; plot half-extents (16:9)
(define HW 14.7)
(define HH 8.3)
;; plot area inside the chrome (border + axis labels live outside this)
(define PL (- 0.94 HW)) (define PR (- HW 0.6))     ; left/right x
(define PT (+ (- HH) 1.35)) (define PB (- HH 0.75)) ; top/bottom z (z+ = down-screen)
(define (mapx u) (+ PL (* (/ u 100.0) (- PR PL))))  ; data 0..100 -> panel x
(define (mapz v) (+ PB (* (/ v 100.0) (- PT PB))))  ; data 0..100 (v up) -> panel z

;; ---- palette ----------------------------------------------------------------
(define C-LINE (vector 0.60 0.72 1.00))        ; pale blue chrome
(define C-DIM  (vector 0.26 0.34 0.58))        ; dim blue
(define C-WHT  (vector 0.94 0.95 1.00))
(define C-PINK (vector 1.00 0.34 0.48))        ; hazard pink/red
(define C-TRK  (vector 0.40 0.09 0.11))        ; dark red track line
(define C-LAV  (vector 0.86 0.82 0.96))        ; lavender fill (waypoints, panel)
(define C-BLK  (vector 0.02 0.01 0.02))

;; ---- primitive builders -----------------------------------------------------
(define (mk-line a b col w)
  (let* ((mid (vector (* 0.5 (+ (vx a)(vx b))) 0.02 (* 0.5 (+ (vz a)(vz b)))))
         (dx (- (vx b)(vx a))) (dz (- (vz b)(vz a)))
         (len (max 0.001 (sqrt (+ (* dx dx)(* dz dz))))))
    (let ((p (build-cube)))
      (with-primitive p (hint-solid)(hint-unlit)(colour col)
        (translate mid) (rotate (vector 0 (deg (atan dx dz)) 0))
        (scale (vector (* 0.045 w) 0.02 len)))
      p)))
(define (mk-box pos sx sz col lw)
  (let ((p (build-cube)))
    (with-primitive p (hint-solid #f)(hint-wire)(hint-unlit)(backfacecull #f)
      (line-width lw)(wire-colour col)(colour C-BLK)
      (translate pos)(scale (vector sx 0.01 sz)))
    p))
(define (mk-fill pos sx sz col)
  (let ((p (build-cube)))
    (with-primitive p (hint-solid)(hint-unlit)(colour col)
      (translate pos)(scale (vector sx 0.015 sz)))
    p))
(define (mk-text s pos scl col)
  (if (= (string-length s) 0) -1
    (let ((p (build-text s)))
      (with-primitive p (hint-unlit)(colour col)
        (translate (vector (vx pos) 0.12 (vz pos)))
        (rotate (vector -90 0 0))(scale (vector scl scl scl)))
      p)))

;; ---- dynamic prim tracking --------------------------------------------------
(define *dyn* '())
(define (dyn! id) (when (>= id 0) (set! *dyn* (cons id *dyn*))) id)
(define (clear-dyn!) (for-each (lambda (id) (when (>= id 0) (destroy id))) *dyn*) (set! *dyn* '()))
(define (line! a b col w) (dyn! (mk-line a b col w)))
(define (box! p sx sz col w) (dyn! (mk-box p sx sz col w)))
(define (fill! p sx sz col) (dyn! (mk-fill p sx sz col)))
(define (txt! s p scl col)
  (dyn! (mk-text s p scl col))
  (dyn! (mk-text s (v2 (+ (vx p) 0.035) (vz p)) scl col)))

;; ---- chrome: ticked border + axis labels ------------------------------------
(define (chrome! t)
  ;; outer frame
  (line! (v2 PL PT) (v2 PR PT) C-LINE 1.6)
  (line! (v2 PL PB) (v2 PR PB) C-LINE 1.6)
  (line! (v2 PL PT) (v2 PL PB) C-LINE 1.6)
  (line! (v2 PR PT) (v2 PR PB) C-LINE 1.6)
  ;; ticks every 5 units, longer every 25 (all four edges)
  (let loop ((i 0)) (when (<= i 100)
    (let* ((big (= 0 (modulo i 25))) (tl (if big 0.30 0.15)) (x (mapx i)) (z (mapz i)))
      (line! (v2 x PT) (v2 x (- PT tl)) C-LINE (if big 1.4 0.9))
      (line! (v2 x PB) (v2 x (+ PB tl)) C-LINE (if big 1.4 0.9))
      (line! (v2 PL z) (v2 (- PL tl) z) C-LINE (if big 1.4 0.9))
      (line! (v2 PR z) (v2 (+ PR tl) z) C-LINE (if big 1.4 0.9)))
    (loop (+ i 5))))
  ;; axis labels: left column + bottom row
  (for-each (lambda (v) (txt! (pad v 3) (v2 (- PL 0.62) (+ (mapz v) 0.12)) 0.24 C-LINE))
            (list 0 25 50 75 100))
  (for-each (lambda (u) (txt! (pad u 3) (v2 (- (mapx u) 0.25) (+ PB 0.55)) 0.24 C-LINE))
            (list 0 25 50 75 100))
  ;; header status row (above the frame)
  (let* ((total (+ 130 (inexact->exact (floor (* t 0.4)))))
         (row (- PT 0.55))
         (dot (lambda (x) (fill! (v2 x row) 0.06 0.06 C-LINE))))
    (txt! "ACTIV 006"  (v2 PL row) 0.30 C-WHT)              (dot (+ PL 3.1))
    (txt! "COAST 000"  (v2 (+ PL 3.7) row) 0.30 C-WHT)      (dot (+ PL 6.8))
    (txt! (string-append "TOTAL " (pad total 4)) (v2 (+ PL 7.4) row) 0.30 C-WHT) (dot (+ PL 10.7))
    (txt! "NEW/S 0"    (v2 (+ PL 11.3) row) 0.30 C-WHT)     (dot (+ PL 13.9))
    (txt! "TRK STRICT" (v2 (+ PL 14.5) row) 0.30 C-WHT)     (dot (+ PL 18.0))
    (txt! "MATCH R40 G30" (v2 (+ PL 18.6) row) 0.30 C-WHT)
    (txt! "SIGNAL" (v2 (- PR 1.9) row) 0.30 C-WHT)))

;; ---- target glyph + callouts ------------------------------------------------
;; boxed sensor glyph: wire box, inner cross + core dot
(define (glyph! p)
  (box! p 0.62 0.62 C-WHT 3.0)
  (box! p 0.46 0.46 C-LINE 1.8)
  (line! (v2 (- (vx p) 0.16) (vz p)) (v2 (+ (vx p) 0.16) (vz p)) C-WHT 1.2)
  (line! (v2 (vx p) (- (vz p) 0.16)) (v2 (vx p) (+ (vz p) 0.16)) C-WHT 1.2)
  (fill! p 0.07 0.07 C-PINK))

;; line! with both ends extended by `ext` (miter fix for polygon corners: butt-joined
;; stretched-cube segments leave notches/overshoots at joints — overlap the ends)
(define (line-ext! a b col w ext)
  (let* ((dx (- (vx b)(vx a))) (dz (- (vz b)(vz a)))
         (len (max 1e-6 (sqrt (+ (* dx dx)(* dz dz)))))
         (ex (* ext (/ dx len))) (ez (* ext (/ dz len))))
    (line! (v2 (- (vx a) ex) (- (vz a) ez)) (v2 (+ (vx b) ex) (+ (vz b) ez)) col w)))

;; triangle as ONE real polygon prim (shared vertices -> GL joins the corners;
;; no butt-joint notches, no overlap trick needed)
(define (mk-tri a b c col lw)
  (let ((p (build-polygons 3 'triangle-list)))
    (with-primitive p (hint-solid #f)(hint-wire)(hint-unlit)(backfacecull #f)
      (line-width lw)(wire-colour col)(colour C-BLK)
      (pdata-set! "p" 0 a)(pdata-set! "p" 1 b)(pdata-set! "p" 2 c))
    p))
(define (tri-poly! a b c col lw) (dyn! (mk-tri a b c col lw)))

;; pink hazard triangle + value, hovering above-left of the target
(define (tri! p val)
  (let* ((tx (- (vx p) 0.55)) (tz (- (vz p) 0.85)) (s 0.26)
         (a (v2 tx (- tz s))) (b (v2 (- tx s) (+ tz s))) (c (v2 (+ tx s) (+ tz s))))
    (tri-poly! a b c C-PINK 4.5)
    (line! (v2 tx (+ tz s)) (v2 (vx p) (- (vz p) 0.34)) C-PINK 0.8)   ; stem to target
    (txt! (number->string (f1 val)) (v2 (+ tx 0.42) (- tz 0.1)) 0.28 C-PINK)))

;; ID label box above, leader line down to the glyph
(define (id-label! p id)
  (let* ((lz (- (vz p) 1.75)) (lp (v2 (vx p) lz)))
    (line! (v2 (vx p) (+ lz 0.32)) (v2 (vx p) (- (vz p) 0.34)) C-TRK 0.9)
    (fill! lp 1.5 0.55 C-BLK)
    (box! lp 1.5 0.55 C-WHT 2.4)
    (txt! (string-append "ID " (number->string id)) (v2 (- (vx p) 0.6) (+ lz 0.1)) 0.30 C-WHT)))

;; X/Y data box off to the right, connected by a line
(define (data-box! p u v flag)
  (let* ((bx (+ (vx p) 2.6)) (bp (v2 bx (vz p))))
    (line! (v2 (+ (vx p) 0.34) (vz p)) (v2 (- bx 0.8) (vz p)) C-DIM 1.0)
    (fill! bp 1.6 1.15 C-BLK)
    (box! bp 1.6 1.15 C-WHT 2.4)
    (txt! (string-append "X" (pad (inexact->exact (round u)) 3)) (v2 (- bx 0.62) (- (vz p) 0.30)) 0.28 C-WHT)
    (txt! (string-append "Y" (pad (inexact->exact (round v)) 3)) (v2 (- bx 0.62) (+ (vz p) 0.06)) 0.28 C-WHT)
    (txt! flag (v2 (- bx 0.62) (+ (vz p) 0.42)) 0.28 C-LINE)))

;; ---- targets ----------------------------------------------------------------
;; (id u v phase value flag)  — u,v in 0..100 plot space
(define targets
  (list (list 116 76 88 0.0 0.4  "N")
        (list 127 28 55 1.7 0.8  "N")
        (list 117 68 57 3.1 0.4  "N")
        (list 125 58 42 4.4 0.43 "NN")
        (list 121 85 28 5.6 0.5  "NN")
        (list 119 40 70 2.3 1.5  "N")))
;; kill point (track convergence) in plot space
(define KU 50.0) (define KV 4.0)

(define (target-pos tgt t)
  (let* ((u (list-ref tgt 1)) (v (list-ref tgt 2)) (ph (list-ref tgt 3)))
    (list (+ u (* 2.4 (sin (+ ph (* t 0.10)))))
          (+ v (* 1.8 (sin (+ (* ph 2.0) (* t 0.13))))))))

(define (draw-target! tgt t)
  (let* ((uv (target-pos tgt t)) (u (car uv)) (v (cadr uv))
         (p (v2 (mapx u) (mapz v)))
         (kp (v2 (mapx KU) (mapz KV)))
         ;; track waypoint: partway to the kill point, kinked onto the target's z
         (wu (+ u (* 0.45 (- KU u))))
         (wp (v2 (mapx wu) (vz p))))
    ;; track: target -> waypoint (horizontal) -> kill point
    (line! p wp C-TRK 1.0)
    (line! wp kp C-TRK 1.0)
    (fill! wp 0.30 0.22 C-LAV)                     ; waypoint block
    (glyph! p)
    (id-label! p (car tgt))
    (data-box! p u v (list-ref tgt 5))
    (tri! p (+ (list-ref tgt 4) (* 0.04 (sin (+ (* t 0.9) (list-ref tgt 3))))))))

;; ---- kill point crosshair ---------------------------------------------------
(define (killpoint! t)
  (let ((p (v2 (mapx KU) (mapz KV))) (r 0.22))
    (line! (v2 (- (vx p) r) (vz p)) (v2 (+ (vx p) r) (vz p)) C-PINK 1.6)
    (line! (v2 (vx p) (- (vz p) r)) (v2 (vx p) (+ (vz p) r)) C-PINK 1.6)
    (box! p 0.26 0.26 C-PINK 1.2)
    (when (< (modulo (inexact->exact (floor (* t 2))) 2) 1)
      (fill! p 0.10 0.10 C-PINK))))

;; ---- readout panel (light box, left-centre) ---------------------------------
(define (panel! t)
  (let ((px (mapx 17)) (pz (mapz 62)))
    (fill! (v2 px pz) 2.6 1.7 C-LAV)
    (let loop ((i 0)) (when (< i 4)
      (let* ((tgt (list-ref targets i))
             (uv (target-pos tgt t)))
        (txt! (string-append "X" (pad (inexact->exact (round (car uv))) 3)
                             "  " (number->string (f1 (+ 100 (* 60 (sin (+ i (* t 0.2)))))))
                             )
              (v2 (- px 1.1) (+ (- pz 0.6) (* i 0.42))) 0.26 (vector 0.30 0.14 0.40)))
      (loop (+ i 1))))))

;; ---- legend (lower-left) ----------------------------------------------------
(define (legend!)
  (let ((lx (mapx 6)) (lz (mapz 14)))
    (fill! (v2 lx lz) 2.1 1.9 C-BLK)
    (box! (v2 lx lz) 2.1 1.9 C-LINE 1.2)
    (let ((rows (list (list C-LINE "TRACK ACTIV")
                      (list C-WHT  "TRACK COAST")
                      (list C-LAV  "MARK STRICT")
                      (list C-PINK "ALARM RANGE")
                      (list C-TRK  "SPEED VECTOR"))))
      (let loop ((i 0)) (when (< i 5)
        (let ((z (+ (- lz 0.72) (* i 0.36))))
          (fill! (v2 (- lx 0.8) z) 0.28 0.14 (car (list-ref rows i)))
          (txt! (cadr (list-ref rows i)) (v2 (- lx 0.5) (+ z 0.08)) 0.23 C-LINE))
        (loop (+ i 1)))))))

;; ---- camera: flat top-down panel -------------------------------------------
(define (panel-camera)
  (set-fov 12)
  (let* ((zoom (/ (camera-dist) 10)) (d (* 84 zoom)))
    (set-camera-transform (look-at (vector 0 d 0) (vector 0 0 0) (vector 0 0 -1)))))

(retained)
(every-frame
  (begin
    (clear)
    (set! *fc* (+ *fc* 1))
    (when (< *fc* 24) (set-window-size 960 540))
    (let ((k (key-poll)))
      (when (= k 110) (set! *ntsc* (not *ntsc*)) (ntsc *ntsc*)))   ; n = toggle NTSC
    (let ((t (time)))
      (chrome! t)
      (for-each (lambda (tgt) (draw-target! tgt t)) targets)
      (killpoint! t)
      (panel! t)
      (legend!))
    (panel-camera)
    (post-shader ntsc-post)))
