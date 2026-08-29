; ISO INDUSTRIAL ZONE / NERV TACTICAL DISPLAY — a generative wireframe
; industrial site, Evangelion MAGI-readout style: hidden-line wireframe
; structures (solid faces painted black so they occlude) in NERV orange + acid
; green with rare warning-red / Eva-01 purple units. Cooling towers, reactor
; containment domes, smokestacks with rising smoke, cryo tank farms, gas
; holders, turbine halls, switchyards — and a high-voltage pylon line with
; sagging catenary conductors crossing the site. Near-orthographic isometric
; view, slow auto-rotation, mouse-wheel zoom ((camera-dist)); the camera
; target tweens to each captioned structure. Service traffic runs the roads
; as fading light trails (position sampled back through time — stateless).
; Technical-drawing captions (kinked leader + typewriter title) evolve every
; few seconds. The whole frame passes through a glowing CRT post shader:
; bloom, scanlines, aperture grille, curvature, chromatic aberration,
; vignette, flicker. Load via CLI or Ctrl+E.

(clear)
(set-window-size 540 960)
(hide-editor)
(anti-alias #t)
(background (vector 0.004 0.006 0.010))

(define TWO-PI 6.2831853)
(define (fract x) (- x (floor x)))
(define (hsh i) (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453)))

;; ---- grid config -----------------------------------------------------------
(define GRID 9)                  ; blocks per side
(define CELL 1.3)                ; block pitch
(define HALF (* 0.5 GRID CELL))
(define SPAN (* GRID CELL))
(define BW   0.86)               ; structure footprint (< CELL leaves roads)
(define CARY 0.06)               ; vehicle height above the road

;; ---- palette: NERV tactical readout ----------------------------------------
(define C-BLACK  (vector 0.012 0.013 0.020))  ; hidden-line fill (occluding)
(define C-SLAB   (vector 0.010 0.011 0.016))
(define C-STREET (vector 0.30 0.13 0.02))     ; dim orange grid lines
(define C-AVENUE (vector 0.95 0.42 0.05))     ; NERV orange service roads
(define C-EDGE   (vector 1.00 0.50 0.08))     ; default wire: NERV orange
(define C-GREEN  (vector 0.45 1.00 0.25))     ; MAGI acid green
(define C-RED    (vector 1.00 0.12 0.10))     ; ALERT red
(define C-PURPLE (vector 0.62 0.35 1.00))     ; Eva-01 purple
(define C-SMOKE  (vector 0.55 0.58 0.62))     ; steam/smoke gray
;; wire colour per structure: mostly orange, some green, rare red/purple
(define (wire-col r)
  (cond ((< r 0.55) C-EDGE)
        ((< r 0.85) C-GREEN)
        ((< r 0.93) C-PURPLE)
        (else       C-RED)))

;; ---- persistent-scene registry (retained mode) -----------------------------
;; The static site/streets/pylons are built ONCE and persist. Only the animated
;; prims (smoke, beacons, core lights, traffic, powerline pulse, caption) are
;; rebuilt each frame — so each frame we destroy last frame's animated prims,
;; then rebuild. dyn! records every animated prim's id; clear-dyn! removes them.
(define *dyn* '())
(define (dyn! id) (set! *dyn* (cons id *dyn*)) id)
(define (clear-dyn!) (for-each destroy *dyn*) (set! *dyn* '()))

;; ---- iso camera: ~35 deg elevation, slow spin, target tweens to caption ----
(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))
(define (smoothstep x)
  (let ((c (max 0.0 (min 1.0 x)))) (* c c (- 3.0 (* 2.0 c)))))
(define (vmix a b k) (vadd a (vmul (vsub b a) k)))
;; camera target: tweens to each captioned structure (cap-focus is defined
;; below; resolved at call time). Focus pulls 40% of the way from site centre.
(define (camera-target)
  (let* ((slot (inexact->exact (floor (/ (time) CAPD))))
         (prog (fract (/ (time) CAPD)))
         (k    (smoothstep (/ prog 0.4)))         ; ease across slot change
         (f    (vmix (cap-focus (- slot 1)) (cap-focus slot) k)))
    (vmix (vector 0 0.55 0) f 0.4)))
(define (iso-camera)
  (set-fov 12)
  (let* ((el (+ 0.6155 (* 0.045 (sin (* (time) 0.11)))))  ; gentle breathe
         (az (* (time) 0.15))                             ; slow spin
         (d  (* 3.55 (camera-dist)))                      ; MOUSE WHEEL zoom
         (ce (cos el)) (se (sin el))
         (tgt (camera-target))
         (eye (vadd tgt
                (vector (* d ce (sin az)) (* d se) (* d ce (cos az))))))
    (set-camera-transform (look-at eye tgt (vector 0 1 0)))))

;; ---- small unlit helpers ---------------------------------------------------
(define (glow-box pos scl col op)
  (with-state
    (translate pos) (scale scl)
    (hint-solid) (hint-unlit)
    (colour col) (opacity op)
    (build-cube)))
;; hidden-line primitives: near-black occluding fill + bright unlit wire edges.
;; NB: wire-colour/wire-opacity only act on a GRABBED prim (build context
;; ignores them) — so build first, then grab to style.
(define (hl-style b wire lw)
  (with-primitive b
    (hint-solid) (hint-wire) (hint-unlit)
    (line-width lw)
    (colour C-BLACK)
    (wire-colour wire) (wire-opacity 1.0)))
(define (hl-box cx cz y0 w h d wire)
  (hl-style (with-state
              (translate (vector cx (+ y0 (* 0.5 h)) cz))
              (scale (vector w h d))
              (build-cube))
            wire 1.6))
(define (hl-cyl cx cz y0 h r wire rs)      ; cylinder base at y0, axis +Y
  (hl-style (with-state
              (translate (vector cx y0 cz))
              (build-cylinder h r 1 rs))
            wire 1.4))
(define (hl-sphere cx cy cz r wire)
  (hl-style (with-state
              (translate (vector cx cy cz))
              (scale (vector r r r))
              (build-sphere 10 7))
            wire 1.0))

;; ---- ground: dark slab + orange road grid ----------------------------------
(define (streets)
  (with-state
    (translate (vector 0 -0.06 0))
    (scale (vector (+ SPAN 1.4) 0.1 (+ SPAN 1.4)))
    (hint-solid) (hint-unlit)
    (colour C-SLAB)
    (build-cube))
  (let loop ((i 0))
    (when (<= i GRID)
      (let* ((x (+ (- HALF) (* i CELL)))
             (ave (or (= i 0) (= i GRID) (= 0 (modulo i 3))))
             (col (if ave C-AVENUE C-STREET))
             (w   (if ave 0.030 0.014))
             (op  (if ave 0.95 0.75)))
        (glow-box (vector x 0.004 0) (vector w 0.006 SPAN) col op)
        (glow-box (vector 0 0.004 x) (vector SPAN 0.006 w) col op)
        (when ave
          (glow-box (vector x 0.002 0) (vector 0.16 0.004 SPAN) col 0.10)
          (glow-box (vector 0 0.002 x) (vector SPAN 0.004 0.16) col 0.10)))
      (loop (+ i 1)))))

;; ---- per-cell derived values (shared by site + caption + camera) -----------
(define (cell-occ? id) (> (hsh (+ id 0.5)) 0.15))
(define (cell-base-h gx gz)
  (let* ((id (+ gx (* gz GRID)))
         (dx (- gx (* 0.5 (- GRID 1))))
         (dz (- gz (* 0.5 (- GRID 1))))
         (fall (exp (* -0.075 (+ (* dx dx) (* dz dz))))))
    (* (+ 0.30 (* 3.6 (expt (hsh (+ id 7)) 1.6)))
       (+ 0.42 (* 0.85 fall)))))
(define (cell-sty id) (hsh (+ id 31)))
(define (cell-top-h id h)                 ; drawn top height per structure type
  (let ((sty (cell-sty id)))
    (cond ((< sty 0.13) (* h 0.95))       ; cooling tower
          ((< sty 0.25) (+ (* h 0.5) (* BW 0.30)))   ; reactor + dome
          ((< sty 0.42) (* h 1.05))       ; smokestack chamber
          ((< sty 0.58) (* h 0.34))       ; tank farm
          ((< sty 0.70) (* BW 0.78))      ; gas holder
          ((< sty 0.88) (* h 0.38))       ; turbine hall
          (else         0.6))))           ; switchyard

;; ---- smoke/steam: rising fading wire puffs (stateless, time-driven) --------
(define (smoke cx cz y0 seed n spread rise)
  (let loop ((k 0))
    (when (< k n)
      (let* ((prog (fract (+ (* (time) 0.13) (* k (/ 1.0 n)) (hsh seed))))
             (y (+ y0 (* prog rise)))
             (r (+ 0.055 (* prog spread)))
             (op (* (- 1.0 prog) (min 1.0 (* prog 6.0)) 0.55))
             (wob (* 0.08 prog (sin (+ (* 1.7 (time)) (* 4 k) seed))))
             (s (dyn! (build-sphere 8 5))))
        (with-primitive s
          (translate (vector (+ cx wob) y (+ cz (* 0.5 wob))))
          (scale (vector r r r))
          (hint-solid #f) (hint-wire) (hint-unlit)
          (line-width 1.0)
          (wire-colour C-SMOKE) (wire-opacity op)))
      (loop (+ k 1)))))

;; ---- industrial structure builders -----------------------------------------
;; cooling tower: stacked cylinders approximate the hyperboloid; steam on top
(define (cooling-tower cx cz h wire id)
  (let* ((r1 (* BW 0.46)) (r2 (* BW 0.30)) (r3 (* BW 0.35))
         (h1 (* h 0.45)) (h2 (* h 0.30)) (h3 (* h 0.20)))
    (hl-cyl cx cz 0 h1 r1 wire 12)
    (hl-cyl cx cz h1 h2 r2 wire 12)
    (hl-cyl cx cz (+ h1 h2) h3 r3 wire 12)))
;; reactor: containment drum + dome + service box (static geometry only)
(define (reactor cx cz h wire id)
  (let* ((r (* BW 0.36)) (dh (* h 0.5)))
    (hl-cyl cx cz 0 dh r wire 14)
    (hl-sphere cx dh cz (* r 0.98) wire)
    (hl-box (+ cx (* BW 0.32)) (+ cz (* BW 0.30)) 0
            (* BW 0.3) (* dh 0.45) (* BW 0.3) wire)))
;; smokestack chamber: low hall + 2 striped stacks + painted warning bands
(define (stack-hall cx cz h wire id)
  (let* ((sh (* h 1.05)) (sr (* BW 0.075))
         (x1 (- cx (* BW 0.22))) (x2 (+ cx (* BW 0.22)))
         (zz (- cz (* BW 0.1))))
    (hl-box cx (+ cz (* BW 0.22)) 0 (* BW 0.9) (* h 0.22) (* BW 0.45) wire)
    (hl-cyl x1 zz 0 sh sr wire 8)
    (hl-cyl x2 zz 0 (* sh 0.82) sr wire 8)
    ;; painted warning band near each stack top
    (glow-box (vector x1 (* sh 0.9) zz) (vector 0.16 0.05 0.16) C-RED 0.85)
    (glow-box (vector x2 (* sh 0.74) zz) (vector 0.16 0.05 0.16) C-RED 0.85)))
;; --- animated emitters, extracted from the builders above (rebuilt per frame):
;; smoke, the reactor's blinking core light, and each stack's rising steam.
(define (cooling-smoke cx cz h id)
  (smoke cx cz (* h 0.95) (+ id 77) 4 0.30 1.1))
(define (reactor-light cx cz h id)
  (let* ((r (* BW 0.36)) (dh (* h 0.5)))
    (dyn! (glow-box (vector cx (+ dh (* r 0.9)) cz)
                    (vector 0.05 0.05 0.05) C-RED
                    (+ 0.3 (* 0.7 (abs (sin (+ (* 1.8 (time)) id)))))))))
(define (stack-smoke cx cz h id)
  (let* ((sh (* h 1.05))
         (x1 (- cx (* BW 0.22))) (x2 (+ cx (* BW 0.22)))
         (zz (- cz (* BW 0.1))))
    (smoke x1 zz sh (+ id 5) 5 0.22 1.3)
    (smoke x2 zz (* sh 0.82) (+ id 9) 4 0.20 1.1)))
;; tank farm: 2x2 cryo cylinders + one spherical pressure tank
(define (tank-farm cx cz h wire id)
  (let* ((r (* BW 0.17)) (th (* h 0.3)) (g (* BW 0.24)))
    (hl-cyl (- cx g) (- cz g) 0 th r wire 10)
    (hl-cyl (+ cx g) (- cz g) 0 (* th 0.8) r wire 10)
    (hl-cyl (- cx g) (+ cz g) 0 (* th 0.9) r wire 10)
    (hl-sphere (+ cx g) (+ r 0.02) (+ cz g) r wire)
    ;; manifold pipe connecting the row
    (glow-box (vector cx (* th 0.5) (- cz g))
              (vector (* g 2.2) 0.018 0.018) (vmul wire 0.6) 0.9)))
;; gas holder: big half-sunk sphere on a shallow ring
(define (gas-holder cx cz wire id)
  (let ((r (* BW 0.40)))
    (hl-cyl cx cz 0 (* r 0.25) (* r 1.05) wire 14)
    (hl-sphere cx (* r 0.95) cz r wire)))
;; turbine hall: long shed + roof monitor + intake pipe
(define (turbine-hall cx cz h wire id)
  (let ((long (> (hsh (+ id 41)) 0.5)))
    (if long
        (begin
          (hl-box cx cz 0 (* BW 1.0) (* h 0.32) (* BW 0.55) wire)
          (hl-box cx cz (* h 0.32) (* BW 0.6) (* h 0.06) (* BW 0.28) wire))
        (begin
          (hl-box cx cz 0 (* BW 0.55) (* h 0.32) (* BW 1.0) wire)
          (hl-box cx cz (* h 0.32) (* BW 0.28) (* h 0.06) (* BW 0.6) wire)))
    (hl-cyl (+ cx (* BW 0.3)) (- cz (* BW 0.3)) 0 (* h 0.38)
            (* BW 0.06) wire 8)))
;; switchyard: transformer boxes + bus poles + short bus-bar wires
(define (switchyard cx cz wire id)
  (hl-box (- cx (* BW 0.22)) cz 0 (* BW 0.3) 0.28 (* BW 0.3) wire)
  (hl-box (+ cx (* BW 0.2)) (- cz (* BW 0.2)) 0 (* BW 0.24) 0.2
          (* BW 0.24) wire)
  (let loop ((k 0))
    (when (< k 3)
      (let ((px (+ cx (* BW (- (* 0.3 k) 0.3)))))
        (glow-box (vector px 0.28 (+ cz (* BW 0.28)))
                  (vector 0.02 0.56 0.02) (vmul wire 0.8) 0.95))
      (loop (+ k 1))))
  (glow-box (vector cx 0.52 (+ cz (* BW 0.28)))
            (vector (* BW 0.72) 0.014 0.014) (vmul wire 0.7) 0.9))

;; ---- the site: hash-picked industrial structures with height falloff -------
;; Shared cell iterator: derive each occupied cell's values ONCE and hand them to
;; proc. Used by both site-static (built once) and site-dynamic (per frame), so
;; the geometry math stays in one place.
(define (for-each-cell proc)              ; proc: (gx gz id cx cz h sty wire)
  (let ly ((gz 0))
    (when (< gz GRID)
      (let lx ((gx 0))
        (when (< gx GRID)
          (let ((id (+ gx (* gz GRID))))
            (when (cell-occ? id)                          ; a few empty pads
              (let* ((cx (+ (- HALF) (* (+ gx 0.5) CELL)))
                     (cz (+ (- HALF) (* (+ gz 0.5) CELL)))
                     (h  (cell-base-h gx gz))
                     (sty (cell-sty id))
                     (wire (wire-col (hsh (+ id 23)))))
                (proc gx gz id cx cz h sty wire))))
          (lx (+ gx 1))))
      (ly (+ gz 1)))))
;; static structure geometry — built once, persists across frames
(define (site-static)
  (for-each-cell
    (lambda (gx gz id cx cz h sty wire)
      (cond
        ((< sty 0.13) (cooling-tower cx cz h wire id))
        ((< sty 0.25) (reactor cx cz h wire id))
        ((< sty 0.42) (stack-hall cx cz h wire id))
        ((< sty 0.58) (tank-farm cx cz h wire id))
        ((< sty 0.70) (gas-holder cx cz wire id))
        ((< sty 0.88) (turbine-hall cx cz h wire id))
        (else         (switchyard cx cz wire id))))))
;; animated site prims — smoke, reactor core light, and the tall-structure
;; beacons — destroyed + rebuilt each frame
(define (site-dynamic)
  (for-each-cell
    (lambda (gx gz id cx cz h sty wire)
      (cond ((< sty 0.13) (cooling-smoke cx cz h id))
            ((< sty 0.25) (reactor-light cx cz h id))
            ((< sty 0.42) (stack-smoke cx cz h id))
            (else         (void)))
      ;; site beacon on the tallest structures
      (when (> (cell-top-h id h) 2.2)
        (dyn! (glow-box (vector cx (+ (cell-top-h id h) 0.16) cz)
                        (vector 0.05 0.05 0.05) C-RED
                        (+ 0.25 (* 0.75 (abs (sin (+ (* 2.2 (time))
                                                     id)))))))))))

;; ---- high-voltage line: lattice pylons + sagging catenary conductors -------
(define PYL-X (+ (- HALF) (* 3 CELL)))     ; runs along the i=3 avenue
(define NPYL 5)
(define PYL-H 1.7)
(define ARM-Y 1.46)
(define ARM-W 0.30)                        ; crossarm half-width
(define (pyl-z j) (+ (- HALF) (* j (/ SPAN (- NPYL 1)))))
(define (pylon x z)                        ; static mast + insulators
  ;; tapered lattice mast: two stacked wire boxes + crossarm + insulators
  (hl-style (with-state (translate (vector x 0.475 z))
                        (scale (vector 0.26 0.95 0.26)) (build-cube))
            C-EDGE 1.2)
  (hl-style (with-state (translate (vector x 1.25 z))
                        (scale (vector 0.14 0.65 0.14)) (build-cube))
            C-EDGE 1.2)
  (hl-style (with-state (translate (vector x ARM-Y z))
                        (scale (vector (* ARM-W 2.15) 0.055 0.055))
                        (build-cube))
            C-EDGE 1.2)
  (glow-box (vector (- x ARM-W) (- ARM-Y 0.045) z)
            (vector 0.025 0.06 0.025) C-GREEN 0.9)
  (glow-box (vector (+ x ARM-W) (- ARM-Y 0.045) z)
            (vector 0.025 0.06 0.025) C-GREEN 0.9))
(define (pylon-beacon x z)                 ; blinking red aircraft-warning light
  (dyn! (glow-box (vector x (+ PYL-H 0.05) z) (vector 0.03 0.03 0.03) C-RED
                  (+ 0.3 (* 0.7 (abs (sin (+ (* 2.0 (time)) z))))))))
(define (conductor x0 z0 z1 sag col)       ; catenary ribbon between towers
  (let* ((N 9) (rb (build-ribbon N)))
    (with-primitive rb
      (identity) (hint-unlit) (colour col) (opacity 0.85)
      (pdata-index-map!
        (lambda (k v)
          (let* ((s (/ k (- N 1.0)))
                 (y (- ARM-Y (* sag 4.0 s (- 1.0 s)))))
            (vector x0 y (+ z0 (* s (- z1 z0)))))) "p")
      (pdata-index-map! (lambda (k w) (vector 0.012 0.012 0.012)) "w"))))
(define (powerline-static)                 ; masts + catenary conductors (once)
  (let loop ((j 0))
    (when (< j NPYL)
      (pylon PYL-X (pyl-z j))
      (when (< j (- NPYL 1))
        (let ((z0 (pyl-z j)) (z1 (pyl-z (+ j 1))))
          (conductor (- PYL-X ARM-W) z0 z1 0.22 (vmul C-EDGE 0.75))
          (conductor (+ PYL-X ARM-W) z0 z1 0.22 (vmul C-EDGE 0.75))
          (conductor PYL-X z0 z1 0.28 (vmul C-GREEN 0.6))))
      (loop (+ j 1)))))
(define (powerline-dynamic)                ; pylon beacons + energy pulses (frame)
  (let loop ((j 0))
    (when (< j NPYL)
      (pylon-beacon PYL-X (pyl-z j))
      (when (< j (- NPYL 1))
        (let ((z0 (pyl-z j)) (z1 (pyl-z (+ j 1))))
          ;; energy pulse racing down the middle conductor
          (let* ((p (fract (* (time) 0.5)))
                 (z (+ z0 (* p (- z1 z0))))
                 (y (- ARM-Y (* 0.28 4.0 p (- 1.0 p)))))
            (dyn! (glow-box (vector PYL-X y z) (vector 0.04 0.04 0.04)
                            C-GREEN 0.9)))))
      (loop (+ j 1)))))

;; ---- traffic: service vehicles on roads; trail = past positions ------------
(define NCARS 28)
(define (lane-of i)                       ; snap to a road line (cell edge)
  (+ (- HALF) (* (floor (* (hsh (+ i 1)) (+ GRID 1))) CELL)))
(define (car-pos i t horiz)
  (let* ((spd  (+ 0.8 (* 1.1 (hsh (+ i 4)))))
         (dir  (if (> (hsh (+ i 8)) 0.5) 1.0 -1.0))
         (lane (lane-of i))
         (p (- (fract (/ (+ (* spd dir t) (* 5.7 (hsh (+ i 2)))) SPAN)) 0.5))
         (along (* p SPAN)))
    (if horiz (vector along CARY lane) (vector lane CARY along))))
(define (car-col i)
  (let ((r (hsh (+ i 5))))
    (cond ((< r 0.30) C-RED)                      ; taillights / alerts
          ((< r 0.50) C-GREEN)                    ; MAGI green couriers
          (else       (vector 1.0 0.72 0.30)))))  ; NERV amber headlights
(define (trail i t horiz col T dt w-core w-glow)
  (let ((rb (dyn! (build-ribbon T))))             ; glow halo pass
    (with-primitive rb
      (identity) (hint-unlit) (colour col) (opacity 0.16)
      (pdata-index-map!
        (lambda (k v) (car-pos i (- t (* k dt)) horiz)) "p")
      (pdata-index-map!
        (lambda (k w)
          (let ((tp (* w-glow (- 1.0 (/ k (- T 1.0)))))) (vector tp tp tp)))
        "w")))
  (let ((rb (dyn! (build-ribbon T))))             ; bright core pass
    (with-primitive rb
      (identity) (hint-unlit) (colour col)
      (pdata-index-map!
        (lambda (k v) (vadd (car-pos i (- t (* k dt)) horiz)
                            (vector 0 0.012 0))) "p")
      (pdata-index-map!
        (lambda (k w)
          (let ((tp (* w-core (- 1.0 (/ k (- T 1.0)))))) (vector tp tp tp)))
        "w"))))
(define (traffic)
  (let ((t (time)))
    (let loop ((i 0))
      (when (< i NCARS)
        (trail i t (even? i) (car-col i) 16 0.05 0.05 0.12)
        (loop (+ i 1))))
    ;; two slow heavy haulers, extra-long acid-green trails
    (trail 101 (* t 0.45) #t (vector 0.7 1.0 0.4) 26 0.09 0.07 0.18)
    (trail 202 (* t 0.45) #f (vector 0.7 1.0 0.4) 26 0.09 0.07 0.18)))

;; ---- technical caption: kinked leader + typewriter title on a random -------
;; structure; the pick evolves every few seconds (hash of the time slot)
(define CAPD 4.0)                          ; seconds per pick
(define CW 0.44)                           ; glyph pen advance (font atlas)
(define CAP-NAMES
  (vector "REACTOR-CORE" "COOLING-TWR" "STACK-CHAMBER" "CRYO-TANK"
          "GASHOLDER" "TURBINE-HALL" "SWITCHYARD" "PYLON-GRID"))
(define (pick-cell slot)                   ; probe until an occupied cell
  (let loop ((k 0))
    (let* ((gx (modulo (inexact->exact
                         (floor (* (hsh (+ (* slot 1.7) (* 0.13 k) 0.1))
                                   GRID))) GRID))
           (gz (modulo (inexact->exact
                         (floor (* (hsh (+ (* slot 1.7) (* 0.31 k) 0.7))
                                   GRID))) GRID)))
      (if (or (cell-occ? (+ gx (* gz GRID))) (> k 20))
          (vector gx gz 0) (loop (+ k 1))))))
;; world-space focus point of a slot's picked structure (for the camera tween)
(define (cap-focus slot)
  (let* ((cell (pick-cell slot))
         (gx (inexact->exact (vx cell))) (gz (inexact->exact (vy cell)))
         (id (+ gx (* gz GRID)))
         (cx (+ (- HALF) (* (+ gx 0.5) CELL)))
         (cz (+ (- HALF) (* (+ gz 0.5) CELL)))
         (top (cell-top-h id (cell-base-h gx gz))))
    (vector cx (* 0.45 top) cz)))
(define (leader-ribbon a b c col w)        ; 3-point unlit line
  (let ((rb (dyn! (build-ribbon 3))))
    (with-primitive rb
      (identity) (hint-unlit) (colour col)
      (pdata-index-map!
        (lambda (k v) (cond ((= k 0) a) ((= k 1) b) (else c))) "p")
      (pdata-index-map! (lambda (k v) (vector w w w)) "w"))))
;; caption name follows the picked structure's actual type
(define (sty-name id)
  (let ((sty (cell-sty id)))
    (cond ((< sty 0.13) "COOLING-TWR")
          ((< sty 0.25) "REACTOR-CORE")
          ((< sty 0.42) "STACK-CHAMBER")
          ((< sty 0.58) "CRYO-TANK")
          ((< sty 0.70) "GASHOLDER")
          ((< sty 0.88) "TURBINE-HALL")
          (else         "SWITCHYARD"))))
(define (caption)
  (let* ((slot (inexact->exact (floor (/ (time) CAPD))))
         (prog (fract (/ (time) CAPD)))
         (cell (pick-cell slot))
         (gx (inexact->exact (vx cell))) (gz (inexact->exact (vy cell)))
         (id (+ gx (* gz GRID)))
         (cx (+ (- HALF) (* (+ gx 0.5) CELL)))
         (cz (+ (- HALF) (* (+ gz 0.5) CELL)))
         (top (cell-top-h id (cell-base-h gx gz)))
         (az (* (time) 0.15))
         (s  (vector (cos az) 0 (- (sin az))))      ; camera-right in world
         (blink (if (> (fract (* (time) 2.5)) 0.25) 1.0 0.35))
         (ccol C-GREEN)
         ;; caption text: TYPE-ID // H:height, revealed typewriter-style
         (full (string-append
                 (sty-name id)
                 "-" (number->string (inexact->exact id))
                 " H:" (number->string
                         (inexact->exact (floor (* top 100))))))
         (n (string-length full))
         (shown (substring full 0
                  (min n (inexact->exact (floor (* prog 2.5 n))))))
         (sc 0.42)                                   ; text cap height
         (tw (* CW n (/ sc 0.9)))
         ;; leader: 45-degree angled segment out of the roof, then horizontal
         ;; (classic technical-drawing callout, kinked not straight).
         ;; Kink toward the SCREEN centre (dot with camera-right), so the
         ;; caption never runs off-frame as the camera orbits/tweens.
         (side (if (< (vdot (vsub (vector cx 0 cz) (camera-target)) s) 0)
                   1.0 -1.0))
         (rise 0.75)
         (anchor (vector cx (+ top 0.05) cz))
         (elbow  (vadd anchor (vadd (vector 0 rise 0)
                                    (vmul s (* side rise)))))  ; 45 deg
         (tip    (vadd elbow (vmul s (* side (+ tw 0.15))))))
    ;; highlight bracket: pulsing wire box around the picked structure
    (let ((b (dyn! (with-state
               (translate (vector cx (* 0.5 top) cz))
               (scale (vector (* BW 1.18) (* top 1.04) (* BW 1.18)))
               (build-cube)))))
      (with-primitive b
        (hint-solid #f) (hint-wire) (hint-unlit)
        (line-width 1.5)
        (wire-colour ccol)
        (wire-opacity (* blink (+ 0.55 (* 0.45 (sin (* 6 (time)))))))))
    ;; kinked leader: anchor -/45deg/-> elbow --horizontal--> tip (= underline)
    (leader-ribbon anchor elbow tip ccol 0.022)
    ;; anchor tick: small blinking marker at the roof
    (dyn! (glow-box anchor (vector 0.07 0.07 0.07) ccol blink))
    ;; caption title: camera-facing (yaw only), sitting on the horizontal bar
    (when (> (string-length shown) 0)
      (let ((tp (dyn! (build-text shown)))
            (start (if (> side 0)
                       (vadd elbow (vmul s 0.08))
                       (vadd tip   (vmul s 0.08)))))
        (with-primitive tp
          (identity) (hint-unlit)
          (translate (vadd start (vector 0 0.07 0)))
          (rotate (vector 0 (* az 57.2958) 0))
          (scale (vector (/ sc 0.9) (/ sc 0.9) (/ sc 0.9)))
          (colour ccol))))))

;; ---- glowing CRT post shader: bloom + scanlines + grille + curvature -------
(define crt "
uniform sampler2D tex;
uniform float time;
uniform vec2 resolution;
varying vec2 uv;
vec2 curve(vec2 p) {
  p = p * 2.0 - 1.0;
  vec2 o = abs(p.yx) / vec2(6.0, 5.0);       // gentle barrel curvature
  p = p + p * o * o;
  return p * 0.5 + 0.5;
}
void main() {
  vec2 u = curve(uv);
  if (u.x < 0.0 || u.x > 1.0 || u.y < 0.0 || u.y > 1.0) {
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
  float ca = 0.0014;                          // chromatic aberration
  vec3 col;
  col.r = texture2D(tex, u + vec2(ca, 0.0)).r;
  col.g = texture2D(tex, u).g;
  col.b = texture2D(tex, u - vec2(ca, 0.0)).b;
  // cheap wide bloom: 8-tap ring + 4-tap far ring, keep only bright energy
  vec2 px = 2.5 / resolution;
  vec3 bl = vec3(0.0);
  bl += texture2D(tex, u + vec2( px.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(-px.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(0.0,  px.y)).rgb;
  bl += texture2D(tex, u + vec2(0.0, -px.y)).rgb;
  bl += texture2D(tex, u + vec2( px.x,  px.y)).rgb;
  bl += texture2D(tex, u + vec2(-px.x,  px.y)).rgb;
  bl += texture2D(tex, u + vec2( px.x, -px.y)).rgb;
  bl += texture2D(tex, u + vec2(-px.x, -px.y)).rgb;
  vec2 fx = 7.0 / resolution;
  bl += texture2D(tex, u + vec2( fx.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(-fx.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(0.0,  fx.y)).rgb;
  bl += texture2D(tex, u + vec2(0.0, -fx.y)).rgb;
  bl /= 12.0;
  col += max(bl - 0.18, 0.0) * 1.5;           // phosphor glow
  col *= 0.86 + 0.14 * sin(u.y * resolution.y * 3.14159);   // scanlines
  float m = mod(gl_FragCoord.x, 3.0);         // aperture grille
  col *= vec3(m < 1.0 ? 1.12 : 0.82,
              (m >= 1.0 && m < 2.0) ? 1.12 : 0.82,
              m >= 2.0 ? 1.12 : 0.82);
  // rolling refresh band
  col *= 1.0 + 0.04 * sin((u.y + time * 0.13) * 12.0);
  float vig = 16.0 * u.x * u.y * (1.0 - u.x) * (1.0 - u.y);
  col *= pow(vig, 0.3);                       // vignette
  col *= 1.05 + 0.03 * sin(time * 9.0);       // flicker
  // slight warm grade, NERV console
  col = pow(col, vec3(0.95, 1.0, 1.08));
  gl_FragColor = vec4(col, 1.0);
}")

;; Retained + persistent scene. The buffer compiles ONCE (no per-frame re-parse
;; of every define/string-append). The static geometry — streets, all buildings,
;; pylons + conductors — is built ONCE here and persists across frames. The
;; every-frame thunk then destroys only LAST frame's animated prims (smoke,
;; beacons, core lights, traffic, powerline pulse, caption) and rebuilds them —
;; so the ~400 static prims are never rebuilt. (Racket host only; on the s7 host
;; retained is a no-op, so it falls back to immediate re-eval each frame.)
(retained)
(streets)                                  ; static ground grid  \
(site-static)                              ; static structures    } built once
(powerline-static)                         ; static HV line      /
(every-frame
  (begin
    (clear-dyn!)                           ; remove last frame's animated prims
    (iso-camera)
    (site-dynamic)                         ; smoke, core lights, site beacons
    (powerline-dynamic)                    ; pylon beacons + energy pulses
    (traffic)                              ; vehicle light-trails
    (caption)                              ; bracket + leader + typewriter title
    (post-shader crt)))
