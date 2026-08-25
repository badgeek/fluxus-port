; camera-test.scm — exercises the script-driven camera wiring.
; Paste into any of the four apps' editor and press Ctrl+E (or Shift+Enter).
; Works in both the s7 and the Racket hosts (dialect-neutral: let/when/do only).
;
; What it shows:
;   - set-fov               : narrows/widens the lens
;   - set-camera-transform  : an auto-orbiting camera (overrides the mouse orbit
;                             until you call (camera-reset))
;   - a ring of cubes, each scaled + coloured by an audio band (gh n)
;
; Make some noise (or it just sits quiet at base size).

(background (vector 0.02 0.02 0.06))

; ---- camera -----------------------------------------------------------------
(set-fov 45)

; auto-orbit: eye circles the origin, height bobs a little. The view matrix is
; just translate(-eye) (column-major, translation in the last row) — enough to
; prove set-camera-transform drives the real engine camera.
(let* ((a  (* 0.4 (time)))
       (d  16)
       (ex (* d (sin a)))
       (ez (* d (cos a)))
       (ey (+ 3 (* 2 (sin (* 0.7 (time)))))))
  (set-camera-transform
    (vector 1        0        0        0
            0        1        0        0
            0        0        1        0
            (- ex)   (- ey)   (- ez)   1)))

; ---- a ring of audio-reactive cubes ----------------------------------------
(define (ring n)
  (when (> n 0)
    (with-state
      (rotate (vector 0 (* n 30) 0))          ; 12 cubes, 30 deg apart
      (translate (vector 6 0 0))
      (let ((s (+ 0.6 (* 4 (gh n)))))
        (scale (vector s s s)))
      (colour (vector (+ 0.2 (gh n)) 0.4 (+ 0.3 (gh (+ n 4)))))
      (build-cube))
    (ring (- n 1))))
(ring 12)

; centre marker so the orbit is obvious
(with-state
  (colour (vector 1 1 1))
  (scale (vector 0.4 0.4 0.4))
  (build-sphere 12 12))

; ---- tip --------------------------------------------------------------------
; Replace the (set-camera-transform ...) block with (camera-reset) to hand the
; camera back to the mouse (drag = orbit, wheel = dolly).
