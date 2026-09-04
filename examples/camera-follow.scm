(clear)
(retained)
(define (frac x) (- x (floor x)))
(define (hash01 n) (frac (* (sin (* (+ n 1) 78.233)) 43758.5453)))
(define (int-range a b) (if (> a b) '() (cons a (int-range (+ a 1) b))))
(define (v- a b) (vector (- (vx a)(vx b)) (- (vy a)(vy b)) (- (vz a)(vz b))))
(define (v+ a b) (vector (+ (vx a)(vx b)) (+ (vy a)(vy b)) (+ (vz a)(vz b))))
(define (v* a s) (vector (* (vx a) s) (* (vy a) s) (* (vz a) s)))
(define (vdot a b) (+ (* (vx a)(vx b)) (* (vy a)(vy b)) (* (vz a)(vz b))))
(define (vcross a b) (vector (- (* (vy a)(vz b)) (* (vz a)(vy b))) (- (* (vz a)(vx b)) (* (vx a)(vz b))) (- (* (vx a)(vy b)) (* (vy a)(vx b)))))
(define (vnorm a) (let ((m (sqrt (vdot a a)))) (if (< m 1e-9) a (v* a (/ 1.0 m)))))
(define (look-at-view eye tgt up)
  (let* ((f (vnorm (v- tgt eye))) (s (vnorm (vcross f up))) (u (vcross s f)) (m (make-vector 16 0.0)))
    (vector-set! m 0 (vx s)) (vector-set! m 4 (vy s)) (vector-set! m 8 (vz s))
    (vector-set! m 1 (vx u)) (vector-set! m 5 (vy u)) (vector-set! m 9 (vz u))
    (vector-set! m 2 (- (vx f))) (vector-set! m 6 (- (vy f))) (vector-set! m 10 (- (vz f)))
    (vector-set! m 12 (- (vdot s eye))) (vector-set! m 13 (- (vdot u eye))) (vector-set! m 14 (vdot f eye)) (vector-set! m 15 1.0) m))
(define GH 22.0) (define GS 1.6)
(for-each (lambda (k) (let ((c (* k GS)))
  (with-state (hint-unlit) (colour (vector 0.12 0.3 0.42)) (translate (vector 0 0 c)) (scale (vector (* 2 GH) 0.02 0.05)) (build-cube))
  (with-state (hint-unlit) (colour (vector 0.12 0.3 0.42)) (translate (vector c 0 0)) (scale (vector 0.05 0.02 (* 2 GH))) (build-cube))))
  (int-range (inexact->exact (- (floor (/ GH GS)))) (inexact->exact (floor (/ GH GS)))))
(for-each (lambda (i)
    (let* ((ang (* 6.2831853 (hash01 (+ i 5)))) (r (+ 9.0 (* 10.0 (hash01 (+ i 33))))))
      (with-state (hint-solid #t)
        (colour (vector (+ 0.30 (* 0.55 (hash01 (+ i 1)))) (+ 0.30 (* 0.55 (hash01 (+ i 50)))) (+ 0.40 (* 0.55 (hash01 (+ i 90))))))
        (translate (vector (* r (cos ang)) (+ 0.5 (* 1.8 (hash01 (+ i 20)))) (* r (sin ang))))
        (let ((s (+ 0.5 (* 1.1 (hash01 (+ i 7)))))) (scale (vector s s s)))
        (build-cube))))
  (int-range 0 43))
;; --- the car: body (orange) + nose (white) parented to a steer node ---
(define *hero* (build-node))
(with-state (parent *hero*) (hint-solid #t) (colour (vector 1 0.55 0.1))
            (scale (vector 1.0 0.5 1.6)) (build-cube))       ; car body
(with-state (parent *hero*) (hint-unlit) (colour (vector 1 1 1))
            (translate (vector 0 0 0.95)) (scale (vector 0.5 0.35 0.4)) (build-cube)) ; nose

;; arrow keys -> control-char slots 1..4 (see FluxusComponent timer poll)
(define (key-left?)  (key-down? 1))
(define (key-right?) (key-down? 2))
(define (key-up?)    (key-down? 3))
(define (key-down-arrow?) (key-down? 4))

;; car state (persists in retained closures)
(define *pos* (box (vector 0.0 1.0 0.0)))   ; x,y,z  (y fixed = flat, no naik-turun)
(define *hdg* (box 1.5708))                  ; heading radians (0=+X); start facing +Z
(define *spd* (box 0.0))                      ; scalar speed (signed: negative = reverse)
(define *eye* (box (vector 0.0 6.0 -12.0)))
(define *pt*  (box (time)))                   ; prev frame time (for dt)

(hide-editor)   ; sketch owns the keyboard so arrow polling is live
(show-tweaks)
(every-frame
  (let* ((accel  (tweak "accel"       14.0 2.0 40.0))   ; throttle force
         (turn   (tweak "turn rate"    2.2 0.5 6.0))     ; steer speed (rad/s at full speed)
         (fric   (tweak "friction"     2.0 0.0 8.0))     ; speed decay toward 0
         (maxspd (tweak "max speed"   12.0 2.0 30.0))
         (chase  (tweak "chase dist"   8.0 2.0 20.0))    ; trail distance behind car
         (height (tweak "chase height" 3.2 0.0 10.0))    ; eye lift above car
         (drag   (tweak "chase drag"   0.08 0.01 1.0))   ; 0=floaty eye, 1=rigid snap
         (now (time)) (dt (min 0.05 (max 0.0 (- now (unbox *pt*)))))
         (spd (unbox *spd*)) (hdg (unbox *hdg*)) (pos (unbox *pos*))
         ;; throttle: up=forward, down=reverse
         (thr (+ (if (key-up? ) 1.0 0.0) (if (key-down-arrow?) -1.0 0.0)))
         (spd1 (+ spd (* thr accel dt)))
         ;; friction pulls magnitude toward 0
         (mag (- (abs spd1) (* fric dt)))
         (spd2 (if (<= mag 0.0) 0.0 (* mag (if (< spd1 0.0) -1.0 1.0))))
         (spd3 (max (- maxspd) (min maxspd spd2)))
         ;; steering scaled by speed (parked car can't spin; reverse flips it, car-like)
         (steer (+ (if (key-left? ) -1.0 0.0) (if (key-right?) 1.0 0.0)))
         (hdg2 (+ hdg (* steer turn dt (/ spd3 maxspd))))
         (dir (vector (cos hdg2) 0.0 (sin hdg2)))
         (pos2 (v+ pos (v* dir (* spd3 dt)))))
    (set-box! *spd* spd3) (set-box! *hdg* hdg2) (set-box! *pos* pos2) (set-box! *pt* now)
    ;; place + aim the car along its heading
    (with-primitive *hero* (identity) (translate pos2))
    (node-look-at *hero* (v+ pos2 dir))
    ;; chase cam: trail behind heading, above, drag-smoothed eye
    (let* ((want (v+ (v- pos2 (v* dir chase)) (vector 0 height 0)))
           (eye (v+ (unbox *eye*) (v* (v- want (unbox *eye*)) drag))))
      (set-box! *eye* eye)
      (set-camera-transform (look-at-view eye pos2 (vector 0 1 0))))))
