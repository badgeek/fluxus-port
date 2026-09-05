; golden case: ParticlePrimitive SOLID (camera-facing quads), both orderings.
; The ribbon-particles case only exercises the POINTS path; the quad paths — and
; the depth-sorted variant, which reads the modelview back — need their own case.
(set-window-size 480 360)
(clear)
(background (vector 0.04 0.04 0.06))

(define TWO-PI 6.283185307179586)

; unsorted quads
(let ((pp (build-particles 60)))
  (with-primitive pp
    (identity)
    (hint-none)
    (hint-solid)
    (hint-unlit)
    (translate (vector -0.9 0 0))
    (pdata-index-map!
      (lambda (i v)
        (let ((a (* TWO-PI (/ i 60.0))))
          (vector (* 0.7 (cos a)) (* 0.7 (sin a)) (* 0.5 (cos (* 3 a))))))
      "p")
    (pdata-index-map!
      (lambda (i c) (vector 1.0 (/ i 60.0) 0.2 1.0))
      "c")
    (pdata-index-map! (lambda (i s) (vector 0.18 0.18 0.18)) "s")))

; depth-sorted quads — exercises Backend()->getModelView()
(let ((pp (build-particles 60)))
  (with-primitive pp
    (identity)
    (hint-none)
    (hint-solid)
    (hint-unlit)
    (hint-depth-sort)
    (translate (vector 0.9 0 0))
    (pdata-index-map!
      (lambda (i v)
        (let ((a (* TWO-PI (/ i 60.0))))
          (vector (* 0.7 (cos a)) (* 0.7 (sin a)) (* 0.8 (sin (* 2 a))))))
      "p")
    (pdata-index-map!
      (lambda (i c) (vector 0.2 (/ i 60.0) 1.0 1.0))
      "c")
    (pdata-index-map! (lambda (i s) (vector 0.18 0.18 0.18)) "s")))
