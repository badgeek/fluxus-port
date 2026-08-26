; terrain — a subdivided plane as an audio heightfield (spectrum ridges) with a
; rolling wave, viewed by a slow elevated orbiting camera. Immediate-mode port:
; deform from ORIGINAL vertex pos (so shared quad corners stay welded), spin/orbit
; from (time).

(clear)
(start-audio "system:capture_1" 512 44100)

(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))
(define (fract x) (- x (floor x)))
(define (h i) (- (* 2 (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453))) 1))  ; [-1,1]
(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))

; 48x48 grid, flat in XY with +Z normals — rotate it to lie down (Z -> up).
(define g (build-seg-plane 48 48))

(with-primitive g
  (rotate (vector -90 0 0))          ; lay flat: local +Z normal becomes world +Y
  (scale (vector 30 30 30))
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))

(define (terrain)
  (with-primitive g
    (hint-solid #f)                   ; wire-only, cleaner terrain read
    (hint-wire)
    (line-width 1.4)
    (wire-opacity 1)
    (pdata-index-map!
      (lambda (index val)
        (let* ((o  (pdata-ref "ori" index))
               (n  (pdata-ref "n" index))
               (ox (vx o)) (oy (vy o))
               ; map x across the plane to a frequency band -> spectrum ridges
               (band (inexact->exact (floor (* (+ ox 0.5) 16))))
               (aud  (* (gh band) 1.1))
               ; travelling rolling hills (two octaves) so it reads even in silence
               (wav  (+ (* 0.16 (+ (sin (+ (* 6 ox) (* 1.4 (time))))
                                   (cos (+ (* 5 oy) (* 1.1 (time))))))
                        (* 0.06 (sin (+ (* 18 ox) (* 14 oy) (* 3 (time)))))))
               (h    (+ aud wav)))
          (vadd o (v* n h))))
      "p")
    ; colour the whole grid, pulsing with overall level
    (colour (vector (+ 0.2 (* 2 (gain))) 0.55 (+ 0.4 (gh 10))))
    (wire-colour (vector (+ 0.3 (gh 1)) 0.8 1))))

; hashed waypoint above the terrain for cut-step s (always above the plane)
(define (cam-waypoint s)
  (let* ((az   (* (h s) 3.14159))
         (el   (+ 0.28 (* 0.5 (abs (h (+ s 7))))))    ; 0.28..0.78 rad
         (base (+ 8 (* 6 (abs (h (+ s 3))))))         ; 8..14
         (ch   (cos el)) (sh (sin el)))
    (vector (* base ch (sin az)) (* base sh) (* base ch (cos az)))))

(define (vlerp a b s) (vadd a (v* (vsub b a) s)))

; damped jump camera with REAL inertia via (persist ...): a new hashed waypoint
; every ~2s (a hard target change), but the actual eye EASES toward it each frame
; and lags/springs — state carried across frames by the engine's persist store.
; bass pulls the target IN (zoom punch); per-frame shake on top.
(define (camera)
  (let* ((t      (time))
         (bass   (gh 0))
         (rate   0.5)                                 ; ~one new waypoint / 2s
         (s      (floor (* t rate)))
         (target (v* (cam-waypoint s) (- 1 (* 0.5 bass))))  ; where we want to be
         (cur    (persist "cam" target))              ; eye from the previous frame
         (nxt    (vlerp cur target 0.06))             ; ease toward target = damping
         (shk    (* 1.6 (+ (gain) (gh 1))))
         (jit    (vector (* shk (h (frame)))
                         (* shk 0.4 (h (+ (frame) 11)))
                         (* shk (h (+ (frame) 23))))))
    (persist! "cam" nxt)                              ; carry to next frame
    (set-camera-transform (look-at (vadd nxt jit) (vector 0 0 0) (vector 0 1 0)))))

(define (renderchain)
  (camera)
  (terrain))

(every-frame (renderchain))
