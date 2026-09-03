; HAND-REACTIVE — MediaPipe hand landmarks over OSC drive a live 3D skeleton.
; Drive it with either:
;   tools/mediapipe_tracker.py   (real camera; pip install mediapipe opencv-python)
;   test/hand_sim.py             (synthetic hand, no camera — for verifying)
; OSC schema: /hands <n>, /hand/0 = 63 floats (21 landmarks x,y,z), /pinch/0 <dist>.

(clear)
(osc-source 8000)
(define SC 9.0)                                  ; landmark -> world scale

(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye))) (s (vnormalise (vcross f up))) (u (vcross s f)))
    (vector (vx s)(vx u)(- (vx f)) 0 (vy s)(vy u)(- (vy f)) 0
            (vz s)(vz u)(- (vz f)) 0 (- (vdot s eye))(- (vdot u eye))(vdot f eye) 1)))

;; landmark i -> world point (image x,y in 0..1, y down; z relative depth)
(define (lm k) (osc "/hand/0" k))
(define (lmp i)
  (vector (* (- (lm (* i 3)) 0.5) SC)
          (* (- 0.5 (lm (+ (* i 3) 1))) SC)
          (* (- (lm (+ (* i 3) 2))) (* SC 2.2))))

;; MediaPipe 21-landmark bone topology
(define BONES
  '((0 1)(1 2)(2 3)(3 4)          ; thumb
    (0 5)(5 6)(6 7)(7 8)          ; index
    (5 9)(9 10)(10 11)(11 12)     ; middle
    (9 13)(13 14)(14 15)(15 16)   ; ring
    (13 17)(17 18)(18 19)(19 20)  ; pinky
    (0 17)))                      ; palm base
(define TIPS '(4 8 12 16 20))

(define (lerp a b t) (vadd (vmul a (- 1.0 t)) (vmul b t)))
(define (node p size col)
  (let ((c (build-cube)))
    (with-primitive c (hint-solid)(hint-unlit)(colour col)(translate p)(scale (vector size size size)))
    c))

(define (draw-hand col)
  ;; dotted bones (5 cubes interpolated per connection — no 3D rotation needed)
  (for-each (lambda (b)
    (let ((pa (lmp (car b))) (pb (lmp (cadr b))))
      (let loop ((k 1)) (when (< k 6)
        (node (lerp pa pb (/ k 6.0)) 0.10 col) (loop (+ k 1))))))
    BONES)
  ;; landmark nodes (tips brighter/bigger)
  (let loop ((i 0)) (when (< i 21)
    (let ((tip (memv i TIPS)))
      (node (lmp i) (if tip 0.34 0.22) (if tip (vector 1 1 1) col)))
    (loop (+ i 1)))))

(retained)
(every-frame
  (begin
    (clear)
    (set-fov 40)
    (set-camera-transform (look-at (vector 0 0 11) (vector 0 0 0) (vector 0 1 0)))
    (if (>= (osc "/hands" 0) 1.0)
        (let* ((pinch (osc "/pinch/0" 0))          ; small = pinched
               (tp (min 1.0 (* pinch 6.0)))         ; 0 pinched .. 1 open
               (col (vector (- 1.0 tp) tp 0.35)))   ; red pinched -> green open
          (draw-hand col))
        (let ((t (build-text "NO HAND  (run test/hand_sim.py or tools/mediapipe_tracker.py)")))
          (with-primitive t (hint-unlit)(colour (vector 1 0.4 0.4))
            (translate (vector -6 0 0))(rotate (vector 0 0 0))(scale (vector 0.5 0.5 0.5)))))))
