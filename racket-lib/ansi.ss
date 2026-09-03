#lang racket/base
;; ANSI / VT string helpers for the (build-terminal …) primitive. Pure string
;; formatting — no FFI, so this file also loads standalone on the racket CLI. The
;; strings you build here are fed to (terminal-write …); libvterm parses them and
;; the terminal prim renders the resulting cell grid. See fluxus-engine.ss for the
;; terminal-* commands, and CLAUDE.md for the render model.
;;
;; Typical per-frame use (retained mode):
;;   (define t (build-terminal 40 20))
;;   (every-frame
;;     (with-primitive t
;;       (terminal-clear)
;;       (terminal-write (string-append (ansi-home)
;;                                      (ansi-at 2 3 (styled '(0 255 180) '(20 20 40) "hi"))))
;;       (terminal-draw)))

(provide ansi-esc ansi-reset ansi-fg ansi-bg ansi-move ansi-home ansi-clear
         ansi-at styled ansi-put)

(define ansi-esc "\u1b")   ; ESC (Racket has no \e)

;; SGR reset — clears fg/bg/attrs back to the terminal defaults.
(define (ansi-reset) (string-append ansi-esc "[0m"))

;; truecolor SGR. r g b are 0..255.
(define (ansi-fg r g b) (format "~a[38;2;~a;~a;~am" ansi-esc r g b))
(define (ansi-bg r g b) (format "~a[48;2;~a;~a;~am" ansi-esc r g b))

;; cursor position — row/col are 1-based (VT convention).
(define (ansi-move row col) (format "~a[~a;~aH" ansi-esc row col))
(define (ansi-home)  (string-append ansi-esc "[H"))
(define (ansi-clear) (string-append ansi-esc "[2J"))

;; move to (row,col) then emit str.
(define (ansi-at row col str) (string-append (ansi-move row col) str))

;; wrap a run in fg + bg then reset. fg/bg are (list r g b) or #f to skip.
(define (styled fg bg str)
  (string-append
   (if fg (apply ansi-fg fg) "")
   (if bg (apply ansi-bg bg) "")
   str
   (ansi-reset)))

;; concatenate any number of strings (convenience).
(define (ansi-put . strs) (apply string-append strs))
