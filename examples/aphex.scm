; "aphex" — a sound-deformed blob with a camera that hard-cuts to random
; positions (always aimed at the object), cut-rate + shake driven by audio.
;
; This port is immediate-mode (whole buffer re-evals each frame, scene Clear()ed),
; so ALL accumulation is a function of (time)/(frame), never (delta), and the
; per-step "randomness" is a deterministic hash of the step index — the same
; within a cut, so the camera holds still between cuts (hard cuts, not drift).

(clear)
(start-audio "system:capture_1" 512 44100)

; ---- little maths helpers ---------------------------------------------------
(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))
(define (fract x) (- x (floor x)))
; deterministic pseudo-random in [-1,1] from an integer seed
(define (h i) (- (* 2 (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453))) 1))

; gluLookAt-style view matrix (column-major, fluxus order), eye -> target
(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))

; ---- the object -------------------------------------------------------------
(define obj (build-nurbs-sphere 12 30))

(with-primitive obj
  (scale (vector 2 2 2))
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))

(define (deform)
  (with-primitive obj
    (hint-wire)
    (line-width 1.5)
    (backfacecull 1)
    (opacity (+ 0.15 (* (gh 6) 20)))
    (wire-opacity (+ 0.4 (* (gh 3) 60)))
    (wire-colour (vector 1 0.5 0.1))
    (colour (vector (+ 0.4 (gh 2)) 0.08 (+ 0.3 (gh 9))))
    (rotate (vector 0 (* (time) 30) (* (time) 8)))     ; time-based spin (accumulates)
    (pdata-index-map!
      (lambda (index val)
        (let* ((n   (pdata-ref "n" index))
               (o   (pdata-ref "ori" index))
               ; audio band pushes the vertex out along its normal, plus a slow
               ; travelling wobble so it lives even in silence
               (amt (+ (* (gh index) 8)
                       (* 1.2 (sin (+ (* 2.5 (time)) (* 0.6 index)))))))
          (vadd o (v* n amt))))
      "p")))

; ---- the jump-cut camera ----------------------------------------------------
(define (jump-camera)
  (let* ((bass (gh 0))                                   ; kick band
         (rate (+ 1.2 (* 5 bass)))                       ; louder = faster cuts
         (step (floor (* (time) rate)))                  ; discrete cut index
         (az   (* (h step) 3.14159))                     ; azimuth
         (el   (* (h (+ step 7)) 0.9))                   ; elevation (avoid poles)
         (dist (+ 7 (* 5 (h (+ step 3)))))               ; 2..12
         (ce   (cos el)) (se (sin el))
         (eye  (vector (* dist ce (sin az))
                       (* dist se)
                       (* dist ce (cos az))))
         ; fast per-frame audio shake (frame-based so it jitters within a hold)
         (shk  (* 2.5 (+ (gain) (gh 1))))
         (jit  (vector (* shk (h (frame)))
                       (* shk (h (+ (frame) 11)))
                       (* shk (h (+ (frame) 23))))))
    (set-camera-transform (look-at (vadd eye jit) (vector 0 0 0) (vector 0 1 0)))))

(define (renderchain)
  (jump-camera)
  (deform))

(every-frame (renderchain))
