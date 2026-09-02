; calib-3d.scm — camera + 3D calibration frame (9:16).
; Known-size world geometry so framing errors are readable as numbers:
;   - wireframe ground grid at y=-1, 10x10 world units, 1-unit cells
;   - three depth cubes (near/mid/far) on the grid, 1 unit each
;   - a 2x2x2 hero cube at origin: at FOV 50 / dist 10 it should span ~21% of
;     the frame HEIGHT (2 / (2*10*tan25) = 0.214)
;   - white 2D horizon rule drawn where the grid's far edge should project,
;     to check ground-plane vs overlay alignment
;   - HUD captions on the z=0 plane. NOTE: screen-correct only while the camera is
;     ON-AXIS (the default pose below). They are NOT billboarded — `concat` is a
;     no-op stub in this port, so a true camera-pinned HUD would need manual
;     camera-basis math (camera-yaw/pitch/dist). Orbit with the mouse and they swim.
; R resets the orbit camera. Drag = orbit, wheel = dolly.

(set-window-size 540 960)
(background (vector 0.05 0.05 0.07))

; Start HEAD-ON: the default orbit camera is tilted (yaw/pitch 0.3 rad), which
; shifts the 3D cubes' projection while the 2D ticks stay on the z=0 plane — so
; the tick comparison is only valid with the camera on-axis. This override pins
; the eye at (0,0,-10) looking down +Z; press R (camera-reset) to release it
; back to the mouse orbit.
(set-camera-position (vector 0 0 10))

(define FOV 50.0)
(define CAM-DIST 10.0)
(define DEG (/ 3.141592653589793 180.0))
(define WHITE (vector 1 1 1))
(define GRID  (vector 0.2 0.9 0.4))
(define ALARM (vector 1 0.3 0.1))
(define CYAN  (vector 0.1 1 1))
(define YELL  (vector 1 0.9 0.1))
(define DIM   (vector 0.32 0.32 0.4))   ; faint guide lines
(define GRID-Y   -1.0)   ; ground plane height
(define GRID-FAR -5.0)   ; farthest grid z (camera looks -Z from +CAM-DIST)

;; wire-look for a freshly built prim (gotcha #6: hint-solid defaults ON)
(define (wire-look col)
  (hint-solid #f) (hint-wire) (hint-unlit) (backfacecull #f) (wire-colour col))

(define (wire-cube x y z s col)
  (with-state
    (translate (vector x y z)) (scale (vector s s s))
    (wire-look col) (build-cube)))

;; ground grid: lines every 1 unit on y=-1, spanning [-5,5]^2
(define (grid-line ax ay az bx by bz)
  (line-col ax ay az bx by bz GRID 0.008))

;; coloured / variable-width 2-pt line. Force SOLID + no-wire: an earlier
;; wire-cube leaks HINT_WIRE + its red WireColour into the build context, and a
;; ribbon's solid pass uses State.Colour only when HINT_WIRE is OFF (else it draws
;; a wire in State.WireColour). So pin the render mode, then set (colour).
(define (line-col ax ay az bx by bz col w)
  (let ((rb (build-ribbon 2)))
    (with-primitive rb
      (identity) (hint-solid #t) (hint-wire #f) (hint-unlit) (colour col)
      (pdata-index-map! (lambda (i v) (if (= i 0) (vector ax ay az) (vector bx by bz))) "p")
      (pdata-index-map! (lambda (i ww) w) "w"))))

(define (ground-grid)
  (let loop ((i -5))
    (when (<= i 5)
      (grid-line -5 -1 i 5 -1 i)        ; x-direction lines
      (grid-line i -1 -5 i -1 5)        ; z-direction lines
      (loop (+ i 1)))))

;; outline circle on z=0 plane (closed ribbon, radius r, centre cx,cy)
(define (circle cx cy r col w)
  (let* ((n 48) (rb (build-ribbon (+ n 1))))
    (with-primitive rb
      (identity) (hint-solid #t) (hint-wire #f) (hint-unlit) (colour col)
      (pdata-index-map!
        (lambda (i v)
          (let ((a (* 2.0 3.141592653589793 (/ i n))))
            (vector (+ cx (* r (cos a))) (+ cy (* r (sin a))) 0)))
        "p")
      (pdata-index-map! (lambda (i ww) w) "w"))))

;; left-anchored 2D-plane text (z=0). Screen-fixed only while the camera is
;; on-axis; not a billboard (see header note on the concat stub).
(define CW 0.44)
(define (ltext str x y h col)
  (let ((tp (build-text str)) (sc (/ h 0.9)))
    (with-primitive tp
      (identity) (hint-unlit)
      (translate (vector x (- y (* 0.5 h)) 0))
      (scale (vector sc sc sc)) (colour col))))

(every-frame
  (when (= (key-poll) 114) (camera-reset))   ; R = reset orbit
  (set-fov FOV)
  (ortho #f)
  (let* ((sz (get-screen-size))
         (asp (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 0.5625))
         (hh  (* CAM-DIST (tan (* 0.5 FOV DEG))))
         (hw  (* hh asp)))
    ; --- 3D: ground grid + depth cubes ---
    (ground-grid)
    (wire-cube 0 0 0 2 WHITE)                 ; hero: 2 units, centre origin
    ; three depth references spread LATERALLY so they never pile up (cubes on a
    ; single ground line overlap when their size exceeds their spacing). Each sits
    ; in its own region and its screen size reads its depth: right=near/big,
    ; left=mid, centre=far/small at the horizon. All fully in-frame.
    (wire-cube  1.5 -0.5  2 1 ALARM)          ; right  (dist 8,  big)
    (wire-cube -1.7 -0.5 -2 1 ALARM)          ; left   (dist 12, medium)
    (wire-cube  0.0 -0.5 -5 1 ALARM)          ; centre (dist 15, small, at horizon)
    ; --- 2D overlay refs on the z=0 plane ---
    ; rule-of-thirds guides (faint): verticals at x=+/-hw/3, horizontals at y=+/-hh/3
    (let ((tx (/ hw 3.0)) (ty (/ hh 3.0)) (ex (* 0.95 hw)) (ey (* 0.95 hh)))
      (line-col    tx  (- ey) 0    tx  ey 0 DIM 0.004)
      (line-col (- tx) (- ey) 0 (- tx) ey 0 DIM 0.004)
      (line-col (- ex)    ty  0 ex    ty  0 DIM 0.004)
      (line-col (- ex) (- ty) 0 ex (- ty) 0 DIM 0.004))
    ; centre cross
    (grid-line (* -0.05 hw) 0 0 (* 0.05 hw) 0 0)
    (grid-line 0 (* -0.03 hh) 0 0 (* 0.03 hh) 0)
    ; hero-cube expected extent. The visible silhouette is the cube's NEAR face
    ; (z=+1 toward the camera at +10, i.e. distance 9), which projects LARGER
    ; than the z=0 cross-section: apparent y at z=0 scale = 1 * 10/9. Ticks sit
    ; at that projected height — cube TOP/BOTTOM edges should touch them.
    (let ((py (/ 10.0 9.0))                     ; near-face TOP/BOTTOM projected y
          (px (/ 10.0 9.0))                     ; near-face LEFT/RIGHT projected x
          (on (even? (inexact->exact (floor (* (time) 3))))))  ; ~1.5 Hz blink
      (when on
        ; vertical extent: horizontal ticks pushed right of the cube, cyan, thick
        (line-col (* 0.45 hw) py 0 (* 0.62 hw) py 0 CYAN 0.04)
        (line-col (* 0.45 hw) (- py) 0 (* 0.62 hw) (- py) 0 CYAN 0.04)
        ; horizontal extent: vertical ticks above the cube at x=+/-px; they should
        ; screen-align with the cube's near-face LEFT/RIGHT vertical edges.
        (line-col px 1.25 0 px 1.75 0 CYAN 0.04)
        (line-col (- px) 1.25 0 (- px) 1.75 0 CYAN 0.04)))
    ; horizon rule: where the ground plane's FAR edge (y=-1, z=-5) projects.
    ; perspective screen-y = gy * C / (C - z) = -1 * 10/15 = -2/3. If this yellow
    ; rule sits on the grid's farthest visible line, ground & overlay agree.
    (let ((yh (* GRID-Y (/ CAM-DIST (- CAM-DIST GRID-FAR)))))
      (line-col (* -0.9 hw) yh 0 (* 0.9 hw) yh 0 YELL 0.012))
    ; circles at the 4 frame corners, inset so the whole ring is on-screen
    (let* ((r (* 0.08 hh)) (mx (- (* 0.9 hw) r)) (my (- (* 0.9 hh) r)))
      (circle (- mx) my r ALARM 0.02)     ; top-left
      (circle mx my r ALARM 0.02)         ; top-right
      (circle (- mx) (- my) r ALARM 0.02) ; bottom-left
      (circle mx (- my) r ALARM 0.02))    ; bottom-right
    ; frame captions (2D margins)
    (ltext "CALIB 3D / FOV 50 / DIST 10" (* -0.82 hw) (* 0.90 hh) (* 0.032 hh) WHITE)
    (ltext "R=RESET  DRAG=ORBIT  WHEEL=DOLLY" (* -0.82 hw) (* -0.90 hh) (* 0.032 hh) WHITE)))
