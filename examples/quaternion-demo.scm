;; quaternion-demo.scm — a clean, readable quaternion demonstration.
;;
;; TWO objects, side by side over a wireframe grid:
;;   LEFT  — SLERP: an XYZ axis-triad smoothly tumbles between random keyframe
;;           orientations via (qslerp k0 k1 t). Shortest-arc, no gimbal lock.
;;   RIGHT — ROTATE: an identical gizmo you rotate LIVE by dragging the slider
;;           panel (top-right): axis x/y/z + angle -> (qaxisangle axis angle).
;;
;; Retained mode: everything is built ONCE; the thunk only re-orients the two
;; gizmos. Upright text billboards face the camera as a guide.

(clear)
(retained)

;; front, slightly-elevated camera (yaw 0 = symmetric; negative pitch looks DOWN;
;; yaw-then-pitch keeps the horizon level).
(set-camera-transform
  (mmul (mtranslate (vector 0 -0.8 -12.0))
        (mrotate (vector -18 0 0))))
(show-tweaks)   ; the slider panel IS the rotate interface

;; ---- helpers ---------------------------------------------------------------
(define (frac x) (- x (floor x)))
(define (smoothstep t) (* t t (- 3.0 (* 2.0 t))))
(define (hash n) (- (* 2.0 (frac (* (sin (* (+ n 1) 12.9898)) 43758.5453))) 1.0))

(define (set-node-pose id q pos)          ; rotation quat + world pos -> local xform
  (let ((m (qtomatrix q)))
    (vector-set! m 12 (vx pos)) (vector-set! m 13 (vy pos)) (vector-set! m 14 (vz pos))
    (with-primitive id (set-transform m))))

;; an XYZ axis-triad + a long +Z "nose" as a child of node `id` (orientation reads
;; unambiguously — you can see roll, pitch and yaw).
(define (build-gizmo id)
  (define (bar col dir) (with-state (parent id) (hint-unlit) (colour col)
                          (translate (vmul dir 0.55)) (scale (vmul dir 1.0)) (build-cube)))
  (bar (vector 1 0.3 0.3) (vector 1.1 0.14 0.14))   ; X red
  (bar (vector 0.3 1 0.3) (vector 0.14 1.1 0.14))   ; Y green
  (bar (vector 0.4 0.6 1) (vector 0.14 0.14 1.6))   ; Z blue nose (longer)
  (with-state (parent id) (hint-unlit) (colour (vector 1 1 1))
              (scale (vector 0.34 0.34 0.34)) (build-cube)))

;; upright text billboard, centred at world x=cx, facing the (yaw-0) camera.
(define CW 0.44)
(define (billboard str cx y h col)
  (let ((w (* CW (string-length str) h)))
    (with-state (hint-unlit) (backfacecull #f)
                (colour col)
                (translate (vector (- cx (* 0.5 w)) y 0))   ; left origin, so shift left to centre
                (scale (vector h h h))
                (build-text str))))

;; ---- wireframe grid ground (hand-laid thin bars; reliable) ------------------
(define GH 13.0) (define GS 1.3)
(define (int-range a b) (if (> a b) '() (cons a (int-range (+ a 1) b))))
(for-each
  (lambda (k)
    (let ((c (* k GS)))
      (with-state (hint-unlit) (colour (vector 0.14 0.34 0.46))
                  (translate (vector 0 0 c)) (scale (vector (* 2 GH) 0.02 0.04)) (build-cube))
      (with-state (hint-unlit) (colour (vector 0.14 0.34 0.46))
                  (translate (vector c 0 0)) (scale (vector 0.04 0.02 (* 2 GH))) (build-cube))))
  (int-range (inexact->exact (- (floor (/ GH GS)))) (inexact->exact (floor (/ GH GS)))))

;; ---- the two gizmos ---------------------------------------------------------
(define *slerp* (build-node)) (build-gizmo *slerp*)
(define *ctrl*  (build-node)) (build-gizmo *ctrl*)
(define LX -3.6) (define RX 3.6) (define GY 1.7)   ; left/right x, gizmo height

;; ---- slerp keyframes --------------------------------------------------------
(define NKEYS 6)
(define keys (build-vector NKEYS
  (lambda (i) (qaxisangle (vector (hash i) (hash (+ i 7)) (hash (+ i 13)))
                          (* (hash (+ i 3)) 180.0)))))

;; ---- text guide (built once) ------------------------------------------------
(billboard "QUATERNION"        -3.7 4.3 0.9 (vector 0.6 0.75 1.0))
(billboard "SLERP (auto)"       LX  0.2 0.5 (vector 0.5 1.0 0.6))
(billboard "ROTATE (sliders)"   RX  0.2 0.5 (vector 1.0 0.9 0.4))

;; ---- animate ----------------------------------------------------------------
(every-frame
  (let ((t (time)))
    ;; LEFT: slerp between keyframes ("slerp speed" slider)
    (let* ((phase (* t (tweak "slerp speed" 0.4 0.0 2.0)))
           (i  (modulo (inexact->exact (floor phase)) NKEYS))
           (n  (modulo (+ i 1) NKEYS))
           (q  (qslerp (vector-ref keys i) (vector-ref keys n) (smoothstep (frac phase)))))
      (set-node-pose *slerp* q (vector LX GY 0)))
    ;; RIGHT: live axis-angle from the sliders
    (let ((q (qaxisangle (vector (tweak "axis x" 0.3 -1.0 1.0)
                                 (tweak "axis y" 1.0 -1.0 1.0)
                                 (tweak "axis z" 0.0 -1.0 1.0))
                         (tweak "angle" 60.0 0.0 360.0))))
      (set-node-pose *ctrl* q (vector RX GY 0)))))
