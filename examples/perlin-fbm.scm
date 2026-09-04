;; perlin-fbm.scm — a teaching visualisation of fractal Brownian motion (fBm), a
;; port of Andreas Müller's NoiseWorkshop "PerlinLayeringFBM". A noise field is the
;; weighted sum of N Perlin OCTAVES: each octave doubles (lacunarity) in frequency
;; and drops (persistence) in amplitude. The big image is the summed result; the
;; stacked thumbnails on the right are the individual octave layers; the white curve
;; under the result is a horizontal cross-section of the field.
;;
;; Sliders (ImGui panel, top-right): octaves / frequency / lacunarity / persistence.
;; WASD pans the noise window. fBm is heavy, so it recomputes ONLY when something
;; changes (retained mode: the pixel textures persist between frames).

(retained)
(hide-editor)
(set-window-size 960 640)
(background (vector 0.06 0.07 0.09))

;; ---- camera: fixed front-on view -------------------------------------------
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

;; ---- config -----------------------------------------------------------------
(define RES 96) (define NP (* RES RES))
(define MAXO 8)
(define (clampf x lo hi) (max lo (min hi x)))

;; ---- pixel buffers (scheme-side, filled on recompute) -----------------------
(define resbuf (make-vector NP 0.0))
(define laybuf (build-vector MAXO (lambda (o) (make-vector NP 0.0))))
(define ampbuf (make-vector MAXO 0.0))

;; ---- prims: result image + octave thumbnails + cross-section ----------------
(define result (build-pixels RES RES))
(with-primitive result (hint-unlit) (translate (vector -1.7 0.35 0)) (scale (vector 4.6 4.6 1)))
(define thumbs (build-vector MAXO (lambda (o) (build-pixels RES RES))))
(define xsect (build-ribbon RES))
(with-primitive xsect (hint-unlit) (hint-wire #f) (hint-solid #t) (colour (vector 0.9 1.0 0.95)))

;; grayscale colour for a 0..1 value (RGBA)
(define (grey v) (vector v v v 1.0))

;; ---- fBm recompute (fills resbuf + laybuf + ampbuf) -------------------------
(define (recompute! noct freq lac pers panx pany)
  (do ((i 0 (+ i 1))) ((= i NP))
    (let ((x (modulo i RES)) (y (quotient i RES)))
      (let loop ((o 0) (amp 1.0) (tot 0.0) (fin 0.0)
                 (fx (* (+ panx x) freq)) (fy (* (+ pany y) freq)))
        (if (< o noct)
            (let* ((amp2 (* amp pers))
                   (ln   (noise fx fy)))          ; single Perlin octave, 0..1
              (vector-set! (vector-ref laybuf o) i ln)
              (when (= i 0) (vector-set! ampbuf o amp2))
              (loop (+ o 1) amp2 (+ tot amp2) (+ fin (* ln amp2)) (* fx lac) (* fy lac)))
            (vector-set! resbuf i (if (> tot 0.0) (/ fin tot) 0.0))))))
  ;; push result -> its texture
  (with-primitive result
    (pdata-index-map! (lambda (i c) (grey (vector-ref resbuf i))) "c") (pixels-upload))
  ;; push each active octave layer -> its thumbnail texture
  (do ((o 0 (+ o 1))) ((= o noct))
    (with-primitive (vector-ref thumbs o)
      (let ((buf (vector-ref laybuf o)))
        (pdata-index-map! (lambda (i c) (grey (vector-ref buf i))) "c"))
      (pixels-upload)))
  ;; cross-section: middle row of the result, as a ribbon spanning the image width
  (with-primitive xsect
    (let ((y0 (* RES (quotient RES 2))))
      (pdata-index-map!
        (lambda (i p)
          (let ((v (vector-ref resbuf (+ y0 (min (- RES 1) i)))))
            (vector (+ -1.7 (* (- (/ (exact->inexact i) (- RES 1)) 0.5) 4.6))   ; span result width
                    (+ -2.55 (* (- v 0.5) 0.9))                                  ; height from value
                    0.02)))
        "p")
      (pdata-index-map! (lambda (i w) (vector 0.012 0.012 0.012)) "w"))))

;; ---- persistent last-state so we only recompute on change -------------------
(define *last* (vector -1 0.0 0.0 0.0 0.0 0.0))   ; noct freq lac pers panx pany
(define *pan*  (vector 0.0 0.0))

;; ---- per-frame: read sliders, pan, recompute-on-change, lay out thumbnails --
(every-frame
  (set-camera-transform (look-at (vector 0.0 0.0 9.0) (vector 0.0 0.0 0.0) (vector 0.0 1.0 0.0)))
  ;; WASD pans the noise window (integer steps, like the original's arrow keys)
  (when (key-down? "a") (vector-set! *pan* 0 (- (vector-ref *pan* 0) 4)))
  (when (key-down? "d") (vector-set! *pan* 0 (+ (vector-ref *pan* 0) 4)))
  (when (key-down? "w") (vector-set! *pan* 1 (- (vector-ref *pan* 1) 4)))
  (when (key-down? "s") (vector-set! *pan* 1 (+ (vector-ref *pan* 1) 4)))
  (let* ((noct (inexact->exact (round (tweak "octaves"     5   1   8))))
         (freq (tweak "frequency"   0.022 0.002 0.08))
         (lac  (tweak "lacunarity"  2.0   1.0   4.0))
         (pers (tweak "persistence" 0.55  0.05  0.95))
         (panx (vector-ref *pan* 0)) (pany (vector-ref *pan* 1)))
    ;; recompute only when a parameter (or pan) actually changed
    (when (not (and (= noct (vector-ref *last* 0)) (= freq (vector-ref *last* 1))
                    (= lac (vector-ref *last* 2)) (= pers (vector-ref *last* 3))
                    (= panx (vector-ref *last* 4)) (= pany (vector-ref *last* 5))))
      (recompute! noct freq lac pers panx pany)
      (vector-set! *last* 0 noct) (vector-set! *last* 1 freq) (vector-set! *last* 2 lac)
      (vector-set! *last* 3 pers) (vector-set! *last* 4 panx) (vector-set! *last* 5 pany))
    ;; lay out octave thumbnails on the right: show the active ones, park the rest
    (let ((sz (/ 4.6 noct)))
      (do ((o 0 (+ o 1))) ((= o MAXO))
        (with-primitive (vector-ref thumbs o)
          (identity) (hint-unlit)
          (if (< o noct)
              (begin
                (translate (vector 3.05 (- 2.1 (* o (+ sz 0.12))) 0))
                (scale (vector sz sz 1)))
              (translate (vector 0 -9999 0))))))))   ; hide inactive
