;; terminal-hyperspiral.scm — HYPER SPIRAL layer from hypervisual/main.go (layer 9)
;; ported to the build-terminal ANSI grid (jnbgo aesthetic, see terminal-jnbgo.scm).
;; Faithful conversion of renderHyperSpiral + the HD half-block renderer:
;;   spiral  = sin(r*density*25 - t*5 + theta*arms)
;;   spiral2 = sin(r*density*15 + t*3 - theta*(arms+2))
;;   val     = tanh((spiral + spiral2*0.5) * 2 * scale)
;; Each terminal cell samples TWO sub-rows and draws "▀" with truecolor fg (top)
;; + bg (bottom) — doubling vertical resolution, exactly like the Go hdMode.
;; Colour ramp = intensityColor(): dim-magenta -> magenta -> white.
;; Tweak panel = the layer's Speed/Density/Scale/Chaos params.
(retained)
(clear)
(hide-editor)
(set-window-size 960 600)

(define cols 64)
(define rows 26)
(define term (build-terminal cols rows))

;; centre + scale the grid into view (once) — same framing as terminal-jnbgo.scm.
(with-primitive term
  (scale (vector 0.36 0.36 0.36))
  (translate (vector (- (/ (* cols 0.5) 2)) (/ (* rows 0.9) 2) 0)))

;; NTSC/CRT finish (composite artefacts + scanlines), jnbgo settings.
(ntsc)
(ntsc-scanlines)
(ntsc-noise 5)
(ntsc-saturation 22)
(ntsc-brightness 8)
(ntsc-contrast 190)

(define (i x) (inexact->exact (floor x)))
(define (clamp01 x) (max 0.0 (min 1.0 x)))
(define (tanh~ x) (let ((e (exp (* 2.0 x)))) (/ (- e 1.0) (+ e 1.0))))

;; intensityColor from main.go: <=0.7 lerp(base/5, base), >0.7 lerp(base, white).
;; Layer 9 base colour: magenta (255,0,255).
(define (lerp a b s) (i (+ a (* (- b a) s))))
(define (intensity-rgb v)
  (let ((v (clamp01 v)))
    (cond ((< v 0.05) (list 0 0 0))
          ((<= v 0.7)
           (let ((s (/ v 0.7)))
             (list (lerp 51 255 s) 0 (lerp 51 255 s))))
          (else
           (let ((s (/ (- v 0.7) 0.3)))
             (list 255 (lerp 0 255 s) 255))))))

;; renderHyperSpiral(nx, ny, t, density, scale, chaos)
(define (hyper-spiral nx ny t density scale chaos)
  (let* ((r (sqrt (+ (* nx nx) (* ny ny))))
         (theta (atan ny nx))
         (arms (+ 3.0 (* chaos 5.0)))
         (s1 (sin (+ (- (* r density 25.0) (* t 5.0)) (* theta arms))))
         (s2 (sin (- (+ (* r density 15.0) (* t 3.0)) (* theta (+ arms 2.0))))))
    (tanh~ (* (+ s1 (* s2 0.5)) 2.0 scale))))

;; scaleFromParam: 0..1 param -> 2^((v-0.5)*5) exponential zoom
(define (scale-from-param v) (expt 2.0 (* (- v 0.5) 5.0)))

;; aspect factor from the Go renderer: nx *= width/renderH * 0.5
(define nx-aspect (* (/ (exact->inexact cols) (exact->inexact rows)) 0.5))

(show-tweaks)
(define (frame-str)
  (let* ((speed   (tweak "Speed"   0.5 0.0 1.0))
         (density (tweak "Density" 0.5 0.1 1.0))
         (bright  (tweak "Brightness" 0.8 0.1 1.0))
         (scl     (scale-from-param (tweak "Scale" 0.5 0.0 1.0)))
         (chaos   (tweak "Chaos"   0.3 0.0 1.0))
         (t (* (time) speed 2.5))
         (sub (* rows 2.0))
         (out (open-output-string)))
    (for ([row (in-range rows)])
      (write-string (ansi-move (+ row 1) 1) out)
      (let ((ny-top (- (* (/ (* 2.0 row)       sub) 2.0) 1.0))
            (ny-bot (- (* (/ (+ (* 2.0 row) 1) sub) 2.0) 1.0)))
        (for ([col (in-range cols)])
          (let* ((nx (* (- (* (/ (exact->inexact col) cols) 2.0) 1.0) nx-aspect))
                 (vt (* (clamp01 (hyper-spiral nx ny-top t density scl chaos)) bright))
                 (vb (* (clamp01 (hyper-spiral nx ny-bot t density scl chaos)) bright)))
            (if (and (< vt 0.05) (< vb 0.05))
                (begin (write-string (ansi-reset) out) (write-string " " out))
                (let ((ct (intensity-rgb vt)) (cb (intensity-rgb vb)))
                  (write-string (apply ansi-fg ct) out)
                  (write-string (apply ansi-bg cb) out)
                  (write-string "▀" out)))))))          ; ▀ half block
    ;; HUD (over the field, jnbgo style)
    (write-string (ansi-at 1 2 (styled (list 255 90 255) #f "LAYER 09 :: HYPER SPIRAL")) out)
    (write-string (ansi-at rows 2 (styled (list 255 160 255) #f
      (string-append "T " (number->string (i (* (time) 10)))
                     "  ARMS " (number->string (i (+ 3 (* (tweak "Chaos" 0.3 0.0 1.0) 5))))))) out)
    (get-output-string out)))

(define *ntsc* (box #t))
(every-frame
  (begin
    (when (= (key-poll) 110)                       ; n = toggle NTSC filter
      (set-box! *ntsc* (not (unbox *ntsc*)))
      (ntsc (unbox *ntsc*)))
    (with-primitive term
      (terminal-clear)
      (terminal-write (frame-str))
      (terminal-draw))))
