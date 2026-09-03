; HARPOON — missile engagement, classic naval-tactical-display UI.
; Rectangular plot with a ticked ruler border + axis numbers, top status bar,
; boxed NATO contact symbols with white track-ID tags and cyan data blocks,
; red hostile carets, magenta engagement lines with range labels, and a legend
; box. Missiles fly between contacts leaving fading tracks. CRT + composite NTSC.
;
; Retained mode: static chrome (frame, ruler, legend) built ONCE; the sim state
; (contacts, missiles) lives in mutable vars and only the moving marks + HUD are
; rebuilt each frame (tracked in *dyn*). Needs the Racket host.

(clear)
(anti-alias #t)
(set-window-size 540 960)                     ; 9:16 vertical (grabs 1080x1920 on retina)

(ntsc #t)
(ntsc-noise 6)
(ntsc-saturation 20)
(ntsc-scanlines #f)

(define PI 3.14159265358979)

;; ---- 2D helpers (world XZ plane; Y up; map is flat) -------------------------
(define (v2 x z) (vector x 0.02 z))
(define (v+ a b) (vector (+ (vx a) (vx b)) 0.02 (+ (vz a) (vz b))))
(define (v- a b) (vector (- (vx a) (vx b)) 0.02 (- (vz a) (vz b))))
(define (vs a s) (vector (* (vx a) s) 0.02 (* (vz a) s)))
(define (vlen a) (sqrt (+ (* (vx a) (vx a)) (* (vz a) (vz a)))))
(define (vnrm a) (let ((l (vlen a))) (if (> l 1e-6) (vs a (/ 1.0 l)) (v2 0 0))))
(define (deg r) (* r 57.2957795))
(define (dir-deg d) (deg (atan (vx d) (- (vz d)))))
(define (bearing d) (modulo (inexact->exact (round (dir-deg d))) 360))
(define (pad n w) (let ((s (number->string n))) (string-append (make-string (max 0 (- w (string-length s))) #\0) s)))

(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))

;; ---- palette (blue tactical display) ----------------------------------------
(define C-FRAME (vector 0.45 0.62 1.00))      ; blue border/ruler
(define C-GRIDL (vector 0.14 0.22 0.44))      ; faint interior grid
(define C-OWN   (vector 0.55 1.00 1.00))      ; friendly = cyan
(define C-HOST  (vector 1.00 0.45 0.38))      ; hostile  = red
(define C-ID    (vector 1.00 1.00 1.00))      ; white track-id tags
(define C-DATA  (vector 0.60 0.95 1.00))      ; cyan data blocks
(define C-ENG   (vector 1.00 0.40 0.85))      ; magenta engagement lines
(define C-SAM   (vector 0.80 1.00 0.95))      ; own missile
(define C-VAMP  (vector 1.00 0.42 0.35))      ; inbound missile
(define C-STAT  (vector 0.70 0.95 1.00))      ; status bar
(define C-DRONE (vector 1.00 0.82 0.28))      ; amber recon-drone swarm
(define C-LINK  (vector 0.95 0.72 0.22))      ; amber drone comms-link

;; plot half-extents (portrait 9:16)
(define HW 8.4)
(define HH 15.0)

;; ---- primitive builders -----------------------------------------------------
(define (mk-line a b col w)
  (let* ((mid (vs (v+ a b) 0.5)) (d (v- b a)) (len (max 0.001 (vlen d))))
    (let ((p (build-cube)))
      (with-primitive p
        (hint-solid) (hint-unlit) (colour col)
        (translate mid) (rotate (vector 0 (deg (atan (vx d) (vz d))) 0))
        (scale (vector (* 0.05 w) 0.02 len)))
      p)))
(define (mk-box pos sx sz rot col lw)           ; wire rectangle
  (let ((p (build-cube)))
    (with-primitive p
      (hint-solid #f) (hint-wire) (hint-unlit) (backfacecull #f)
      (line-width lw) (wire-colour col) (colour (vector 0 0 0))
      (translate pos) (rotate (vector 0 rot 0)) (scale (vector sx 0.01 sz)))
    p))
(define (mk-dot pos size col)
  (let ((p (build-cube)))
    (with-primitive p
      (hint-solid) (hint-unlit) (colour col)
      (translate pos) (scale (vector size 0.02 size)))
    p))
(define (mk-text s pos scl col)
  (if (= (string-length s) 0) -1
    (let ((p (build-text s)))
      (with-primitive p
        (hint-unlit) (colour col)
        (translate pos) (rotate (vector -90 0 0)) (scale (vector scl scl scl)))
      p)))

;; ---- STATIC chrome (built once) ---------------------------------------------
;; faint interior grid at a fixed world spacing -> SQUARE cells
(define GSTEP 2.1)
(let loop ((x (- HW))) (when (<= x (+ HW 0.001))
  (mk-line (vector x 0 (- HH)) (vector x 0 HH) C-GRIDL 1.4) (loop (+ x GSTEP))))
(let loop ((z (- HH))) (when (<= z (+ HH 0.001))
  (mk-line (vector (- HW) 0 z) (vector HW 0 z) C-GRIDL 1.4) (loop (+ z GSTEP))))
;; border frame
(mk-line (vector (- HW) 0 (- HH)) (vector HW 0 (- HH)) C-FRAME 3.0)
(mk-line (vector (- HW) 0 HH)     (vector HW 0 HH)     C-FRAME 3.0)
(mk-line (vector (- HW) 0 (- HH)) (vector (- HW) 0 HH) C-FRAME 3.0)
(mk-line (vector HW 0 (- HH))     (vector HW 0 HH)     C-FRAME 3.0)
;; ruler ticks every 5% (long at 25/50/75)
(let loop ((i 0))
  (when (<= i 20)
    (let* ((f (/ i 20.0)) (x (+ (- HW) (* f 2 HW))) (z (- HH (* f 2 HH)))
           (long (= 0 (modulo i 5))) (tl (if long 0.45 0.22)))
      (mk-line (vector x 0 (- HH)) (vector x 0 (- (- HH) tl)) C-FRAME 0.8)     ; top
      (mk-line (vector x 0 HH)     (vector x 0 (+ HH tl))     C-FRAME 0.8)     ; bottom
      (mk-line (vector (- HW) 0 z) (vector (- (- HW) tl) 0 z) C-FRAME 0.8)     ; left
      (mk-line (vector HW 0 z)     (vector (+ HW tl) 0 z)     C-FRAME 0.8))    ; right
    (loop (+ i 1))))
;; axis numbers: left (100 top .. 000 bottom), bottom (000 .. 100)
(let loop ((i 0))
  (when (<= i 4)
    (let* ((v (* i 25)) (f (/ i 4.0)) (zz (- HH (* f 2 HH))))
      ;; left axis numbers, just inside the left border
      (mk-text (pad v 3) (vector (+ (- HW) 0.15) 0 (+ zz 0.5)) 0.34 C-FRAME)
      ;; bottom axis numbers, just inside the bottom border
      (mk-text (pad v 3) (vector (- (+ (- HW) (* f 2 HW)) 0.6) 0 (- HH 0.3)) 0.34 C-FRAME))
    (loop (+ i 1))))
;; legend box (bottom-left, inside the plot)
(let ((lx (+ (- HW) 0.6)) (lz (- HH 0.6)))
  (mk-box (vector (+ lx 2.6) 0 (- lz 1.2)) 3.0 2.8 0 C-FRAME 1.0)
  (mk-dot (vector (+ lx 0.4) 0 (- lz 0.0)) 0.13 C-OWN)   (mk-text "FRIENDLY" (vector (+ lx 0.8) 0 (- lz 0.15)) 0.3 C-OWN)
  (mk-dot (vector (+ lx 0.4) 0 (- lz 0.6)) 0.13 C-HOST)  (mk-text "HOSTILE"  (vector (+ lx 0.8) 0 (- lz 0.75)) 0.3 C-HOST)
  (mk-dot (vector (+ lx 0.4) 0 (- lz 1.2)) 0.13 C-SAM)   (mk-text "SAM"      (vector (+ lx 0.8) 0 (- lz 1.35)) 0.3 C-SAM)
  (mk-dot (vector (+ lx 0.4) 0 (- lz 1.8)) 0.13 C-VAMP)  (mk-text "VAMPIRE"  (vector (+ lx 0.8) 0 (- lz 1.95)) 0.3 C-VAMP)
  (mk-dot (vector (+ lx 0.4) 0 (- lz 2.4)) 0.13 C-DRONE) (mk-text "DRONE"    (vector (+ lx 0.8) 0 (- lz 2.55)) 0.3 C-DRONE))

;; ---- SIM STATE --------------------------------------------------------------
;; contact = [px pz vx vz side kind alive id]  side:'own 'host  kind:'ship 'air
;; missile = [px pz vx vz side alive age trail]
(define *contacts* '())
(define *missiles* '())
(define *flashes* '())
(define *lt* -1.0) (define *spawn-t* 0.0) (define *sam-t* 0.0) (define *vamp-t* 0.0)
(define *kills* 0) (define *nid* 0)
(define *dyn* '())
(define (dyn! id) (when (>= id 0) (set! *dyn* (cons id *dyn*))) id)
(define (clear-dyn!) (for-each (lambda (id) (when (>= id 0) (destroy id))) *dyn*) (set! *dyn* '()))
(define (rnd a b) (+ a (* (- b a) (random))))
(define (next-id!) (set! *nid* (+ *nid* 1)) *nid*)
(define (take-n lst n) (if (or (= n 0) (null? lst)) '() (cons (car lst) (take-n (cdr lst) (- n 1)))))

(define (cx c) (vector-ref c 0)) (define (cz c) (vector-ref c 1))
(define (cpos c) (v2 (cx c) (cz c)))
(define (cvel c) (v2 (vector-ref c 2) (vector-ref c 3)))
(define (calive c) (vector-ref c 6))

;; seed own force: a small CSG near centre
(define (add-own! x z)
  (set! *contacts* (cons (vector x z (rnd -0.05 0.05) (rnd -0.05 0.05) 'own 'ship #t (next-id!)) *contacts*)))
(add-own! -1.5 1.0) (add-own! 1.2 -0.6) (add-own! 0.2 2.4)

;; pre-populate the plot with spread hostiles so it reads busy on load
(define (add-host! x z vx vz kind)
  (set! *contacts* (cons (vector x z vx vz 'host kind #t (next-id!)) *contacts*)))
(add-host! -6.0 -8.0  0.35  0.30 'ship)
(add-host!  6.5  9.0 -0.30 -0.35 'ship)
(add-host!  4.0 -11.0 -0.20 0.45 'air)
(add-host! -5.5 10.0  0.40 -0.30 'ship)
(add-host!  7.0 -3.0 -0.45  0.15 'air)

;; ---- DRONE SWARM (boids) ----------------------------------------------------
;; drone = [px pz vx vz]  amber recon flock; wanders the plot, fires at hostiles
(define *drones* '())
(define *drone-t* 0.0)
(define DR-SPD 2.6)      ; cruise speed (renormalised each frame)
(define DR-SEP 1.4)      ; separation radius
(define DR-NBR 5.0)      ; align/cohere neighbour radius
(define (add-drone! x z) (set! *drones* (cons (vector x z (rnd -0.5 0.5) (rnd -0.5 0.5)) *drones*)))
(let loop ((i 0)) (when (< i 10) (add-drone! (rnd -3.5 3.5) (rnd -4.0 4.0)) (loop (+ i 1))))

(define (update-drones! dt)
  (for-each (lambda (d)
    (let ((px (vector-ref d 0)) (pz (vector-ref d 1))
          (sepx 0.0)(sepz 0.0)(alx 0.0)(alz 0.0)(cox 0.0)(coz 0.0)(nn 0))
      (for-each (lambda (o)
        (unless (eq? o d)
          (let* ((dx (- (vector-ref o 0) px)) (dz (- (vector-ref o 1) pz))
                 (dist (sqrt (+ (* dx dx) (* dz dz)))))
            (when (< dist DR-NBR)
              (set! nn (+ nn 1))
              (set! alx (+ alx (vector-ref o 2))) (set! alz (+ alz (vector-ref o 3)))
              (set! cox (+ cox (vector-ref o 0))) (set! coz (+ coz (vector-ref o 1))))
            (when (and (< dist DR-SEP) (> dist 1e-4))
              (set! sepx (- sepx (/ dx dist))) (set! sepz (- sepz (/ dz dist)))))))
        *drones*)
      (let ((ax (* 2.2 sepx)) (az (* 2.2 sepz)))              ; separation
        (when (> nn 0)
          (set! ax (+ ax (* 0.7 (- (/ alx nn) (vector-ref d 2)))     ; alignment
                        (* 0.14 (- (/ cox nn) px))))                 ; cohesion
          (set! az (+ az (* 0.7 (- (/ alz nn) (vector-ref d 3)))
                        (* 0.14 (- (/ coz nn) pz)))))
        (set! ax (+ ax (rnd -0.7 0.7))) (set! az (+ az (rnd -0.7 0.7)))  ; wander
        (when (> px (- HW 1.5)) (set! ax (- ax 7.0)))          ; steer inside plot
        (when (< px (- 1.5 HW)) (set! ax (+ ax 7.0)))
        (when (> pz (- HH 1.5)) (set! az (- az 7.0)))
        (when (< pz (- 1.5 HH)) (set! az (+ az 7.0)))
        (vector-set! d 2 (+ (vector-ref d 2) (* ax dt)))
        (vector-set! d 3 (+ (vector-ref d 3) (* az dt)))
        (let ((s (sqrt (+ (* (vector-ref d 2) (vector-ref d 2))
                          (* (vector-ref d 3) (vector-ref d 3))))))
          (when (> s 1e-4)
            (vector-set! d 2 (* (vector-ref d 2) (/ DR-SPD s)))
            (vector-set! d 3 (* (vector-ref d 3) (/ DR-SPD s)))))
        (vector-set! d 0 (+ px (* (vector-ref d 2) dt)))
        (vector-set! d 1 (+ pz (* (vector-ref d 3) dt))))))
    *drones*)
  ;; a random drone occasionally fires at the nearest hostile
  (set! *drone-t* (+ *drone-t* dt))
  (when (and (> *drone-t* 1.6) (pair? *drones*) (< (length *missiles*) 12))
    (set! *drone-t* 0.0)
    (let* ((d (list-ref *drones* (inexact->exact (floor (rnd 0 (length *drones*))))))
           (dp (v2 (vector-ref d 0) (vector-ref d 1)))
           (tgt (nearest 'host dp)))
      (when (and tgt (< (random) 0.75)) (launch! dp (cpos tgt) 'own)))))

(define (spawn-hostile!)
  (let* ((ang (rnd 0 (* 2 PI)))
         (px (* (- HW 1.0) (sin ang))) (pz (* (- HH 1.0) (cos ang)))
         (own (find-own)) (tgt (if own (cpos own) (v2 0 0)))
         (dir (vnrm (v- tgt (v2 px pz)))) (spd (rnd 0.5 0.9)))
    (set! *contacts*
      (cons (vector px pz (* (vx dir) spd) (* (vz dir) spd) 'host
                    (if (< (random) 0.3) 'air 'ship) #t (next-id!)) *contacts*))))

(define (find-own)
  (let loop ((cs *contacts*)) (cond ((null? cs) #f)
    ((and (calive (car cs)) (eq? (vector-ref (car cs) 4) 'own)) (car cs))
    (else (loop (cdr cs))))))
(define (nearest side pos)
  (let loop ((cs *contacts*) (best #f) (bd 1e9))
    (if (null? cs) best
      (let ((c (car cs)))
        (if (and (calive c) (eq? (vector-ref c 4) side)
                 (< (vlen (v- (cpos c) pos)) bd))
            (loop (cdr cs) c (vlen (v- (cpos c) pos)))
            (loop (cdr cs) best bd))))))

(define (launch! from to side)
  (let* ((dir (vnrm (v- to from))) (spd (if (eq? side 'own) 6.0 4.2)))
    (set! *missiles* (cons (vector (vx from) (vz from) (* (vx dir) spd) (* (vz dir) spd)
                                   side #t 0.0 '()) *missiles*))))

;; ---- SIM UPDATE -------------------------------------------------------------
(define (update! dt)
  (set! *spawn-t* (+ *spawn-t* dt))
  (when (and (> *spawn-t* 3.0)
             (< (length (filter (lambda (c) (eq? (vector-ref c 4) 'host)) *contacts*)) 7))
    (set! *spawn-t* 0.0) (spawn-hostile!))
  ;; move contacts
  (for-each (lambda (c)
    (when (calive c)
      (vector-set! c 0 (+ (cx c) (* (vector-ref c 2) dt)))
      (vector-set! c 1 (+ (cz c) (* (vector-ref c 3) dt)))
      (when (eq? (vector-ref c 4) 'own)                      ; own drifts, stays in box
        (when (> (abs (cx c)) (- HW 3)) (vector-set! c 2 (- (vector-ref c 2))))
        (when (> (abs (cz c)) (- HH 3)) (vector-set! c 3 (- (vector-ref c 3)))))))
    *contacts*)
  ;; own auto-launch SAM at nearest hostile
  (set! *sam-t* (+ *sam-t* dt))
  (let* ((own (find-own)) (tgt (and own (nearest 'host (cpos own)))))
    (when (and own tgt (> *sam-t* 1.3) (< (length *missiles*) 12))
      (set! *sam-t* 0.0) (launch! (cpos own) (cpos tgt) 'own)))
  ;; hostiles fire VAMPIREs at own
  (set! *vamp-t* (+ *vamp-t* dt))
  (when (> *vamp-t* 2.4)
    (set! *vamp-t* 0.0)
    (let ((own (find-own)))
      (when own
        (for-each (lambda (c)
          (when (and (calive c) (eq? (vector-ref c 4) 'host) (eq? (vector-ref c 5) 'ship)
                     (< (vlen (v- (cpos c) (cpos own))) 10.0) (< (random) 0.5))
            (launch! (cpos c) (cpos own) 'host))) *contacts*))))
  ;; missiles
  (for-each (lambda (m)
    (when (vector-ref m 5)
      (vector-set! m 6 (+ (vector-ref m 6) dt))
      (let* ((mp (v2 (vector-ref m 0) (vector-ref m 1)))
             (own? (eq? (vector-ref m 4) 'own))
             (tgt (if own? (nearest 'host mp) (find-own))))
        (when tgt
          (let ((dir (vnrm (v- (cpos tgt) mp))) (spd (if own? 6.0 4.2)))
            (vector-set! m 2 (* (vx dir) spd)) (vector-set! m 3 (* (vz dir) spd))))
        (vector-set! m 0 (+ (vector-ref m 0) (* (vector-ref m 2) dt)))
        (vector-set! m 1 (+ (vector-ref m 1) (* (vector-ref m 3) dt)))
        (vector-set! m 7 (cons (list (vector-ref m 0) (vector-ref m 1)) (vector-ref m 7)))
        (when (> (length (vector-ref m 7)) 14) (vector-set! m 7 (take-n (vector-ref m 7) 14)))
        (when (and tgt (< (vlen (v- (v2 (vector-ref m 0) (vector-ref m 1)) (cpos tgt))) 0.55))
          (vector-set! m 5 #f)
          (when (< (random) (if own? 1.0 0.55))            ; hostile PDMS sometimes miss
            (vector-set! tgt 6 #f)
            (when own? (set! *kills* (+ *kills* 1)))
            (set! *flashes* (cons (list (vector-ref m 0) (vector-ref m 1) 0.0) *flashes*))))
        (when (or (> (abs (vector-ref m 0)) (+ HW 2)) (> (abs (vector-ref m 1)) (+ HH 2)))
          (vector-set! m 5 #f)))))
    *missiles*)
  (update-drones! dt)
  (set! *flashes* (map (lambda (f) (list (car f) (cadr f) (+ (caddr f) dt))) *flashes*))
  (set! *contacts* (filter calive *contacts*))
  (set! *missiles* (filter (lambda (m) (vector-ref m 5)) *missiles*))
  (set! *flashes*  (filter (lambda (f) (< (caddr f) 0.45)) *flashes*)))

;; ---- DRAW dynamic -----------------------------------------------------------
(define (draw-contact c)
  (let* ((p (cpos c)) (own (eq? (vector-ref c 4) 'own))
         (col (if own C-OWN C-HOST)) (spd (* (vlen (cvel c)) 100)))
    ;; symbol
    (if own
        (begin (dyn! (mk-box p 0.85 0.85 0 col 2.0)) (dyn! (mk-dot p 0.18 col)))
        (begin (dyn! (mk-box p 0.65 0.65 45 col 2.0))                 ; diamond
               (when (eq? (vector-ref c 5) 'air) (dyn! (mk-dot p 0.16 col)))))
    ;; velocity leader
    (dyn! (mk-line p (v+ p (vs (vnrm (cvel c)) 1.6)) col 1.1))
    ;; white boxed track ID above
    (let ((idp (v+ p (v2 0 -1.5))))
      (dyn! (mk-box idp 1.0 0.42 0 C-ID 1.3))
      (dyn! (mk-text (string-append "1D" (pad (vector-ref c 7) 2))
                     (v+ idp (v2 -0.66 0.2)) 0.46 C-ID)))
    ;; cyan data block to the right
    (dyn! (mk-text (string-append "K" (pad (bearing (cvel c)) 3))
                   (v+ p (v2 1.05 -0.15)) 0.44 C-DATA))
    (dyn! (mk-text (string-append "Y" (pad (inexact->exact (round spd)) 3))
                   (v+ p (v2 1.05 0.5)) 0.44 C-DATA))))

(define (draw!)
  ;; engagement lines: each SAM to its current target (magenta + range label)
  (let ((own (find-own)))
    (for-each (lambda (m)
      (when (and (vector-ref m 5) (eq? (vector-ref m 4) 'own))
        (let* ((mp (v2 (vector-ref m 0) (vector-ref m 1)))
               (tgt (nearest 'host mp)))
          (when tgt
            (dyn! (mk-line mp (cpos tgt) C-ENG 0.7))
            (let ((mid (vs (v+ mp (cpos tgt)) 0.5))
                  (rng (/ (round (* (vlen (v- (cpos tgt) mp)) 10)) 100.0)))
              (dyn! (mk-text (number->string rng) (v+ mid (v2 0.1 -0.25)) 0.28 C-ENG))))))) *missiles*))
  (for-each draw-contact *contacts*)
  ;; drone swarm: square box + one heading line out of the box centre
  (for-each (lambda (d)
    (let* ((p (v2 (vector-ref d 0) (vector-ref d 1)))
           (f (vnrm (v2 (vector-ref d 2) (vector-ref d 3)))))    ; unit heading
      (dyn! (mk-box p 0.4 0.4 0 C-DRONE 1.4))                    ; square
      (dyn! (mk-line p (v+ p (vs f 0.95)) C-DRONE 1.0))))        ; heading line
    *drones*)
  ;; intermittent comms mesh: a random subset of near neighbour pairs, each frame
  (let loop ((ds *drones*))
    (unless (null? ds)
      (let ((a (car ds)))
        (for-each (lambda (b)
          (let* ((dx (- (vector-ref b 0) (vector-ref a 0)))
                 (dz (- (vector-ref b 1) (vector-ref a 1)))
                 (dist (sqrt (+ (* dx dx) (* dz dz)))))
            (when (and (< dist 5.5) (< (random) 0.14))
              (dyn! (mk-line (v2 (vector-ref a 0) (vector-ref a 1))
                             (v2 (vector-ref b 0) (vector-ref b 1)) C-LINK 2.2)))))
          (cdr ds)))
      (loop (cdr ds))))
  ;; missiles + trails
  (for-each (lambda (m)
    (let* ((p (v2 (vector-ref m 0) (vector-ref m 1)))
           (col (if (eq? (vector-ref m 4) 'own) C-SAM C-VAMP)))
      (let loop ((ts (vector-ref m 7)) (prev p))
        (unless (null? ts)
          (let ((tp (v2 (caar ts) (cadar ts)))) (dyn! (mk-line prev tp col 0.5)) (loop (cdr ts) tp))))
      (dyn! (mk-dot p 0.12 col)))) *missiles*)
  ;; flashes
  (for-each (lambda (f)
    (dyn! (mk-box (v2 (car f) (cadr f)) (+ 0.2 (* 1.6 (caddr f))) (+ 0.2 (* 1.6 (caddr f))) 45 C-VAMP 1.3)))
    *flashes*))

;; ---- HUD: status bar + counts -----------------------------------------------
(define (hud!)
  (let ((host (length (filter (lambda (c) (eq? (vector-ref c 4) 'host)) *contacts*)))
        (vamp (length (filter (lambda (m) (eq? (vector-ref m 4) 'host)) *missiles*))))
    (dyn! (mk-text (string-append "HARPOON  ACTIV " (pad host 3) "  DRN " (pad (length *drones*) 2)
                                  "  KILLS " (pad *kills* 3))
                   (vector (+ (- HW) 0.5) 0 (+ (- HH) 0.9)) 0.44 C-STAT))
    (dyn! (mk-text (string-append "WATCH R4D G3D   T+" (pad (inexact->exact (floor (time))) 4))
                   (vector (+ (- HW) 0.5) 0 (- HH 0.9)) 0.4 C-STAT))
    (when (> vamp 0)
      (dyn! (mk-text (string-append "*** VAMPIRE INBOUND x" (number->string vamp) " ***")
                     (vector (+ (- HW) 0.5) 0 (- HH 1.7)) 0.5 C-VAMP)))))

;; ---- CRT post shader --------------------------------------------------------
(define crt "
uniform sampler2D tex;
uniform float time;
uniform vec2 resolution;
varying vec2 uv;
vec2 curve(vec2 p){ p=p*2.0-1.0; vec2 o=abs(p.yx)/vec2(7.0,6.0); p=p+p*o*o; return p*0.5+0.5; }
void main(){
  vec2 u=curve(uv);
  if(u.x<0.0||u.x>1.0||u.y<0.0||u.y>1.0){ gl_FragColor=vec4(0.0,0.0,0.0,1.0); return; }
  float ca=0.0009;
  vec3 col=vec3(texture2D(tex,u+vec2(ca,0.0)).r, texture2D(tex,u).g, texture2D(tex,u-vec2(ca,0.0)).b);
  col*=2.4;                                              // overall gain (bright tactical CRT)
  col=pow(col, vec3(0.85));                              // lift low end
  col*=0.95+0.05*sin(u.y*1400.0);                        // subtle scanlines
  col*=1.03+0.02*sin(time*5.0);                          // gentle flicker
  float d=length(u-0.5); col*=1.0-d*d*0.20;              // light vignette
  gl_FragColor=vec4(col,1.0);
}")

;; ---- camera: top-down ------------------------------------------------------
(define (map-camera)
  (set-fov 12)
  (let* ((zoom (/ (camera-dist) 10)) (d (* 162 zoom)))   ; framed: whole plot fills the 9:16 view
    (set-camera-transform (look-at (vector 0 d 0) (vector 0 0 0) (vector 0 0 -1)))))

(retained)
(every-frame
  (begin
    (clear-dyn!)
    (let ((dt (if (< *lt* 0) 0.016 (min 0.05 (- (time) *lt*)))))
      (set! *lt* (time)) (update! dt))
    (draw!) (hud!) (map-camera)
    (post-shader crt)))
