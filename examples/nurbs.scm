; real NURBS surface (GLU tessellation). build-nurbs-plane makes a grid of control
; points (pdata "p"); moving them bends the smooth surface. Here the CVs ripple with
; sin waves + audio. hint-unlit so the colour shows at full brightness.

(retained)
(start-audio "system:capture_1" 512 44100)

(define n (build-nurbs-plane 12 12))
(with-primitive n
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))
(define NC (with-primitive n (pdata-size)))     ; control-point count

(every-frame
  (with-primitive n
    (identity)
    (rotate (vector -55 (* (time) 20) 0))
    (scale (vector 7 7 7))
    (hint-unlit)
    (wire-opacity 1)
    (colour (vector (+ 0.2 (gain)) 0.6 (+ 0.7 (gh 6))))
    (pdata-index-map!
      (lambda (i v)
        (let* ((o  (pdata-ref "ori" i))
               (ox (vx o)) (oy (vy o))
               (band (modulo (inexact->exact (floor (* (+ ox 0.5) 12))) 16))
               (z  (+ (* 0.12 (sin (+ (* 8 ox) (* 2 (time)))))
                      (* 0.12 (cos (+ (* 7 oy) (* 1.7 (time)))))
                      (* 0.5 (gh band)))))
          (vector ox oy z)))
      "p")))
