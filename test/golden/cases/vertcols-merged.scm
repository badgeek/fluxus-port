; golden case: per-vertex colours and (build-merged …).
; Exercises the indexed draw path and HINT_VERTCOLS — the combination the
; static-scene optimisation relies on, so a seam change that breaks index or
; colour handling shows up here rather than in a real sketch weeks later.
(set-window-size 480 360)
(clear)
(background (vector 0.04 0.04 0.06))

(define (hsh i) (- (* 9301 (+ i 7)) (* 233 (floor (/ (* 9301 (+ i 7)) 233)))))

(define ids
  (let loop ((i 0) (acc '()))
    (if (= i 16)
        acc
        (loop (+ i 1)
              (cons (with-state
                      (translate (vector (- (* 0.55 (modulo i 4)) 0.8)
                                         (- (* 0.55 (floor (/ i 4))) 0.8)
                                         0))
                      (scale (vector 0.45 0.45 0.45))
                      (colour (vector (/ (modulo i 4) 3.0)
                                      (/ (floor (/ i 4)) 3.0)
                                      0.7))
                      (build-cube))
                    acc)))))

(let ((m (build-merged ids)))
  (with-primitive m (hint-vertcols))
  (for-each destroy ids))
