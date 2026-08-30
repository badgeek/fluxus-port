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
(define C-SMOKE  (vector 1.00 1.00 1.00))     ; steam/smoke white
(define C-GRID   (vector 0.20 0.85 0.40))     ; green road grid
(define C-GRID-D (vector 0.12 0.50 0.24))     ; dim green sub-grid
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

;; ---- generative EVOLUTION: the factory over-industrialises (one-way) --------
;; evo rises 0 -> 1 over GROW-TOTAL seconds then HOLDS (never loops).
;;   phase 1 (evo 0..0.5): a square growth front expands from the centre, razing
;;     forest into factory.
;;   phase 2 (evo 0.5..1): densification (indus) — empty lots fill in, structures
;;     grow taller + gain annex clutter, the comm network thickens — until the
;;     whole grid is a maxed-out over-industrialised complex.
;; The static scene rebuilds each time the quantised evolution step advances.
(define CENTER (* 0.5 (- GRID 1)))         ; grid centre index (4.0 for GRID 9)
(define GROW-MAX (+ CENTER 1.3))
;; time-based growth: evolution advances monotonically over GROW-TOTAL seconds
;; then holds (one-way, no loop).
(define GROW-TOTAL 210.0)                   ; seconds: bare centre -> fully maxed out
;; growth runs on a VIRTUAL clock (*evo-elapsed*) advanced once per frame by
;; dt * *grow-speed*, so +/- can scale industrialisation speed live without the
;; evolution value jumping (scaling raw elapsed retroactively would). evo-tick!
;; is called once at the top of every-frame (before anything reads (evo)).
(define *evo-elapsed* 0.0)                  ; accumulated virtual growth-seconds
(define *last-t* -1.0)                       ; previous real time sampled
(define *grow-speed* 1.0)                    ; +/- multiplier (0.0625 .. 16)
(define (evo-tick!)
  (when (< *last-t* 0) (set! *last-t* (time)))
  (let ((dt (- (time) *last-t*)))
    (set! *last-t* (time))
    (set! *evo-elapsed* (+ *evo-elapsed* (* dt *grow-speed*)))))
(define (grow-faster!) (set! *grow-speed* (min 16.0    (* *grow-speed* 1.5))))
(define (grow-slower!) (set! *grow-speed* (max 0.0625  (/ *grow-speed* 1.5))))
(define (evo)                              ; 0 -> 1 monotonic, holds at 1
  ;; while recording, compress the whole arc to fit the clip (live stays GROW-TOTAL)
  (min 1.0 (/ *evo-elapsed* (if *rec* (* REC-LEN 0.92) GROW-TOTAL))))
(define (evo-step) (inexact->exact (floor (* (evo) 52))))  ; rebuild trigger
(define (indus) (max 0.0 (min 1.0 (/ (- (evo) 0.45) 0.55)))) ; densification 0..1
;; per-cell construction raise: a building scales up from the ground over
;; RAISE-DUR seconds after the cell is first built.
(define RAISE-DUR 0.4)
(define *birth* (make-vector (* GRID GRID) -1.0))
(define (cell-birth! id) (when (< (vector-ref *birth* id) 0) (vector-set! *birth* id (time))))
(define (cell-raise id)
  (let ((b (vector-ref *birth* id)))
    (if (< b 0) 0.0 (smoothstep (min 1.0 (/ (- (time) b) RAISE-DUR))))))
(define (reset-anim!)                      ; press R: restart from bare forest
  (set! *evo-elapsed* 0.0) (set! *last-t* -1.0) (set! *built-step* -999)
  (let loop ((i 0)) (when (< i (* GRID GRID)) (vector-set! *birth* i -1.0) (loop (+ i 1)))))
;; offline recording: restart the animation from forest, then render a frame-locked
;; 60fps MP4; auto-stops after REC-LEN seconds of footage (see the thunk).
(define REC-LEN 60.0)
(define REC-FPS 60)
(define *rec* #f)
(define *rec-frames* 0)                     ; rendered frames since record start
(define (start-record)
  (reset-anim!)
  (set! *rec-frames* 0)
  (set-export #t "/Users/manticore/Movies/iso-city-1min.mp4" REC-FPS)
  (set! *rec* #t))
(define (cell-metric gx gz)                ; Chebyshev (square) dist + per-cell jitter
  (let ((dx (- gx CENTER)) (dz (- gz CENTER)))
    (+ (max (abs dx) (abs dz))
       (* 0.9 (hsh (+ gx (* gz GRID) 3))))))
(define (growth-front)                     ; reaches full coverage at evo 0.5, holds
  (* GROW-MAX (min 1.0 (/ (evo) 0.5))))
(define (cell-factory? gx gz) (< (cell-metric gx gz) (growth-front)))

;; static-scene rebuild registry: prims built while *in-site* is true are recorded
;; so a growth-flip can destroy + rebuild them. (streets/powerline are built once
;; outside this and persist untouched.)
(define *site-ids* '())
(define *in-site* #f)
(define (site-track! id) (when *in-site* (set! *site-ids* (cons id *site-ids*))) id)

;; ---- persistent smoke-sphere pool -------------------------------------------
;; Smoke is ~144 puffs; rebuilding them every frame was the #1 per-frame cost
;; (build_sphere). Instead build each sphere ONCE and per frame just grab it and
;; re-set its transform + opacity. Cells iterate in a fixed order every frame, so
;; the cursor maps a stable pool sphere to each puff. Pool is NOT in *dyn* — it
;; persists like the static site (freed only on reload, when *pool-n* re-inits).
(define *pool* (make-vector 1024 -1))
(define *pool-n* 0)                        ; spheres built so far (grows on frame 1)
(define *pool-cur* 0)                      ; per-frame cursor
(define (pool-frame-begin!)                ; reset cursor + park all puffs off-screen;
  (set! *pool-cur* 0)                       ; smoke() repositions only the ones emitted
  (let loop ((i 0))
    (when (< i *pool-n*)
      (with-primitive (vector-ref *pool* i)
        (identity) (translate (vector 0 -9999 0)) (wire-opacity 0.0))
      (loop (+ i 1)))))
(define (pool-sphere!)                     ; next pooled sphere id (build once)
  (let ((i *pool-cur*))
    (set! *pool-cur* (+ i 1))
    (when (>= i *pool-n*)
      (vector-set! *pool* i (build-sphere 8 5))
      (set! *pool-n* (+ i 1)))
    (vector-ref *pool* i)))

;; ---- persistent caption text -----------------------------------------------
;; The caption title is rebuilt (build-text = a glyph-quad mesh) every frame even
;; though the string only changes as the typewriter reveals a char (~a few times
;; a second) or the picked structure flips (every CAPD). Keep one text prim and
;; rebuild it ONLY when the shown string changes; the per-frame transform/colour
;; is applied by grabbing it. Not in *dyn* — cap-text! owns its lifecycle.
(define *cap-id* -1)
(define *cap-str* "")
(define (cap-text! shown)                  ; returns the text prim id (-1 if empty)
  (unless (string=? shown *cap-str*)
    (when (>= *cap-id* 0) (destroy *cap-id*) (set! *cap-id* -1))
    (when (> (string-length shown) 0) (set! *cap-id* (build-text shown)))
    (set! *cap-str* shown))
  *cap-id*)

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
    ;; COMPOSE-Y raises the aim point so the grid sits lower in the 9:16 frame,
    ;; clearing the HUD readout at the top.
    (vadd (vmix (vector 0 0.55 0) f 0.4) (vector 0 COMPOSE-Y 0))))
(define COMPOSE-Y 2.6)
;; camera basis, captured each frame so the HUD can pin text to the screen
(define *cam-eye* (vector 0 0 0))
(define *cam-right* (vector 1 0 0))
(define *cam-up* (vector 0 1 0))
(define *cam-fwd* (vector 0 0 -1))
(define *cam-az* 0.0)
(define (iso-camera)
  (set-fov 12)
  (let* ((el (+ 0.6155 (* 0.045 (sin (* (time) 0.11)))))  ; gentle breathe
         (az (* (time) 0.15))                             ; slow spin
         (d  (* 3.55 (camera-dist)))                      ; MOUSE WHEEL zoom
         (ce (cos el)) (se (sin el))
         (tgt (camera-target))
         (eye (vadd tgt
                (vector (* d ce (sin az)) (* d se) (* d ce (cos az))))))
    (let* ((f (vnormalise (vsub tgt eye)))
           (s (vnormalise (vcross f (vector 0 1 0))))
           (u (vcross s f)))
      (set! *cam-eye* eye) (set! *cam-right* s) (set! *cam-up* u)
      (set! *cam-fwd* f)   (set! *cam-az* az))
    (set-camera-transform (look-at eye tgt (vector 0 1 0)))))

;; ---- small unlit helpers ---------------------------------------------------
(define (glow-box pos scl col op)
  (site-track!
    (with-state
      (translate pos) (scale scl)
      (hint-solid) (hint-unlit)
      (colour col) (opacity op)
      (build-cube))))
;; hidden-line primitives: near-black occluding fill + bright unlit wire edges.
;; NB: wire-colour/wire-opacity only act on a GRABBED prim (build context
;; ignores them) — so build first, then grab to style.
(define (hl-style b wire lw)               ; used by the powerline pylons (kept)
  (with-primitive b
    (hint-solid) (hint-wire) (hint-unlit)
    (line-width lw)
    (colour C-BLACK)
    (wire-colour wire) (wire-opacity 1.0)))
;; buildings: bold SEE-THROUGH wireframe — no solid fill, backface cull off so
;; every edge (front + back) draws. Line uses the wire colour directly.
(define BLD-LW 3.0)                         ; building wireframe line width
(define (hl-wire b wire lw)
  (with-primitive b
    (hint-solid #f) (hint-wire) (hint-unlit)   ; solid OFF — true see-through wire
    (backfacecull #f)
    (line-width lw)
    (wire-colour wire) (wire-opacity 1.0)))
(define (hl-box cx cz y0 w h d wire)
  (let ((b (with-state
             (translate (vector cx (+ y0 (* 0.5 h)) cz))
             (scale (vector w h d))
             (build-cube))))
    (hl-wire b wire BLD-LW)
    (site-track! b)))
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
             (col (if ave C-GRID C-GRID-D))     ; green grid
             (w   (if ave 0.008 0.004))         ; thinner
             (op  (if ave 0.95 0.70)))
        (glow-box (vector x 0.004 0) (vector w 0.006 SPAN) col op)
        (glow-box (vector 0 0.004 x) (vector SPAN 0.006 w) col op)
        (when ave
          (glow-box (vector x 0.002 0) (vector 0.10 0.004 SPAN) col 0.08)
          (glow-box (vector 0 0.002 x) (vector SPAN 0.004 0.10) col 0.08)))
      (loop (+ i 1))))
  ;; fine sub-grid: thin green lines subdividing each cell (SUB per cell)
  (let ((SUB 4))
    (let loop ((i 0))
      (when (<= i (* GRID SUB))
        (when (not (= 0 (modulo i SUB)))       ; skip where main lines already are
          (let ((x (+ (- HALF) (* i (/ CELL SUB)))))
            (glow-box (vector x 0.0025 0) (vector 0.0035 0.004 SPAN) C-GRID 0.8)
            (glow-box (vector 0 0.0025 x) (vector SPAN 0.004 0.0035) C-GRID 0.8)))
        (loop (+ i 1))))))

;; ---- rolling terrain contour (value noise) surrounding the razed grid -------
;; value noise = smooth bilinear interp of a hashed lattice; 2 octaves for hills.
(define (vnoise x z)
  (let* ((x0 (floor x)) (z0 (floor z))
         (sx (smoothstep (- x x0))) (sz (smoothstep (- z z0)))
         (hh (lambda (i j) (hsh (+ (* i 57.0) (* j 131.0) 0.3))))
         (n00 (hh x0 z0)) (n10 (hh (+ x0 1) z0))
         (n01 (hh x0 (+ z0 1))) (n11 (hh (+ x0 1) (+ z0 1)))
         (a (+ n00 (* sx (- n10 n00))))
         (b (+ n01 (* sx (- n11 n01)))))
    (+ a (* sz (- b a)))))
(define (fbm x z) (+ (* 0.65 (vnoise x z)) (* 0.35 (vnoise (* 2.0 x) (* 2.0 z)))))
;; global wind: slowly drifting X/Z push from value noise over time. Smoke drifts
;; with it (more the higher it rises = wind shear).
(define WIND-AMP 1.6)
(define (wind-x) (* WIND-AMP (- (fbm (* (time) 0.11)  3.0) 0.5)))
(define (wind-z) (* WIND-AMP (- (fbm (* (time) 0.09) 47.0) 0.5)))
(define TERR-N 30)                          ; mesh resolution
(define TERR-EXT (* SPAN 1.7))              ; extends well beyond the city
(define TERR-AMP 0.75)                      ; hill height
(define (terrain)
  (let ((p (with-state
             (translate (vector 0 -0.08 0))
             (rotate (vector 90 0 0))       ; lay the XY plane flat onto XZ
             (scale (vector TERR-EXT TERR-EXT 1))
             (build-seg-plane TERR-N TERR-N))))
    (with-primitive p
      (pdata-index-map!
        (lambda (i v)
          (let ((lx (vx v)) (ly (vy v)))    ; local plane coords (~ -0.5..0.5)
            (vector lx ly (* (/ TERR-AMP TERR-EXT)
                             (- (fbm (* (+ lx 0.5) 7.0) (* (+ ly 0.5) 7.0)) 0.5)))))
        "p")
      (hint-solid #f) (hint-wire) (hint-unlit)
      (line-width 1.0)
      (colour C-GRID-D) (wire-colour C-GRID-D) (wire-opacity 0.42))))

;; ---- per-cell derived values (shared by site + caption + camera) -----------
;; occupancy tightens as the zone industrialises: empty lots fill in (threshold
;; 0.15 -> 0, so every cell ends up built at full over-industrialisation).
(define (cell-occ? id) (> (hsh (+ id 0.5)) (* 0.15 (- 1.0 (indus)))))
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
             ;; slow fade-out: stays bright as it rises, only fades near the top
             (op (* (expt (- 1.0 prog) 0.4) (min 1.0 (* prog 6.0)) 0.55))
             (wob (* 0.08 prog (sin (+ (* 1.7 (time)) (* 4 k) seed))))
             (s (pool-sphere!)))              ; persistent: built once, reused
        (with-primitive s
          (identity)                          ; reset last frame's transform
          (translate (vector (+ cx wob (* (wind-x) prog))
                             y
                             (+ cz (* 0.5 wob) (* (wind-z) prog))))
          (scale (vector r r r))
          (hint-solid #f) (hint-wire) (hint-unlit)
          (line-width 1.0)
          (colour C-SMOKE) (opacity op)
          (wire-colour C-SMOKE) (wire-opacity op)))
      (loop (+ k 1)))))

;; ---- industrial structure builders (blocky / voxel — all cube geometry) -----
;; cooling tower: a tapered stack of boxes (wide base, pinched waist, flared top)
(define (cooling-tower cx cz h wire id)
  (let* ((h1 (* h 0.45)) (h2 (* h 0.30)) (h3 (* h 0.20)))
    (hl-box cx cz 0          (* BW 0.92) h1 (* BW 0.92) wire)  ; wide base
    (hl-box cx cz h1         (* BW 0.60) h2 (* BW 0.60) wire)  ; waist
    (hl-box cx cz (+ h1 h2)  (* BW 0.72) h3 (* BW 0.72) wire)  ; flared rim
    ;; corner buttresses on the base for a chunkier silhouette
    (let ((o (* BW 0.36)) (bw (* BW 0.16)))
      (hl-box (- cx o) (- cz o) 0 bw (* h1 0.7) bw wire)
      (hl-box (+ cx o) (+ cz o) 0 bw (* h1 0.7) bw wire))))
;; reactor: containment drum + stepped (ziggurat) dome + service box
(define (reactor cx cz h wire id)
  (let* ((dh (* h 0.5)))
    (hl-box cx cz 0  (* BW 0.72) dh (* BW 0.72) wire)                        ; drum
    (hl-box cx cz dh (* BW 0.56) (* BW 0.20) (* BW 0.56) wire)               ; dome tier 1
    (hl-box cx cz (+ dh (* BW 0.20)) (* BW 0.34) (* BW 0.16) (* BW 0.34) wire) ; dome tier 2
    (hl-box (+ cx (* BW 0.32)) (+ cz (* BW 0.30)) 0
            (* BW 0.3) (* dh 0.45) (* BW 0.3) wire)))                        ; service block
;; smokestack chamber: low hall + 2 square stacks + painted warning bands
(define (stack-hall cx cz h wire id)
  (let* ((sh (* h 1.05)) (sw (* BW 0.15))
         (x1 (- cx (* BW 0.22))) (x2 (+ cx (* BW 0.22)))
         (zz (- cz (* BW 0.1))))
    (hl-box cx (+ cz (* BW 0.22)) 0 (* BW 0.9) (* h 0.22) (* BW 0.45) wire)
    (hl-box x1 zz 0 sw sh sw wire)
    (hl-box x2 zz 0 sw (* sh 0.82) sw wire)
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
;; tank farm: 2x2 cubic cryo tanks + one taller cubic pressure tank
(define (tank-farm cx cz h wire id)
  (let* ((tw (* BW 0.34)) (th (* h 0.3)) (g (* BW 0.24)))
    (hl-box (- cx g) (- cz g) 0 tw th tw wire)
    (hl-box (+ cx g) (- cz g) 0 tw (* th 0.8) tw wire)
    (hl-box (- cx g) (+ cz g) 0 tw (* th 0.9) tw wire)
    (hl-box (+ cx g) (+ cz g) 0 tw (* tw 1.0) tw wire)   ; pressure cube
    ;; manifold pipe connecting the row
    (glow-box (vector cx (* th 0.5) (- cz g))
              (vector (* g 2.2) 0.018 0.018) (vmul wire 0.6) 0.9)))
;; gas holder: telescoping cubic drum (wide base + narrower upper lift)
(define (gas-holder cx cz wire id)
  (hl-box cx cz 0             (* BW 0.84) (* BW 0.30) (* BW 0.84) wire)   ; base drum
  (hl-box cx cz (* BW 0.30)   (* BW 0.66) (* BW 0.48) (* BW 0.66) wire))  ; upper lift
;; turbine hall: long shed + roof monitor + square intake tower
(define (turbine-hall cx cz h wire id)
  (let ((long (> (hsh (+ id 41)) 0.5)))
    (if long
        (begin
          (hl-box cx cz 0 (* BW 1.0) (* h 0.32) (* BW 0.55) wire)
          (hl-box cx cz (* h 0.32) (* BW 0.6) (* h 0.06) (* BW 0.28) wire))
        (begin
          (hl-box cx cz 0 (* BW 0.55) (* h 0.32) (* BW 1.0) wire)
          (hl-box cx cz (* h 0.32) (* BW 0.28) (* h 0.06) (* BW 0.6) wire)))
    (hl-box (+ cx (* BW 0.3)) (- cz (* BW 0.3)) 0
            (* BW 0.12) (* h 0.38) (* BW 0.12) wire)))
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
(define (build-structure cx cz h sty wire id)
  (cond
    ((< sty 0.13) (cooling-tower cx cz h wire id))
    ((< sty 0.25) (reactor cx cz h wire id))
    ((< sty 0.42) (stack-hall cx cz h wire id))
    ((< sty 0.58) (tank-farm cx cz h wire id))
    ((< sty 0.70) (gas-holder cx cz wire id))
    ((< sty 0.88) (turbine-hall cx cz h wire id))
    (else         (switchyard cx cz wire id))))

;; ---- forest: low-poly cube trees (razed as the factory front reaches them) --
(define C-TREE (vector 0.28 0.85 0.34))    ; forest green wire
(define (forest-cell? id) (> (hsh (+ id 0.5)) 0.06))   ; dense trees, a few gaps
(define (tree cx cz id)
  (let* ((s  (+ 0.55 (* 0.7 (hsh (+ id 11)))))          ; per-tree size
         (th (* s 0.45)))                                ; trunk height
    (hl-box cx cz 0 (* s 0.10) th (* s 0.10) C-TREE)     ; trunk
    ;; stacked shrinking canopy cubes — a chunky low-poly conifer
    (hl-box cx cz th               (* s 0.52) (* s 0.34) (* s 0.52) C-TREE)
    (hl-box cx cz (+ th (* s 0.30)) (* s 0.36) (* s 0.28) (* s 0.36) C-TREE)
    (hl-box cx cz (+ th (* s 0.52)) (* s 0.20) (* s 0.22) (* s 0.20) C-TREE)))

;; ---- comm links: dashed semicircular arcs between nearby factory rooftops ---
(define C-COMM (vector 0.30 0.95 0.95))    ; cyan data link
(define (vdist a b)
  (let ((d (vsub a b)))
    (sqrt (+ (* (vx d) (vx d)) (* (vy d) (vy d)) (* (vz d) (vz d))))))
(define (arc-pt p0 p1 arcH s)              ; point on a vertical semicircular arc
  (vadd (vmix p0 p1 s) (vector 0 (* (sin (* 3.14159 s)) arcH) 0)))
(define COMM-ARCN 11)                      ; arc ribbon resolution
(define COMM-SPD 0.33)                      ; packet travel speed (loops/sec)
(define *comm-links* '())                   ; list of (cons p0 p1) — set on rebuild
(define (comm-arc p0 p1)                    ; faint continuous arc + register the pair
  (let ((arcH (* (vdist p0 p1) 0.42)))
    (let ((rb (site-track! (build-ribbon COMM-ARCN))))
      (with-primitive rb
        (identity) (hint-unlit) (colour C-COMM) (opacity 0.16)
        (pdata-index-map!
          (lambda (k v) (arc-pt p0 p1 arcH (/ k (- COMM-ARCN 1.0)))) "p")
        (pdata-index-map! (lambda (k v) (vector 0.008 0.008 0.008)) "w")))
    (set! *comm-links* (cons (cons p0 p1) *comm-links*))))
;; sparse network: each rooftop links to its nearest neighbour within range
(define (comm-links tops)
  (set! *comm-links* '())
  (let* ((v (list->vector tops)) (n (vector-length v)))
    (let loop ((i 0))
      (when (< i n)
        (let ((pi (vector-ref v i)))
          (let find ((j 0) (best -1) (bd 1e9))
            (cond ((>= j n)
                   (when (and (>= best 0) (< i best)
                              (< bd (* (+ 2.2 (* 1.6 (indus))) CELL))   ; longer links
                              (> (hsh (+ i 2)) (* 0.42 (- 1.0 (indus))))) ; denser net
                     (comm-arc pi (vector-ref v best))))
                  ((= j i) (find (+ j 1) best bd))
                  (else (let ((d (vdist pi (vector-ref v j))))
                          (if (< d bd) (find (+ j 1) j d)
                              (find (+ j 1) best bd)))))))
        (loop (+ i 1))))))
;; dynamic: a bright circular packet + a fading trail travelling each link
(define COMM-TN 9)                         ; trail sample count
(define COMM-DT 0.04)                      ; trail sample spacing (in arc param s)
(define (comm-packet p0 p1 arcH s)
  ;; bright head only (trail hidden)
  (dyn! (glow-box (arc-pt p0 p1 arcH s) (vector 0.055 0.055 0.055) C-COMM 0.95)))
(define (comm-dots)
  (let loop ((ls *comm-links*) (i 0))
    (unless (null? ls)
      (let* ((p0 (caar ls)) (p1 (cdar ls))
             (arcH (* (vdist p0 p1) 0.42))
             (s   (fract (+ (* (time) COMM-SPD) (* 0.37 i)))))
        (comm-packet p0 p1 arcH s))
      (loop (cdr ls) (+ i 1)))))

;; over-industrialisation clutter: a ring of small pipe/tank cubes around a
;; structure, growing in number with the densification intensity.
(define (industrial-annex cx cz id)
  (let ((n (inexact->exact (floor (* 5 (indus)))))
        (wire (wire-col (hsh (+ id 23)))))
    (let loop ((k 0))
      (when (< k n)
        (let* ((a  (* 6.2831853 (hsh (+ id (* k 7) 50))))
               (rr (* BW (+ 0.34 (* 0.16 (hsh (+ id k 60))))))
               (ax (+ cx (* rr (cos a)))) (az (+ cz (* rr (sin a))))
               (ah (* BW (+ 0.12 (* 0.34 (hsh (+ id k 70)))))))
          (hl-box ax az 0 (* BW 0.14) ah (* BW 0.14) wire))
        (loop (+ k 1))))))

;; build the whole static scene for the CURRENT growth front: factory inside,
;; forest outside. Called only on a growth flip (see maybe-rebuild-site!).
(define (build-site)
  (let ((tops '()))
    (let ly ((gz 0))
      (when (< gz GRID)
        (let lx ((gx 0))
          (when (< gx GRID)
            (let* ((id (+ gx (* gz GRID)))
                   (cx (+ (- HALF) (* (+ gx 0.5) CELL)))
                   (cz (+ (- HALF) (* (+ gz 0.5) CELL))))
              (if (cell-factory? gx gz)
                  (when (cell-occ? id)          ; factory (empty pads stay bare)
                    (cell-birth! id)             ; record first-built time (raise anim)
                    ;; grow taller with industrialisation, and RISE from the ground
                    ;; over RAISE-DUR when first constructed
                    (let ((h (* (cell-base-h gx gz) (+ 1.0 (* 0.6 (indus)))
                                (max 0.02 (cell-raise id)))))
                      (build-structure cx cz h (cell-sty id)
                                       (wire-col (hsh (+ id 23))) id)
                      (when (> (indus) 0.3) (industrial-annex cx cz id))
                      (set! tops (cons (vector cx (cell-top-h id h) cz) tops))))
                  (when (forest-cell? id)        ; forest ring, not yet razed
                    (tree cx cz id))))
            (lx (+ gx 1))))
        (ly (+ gz 1))))
    (comm-links tops)))
;; rebuild when the evolution step advances (front/density) — OR every frame while
;; the construction animation is live, so buildings visibly rise from the ground.
;; Once fully evolved + settled, it holds and only rebuilds on a step change.
(define *built-step* -1)
(define (maybe-rebuild-site!)
  (let ((s (evo-step))
        (anim (< *evo-elapsed* (+ GROW-TOTAL RAISE-DUR 0.5))))
    (when (or anim (not (= s *built-step*)))
      (for-each destroy *site-ids*)
      (set! *site-ids* '())
      (set! *in-site* #t)
      (build-site)
      (set! *in-site* #f)
      (set! *built-step* s))))
;; animated site prims — smoke / core lights / beacons for CURRENT factory cells
(define (site-dynamic)
  (for-each-cell
    (lambda (gx gz id cx cz h sty wire)
      (when (cell-factory? gx gz)
        (cond ((< sty 0.13) (cooling-smoke cx cz h id))
              ((< sty 0.25) (reactor-light cx cz h id))
              ((< sty 0.42) (stack-smoke cx cz h id))
              (else         (void)))
        ;; site beacon on the tallest structures
        (when (> (cell-top-h id h) 2.2)
          (dyn! (glow-box (vector cx (+ (cell-top-h id h) 0.16) cz)
                          (vector 0.05 0.05 0.05) C-RED
                          (+ 0.25 (* 0.75 (abs (sin (+ (* 2.2 (time))
                                                       id))))))))))))

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
;; ---- traffic: vehicles make trips BETWEEN buildings, stop-and-go -----------
;; Each vehicle runs A->B along the road grid (move in X, then Z), easing out of
;; the origin and braking into the destination (start-stop), then parks briefly
;; before the next leg. Endpoints are factory buildings only, so nothing drives
;; on the forest. Stateless: the trip is a pure function of (vehicle, time-slot);
;; each leg's destination is the next leg's origin, so trips chain continuously.
(define NCARS 14)
(define TRIP-DUR 5.5)                      ; seconds per building-to-building leg
(define (fac-cell seed)                    ; a factory+occupied cell centre, or #f
  (let loop ((k 0))
    (if (> k 12) #f
        (let* ((gx (modulo (inexact->exact (floor (* (hsh (+ seed (* 0.13 k) 0.1)) GRID))) GRID))
               (gz (modulo (inexact->exact (floor (* (hsh (+ seed (* 0.31 k) 0.7)) GRID))) GRID)))
          (if (and (cell-factory? gx gz) (cell-occ? (+ gx (* gz GRID))))
              (vector (+ (- HALF) (* (+ gx 0.5) CELL)) CARY
                      (+ (- HALF) (* (+ gz 0.5) CELL)))
              (loop (+ k 1)))))))
;; route on the ROAD GRID (cell edges), never through building centres: stub from
;; the origin cell to its nearest road, run an X-corridor then a Z-corridor along
;; edge-roads (the clear gaps between buildings), then stub into the destination.
(define (snap-road w) (+ (- HALF) (* (round (/ (+ w HALF) CELL)) CELL)))
(define (route a b)
  ;; A -> [perp. exit stub in Z] -> X-corridor (edge road) -> Z-corridor (edge road)
  ;; -> [perp. entry stub in X] -> B. Corridors run on cell edges (clear of building
  ;; footprints); the only moves that touch a building are the half-cell stubs into
  ;; its own cell, straight/perpendicular to the face.
  (let* ((ax (vx a)) (az (vz a)) (bx (vx b)) (bz (vz b))
         (azr (snap-road az))                 ; horizontal road bordering A
         (bxr (snap-road bx)))                ; vertical road bordering B
    (list a
          (vector ax  CARY azr)               ; exit A straight (Z) onto a road
          (vector bxr CARY azr)               ; X-corridor along that road
          (vector bxr CARY bz)                ; Z-corridor along B's side road
          b)))                                ; enter B straight (X), perpendicular
(define (poly-pos pts p)                    ; position at fraction p by arc-length
  (let* ((segs (let loop ((ps pts) (acc '()))
                 (if (null? (cdr ps)) (reverse acc)
                     (loop (cdr ps) (cons (vdist (car ps) (cadr ps)) acc)))))
         (total (max 0.0001 (apply + segs)))
         (target (* p total)))
    (let loop ((ps pts) (sl segs) (acc 0.0))
      (if (null? sl) (last pts)
          (let ((nx (+ acc (car sl))))
            (if (>= nx target)
                (vmix (car ps) (cadr ps) (/ (- target acc) (max 0.0001 (car sl))))
                (loop (cdr ps) (cdr sl) nx)))))))
(define (trip-pos a b p) (poly-pos (route a b) p))
(define (trip-ease p) (smoothstep (min 1.0 (/ p 0.72))))  ; accel/brake, park at end
(define (car-col i)
  (let ((r (hsh (+ i 5))))
    (cond ((< r 0.30) C-RED)
          ((< r 0.50) C-GREEN)
          (else       (vector 1.0 0.72 0.30)))))
(define (vehicle i t)
  (let* ((dur  (+ TRIP-DUR (* 4.0 (hsh (+ i 20)))))  ; per-vehicle leg duration
         (tt   (+ t (* 11.0 (hsh (+ i 30)))))        ; per-vehicle phase offset
         (slot (floor (/ tt dur)))
         (prog (fract (/ tt dur)))
         (a (fac-cell (+ i (* slot 1.7))))          ; this leg's origin
         (b (fac-cell (+ i (* (+ slot 1) 1.7)))))   ; = next leg's origin (chained)
    (when (and a b (> (vdist a b) 0.1))
      (let* ((pts (route a b)) (np (length pts))
             (col (car-col i)) (T 4)
             (headp (poly-pos pts (trip-ease prog))))
        ;; glitchy trajectory line: the full planned route, flickering on/off like
        ;; a nav / targeting overlay
        ;; draw each route segment as its own straight ribbon — a single ribbon
        ;; folds/pinches at the 90-degree corners; per-segment keeps them straight.
        (let ((fl (if (> (fract (+ (* (time) 7.0) (* i 0.37))) 0.22) 0.75 0.25)))
          (let seg ((ps pts))
            (when (and (pair? ps) (pair? (cdr ps)))
              (let* ((p0 (car ps)) (p1 (cadr ps))
                     (len (vdist p1 p0))
                     (e   (if (> len 0.001) (vmul (vsub p1 p0) (/ 0.012 len)) (vector 0 0 0)))
                     (q0  (vadd (vsub p0 e) (vector 0 0.02 0)))    ; extend past both ends
                     (q1  (vadd (vadd p1 e) (vector 0 0.02 0))))   ; so corners overlap
                (let ((rb (dyn! (build-ribbon 2))))
                  (with-primitive rb
                    (identity) (hint-unlit) (colour C-GREEN) (opacity fl)
                    (pdata-index-map! (lambda (k v) (if (= k 0) q0 q1)) "p")
                    (pdata-index-map! (lambda (k w) (vector 0.022 0.022 0.022)) "w"))))
              (seg (cdr ps)))))
        ;; short fading trail behind the head
        (let ((rb (dyn! (build-ribbon T))))
          (with-primitive rb
            (identity) (hint-unlit) (colour col)
            (pdata-index-map!
              (lambda (k v) (poly-pos pts (trip-ease (max 0.0 (- prog (* k 0.014)))))) "p")
            (pdata-index-map!
              (lambda (k w) (let ((tp (* 0.05 (- 1.0 (/ k (- T 1.0)))))) (vector tp tp tp))) "w")))
        (dyn! (glow-box headp (vector 0.05 0.05 0.05) col 0.9))))))   ; bright vehicle
(define (traffic)
  (let ((t (time)))
    (let loop ((i 0))
      (when (< i NCARS)
        (vehicle i t)
        (loop (+ i 1))))))

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
      (if (or (and (cell-occ? (+ gx (* gz GRID))) (cell-factory? gx gz)) (> k 30))
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
    ;; caption title: camera-facing (yaw only), sitting on the horizontal bar.
    ;; Persistent text prim (rebuilt only when the string changes); position it
    ;; each frame by grabbing it.
    (let ((tp (cap-text! shown))
          (start (if (> side 0)
                     (vadd elbow (vmul s 0.08))
                     (vadd tip   (vmul s 0.08)))))
      (when (>= tp 0)
        (with-primitive tp
          (identity) (hint-unlit)
          (translate (vadd start (vector 0 0.07 0)))
          (rotate (vector 0 (* az 57.2958) 0))
          (scale (vector (/ sc 0.9) (/ sc 0.9) (/ sc 0.9)))
          (colour ccol))))))

;; ---- HUD infographic: screen-pinned readout of forest vs industrial --------
;; Pinned to the camera (position built from the camera basis, so it stays put
;; on screen as the view orbits) — a live biome monitor of the environmental
;; cost: trees remaining vs cells industrialised, and % razed.
;; NB: fov is ~12deg (telephoto) so the visible extent at HUD-D is small — offsets
;; and scale are correspondingly tiny (visible ~+/-0.18 wide, +/-0.32 tall @ D=3).
(define HUD-D  3.0)                         ; distance in front of the eye
(define HUD-X -0.150)                       ; screen-left offset
(define HUD-Y0 0.250)                       ; top line offset
(define HUD-LH 0.055)                       ; line spacing
(define HUD-SC 0.050)                       ; text size
(define (hud-line row col s)
  (when (> (string-length s) 0)
    ;; subtle CRT-terminal character: a gentle brightness breathe, per line — just
    ;; enough to feel alive without flickering.
    (let* ((flk (+ 0.93 (* 0.07 (sin (+ (* (time) 2.2) (* row 0.9))))))
           (pos (vadd *cam-eye*
                  (vadd (vmul *cam-fwd* HUD-D)
                    (vadd (vmul *cam-right* HUD-X)
                          (vmul *cam-up* (- HUD-Y0 (* row HUD-LH)))))))
           (tp (dyn! (build-text s))))
      (with-primitive tp
        (identity) (hint-unlit)
        (translate pos)
        (rotate (vector 0 (* *cam-az* 57.2958) 0))   ; yaw-billboard to the camera
        (scale (vector HUD-SC HUD-SC HUD-SC))
        (colour (vmul col flk))))))
;; bottom-right credit, right-aligned + dim
(define (hud-credit)
  (let* ((s "made with FLUXUS")
         (sc 0.030)
         (w  (* (string-length s) CW (/ sc 0.9)))     ; approx text width
         (pos (vadd *cam-eye*
                (vadd (vmul *cam-fwd* HUD-D)
                  (vadd (vmul *cam-right* (- 0.165 w))  ; right edge minus width
                        (vmul *cam-up* -0.225)))))      ; bottom
         (tp (dyn! (build-text s))))
    (with-primitive tp
      (identity) (hint-unlit)
      (translate pos)
      (rotate (vector 0 (* *cam-az* 57.2958) 0))
      (scale (vector sc sc sc))
      (colour (vmul C-SMOKE 0.5)))))
(define (hud-infographic)
  (let ((trees 0) (fac 0))
    (let ly ((gz 0)) (when (< gz GRID)
      (let lx ((gx 0)) (when (< gx GRID)
        (let ((id (+ gx (* gz GRID))))
          (if (cell-factory? gx gz)
              (when (cell-occ? id)    (set! fac   (+ fac 1)))
              (when (forest-cell? id) (set! trees (+ trees 1)))))
        (lx (+ gx 1)))) (ly (+ gz 1))))
    ;; CO2 as absolute mass (kt): per-cell emission grows with densification, so
    ;; it keeps climbing after full coverage as the zone over-industrialises.
    (let ((co2 (inexact->exact (floor (* fac (+ 12 (* 62 (indus))))))))
      (hud-line 0 C-GREEN "BIOSPHERE")
      (hud-line 1 C-TREE  (string-append "FOREST " (number->string trees)))
      (hud-line 2 C-EDGE  (string-append "INDUS  " (number->string fac)))
      (hud-line 3 C-RED   (string-append "CO2 " (number->string co2) "kt"))
      ;; growth-speed readout — only while off 1x (+/- keys), keeps the clip clean
      (when (not (= *grow-speed* 1.0))
        (hud-line 4 C-EDGE
          (string-append "GROWTH x"
            (number->string (/ (round (* *grow-speed* 100)) 100))))))
    ;; (hud-credit)                          ; "made with FLUXUS" (hidden)
    ))

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
  // wide bloom: 8-tap near ring + 4-tap far ring (bigger radius = softer glow)
  vec2 px = 3.6 / resolution;
  vec3 bl = vec3(0.0);
  bl += texture2D(tex, u + vec2( px.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(-px.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(0.0,  px.y)).rgb;
  bl += texture2D(tex, u + vec2(0.0, -px.y)).rgb;
  bl += texture2D(tex, u + vec2( px.x,  px.y)).rgb;
  bl += texture2D(tex, u + vec2(-px.x,  px.y)).rgb;
  bl += texture2D(tex, u + vec2( px.x, -px.y)).rgb;
  bl += texture2D(tex, u + vec2(-px.x, -px.y)).rgb;
  vec2 fx = 12.0 / resolution;
  bl += texture2D(tex, u + vec2( fx.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(-fx.x, 0.0)).rgb;
  bl += texture2D(tex, u + vec2(0.0,  fx.y)).rgb;
  bl += texture2D(tex, u + vec2(0.0, -fx.y)).rgb;
  bl /= 12.0;
  col = mix(col, bl, 0.20);                    // soft blur (dreamy haze)
  col += max(bl - 0.10, 0.0) * 2.4;            // phosphor glow (brighter, wider)
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

;; Retained + persistent scene. streets + powerline are built ONCE and persist.
;; The site (forest + factory) is GENERATIVE: it rebuilds only when the growth
;; front crosses a cell (maybe-rebuild-site!), not every frame — so it stays cheap
;; while the factory slowly reclaims the forest. The thunk otherwise destroys +
;; rebuilds only the animated prims (smoke, beacons, traffic, caption).
(retained)
(terrain)                                  ; rolling noise terrain \  built once
(streets)                                  ; static ground grid    } (persist)
(powerline-static)                         ; static HV line       /
(every-frame
  (begin
    (clear-dyn!)                           ; remove last frame's animated prims
    (pool-frame-begin!)                    ; reset persistent smoke-pool cursor
    (let ((k (key-poll)))
      (when (or (= k 114) (= k 82)) (reset-anim!))    ; R -> restart the animation
      (when (or (= k 118) (= k 86)) (start-record))   ; V -> record a 1-min video
      (when (or (= k 43) (= k 61)) (grow-faster!))    ; + / = -> faster industrial growth
      (when (or (= k 45) (= k 95)) (grow-slower!)))   ; - / _ -> slower industrial growth
    (evo-tick!)                            ; advance the virtual growth clock (once/frame)
    (when *rec*                            ; count rendered frames; auto-stop at REC-LEN
      (set! *rec-frames* (+ *rec-frames* 1))
      (when (>= *rec-frames* (inexact->exact (floor (* REC-LEN REC-FPS))))
        (set-export #f "" 0) (set! *rec* #f)))
    (maybe-rebuild-site!)                  ; regrow factory / raze forest on a flip
    (iso-camera)
    (site-dynamic)                         ; smoke, core lights, site beacons
    (comm-dots)                            ; packets travelling the comm arcs
    (powerline-dynamic)                    ; pylon beacons + energy pulses
    (traffic)                              ; vehicle trails (factory roads only)
    (caption)                              ; bracket + leader + typewriter title
    (hud-infographic)                      ; screen-pinned forest/industrial readout
    (post-shader crt)))
