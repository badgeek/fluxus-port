; bauhaus.scm — a black & white, Bauhaus-style slide layout (9:16) with a twist of
; generative terrain. Strictly monochrome: white marks on black. Grid-aligned,
; asymmetric, geometric — a heavy rule, a big left-set title, an outline circle,
; one solid block, a diagonal, and a band of generative ridgelines.

(set-window-size 540 960)
(background (vector 0 0 0))

(define FOV 50.0)
(define CAM-DIST 10.0)
(define DEG (/ 3.141592653589793 180.0))
(define TWO-PI 6.283185307179586)
(define CW 0.44)
(define WHITE (vector 1 1 1))

(define (frac q) (- q (floor q)))

;; ---- primitive marks (all white, z=0 graphic plane) ------------------------
(define (line ax ay bx by th)
  (let ((rb (build-ribbon 2)))
    (with-primitive rb
      (identity) (hint-unlit) (colour WHITE)
      (pdata-index-map! (lambda (i v) (if (= i 0) (vector ax ay 0) (vector bx by 0))) "p")
      (pdata-index-map! (lambda (i w) (vector th th th)) "w"))))

(define (rect cx cy w h)                       ; solid white block
  (with-state
    (translate (vector cx cy 0)) (scale (vector w h 0.01))
    (hint-unlit) (colour WHITE) (build-cube)))

(define (circle cx cy r th)                    ; outline circle
  (let ((rb (build-ribbon 65)))
    (with-primitive rb
      (identity) (hint-unlit) (colour WHITE)
      (pdata-index-map!
        (lambda (i v) (let ((a (* TWO-PI (/ i 64.0))))
                        (vector (+ cx (* r (cos a))) (+ cy (* r (sin a))) 0))) "p")
      (pdata-index-map! (lambda (i w) (vector th th th)) "w"))))

; left-anchored text: x = left edge, y = vertical centre, h = cap height (world)
(define (ltext str x y h)
  (let ((tp (build-text str)) (sc (/ h 0.9)))
    (with-primitive tp
      (identity) (hint-unlit)
      (translate (vector x (- y (* 0.5 h)) 0))
      (scale (vector sc sc sc)) (colour WHITE))))
(define (text-w str h) (* CW (string-length str) (/ h 0.9)))

;; ---- the generative terrain twist: a band of white ridgelines --------------
;; stacked horizontal scan-lines (Rutt-Etra), a deterministic heightfield so it
;; is generative but static — a graphic element, not an animation.
(define (ridge-h fx row)
  (let ((wx (* 8.0 fx)))
    (* (let ((w (- 1.0 (* 3.0 fx fx)))) (if (> w 0.0) (* w w) 0.0))   ; centre window
       (+ 0.15
          (* 0.6 (abs (sin (+ (* 0.9 wx) (* 0.5 row)))))
          (* 0.35 (abs (sin (+ (* 2.1 wx) row 1.3))))))))

(define (ridges cx cy w h rows nx)
  (let loop ((r (- rows 1)))                    ; far (top) -> near (bottom)
    (when (>= r 0)
      (let* ((ry (+ (- cy (* 0.5 h)) (* h (/ (exact->inexact r) (- rows 1)))))
             (amp (* 0.16 h))
             (rb (build-ribbon (+ nx 1))))
        (with-primitive rb
          (identity) (hint-unlit) (colour WHITE)
          (pdata-index-map!
            (lambda (i v)
              (let ((fx (- (/ i (exact->inexact nx)) 0.5)))
                (vector (+ cx (* w fx)) (+ ry (* amp (ridge-h fx r))) 0)))
            "p")
          (pdata-index-map! (lambda (i wd) (vector 0.01 0.01 0.01)) "w")))
      (loop (- r 1)))))

;; ---- layout ----------------------------------------------------------------
(every-frame
  (set-fov FOV)
  (ortho #f)
  (let* ((sz (get-screen-size))
         (asp (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 0.5625))
         (hh  (* CAM-DIST (tan (* 0.5 FOV DEG))))
         (hw  (* hh asp))
         (ml  (* -0.82 hw))                     ; left grid margin
         (mr  (*  0.82 hw)))
    ; --- top band: index + heavy rule (clean, nothing crossing it) ---
    (ltext "01" ml (* 0.88 hh) (* 0.045 hh))
    (ltext "FLUXUS LIVES / 2005" (* 0.30 hw) (* 0.88 hh) (* 0.032 hh))
    (line ml (* 0.82 hh) mr (* 0.82 hh) 0.03)
    ; --- title, left-set, stacked bold ---
    (ltext "FLUXUS" ml (* 0.60 hh) (* 0.155 hh))
    (ltext "LIVES"  ml (* 0.43 hh) (* 0.155 hh))
    ; --- circle: counterweight in the open right space, below the title ---
    (circle (* 0.42 hw) (* 0.14 hh) (* 0.20 hh) 0.022)
    ; --- solid square, grid-aligned under the title ---
    (rect (+ ml (* 0.055 hh)) (* 0.27 hh) (* 0.11 hh) (* 0.11 hh))
    ; --- horizon rule (slight diagonal) above the terrain ---
    (line ml (* -0.03 hh) mr (* -0.07 hh) 0.02)
    ; --- the generative terrain twist: ridgeline band ---
    (ridges 0.0 (* -0.44 hh) (* 1.55 hw) (* 0.52 hh) 32 110)
    ; --- footer rule + caption ---
    (line ml (* -0.82 hh) mr (* -0.82 hh) 0.02)
    (ltext "LIVE CODED / SCHEME" ml (* -0.88 hh) (* 0.032 hh))
    ; one-shot calibration grab after load
    (when (> (time) 5.0) (screenshot "/tmp/bauhaus-cap.png"))))
