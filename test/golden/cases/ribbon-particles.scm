; golden case: RibbonPrimitive + ParticlePrimitive.
; Both still draw with glBegin/glEnd, so they are exactly what ladder step 2
; (moving immediate mode behind IRenderBackend) rewrites. Static by construction:
; positions come from the vertex index, never from (time).
(set-window-size 480 360)
(clear)
(background (vector 0.04 0.04 0.06))

(define TWO-PI 6.283185307179586)

(let ((rb (build-ribbon 48)))
  (with-primitive rb
    (identity)
    (hint-unlit)
    (colour (vector 1.0 0.55 0.2))
    (pdata-index-map!
      (lambda (i v)
        (let ((a (* TWO-PI (/ i 47.0))))
          (vector (* 1.6 (cos a)) (* 0.9 (sin (* 2 a))) 0)))
      "p")
    (pdata-index-map! (lambda (i w) (vector 0.06 0.06 0.06)) "w")))

; vertcols ribbon: RibbonPrimitive's SOLID path has a separate per-vertex-colour
; branch, so the flat-colour ribbon above does not cover it.
(let ((rb (build-ribbon 40)))
  (with-primitive rb
    (identity)
    (hint-unlit)
    (hint-vertcols)
    (translate (vector 0 1.1 0))
    (pdata-index-map!
      (lambda (i v) (vector (- (* 3.2 (/ i 39.0)) 1.6) (* 0.25 (sin (* 6 (/ i 39.0)))) 0))
      "p")
    (pdata-index-map! (lambda (i w) (vector 0.09 0.09 0.09)) "w")
    (pdata-index-map!
      (lambda (i c) (vector (/ i 39.0) (- 1.0 (/ i 39.0)) 0.6 1.0))
      "c")))

; wire ribbon: the WIRE branch is a LINE_STRIP, a different draw again.
(let ((rb (build-ribbon 40)))
  (with-primitive rb
    (identity)
    (hint-solid #f)
    (hint-wire)
    (hint-unlit)
    (translate (vector 0 -1.2 0))
    (wire-colour (vector 0.4 1.0 0.8))
    (pdata-index-map!
      (lambda (i v) (vector (- (* 3.2 (/ i 39.0)) 1.6) (* 0.3 (cos (* 5 (/ i 39.0)))) 0))
      "p")
    (pdata-index-map! (lambda (i w) (vector 0.05 0.05 0.05)) "w")))

(let ((pp (build-particles 240)))
  (with-primitive pp
    (identity)
    (hint-unlit)
    (pdata-index-map!
      (lambda (i v)
        (let ((a (* TWO-PI (/ i 240.0))) (r (+ 0.4 (* 0.9 (/ i 240.0)))))
          (vector (* r (cos (* 7 a))) (* r (sin (* 5 a))) 0)))
      "p")
    (pdata-index-map!
      (lambda (i c) (vector (/ i 240.0) 0.8 (- 1.0 (/ i 240.0)) 1.0))
      "c")
    (pdata-index-map! (lambda (i s) (vector 0.05 0.05 0.05)) "s")))
