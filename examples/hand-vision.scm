; HAND-VISION — in-app Apple Vision hand tracking (GPU/ANE), NO OSC, NO Python.
; (hand-tracking #t) opens the camera + runs VNDetectHumanHandPoseRequest; scripts
; read landmarks via (hand-count), (hand h j) -> #(x y z), (hand-pinch h).
; 21 landmarks/hand, MediaPipe order. Vision is 2D so z=0 (flat skeleton).
; Toggle: key t = tracking on/off.  Needs the in-app HandHost (macOS build).

(clear)
(hand-tracking #t)                              ; open camera + start Vision (prompts once)
(define SC 9.0)

(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye))) (s (vnormalise (vcross f up))) (u (vcross s f)))
    (vector (vx s)(vx u)(- (vx f)) 0 (vy s)(vy u)(- (vy f)) 0
            (vz s)(vz u)(- (vz f)) 0 (- (vdot s eye))(- (vdot u eye))(vdot f eye) 1)))

;; landmark i of hand h -> world point (x,y in 0..1 image, y down; z depth)
(define (lmp h i)
  (let ((p (hand h i)))
    (vector (* (- (vx p) 0.5) SC)
            (* (- 0.5 (vy p)) SC)
            (* (- (vz p)) (* SC 2.2)))))

(define BONES
  '((0 1)(1 2)(2 3)(3 4) (0 5)(5 6)(6 7)(7 8) (5 9)(9 10)(10 11)(11 12)
    (9 13)(13 14)(14 15)(15 16) (13 17)(17 18)(18 19)(19 20) (0 17)))
(define TIPS '(4 8 12 16 20))

(define (lerp a b t) (vadd (vmul a (- 1.0 t)) (vmul b t)))
(define (node p size col)
  (let ((c (build-cube)))
    (with-primitive c (hint-solid)(hint-unlit)(colour col)(translate p)(scale (vector size size size)))
    c))

(define (draw-hand h col)
  (for-each (lambda (b)
    (let ((pa (lmp h (car b))) (pb (lmp h (cadr b))))
      (let loop ((k 1)) (when (< k 6) (node (lerp pa pb (/ k 6.0)) 0.10 col) (loop (+ k 1))))))
    BONES)
  (let loop ((i 0)) (when (< i 21)
    (node (lmp h i) (if (memv i TIPS) 0.34 0.22) (if (memv i TIPS) (vector 1 1 1) col))
    (loop (+ i 1)))))

(retained)
(every-frame
  (begin
    (clear)
    (when (= (key-poll) 116) (hand-tracking #f))   ; 't' -> stop tracking (release camera)
    (set-fov 40)
    (set-camera-transform (look-at (vector 0 0 11) (vector 0 0 0) (vector 0 1 0)))
    (let ((n (hand-count)))
      (if (> n 0)
          (let loop ((h 0)) (when (< h n)
            (let* ((tp (min 1.0 (* (hand-pinch h) 6.0)))     ; 0 pinched .. 1 open
                   (col (vector (- 1.0 tp) tp 0.35)))
              (draw-hand h col))
            (loop (+ h 1))))
          (let ((t (build-text "SHOW HAND TO CAMERA   (t = stop tracking)")))
            (with-primitive t (hint-unlit)(colour (vector 1 0.5 0.3))
              (translate (vector -6 0 0))(scale (vector 0.5 0.5 0.5))))))))
