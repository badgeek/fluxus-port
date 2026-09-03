; OSC end-to-end test sketch.
;   receives on port 8000, echoes the latest /in args back to /out on 127.0.0.1:9001.
; Drive it with test/osc_e2e.py (sends /in, asserts the /out echo). Also draws the
; live values so the round-trip is visible in the window. Needs the OSC host (all
; four apps create it) — works on s7 or Racket.

(clear)
(osc-source 8000)                       ; listen
(osc-destination "127.0.0.1" 9001)      ; echo target (the python harness listens here)

(define (fmt x) (number->string (/ (round (* x 1000.0)) 1000.0)))
(define (label s y col)
  (let ((p (build-text s)))
    (with-primitive p
      (hint-unlit) (colour col)
      (translate (vector -7.5 0.1 y)) (rotate (vector -90 0 0)) (scale (vector 0.7 0.7 0.7)))
    p))

(retained)
(every-frame
  (begin
    (clear)                             ; wipe + rebuild the readout each frame
    (let ((a (osc "/in" 0)) (b (osc "/in" 1)))
      (osc-send "/out" a b)             ; echo both args straight back
      (label "OSC E2E  (echo /in -> /out)" -4 (vector 0.55 1.0 0.65))
      (label (string-append "/in[0] = " (fmt a)) -1.5 (vector 1 1 1))
      (label (string-append "/in[1] = " (fmt b))  0.5 (vector 1 1 1))
      (label (string-append "last addr = " (osc-msg)) 3.0 (vector 0.7 0.9 1.0)))))
