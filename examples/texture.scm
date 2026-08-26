; textures: (load-texture path) decodes an image via JUCE and uploads a GL
; texture; (texture id) applies it. Built-in mesh texcoords (sphere/cube/torus
; already have them) map the image onto the surface. Retained so the texture is
; loaded once and the meshes built once.

(retained)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)

(define tex (load-texture "assets/test.png"))   ; relative to the app's cwd (repo root)

(texture tex)
(hint-unlit)                                     ; show the texture at full brightness
(colour (vector 1 1 1))
(define S (build-sphere 48 48))
(define C (build-cube))
(define T (build-torus 0.4 1.0 24 24))

(every-frame
  (with-primitive S
    (identity)
    (rotate (vector (* (time) 10) (* (time) 15) 0))
    (let ((s (+ 1.6 (* 3 (gain))))) (scale (vector s s s))))
  (with-primitive C
    (identity)
    (translate (vector 3.5 0 0))
    (rotate (vector (* (time) -20) 0 (* (time) 25)))
    (let ((s (+ 0.8 (* 3 (gh 2))))) (scale (vector s s s))))
  (with-primitive T
    (identity)
    (translate (vector -3.5 0 0))
    (rotate (vector (* (time) 30) (* (time) 10) 0))))
