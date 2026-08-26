; particle cloud of audio-reactive cubes.
;
; IMPORTANT (this port): draw NEW prims OUTSIDE (with-primitive ...). While a prim
; is grabbed, turtle commands (rotate/translate/scale/colour) edit the GRABBED
; prim, so a draw-cube inside a grab ignores them (all cubes land at the origin,
; scale 1). Drawing at top level, the turtle state applies normally.
;
; Audio = default INPUT (mic). (start-audio "...") args are ignored; play sound
; out loud or route system audio to an input device.

(clear)
(start-audio "iTunes:out1" 512 44100)

(define N 120)
(define (fract q) (- q (floor q)))
(define (h i) (- (* 2 (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453))) 1)) ; [-1,1]

; one cube for particle i: fixed hashed position, size/colour from an audio band
(define (cube i)
  (let* ((pos (vector (* 26 (h i)) (* 26 (h (+ i 7))) (* 26 (h (+ i 13)))))
         (a   (gh (modulo i 8)))                 ; this particle's band, 0..~1
         (s   (+ 0.3 (* 7 a))))                  ; base size + big audio kick
    (with-state
      (rotate (vector 0 (* (time) 12) (* (sin (time)) 12)))   ; whole cloud spins
      (translate pos)
      (colour (vector (+ 0.15 a) (+ 0.35 (* 0.6 a)) (+ 0.7 (- 1 a))))
      (scale (vector s s s))
      (hint-wire)
      (draw-cube))))

(define (draw-all i) (when (< i N) (cube i) (draw-all (+ i 1))))

(every-frame (draw-all 0))
