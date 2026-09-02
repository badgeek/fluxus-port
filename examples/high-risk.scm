; high-risk.scm — a 9:16 hazard/warning poster. Monochrome + one alarm accent:
; diagonal hazard stripes, an outline warning triangle with an exclamation mark,
; a stacked HIGH/RISK title, right-aligned meta text, and a slow pulsing beacon.
; Lots of tight tangencies on purpose — a calibration stress test.

(set-window-size 540 960)
(background (vector 0 0 0))

(define FOV 50.0)
(define CAM-DIST 10.0)
(define DEG (/ 3.141592653589793 180.0))
(define TWO-PI 6.283185307179586)
(define CW 0.44)
(define WHITE (vector 1 1 1))
(define ALARM (vector 1 0.25 0.1))

;; ---- primitive marks (z=0 graphic plane) -----------------------------------
(define (line ax ay bx by th col)
  (let ((rb (build-ribbon 2)))
    (with-primitive rb
      (identity) (hint-unlit) (colour col)
      (pdata-index-map! (lambda (i v) (if (= i 0) (vector ax ay 0) (vector bx by 0))) "p")
      (pdata-index-map! (lambda (i w) th) "w"))))

(define (rect cx cy w h col)
  (with-state
    (translate (vector cx cy 0)) (scale (vector w h 0.01))
    (hint-unlit) (colour col) (build-cube)))

(define (circle cx cy r th col)
  (let ((rb (build-ribbon 65)))
    (with-primitive rb
      (identity) (hint-unlit) (colour col)
      (pdata-index-map!
        (lambda (i v) (let ((a (* TWO-PI (/ i 64.0))))
                        (vector (+ cx (* r (cos a))) (+ cy (* r (sin a))) 0))) "p")
      (pdata-index-map! (lambda (i w) th) "w"))))

(define (ltext str x y h col)
  (let ((tp (build-text str)) (sc (/ h 0.9)))
    (with-primitive tp
      (identity) (hint-unlit)
      (translate (vector x (- y (* 0.5 h)) 0))
      (scale (vector sc sc sc)) (colour col))))
(define (text-w str h) (* CW (string-length str) (/ h 0.9)))
(define (rtext str xr y h col) (ltext str (- xr (text-w str h)) y h col))

;; outline warning triangle (equilateral, apex up), centre (cx,cy), side s
(define (warn-tri cx cy s th col)
  (let* ((hgt (* s 0.866))            ; equilateral height
         (top (vector cx (+ cy (* 0.577 hgt)) 0))          ; apex
         (bl  (vector (- cx (* 0.5 s)) (- cy (* 0.289 hgt)) 0))
         (br  (vector (+ cx (* 0.5 s)) (- cy (* 0.289 hgt)) 0))
         (rb (build-ribbon 4)))
    (with-primitive rb
      (identity) (hint-unlit) (colour col)
      (pdata-index-map!
        (lambda (i v) (case i ((0) top) ((1) bl) ((2) br) (else top))) "p")
      (pdata-index-map! (lambda (i w) th) "w"))))

;; exclamation mark inside the triangle: stem block + dot block.
;; Interior spans base -0.25s .. apex +0.5s — keep the dot's BOTTOM
;; (centre - half height) clear of the base or it collides.
(define (bang cx cy s col)
  (rect cx (+ cy (* 0.11 s)) (* 0.070 s) (* 0.34 s) col)   ; stem: 0.28 .. -0.06
  (rect cx (- cy (* 0.16 s)) (* 0.070 s) (* 0.075 s) col)) ; dot bottom -0.20, margin 0.05 over base

;; diagonal hazard stripes clipped to a band: y in [y0,y1], x in [x0,x1].
;; each stripe is a short 2-pt ribbon at 45deg; spacing sp, thickness th.
(define (hazard-band x0 x1 y0 y1 sp th col)
  (let ((hgt (- y1 y0)))
    (let loop ((x (- x0 hgt)))
      (when (< x x1)
        ; stripe from (x,y0) rising 45deg; clip its top end at x1
        (let* ((bx x) (tx (+ x hgt))
               (cbx (if (< bx x0) x0 bx))          ; clip bottom-left
               (cby (+ y0 (- cbx bx)))
               (ctx (if (> tx x1) x1 tx))          ; clip top-right
               (cty (+ y0 (- ctx bx))))
          (when (< cbx ctx)
            (line cbx (if (< bx x0) cby y0) ctx (if (> tx x1) cty y1) th col)))
        (loop (+ x sp))))))

;; ---- layout ----------------------------------------------------------------
(every-frame
  (when (= (key-poll) 114) (camera-reset))   ; R = reset the orbit camera
  (set-fov FOV)
  (ortho #f)
  (let* ((sz (get-screen-size))
         (asp (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 0.5625))
         (hh  (* CAM-DIST (tan (* 0.5 FOV DEG))))
         (hw  (* hh asp))
         (ml  (* -0.82 hw)) (mr (* 0.82 hw))
         (pulse (+ 0.55 (* 0.45 (sin (* 2.0 (time)))))))
    ; --- top hazard stripe band, full margin width ---
    (hazard-band ml mr (* 0.86 hh) (* 0.94 hh) (* 0.10 hh) 0.03 ALARM)
    ; --- heavy rule right under the band ---
    (line ml (* 0.82 hh) mr (* 0.82 hh) 0.03 WHITE)
    ; --- title, stacked, left-set ---
    (ltext "HIGH" ml (* 0.62 hh) (* 0.16 hh) WHITE)
    (ltext "RISK" ml (* 0.44 hh) (* 0.16 hh) ALARM)
    ; --- meta, right-aligned to margin (short strings — must not drift) ---
    (rtext "SEC/7" mr (* 0.66 hh) (* 0.034 hh) WHITE)
    (rtext "DO NOT CROSS" mr (* 0.60 hh) (* 0.034 hh) WHITE)
    ; --- warning triangle centre-stage, bang inside, pulsing ---
    (let ((tc (* 0.05 hh)) (ts (* 0.52 hh)))
      (warn-tri 0.0 tc ts 0.030 WHITE)
      (bang 0.0 tc ts (vector 1 (* 0.25 pulse) (* 0.1 pulse))))
    ; --- beacon circle tangent to the lower rule from above ---
    ; tangency by construction: centre = rule y + r + half the two stroke widths
    ; (circle path and rule are both CENTRELINE geometry, so without the stroke
    ; compensation the circle visually sinks by ~half a stroke).
    ; (verdict round 2: ribbon "w" is the HALF-width — strokes extend w to each
    ; side of the path — so the gap is w_circle + w_rule, not half of each)
    (let ((r (* 0.085 hh)) (ry (* -0.42 hh)))
      (circle (* 0.42 hw) (+ ry r 0.022 0.02) r 0.022 ALARM))
    ; --- lower rule the beacon rests on ---
    (line ml (* -0.42 hh) mr (* -0.42 hh) 0.02 WHITE)
    ; --- bottom hazard band mirror + footer caption ---
    (hazard-band ml mr (* -0.94 hh) (* -0.86 hh) (* 0.10 hh) 0.03 WHITE)
    (ltext "KEEP CLEAR / ZONE 04" ml (* -0.80 hh) (* 0.032 hh) WHITE)))
