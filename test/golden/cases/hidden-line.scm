; golden case: hidden-line — solid fill AND wire on the same primitive.
; Both passes run for one prim, so this catches ordering/depth regressions that
; the solid-only and wire-only cases each miss.
(set-window-size 480 360)
(clear)
(background (vector 0.04 0.04 0.06))

(with-state
  (translate (vector -1.1 0 0))
  (colour (vector 0.15 0.15 0.18))
  (wire-colour (vector 1.0 0.5 0.1))
  (hint-wire)
  (build-cube))

(with-state
  (translate (vector 1.1 0 0))
  (rotate (vector 0 35 0))
  (scale (vector 1.2 1.2 1.2))
  (colour (vector 0.1 0.12 0.2))
  (wire-colour (vector 0.2 0.9 1.0))
  (hint-wire)
  (build-sphere 10 10))
