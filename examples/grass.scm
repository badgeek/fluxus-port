;; grass.scm — a field of swaying grass on a noise terrain. CPU port of Andreas
;; Müller's NoiseWorkshop "Grass" (vendor/NoiseWorkshop/Grass), whose geometry
;; shader grows a tapered blade per line segment and sways it with 2D noise +
;; sin(time). This engine has no geometry shader, so each blade is a BILLBOARDED
;; RIBBON (camera-facing tapered strip) whose curved, swaying centreline is
;; rewritten every frame via pdata. Retained: build the pool + ground ONCE, animate
;; only the blade pdata. Wind is live-tweakable (ImGui panel, top-right).

(retained)
(hide-editor)
(set-window-size 900 600)
(background (vector 0.05 0.07 0.10))

;; ---- vector maths for the orbit camera (gluLookAt view matrix) --------------
(define (v- a b) (vector (- (vx a) (vx b)) (- (vy a) (vy b)) (- (vz a) (vz b))))
(define (vdot a b) (+ (* (vx a) (vx b)) (* (vy a) (vy b)) (* (vz a) (vz b))))
(define (vcross a b)
  (vector (- (* (vy a) (vz b)) (* (vz a) (vy b)))
          (- (* (vz a) (vx b)) (* (vx a) (vz b)))
          (- (* (vx a) (vy b)) (* (vy a) (vx b)))))
(define (vnorm a)
  (let ((l (sqrt (vdot a a)))) (if (> l 1e-6) (vector (/ (vx a) l) (/ (vy a) l) (/ (vz a) l)) a)))
(define (look-at eye tgt up)
  (let* ((f (vnorm (v- tgt eye))) (s (vnorm (vcross f up))) (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0.0
            (vy s) (vy u) (- (vy f)) 0.0
            (vz s) (vz u) (- (vz f)) 0.0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1.0)))

;; ---- terrain height (gentle fBm hills) --------------------------------------
(define (snoise x z) (- (* 2.0 (noise x z)) 1.0))    ; noise 0..1 -> -1..1
(define (fbm x z)
  (+ (* 1.00 (snoise (* x 0.35) (* z 0.35)))
     (* 0.45 (snoise (* x 0.80) (* z 0.80)))))
(define AMPT 0.55)
(define (terrain-h x z) (* AMPT (fbm x z)))

;; ---- field layout -----------------------------------------------------------
(define E 4.0)                 ; field half-extent in X and Z
(define GX 26) (define GZ 26)  ; blade grid -> 676 blades
(define NB (* GX GZ))
(define K 6)                   ; ribbon control points per blade
(define TWO-PI 6.283185307179586)

;; per-blade constants (built once): base x/z/height, sway phase, bend dir, tint
(define BX (make-vector NB 0.0)) (define BZ (make-vector NB 0.0))
(define BY (make-vector NB 0.0)) (define PH (make-vector NB 0.0))
(define BDX (make-vector NB 1.0)) (define BDZ (make-vector NB 0.0))
(define BR (make-vector NB 1.0)) (define BH (make-vector NB 1.0))
(define ID (make-vector NB 0))

;; ---- colour: base-darkened green gradient (fake AO like the geom shader) -----
(define (grad-green t br)
  (let* ((ao (+ 0.35 (* 0.65 (min 1.0 (* t 1.7)))))   ; dark at the root
         (r  (* 0.10 (+ 0.5 (* 0.8 t))))
         (g  (+ 0.26 (* 0.50 t)))
         (b  (* 0.10 (+ 0.3 t))))
    (vector (* r ao br) (* g ao br) (* b ao br))))

;; ---- build the ground plane ONCE (noise-displaced, dark green) --------------
(define GN 40)
(define ground (build-seg-plane GN GN))
(with-primitive ground
  (hint-unlit) (hint-vertcols) (backfacecull #f)
  (let ((n (* GN GN 4)) (us (/ 1.0 GN)) (vs (/ 1.0 GN)))
    (pdata-index-map!
      (lambda (i p)
        (let* ((q (quotient i 4)) (c (modulo i 4))
               (gx (quotient q GN)) (gy (modulo q GN))
               (u (+ (/ (exact->inexact gx) GN) (if (or (= c 1) (= c 2)) us 0.0)))
               (v (+ (/ (exact->inexact gy) GN) (if (or (= c 2) (= c 3)) vs 0.0)))
               (wx (* (- u 0.5) 2.0 E)) (wz (* (- v 0.5) 2.0 E)))
          (vector wx (terrain-h wx wz) wz)))
      "p")
    (pdata-index-map!
      (lambda (i c)
        (let* ((q (quotient i 4)) (cc (modulo i 4))
               (gx (quotient q GN)) (gy (modulo q GN))
               (u (+ (/ (exact->inexact gx) GN) (if (or (= cc 1) (= cc 2)) us 0.0)))
               (v (+ (/ (exact->inexact gy) GN) (if (or (= cc 2) (= cc 3)) vs 0.0)))
               (wx (* (- u 0.5) 2.0 E)) (wz (* (- v 0.5) 2.0 E))
               (d (+ 0.5 (* 0.5 (snoise (* wx 0.6) (* wz 0.6))))))
          (vector (* 0.04 (+ 0.5 d)) (* 0.16 (+ 0.5 d)) (* 0.05 (+ 0.5 d)))))
      "c")))

;; ---- scatter + build the blade ribbons ONCE ---------------------------------
(let bloop ((i 0))
  (when (< i NB)
    (let* ((gx (quotient i GZ)) (gz (modulo i GZ))
           (cx (+ (* (- (/ (exact->inexact gx) (- GX 1.0)) 0.5) 2.0 E)))
           (cz (+ (* (- (/ (exact->inexact gz) (- GZ 1.0)) 0.5) 2.0 E)))
           ;; jitter each blade off its grid cell with noise
           (jx (* 0.55 (snoise (* cx 3.1) (* cz 1.7))))
           (jz (* 0.55 (snoise (* cx 1.3) (* cz 2.9))))
           (bx (+ cx jx)) (bz (+ cz jz))
           (by (terrain-h bx bz))
           (wa (* 0.6 (snoise (* bx 0.5) (* bz 0.5))))   ; bend dir varies ~±0.6 rad off +X
           (br (+ 0.7 (* 0.5 (noise (* bx 2.3) (* bz 2.3)))))   ; per-blade brightness
           (bh (+ 0.7 (* 0.6 (noise (* bx 1.1) (* bz 1.9))))))  ; per-blade height mult
      (vector-set! BX i bx) (vector-set! BZ i bz) (vector-set! BY i by)
      (vector-set! PH i (* TWO-PI (noise (* bx 0.9) (* bz 0.9))))
      (vector-set! BDX i (cos wa)) (vector-set! BDZ i (sin wa))
      (vector-set! BR i br) (vector-set! BH i bh)
      (let ((rib (build-ribbon K)))
        (vector-set! ID i rib)
        (with-primitive rib
          (hint-unlit) (hint-solid #t) (hint-wire #f) (hint-vertcols) (backfacecull #f))))
    (bloop (+ i 1))))

;; ---- per-frame: sway every blade + orbit the camera -------------------------
(every-frame
  (let* ((T    (time))
         (WStr (tweak "wind"       0.24  0.0 0.6))
         (WSpd (tweak "wind speed" 1.7   0.0 5.0))
         (GStr (tweak "gust"       0.7   0.0 2.0))
         (H    (tweak "blade h"    0.55  0.1 1.4))
         (W0   (tweak "blade w"    0.028 0.005 0.08)))
    ;; camera: slow orbit, low over the field
    (let* ((ang (* T 0.12))
           (eye (vector (* 8.5 (sin ang)) 2.4 (* 8.5 (cos ang))))
           (tgt (vector 0.0 0.4 0.0)))
      (set-camera-transform (look-at eye tgt (vector 0.0 1.0 0.0))))
    ;; sway each blade: bend grows with t^2 (tip moves most), swing from
    ;; sin(time)+noise-gust so a wave travels across the field.
    (let bloop ((i 0))
      (when (< i NB)
        (let* ((bx (vector-ref BX i)) (bz (vector-ref BZ i)) (by (vector-ref BY i))
               (ph (vector-ref PH i)) (dx (vector-ref BDX i)) (dz (vector-ref BDZ i))
               (br (vector-ref BR i)) (bh (* H (vector-ref BH i)))
               (gust (* GStr (snoise (+ (* bx 0.25) (* T 0.5)) (* bz 0.25))))
               (sway (* WStr (+ (sin (+ (* T WSpd) ph)) gust))))
          (with-primitive (vector-ref ID i)
            (pdata-index-map!
              (lambda (k p)
                (let* ((t (/ k (- K 1.0)))
                       (bend (* t t sway)))
                  (vector (+ bx (* dx bend)) (+ by (* t bh)) (+ bz (* dz bend)))))
              "p")
            (pdata-index-map!
              (lambda (k w) (let ((ww (* W0 (- 1.0 (/ k (- K 1.0)))))) (vector ww ww ww))) "w")
            (pdata-index-map!
              (lambda (k c) (grad-green (/ k (- K 1.0)) br)) "c")))
        (bloop (+ i 1))))))
