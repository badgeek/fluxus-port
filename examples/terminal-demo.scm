;; terminal-demo — ANSI authored in Racket, rendered as an in-scene cell grid.
;; Retained: build the terminal ONCE, then per frame clear/write/draw its cells.
;; (Do NOT (clear) inside the thunk — that wipes the persistent terminal prim.)
(retained)
(clear)
(hide-editor)
(set-window-size 560 360)

(define cols 22)
(define rows 9)
(define t (build-terminal cols rows))

;; centre + scale the grid into view (once).
(with-primitive t
  (scale (vector 0.30 0.30 0.30))
  (translate (vector (- (/ (* cols 0.5) 2)) (/ (* rows 0.9) 2) 0)))

(every-frame
  (with-primitive t
    (terminal-clear)
    (let ((x (+ 3 (modulo (inexact->exact (floor (* (time) 6))) 10))))  ; a marching dot
      (terminal-write
       (string-append
        (ansi-at 1 2 (styled (list 0 255 180) (list 20 20 40) "hello terminal"))
        (ansi-at 3 2 (styled (list 200 200 200) #f "┌────────────┐"))
        (ansi-at 4 2 (string-append (styled (list 200 200 200) #f "│ ")
                                    (styled (list 255 90 90) #f "▓▒░ blocks")
                                    (styled (list 200 200 200) #f " │")))
        (ansi-at 5 2 (styled (list 200 200 200) #f "└────────────┘"))
        (ansi-at 7 2 (styled (list 120 200 255) #f "▀▄█ ░▒▓ ★☆✧"))
        (ansi-at 9 x (styled (list 255 240 120) #f "●")))))
    (terminal-draw)))
