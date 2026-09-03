; tweak panel demo.
; Every (tweak name default lo hi) becomes a slider; drag one and the scene
; follows immediately. Values survive Ctrl+E, so dial it in here and then bake
; the numbers back into the source when you like what you see.
;
; The panel has no built-in hotkey — bind your own with (key-poll), as below, or
; use View -> Show Tweaks. Click the 3D canvas first so keys reach the script
; rather than the code editor.
(retained)

(define *tweaks-on* #t)

(every-frame
  (clear)
  (background (vector 0.05 0.06 0.09))

  ; G toggles the slider panel
  (when (= (key-poll) 103)
    (set! *tweaks-on* (not *tweaks-on*))
    (if *tweaks-on* (show-tweaks) (hide-tweaks)))

  (let ((size    (tweak "size"    1.0  0.2 3.0))
        (spin    (tweak "spin"    25.0 0.0 200.0))
        (spread  (tweak "spread"  2.5  0.0 6.0))
        (hue     (tweak "hue"     0.9  0.0 1.0))
        (spheres (tweak "spheres" 3.0  1.0 12.0)))

    (hint-solid #t) (hint-wire #f)
    (colour (vector hue 0.5 0.2))
    (rotate (vector (* spin (time)) (* spin 1.6 (time)) 0))
    (scale (vector size size size))
    (build-cube)

    (for ((i (in-range (inexact->exact (round spheres)))))
      (with-state
        (rotate (vector 0 (* 360 (/ i spheres)) 0))
        (translate (vector spread 0 0))
        (colour (vector 0.3 (- 1.0 hue) 1.0))
        (scale (vector (* 0.4 size) (* 0.4 size) (* 0.4 size)))
        (build-sphere 12 12)))))
