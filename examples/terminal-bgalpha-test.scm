;; terminal-bgalpha-test — verify (terminal-bg-alpha) tint / see-through.
;; Three terminals, bg alpha 1.0 / 0.4 / 0.0, over a bright cube so the
;; blend is obvious: opaque hides it, 0.4 tints it, 0.0 shows it through.
(retained)
(clear)
(hide-editor)
(set-window-size 700 400)

(define cols 16)
(define rows 5)

(define (mk x alpha)
  (let ((t (build-terminal cols rows)))
    (with-primitive t
      (scale (vector 0.22 0.22 0.22))
      (translate (vector (+ x (- (/ (* cols 0.5) 2))) (/ (* rows 0.9) 2) 0))
      (terminal-bg-alpha alpha))
    t))

;; bright cube built FIRST (drawn first) so translucent bg quads blend over it
(define bg (build-cube))
(with-primitive bg
  (translate (vector 0 0 -1.2))
  (scale (vector 10 2.2 0.1))
  (colour (vector 1.0 0.3 0.05)))

(define ta (mk -13.0 1.0))
(define tb (mk  -1.5 0.4))
(define tc (mk  10.0 0.0))

(define nl (string #\return #\newline))
(define (fill t label)
  (with-primitive t
    (terminal-clear)
    (terminal-write (string-append nl "  " label nl nl "  #### BLOCKS ####"))
    (terminal-draw)))

(every-frame
  (begin
    (fill ta "alpha 1.0")
    (fill tb "alpha 0.4")
    (fill tc "alpha 0.0")
    (screenshot "/tmp/bgalpha3.png")))
