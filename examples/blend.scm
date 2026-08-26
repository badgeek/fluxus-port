; blend-mode: additive glow. A cloud of dim spheres drawn with (blend-mode 'one
; 'one) so overlaps ADD up into bright cores — a cheap nebula/glow. Audio scales +
; brightens them. (blend-mode also takes 'src-alpha 'one-minus-src-alpha etc, or
; raw GL ints.)

(clear)
(start-audio "system:capture_1" 512 44100)
(background (vector 0.0 0.0 0.02))

(define N 60)
(define (fract q) (- q (floor q)))
(define (h i) (- (* 2 (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453))) 1))

(define (blob i)
  (with-state
    (blend-mode 'one 'one)                 ; additive
    (hint-unlit)
    (rotate (vector 0 (* (time) 12) (* (time) 7)))
    (translate (vector (* 6 (h i)) (* 6 (h (+ i 7))) (* 6 (h (+ i 13)))))
    (let ((s (+ 0.4 (* 3 (gh (modulo i 16))))))
      (scale (vector s s s)))
    (colour (vector (+ 0.05 (* 0.4 (gh (modulo i 8))))
                    (+ 0.03 (* 0.2 (gain)))
                    (+ 0.12 (* 0.4 (gh (modulo (+ i 4) 16))))))
    (build-sphere 12 12)))

(define (draw-all i) (when (< i N) (blob i) (draw-all (+ i 1))))
(every-frame (draw-all 0))
