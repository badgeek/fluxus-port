; golden case: PolyPrimitive WIRE pass.
; This path bypasses IRenderBackend entirely (PolyPrimitive.cpp:290-313, raw
; glPolygonMode + glDrawArrays), so it is the one most likely to shift when the
; seam is finished — and it is the look most sketches depend on.
(set-window-size 480 360)
(clear)
(background (vector 0.04 0.04 0.06))

(define (frame x y s r g b)
  (with-state
    (translate (vector x y 0))
    (scale (vector s s s))
    (hint-solid #f)
    (hint-wire)
    (hint-unlit)
    (backfacecull #f)
    (wire-colour (vector r g b))
    (build-cube)))

(frame -1.2 0.0 0.9 0.2 1.0 0.9)
(frame  0.0 0.0 1.1 1.0 0.4 0.2)
(frame  1.2 0.0 0.9 0.5 1.0 0.3)
