;; fps-terrain.scm — first-person flight over infinite generative terrain (Racket).
;; A subdivided plane displaced per-vertex by fBm noise (pdata "p"), height-tinted
;; via vertex colours (pdata "c") with a neon wireframe grid lifted just above it,
;; viewed through a hand-built FPS camera matrix (set-camera-transform).
;;
;; RETAINED mode: the two plane prims are built ONCE; the every-frame thunk only
;; rewrites their pdata + the camera. Flight state lives in the top-level vector
;; `st` (#(steerX scroll-trim speed)) — the thunk is a closure over it, so it just
;; persists; no per-frame re-parse, no global-env tricks.
;;
;; Forward motion rides on (time). STEER (click the window first for key focus):
;;   W / S  faster / slower      A / D  drift left / right

(retained)
(hide-editor)
(set-window-size 900 600)
(background (vector 0.02 0.012 0.05))

;; --- vector maths for the look-at view matrix ------------------------------
(define (v- a b) (vector (- (vx a) (vx b)) (- (vy a) (vy b)) (- (vz a) (vz b))))
(define (vdot a b) (+ (* (vx a) (vx b)) (* (vy a) (vy b)) (* (vz a) (vz b))))
(define (vcross a b)
  (vector (- (* (vy a) (vz b)) (* (vz a) (vy b)))
          (- (* (vz a) (vx b)) (* (vx a) (vz b)))
          (- (* (vx a) (vy b)) (* (vy a) (vx b)))))
(define (vnorm a)
  (let ((l (sqrt (vdot a a)))) (if (> l 1e-6) (vector (/ (vx a) l) (/ (vy a) l) (/ (vz a) l)) a)))
;; gluLookAt -> column-major 16-vec (dMatrix arr layout: translation at 12/13/14)
(define (look-at eye tgt up)
  (let* ((f (vnorm (v- tgt eye)))
         (s (vnorm (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0.0
            (vy s) (vy u) (- (vy f)) 0.0
            (vz s) (vz u) (- (vz f)) 0.0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1.0)))

;; --- terrain height + colour -----------------------------------------------
(define (fbm x z)
  (+ (* 1.00 (noise (* x 0.60) (* z 0.60)))
     (* 0.50 (noise (* x 1.30) (* z 1.30)))
     (* 0.25 (noise (* x 2.70) (* z 2.70)))))
(define (lerp3 a b t)
  (vector (+ (vx a) (* (- (vx b) (vx a)) t))
          (+ (vy a) (* (- (vy b) (vy a)) t))
          (+ (vz a) (* (- (vz b) (vz a)) t))))
(define col-lo (vector 0.12 0.02 0.30))   ; valleys: indigo
(define col-md (vector 0.90 0.10 0.62))   ; slopes:  magenta
(define col-hi (vector 0.45 0.95 1.00))   ; peaks:   cyan-white
(define (height-colour t)
  (if (< t 0.5) (lerp3 col-lo col-md (* t 2.0))
                (lerp3 col-md col-hi (* (- t 0.5) 2.0))))

;; --- grid geometry ---------------------------------------------------------
(define NX 54) (define NY 38)
(define NN (* NX NY 4))
(define AMP 0.52) (define FreqX 5.0) (define FreqZ 6.0) (define LIFT 0.010)
(define usx (/ 1.0 NX)) (define vsy (/ 1.0 NY))
;; MakePlane emits 4 unshared verts per quad; recover each vert's grid (u,v).
(define (vert-u i)
  (let ((gx (quotient (quotient i 4) NY)) (c (modulo i 4)))
    (+ (/ (exact->inexact gx) NX) (if (or (= c 1) (= c 2)) usx 0.0))))
(define (vert-v i)
  (let ((gy (modulo (quotient i 4) NY)) (c (modulo i 4)))
    (+ (/ (exact->inexact gy) NY) (if (or (= c 2) (= c 3)) vsy 0.0))))

;; --- persistent flight state (retained: top-level runs once) ---------------
;; #(steer-target scroll-trim speed  cam-x  cam-vx) — cam-x chases the target with
;; a critically-damped spring so discrete key steps read as a smooth banking glide.
(define st (vector 0.0 0.0 1.0 0.0 0.0))
(define hs (make-vector NN 0.0))   ; scratch height buffer, reused each frame

;; --- build the two layers ONCE ---------------------------------------------
(define terr (build-seg-plane NX NY))
(with-primitive terr
  (hint-unlit) (hint-vertcols) (backfacecull #f)
  (rotate (vector -90 0 0))       ; lay flat: local z (height) -> world Y (up)
  (scale (vector 80 90 18)))      ; wide X, deep Z, tall height
(define grid (build-seg-plane NX NY))
(with-primitive grid
  (hint-unlit) (hint-wire) (hint-solid #f) (backfacecull #f)
  (wire-colour (vector 0.20 1.0 0.95)) (wire-opacity 0.45)
  (rotate (vector -90 0 0))
  (scale (vector 80 90 18)))

;; --- per-frame: steer, displace both layers, drive the camera --------------
(every-frame
  (let* ((scroll (+ (* (time) 1.4) (vector-ref st 1))))
    ;; continuous hold-to-move: key-down? is the LIVE physical state, so holding a
    ;; key accelerates every frame (no key-repeat stutter). W/S trim speed; A/D push
    ;; the steer target, which the damped spring below chases into a smooth glide.
    (when (key-down? "w") (vector-set! st 2 (min 4.0 (+ (vector-ref st 2) 0.06))))
    (when (key-down? "s") (vector-set! st 2 (max 0.0 (- (vector-ref st 2) 0.06))))
    (when (key-down? "a") (vector-set! st 0 (- (vector-ref st 0) 0.6)))
    (when (key-down? "d") (vector-set! st 0 (+ (vector-ref st 0) 0.6)))
    ;; smooth cam-x toward the steer target with a damped spring (mass-spring):
    ;; vx += (target - x)*stiff - vx*damp ; x += vx. Ease-in/out glide + settle,
    ;; instead of snapping per keystroke. Drive BOTH terrain sample and camera off it.
    (let* ((x  (vector-ref st 3)) (vx (vector-ref st 4))
           (vx2 (+ vx (* (- (vector-ref st 0) x) 0.045) (* vx -0.28))))
      (vector-set! st 4 vx2) (vector-set! st 3 (+ x vx2)))
    ;; noise once per vertex -> hs (the expensive part), reused by both layers
    (do ((i 0 (+ i 1))) ((= i NN))
      (vector-set! hs i (fbm (+ (* (vert-u i) FreqX) (* (vector-ref st 3) 0.05))
                             (+ (* (vert-v i) FreqZ) scroll))))
    ;; layer 1: solid, height-tinted fill
    (with-primitive terr
      (do ((i 0 (+ i 1))) ((= i NN))
        (let* ((raw (vector-ref hs i))
               (h   (* AMP (- raw 0.85)))
               (tt  (max 0.0 (min 1.0 (/ (- raw 0.50) 0.80)))))
          (pdata-set! "p" i (vector (- (vert-u i) 0.5) (- (vert-v i) 0.5) h))
          (pdata-set! "c" i (height-colour tt)))))
    ;; layer 2: neon wireframe grid, lifted just above so it doesn't z-fight
    (with-primitive grid
      (do ((i 0 (+ i 1))) ((= i NN))
        (pdata-set! "p" i (vector (- (vert-u i) 0.5) (- (vert-v i) 0.5)
                                  (+ (* AMP (- (vector-ref hs i) 0.85)) LIFT)))))
    ;; FPS camera: skim forward, look into the distance, gentle bob; bank into turns
    (let* ((camx (vector-ref st 3)) (bank (* (vector-ref st 4) 6.0))
           (eyeH (+ 9.5 (* 1.1 (sin (* (time) 0.7)))))
           (up   (vnorm (vector (* (sin (* bank 0.0175)) 1.0) (cos (* bank 0.0175)) 0.0)))
           (eye  (vector (* camx 0.42) eyeH 40.0))
           (tgt  (vector (* camx 0.30) (- eyeH 4.6) -42.0)))
      (set-camera-transform (look-at eye tgt up)))
    ;; integrate the speed trim into the persistent scroll (speed 1.0 = pure time)
    (vector-set! st 1 (+ (vector-ref st 1) (* 0.05 (- (vector-ref st 2) 1.0))))))
