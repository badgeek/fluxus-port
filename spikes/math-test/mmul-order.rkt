#lang racket/base
;; Convention guard for the scheme-side matrix composition order.
;;
;;   racket spikes/math-test/mmul-order.rkt      # RESULT: OK / FAIL
;;
;; The engine's dMatrix::operator* (vendor/fluxus/libfluxus/src/dada.h:472) computes
;; A*B as the standard row-major product B·A, so in a fluxus matrix chain the
;; RIGHTMOST factor applies FIRST. Every upstream .ss chain (poly-tools extrude,
;; camera.ss input-camera, planetarium.ss) is written that way, and the FFI
;; flux_mmul that s7's (mmul) calls routes through the same operator*.
;; racket-lib/fluxus-engine.ss's pure-scheme mmul2 has to match, or the two hosts
;; disagree and upstream chains come out reversed. This asserts it.
;;
;; The REFERENCE numbers below were measured from the C side (flux_mmul /
;; flux_mrotate / flux_maim in app/FluxusCommandsMaths.cpp, linked against the real
;; libfluxus); the engine computes in float32, hence the 1e-6 tolerance.
(require "../../racket-lib/fluxus-engine.ss")

(define fails 0)
(define (fail! . args) (set! fails (add1 fails)) (apply printf args))

(define (close? a b) (< (abs (- a b)) 1e-6))
(define (check-m name got want)
  (if (for/and ([i (in-range 16)]) (close? (vector-ref got i) (vector-ref want i)))
      (printf "ok    ~a\n" name)
      (fail! "FAIL  ~a\n        got  ~a\n        want ~a\n" name got want)))
(define (check name ok? . detail)
  (if ok? (printf "ok    ~a\n" name) (apply fail! (string-append "FAIL  " name "\n") detail)))

(define T (mtranslate (vector 1 0 0)))
(define S (mscale (vector 2 2 2)))

;; 1. THE discriminator. (mmul T S) means "translate first, then scale" under a
;;    plain standard product (translation row would be (2 0 0)); under the engine
;;    convention it means "scale first, then translate" -> (1 0 0).
;;    C reference: flux_mmul(T,S) row3 = (1 0 0 1), flux_mmul(S,T) row3 = (2 0 0 1).
(let ([ts (mmul T S)] [st (mmul S T)])
  (check "(mmul T S) translation row is (1 0 0) — rightmost applies first"
         (and (close? (vector-ref ts 12) 1.0) (close? (vector-ref ts 13) 0.0)
              (close? (vector-ref ts 14) 0.0))
         "        got ~a\n" ts)
  (check "(mmul S T) translation row is (2 0 0)"
         (and (close? (vector-ref st 12) 2.0) (close? (vector-ref st 13) 0.0)
              (close? (vector-ref st 14) 0.0))
         "        got ~a\n" st))

;; 2. The documented one-liner: (mmul (mtranslate t) (mrotate r)) == rotate, THEN
;;    translate — so the translation row comes out UNrotated.
;;    C reference: flux_mmul(mtranslate(1,2,3), mrotate(0,90,0)) row3 = (1 2 3 1);
;;    the other order gives (3 2 -1 1).
(let ([tr (mmul (mtranslate (vector 1 2 3)) (mrotate (vector 0 90 0)))]
      [rt (mmul (mrotate (vector 0 90 0)) (mtranslate (vector 1 2 3)))])
  (check "(mmul (mtranslate 1 2 3) (mrotate 0 90 0)) translation row is (1 2 3)"
         (and (close? (vector-ref tr 12) 1.0) (close? (vector-ref tr 13) 2.0)
              (close? (vector-ref tr 14) 3.0))
         "        got ~a\n" tr)
  (check "(mmul (mrotate 0 90 0) (mtranslate 1 2 3)) translation row is (3 2 -1)"
         (and (close? (vector-ref rt 12) 3.0) (close? (vector-ref rt 13) 2.0)
              (close? (vector-ref rt 14) -1.0))
         "        got ~a\n" rt))

;; 3. scheme (mrotate v) must equal the engine's rotxyz / flux_mrotate — it is built
;;    out of mmul, so the composition order decides it. Reference: flux_mrotate(10,20,30).
(check-m "(mrotate (vector 10 20 30)) == flux_mrotate(10,20,30)"
         (mrotate (vector 10 20 30))
         (vector  0.81379771  0.54383808 -0.20487413 0.0
                 -0.46984628  0.82317299  0.31879577 0.0
                  0.34202012 -0.16317591  0.92541653 0.0
                  0.0         0.0         0.0        1.0))

;; 4. End-to-end: poly-tools' extrude-segment composes
;;      (mmul (maim v up) (mrotate (vector 0 90 0)) (mscale ...))
;;    and lays the (XY-plane) profile out through it, so the cross-section normal is
;;    the matrix's +Z axis. A proper tube needs it PARALLEL to the path tangent
;;    (|n.dir| = 1); the reversed order puts the cross-section plane through the
;;    tangent (|n.dir| = 0) and the extrusion collapses to a twisted flat band.
;;    maim is FFI-only, so the reference matrix flux_maim((1,0,0),(0,1,0)) is inlined.
(let* ([dir (vector 1 0 0)]
       [aim (vector 1 0 0 0   0 0 -1 0   0 1 0 0   0 0 0 1)]  ; flux_maim((1,0,0),(0,1,0))
       [m   (mmul aim (mrotate (vector 0 90 0)) (mscale (vector 1 1 1)))]
       [n   (vtransform-rot (vector 0 0 1) m)]
       [d   (abs (+ (* (vector-ref n 0) (vector-ref dir 0))
                    (* (vector-ref n 1) (vector-ref dir 1))
                    (* (vector-ref n 2) (vector-ref dir 2))))])
  (check "extrude cross-section normal is parallel to the path tangent (|n.dir| = 1)"
         (close? d 1.0) "        normal ~a  |n.dir| ~a\n" n d))

(printf "RESULT: ~a\n" (if (zero? fails) "OK" (format "FAIL (~a)" fails)))
(when (positive? fails) (exit 1))
