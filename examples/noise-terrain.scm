;; noise-terrain.scm — an animated fBm heightfield, a reconstruction of
;; NoiseWorkshop's "NoiseTerrain" scratchpad step (built from Empty3D: a mesh whose
;; vertices are displaced by layered Perlin noise; the noise's extra dimension is
;; time so the landscape slowly undulates). The finished scratchpad source isn't in
;; the vendored repo, so this is a faithful rebuild from the workshop recipe, reusing
;; the fBm from PerlinLayeringFBM.
;;
;; Retained: build the mesh ONCE, re-displace + re-colour its pdata each frame.
;; Height-coloured (deep water -> shallows -> grass -> rock -> snow). Orbit camera.
;; Sliders: octaves / frequency / magnitude / time speed.

(retained)
(hide-editor)
(set-window-size 900 640)
(background (vector 0.03 0.05 0.09))

;; ---- orbit camera -----------------------------------------------------------
(define (v- a b) (vector (- (vx a) (vx b)) (- (vy a) (vy b)) (- (vz a) (vz b))))
(define (vdot a b) (+ (* (vx a) (vx b)) (* (vy a) (vy b)) (* (vz a) (vz b))))
(define (vcross a b)
  (vector (- (* (vy a) (vz b)) (* (vz a) (vy b)))
          (- (* (vz a) (vx b)) (* (vx a) (vz b)))
          (- (* (vx a) (vy b)) (* (vy a) (vx b)))))
(define (vnorm a) (let ((l (sqrt (vdot a a)))) (if (> l 1e-6) (vector (/ (vx a) l) (/ (vy a) l) (/ (vz a) l)) a)))
(define (look-at eye tgt up)
  (let* ((f (vnorm (v- tgt eye))) (s (vnorm (vcross f up))) (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0.0 (vy s) (vy u) (- (vy f)) 0.0
            (vz s) (vz u) (- (vz f)) 0.0 (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1.0)))

;; ---- layered fBm (3D: x, z, time) ------------------------------------------
(define (fbm3 x z t noct lac pers)
  (let loop ((o 0) (amp 1.0) (tot 0.0) (sum 0.0) (fx x) (fz z))
    (if (>= o noct)
        (if (> tot 0.0) (/ sum tot) 0.0)
        (let ((amp2 (* amp pers)))
          (loop (+ o 1) amp2 (+ tot amp2)
                (+ sum (* amp2 (noise fx fz t)))   ; time as the 3rd noise dim -> undulation
                (* fx lac) (* fz lac))))))

;; ---- height -> colour (water / sand / grass / rock / snow) ------------------
(define (v3 r g b) (vector r g b))
(define (lerp3 a b u)
  (vector (+ (vx a) (* (- (vx b) (vx a)) u))
          (+ (vy a) (* (- (vy b) (vy a)) u))
          (+ (vz a) (* (- (vz b) (vz a)) u))))
(define (terrain-col t)
  (cond ((< t 0.30) (lerp3 (v3 0.04 0.10 0.32) (v3 0.10 0.38 0.58) (/ t 0.30)))
        ((< t 0.40) (lerp3 (v3 0.80 0.74 0.45) (v3 0.50 0.68 0.28) (/ (- t 0.30) 0.10)))
        ((< t 0.68) (lerp3 (v3 0.28 0.55 0.20) (v3 0.42 0.36 0.26) (/ (- t 0.40) 0.28)))
        (else       (lerp3 (v3 0.42 0.36 0.26) (v3 0.95 0.97 1.00) (/ (- t 0.68) 0.32)))))

;; ---- mesh (built once) ------------------------------------------------------
(define R 60)
(define NN (* R R 4))
(define usx (/ 1.0 R)) (define vsy (/ 1.0 R))
(define (vert-u i)
  (let ((gx (quotient (quotient i 4) R)) (c (modulo i 4)))
    (+ (/ (exact->inexact gx) R) (if (or (= c 1) (= c 2)) usx 0.0))))
(define (vert-v i)
  (let ((gy (modulo (quotient i 4) R)) (c (modulo i 4)))
    (+ (/ (exact->inexact gy) R) (if (or (= c 2) (= c 3)) vsy 0.0))))
(define hbuf (make-vector NN 0.0))   ; per-vertex normalised height, filled in "p" pass

(define WORLD 14.0) (define HEIGHT 3.2)
(define terr (build-seg-plane R R))
(with-primitive terr
  (hint-unlit) (hint-vertcols) (backfacecull #f)
  (rotate (vector -90 0 0)))         ; lay flat: local z -> world Y

;; ---- per-frame: displace + colour + orbit -----------------------------------
(every-frame
  (let* ((T    (time))
         (noct (inexact->exact (round (tweak "octaves"    5   1   8))))
         (freq (tweak "frequency"  2.4  0.3  6.0))
         (mag  (tweak "magnitude"  1.0  0.0  2.0))
         (tsp  (tweak "time speed" 0.10 0.0  0.6)))
    (let* ((ang (* T 0.10))
           (eye (vector (* WORLD 1.1 (sin ang)) (* HEIGHT 2.3) (* WORLD 1.1 (cos ang))))
           (tgt (vector 0.0 (* HEIGHT 0.2) 0.0)))
      (set-camera-transform (look-at eye tgt (vector 0.0 1.0 0.0))))
    (with-primitive terr
      ;; "p": world height from fBm (also cache the normalised height in hbuf)
      (do ((i 0 (+ i 1))) ((= i NN))
        (let* ((u (vert-u i)) (v (vert-v i))
               (h  (fbm3 (* u freq) (* v freq) (* T tsp) noct 2.0 0.5))
               (hs (max 0.0 (min 1.0 (+ 0.5 (* (- h 0.5) 2.1))))))   ; stretch: lows->water, highs->snow
          (vector-set! hbuf i hs)
          (pdata-set! "p" i (vector (* (- u 0.5) WORLD)
                                    (* (- v 0.5) WORLD)
                                    (* (- (max 0.30 hs) 0.30) HEIGHT mag)))))   ; flat water below 0.30
      ;; "c": height palette (flatten anything below water level to the water colour)
      (do ((i 0 (+ i 1))) ((= i NN))
        (pdata-set! "c" i (terrain-col (max 0.0 (min 1.0 (vector-ref hbuf i)))))))))
