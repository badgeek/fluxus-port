; new engine features: build-cylinder, materials (specular/shinyness), a real
; point light (make-light + light-*), and fog. Retained mode so the light and
; cylinders are created ONCE (in immediate mode make-light would leak a new light
; every frame).

(retained)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)
(fog (vector 0.03 0.03 0.06) 0.025 8 48)          ; colour, density, near, far

(define L (make-light 'point))
(light-diffuse  L (vector 1.0 0.9 0.7))
(light-specular L (vector 1.0 1.0 1.0))

(define N 20)
(define ids
  (for/list ((i (in-range N)))
    (let ((id (build-cylinder 2.0 0.5 10 14)))
      (with-primitive id
        (specular (vector 1 1 1))
        (shinyness 64))
      id)))

(every-frame
  (light-position L (vector (* 11 (sin (time))) 7 (* 11 (cos (time)))))   ; orbit the light
  (for ((i (in-range N)))
    (let ((id (list-ref ids i))
          (a  (gh (modulo i 16))))
      (with-primitive id
        (identity)
        (rotate (vector 0 (* i (/ 360.0 N)) 0))
        (translate (vector 6 -1 0))
        (scale (vector 0.6 (+ 0.6 (* 5 a)) 0.6))                          ; audio bars
        (colour (vector (+ 0.2 a) 0.35 (+ 0.6 (- 1 a))))))))
