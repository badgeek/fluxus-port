; golden case: PolyPrimitive SOLID pass.
; This one goes through Backend()->drawArrays already, so it is the baseline the
; seam work must not disturb. Deliberately static — no (time), no randomness.
(set-window-size 480 360)
(clear)
(background (vector 0.04 0.04 0.06))

(define (block x y z r g b)
  (with-state
    (translate (vector x y z))
    (scale (vector 0.8 0.8 0.8))
    (colour (vector r g b))
    (build-cube)))

(block -1.2  0.0 0.0 0.9 0.45 0.15)
(block  0.0  0.0 0.0 0.2 0.7  0.9)
(block  1.2  0.0 0.0 0.85 0.85 0.3)
(block  0.0  1.1 0.0 0.6 0.3  0.8)
(block  0.0 -1.1 0.0 0.3 0.85 0.5)
