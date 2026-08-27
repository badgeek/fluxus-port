; calib.scm — a calibration frame for positioning drawings on a vertical
; (Instagram 9:16) canvas. Resizes the window from code, then draws screen-space
; guides (centre cross, rule-of-thirds, title-safe margin) plus a centred label,
; so a screenshot can be checked against the intended layout.
;
; Coordinate note: with the default camera (0,0,-10) and FOV below, the visible
; region at the text plane z=0 is x in [-halfw, halfw], y in [-halfh, halfh],
; where halfh = CAM-DIST*tan(FOV/2) and halfw = halfh*aspect. Draw to those.

(set-window-size 540 960)                 ; 9:16 vertical frame (edit to taste)
(background (vector 0.05 0.05 0.08))

(define FOV 50.0)
(define CAM-DIST 10.0)
(define DEG (/ 3.141592653589793 180.0))
(define CW 0.44)

(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))

;; a straight line segment (ax,ay)-(bx,by) at z=0, given colour + thickness
(define (line ax ay bx by col th)
  (let ((rb (build-ribbon 2)))
    (with-primitive rb
      (identity) (hint-unlit)
      (colour col)
      (pdata-index-map! (lambda (i v) (if (= i 0) (vector ax ay 0) (vector bx by 0))) "p")
      (pdata-index-map! (lambda (i w) (vector th th th)) "w"))))

;; a centred line of text at (cx,cy), height ~h world units
(define (label str cx cy h col)
  (let* ((tp (build-text str))
         (sc (/ h 0.9))                    ; glyph height is 0.9 local units
         (w  (* CW (string-length str) sc)))
    (with-primitive tp
      (identity) (hint-unlit)
      (translate (vector (- cx (* 0.5 w)) (- cy (* 0.5 h)) 0))
      (scale (vector sc sc sc))
      (colour col))))

(every-frame
  (set-fov FOV)
  (ortho #f)
  (let* ((sz    (get-screen-size))
         (asp   (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 0.5625))
         (hh    (* CAM-DIST (tan (* 0.5 FOV DEG))))     ; half-height
         (hw    (* hh asp))                             ; half-width
         (dim   (vector 0.25 0.30 0.40))
         (mid   (vector 0.45 0.55 0.75))
         (safe  (vector 0.6 0.35 0.3)))
    ; frame edges
    (line (- hw) (- hh) hw (- hh) dim 0.02)
    (line (- hw)    hh  hw    hh  dim 0.02)
    (line (- hw) (- hh) (- hw) hh dim 0.02)
    (line    hw  (- hh)    hw  hh dim 0.02)
    ; rule of thirds
    (line (/ hw -3.0) (- hh) (/ hw -3.0) hh dim 0.015)
    (line (/ hw  3.0) (- hh) (/ hw  3.0) hh dim 0.015)
    (line (- hw) (/ hh -3.0) hw (/ hh -3.0) dim 0.015)
    (line (- hw) (/ hh  3.0) hw (/ hh  3.0) dim 0.015)
    ; centre cross
    (line 0 (- hh) 0 hh mid 0.02)
    (line (- hw) 0 hw 0 mid 0.02)
    ; title-safe margin (90%)
    (let ((mx (* 0.9 hw)) (my (* 0.9 hh)))
      (line (- mx) (- my) mx (- my) safe 0.015)
      (line (- mx)    my  mx    my  safe 0.015)
      (line (- mx) (- my) (- mx) my safe 0.015)
      (line    mx  (- my)    mx  my safe 0.015))
    ; readout: aspect + visible extents
    (label "CALIBRATE 9:16" 0.0 (* 0.6 hh) (* 0.10 hh) (vector 1 1 1))
    (label "centre" 0.0 0.0 (* 0.06 hh) (vector 0.8 0.9 1.0))
    (label "title-safe" 0.0 (* -0.82 hh) (* 0.05 hh) safe)
    ; grab one frame after load (dedup makes this fire exactly once)
    (when (> (time) 5.0) (screenshot "/tmp/fluxus-cap.png"))))
