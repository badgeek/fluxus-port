; nurbs-audio, fixed to spin in this immediate-mode port.
; The port re-evals the whole buffer each frame and Clear()s the scene, so the
; sphere is rebuilt fresh every frame — a per-frame (delta) rotation can't
; accumulate. Drive the angle from absolute (time) instead.

(clear)
(start-audio "system:capture_1" 512 44100)
(define x (build-sphere 10 30))

(with-primitive x
    (scale (vector 2 2 2))
    (pdata-add "ori" "v")
    (pdata-copy "p" "ori")
)

(define (vertex_1 y)
    (with-primitive x
      (line-width 2)
      (hint-wire)
      (backfacecull 1)
      (opacity (* (gh 5) 1000))
      (wire-opacity (* (gh 3) 100))
      (wire-colour (vector 1 0.5 0))
      (colour (vector 1 0 0))
      (rotate (vector 0 (* (time) y) 0))          ; was (delta) — use (time) here
      (pdata-index-map!
           (lambda (index val)
                (vadd (pdata-ref "ori" index) (vmul (pdata-ref "n" index) (* (gh index) 10)))
           )
        "p"
      )
    )
)

(define (renderchain)
    (vertex_1 50)
)

(every-frame (renderchain))
