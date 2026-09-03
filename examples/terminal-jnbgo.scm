;; terminal-jnbgo.scm — jnbgo-style TUI aesthetic rendered in-scene via build-terminal.
;; Digital-rain glitch field + a centred, emerging manifesto quote + corner HUD stats.
;; (Theme lifted from jnbgo's poetic.txt: "the algorithm sees your patterns".)
(retained)
(clear)
(hide-editor)
(set-window-size 960 600)

(define cols 64)
(define rows 26)
(define t (build-terminal cols rows))

;; centre + scale the grid into view (once).
(with-primitive t
  (scale (vector 0.36 0.36 0.36))
  (translate (vector (- (/ (* cols 0.5) 2)) (/ (* rows 0.9) 2) 0)))

;; final-stage NTSC/CRT filter (composite artefacts + scanlines + signal noise).
(ntsc)
(ntsc-scanlines)
(ntsc-noise 5)
(ntsc-saturation 22)
(ntsc-brightness 22)
(ntsc-contrast 210)

;; per-column rain parameters, seeded once.
(define speeds (for/vector ([c (in-range cols)]) (+ 5.0 (* 11.0 (random)))))
(define offs   (for/vector ([c (in-range cols)]) (* rows (random))))
(define lens   (for/vector ([c (in-range cols)]) (+ 6 (random 13))))

(define glyphs "01<>[]{}=+*/\\|╌╎┆░▒▓·:i!")
(define nglyph (string-length glyphs))
(define (gch n) (string (string-ref glyphs (modulo n nglyph))))

(define quotes
  (vector "THE ALGORITHM SEES YOUR PATTERNS"
          "NOT YOUR POTENTIAL - ONLY YOUR DATA"
          "PERSONALIZATION IS A COMFORTABLE PRISON"
          "YOUR FUTURE SELF IS BEING DESIGNED"
          "THE ECHO CHAMBER HAS NO WINDOWS"))

(define (i x) (inexact->exact (floor x)))
(define (dec2 x)                                   ; x -> "N.NN" (no racket/format dep)
  (let* ((h (i (* x 100))) (f (modulo h 100)))
    (string-append (number->string (quotient h 100)) "."
                   (if (< f 10) (string-append "0" (number->string f)) (number->string f)))))

(define (frame)
  (define tm (time))
  (define out (open-output-string))
  ;; --- digital rain (every cell) ---
  (for ([r (in-range rows)])
    (write-string (ansi-move (+ r 1) 1) out)
    (for ([c (in-range cols)])
      (define head (modulo (i (+ (* (vector-ref speeds c) tm) (vector-ref offs c))) rows))
      (define len  (vector-ref lens c))
      (define d    (modulo (- r head) rows))
      (cond
        [(= d 0)                                   ; bright head
         (write-string (ansi-fg 210 255 225) out)
         (write-string (gch (+ r (* c 3) (i (* tm 10)))) out)]
        [(< d len)                                 ; fading green trail
         (define inten (- 1.0 (/ (exact->inexact d) len)))
         (define g (i (+ 28 (* 205 inten))))
         (write-string (ansi-fg 0 g (quotient g 3)) out)
         (write-string (gch (+ (* r 7) (* c 13) (i (* tm 8)))) out)]
        [else (write-string " " out)])))
  ;; --- centred manifesto panel (typewriter reveal) ---
  (define qi (modulo (i (/ tm 3.5)) (vector-length quotes)))
  (define q  (vector-ref quotes qi))
  (define ql (string-length q))
  (define shown (min ql (i (* (- tm (* qi 3.5)) 22))))   ; chars revealed so far
  (define qtxt  (string-append (substring q 0 shown)
                               (if (< shown ql) "_" "")   ; blinking-ish cursor
                               (make-string (max 0 (- ql shown (if (< shown ql) 1 0))) #\space)))
  (define pw (+ 4 ql))
  (define px (max 2 (+ 1 (quotient (- cols pw) 2))))
  (define py (quotient (- rows 3) 2))
  (write-string (ansi-at py       px (styled (list 0 240 160) (list 6 10 16)
                 (string-append "┌" (make-string (- pw 2) #\─) "┐"))) out)
  (write-string (ansi-at (+ py 1) px (styled (list 225 250 240) (list 6 10 16)
                 (string-append "│ " qtxt " │"))) out)
  (write-string (ansi-at (+ py 2) px (styled (list 0 240 160) (list 6 10 16)
                 (string-append "└" (make-string (- pw 2) #\─) "┘"))) out)
  ;; --- HUD ---
  (write-string (ansi-at 1 2 (styled (list 0 210 150) #f "SYS::ONLINE")) out)
  (write-string (ansi-at 1 (- cols 12) (styled (list 120 200 255) #f "▓▒░ FEED ░▒▓")) out)
  (write-string (ansi-at rows 2 (styled (list 120 200 255) #f
                 (string-append "ENTROPY " (dec2 (+ 3.0 (* 0.9 (abs (sin tm)))))
                                "  DATA " (number->string (+ 100 (modulo (i (* tm 7)) 900)))))) out)
  (get-output-string out))

(every-frame
  (with-primitive t
    (terminal-clear)
    (terminal-write (frame))
    (terminal-draw)))
