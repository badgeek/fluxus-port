; build-text + build-pixels.
; - build-text makes glyph quads textured from a generated 16x16 font atlas,
;   alpha-tested so only the letters show (coloured by the current colour).
; - build-pixels makes a plane backed by a writable "c" pixel buffer; write it
;   with pdata and (pixels-upload) pushes it to the plane's texture.
; Retained so the text mesh + pixel texture are created ONCE.

(retained)
(start-audio "system:capture_1" 512 44100)

(define txt (build-text "FLUXUS  JUCE"))
(define px  (build-pixels 48 48))

(every-frame
  ; --- scrolling / audio-coloured text ---
  (with-primitive txt
    (identity)
    (translate (vector -3.5 2.0 0))
    (scale (vector 0.7 0.7 0.7))
    (rotate (vector 0 (* 12 (sin (* 0.6 (time)))) 0))
    (colour (vector (+ 0.3 (gain)) 1.0 (+ 0.4 (gh 4)))))

  ; --- procedural audio pixel buffer on a plane ---
  (with-primitive px
    (identity)
    (translate (vector 0 -1.6 0))
    (scale (vector 5 5 1))
    (let ((w (pixels-width)))
      (pdata-index-map!
        (lambda (i c)
          (let* ((x (modulo i w)) (y (quotient i w))
                 (v (+ 0.4 (* 0.6 (sin (+ (* 0.4 x) (* 0.3 y) (* 3 (time)))))))
                 (a (gh (modulo x 16))))
            (vector (+ (* v 0.5) a) (* v (+ 0.3 a)) (- 1.0 v))))
        "c"))
    (pixels-upload)))
