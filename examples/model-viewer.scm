;; model viewer — LEFT / RIGHT arrow cycles through assets/models/
;;
;; Scans assets/models/<name>/ for the first loadable model file in each folder,
;; loads one at a time, auto-fits it to the view and plays its first animation
;; clip if it has one. Racket-only (it uses directory-list); the s7 apps have no
;; filesystem access.
;;
;;   LEFT / RIGHT arrow   previous / next model
;;   UP / DOWN arrow      previous / next animation clip of this model
;;   SPACE                pause / resume the animation
;;   M                    draw mode: fill / points / wireframe / hidden-line
;;                        (openFrameworks' OF_MESH_FILL / _POINTS / _WIREFRAME,
;;                         plus fluxus's own occluded-wire mode)
;;   K                    skinning: dual quaternion / linear blend
;;   S                    turntable spin on / off (off by default — a spinning
;;                        rig makes it impossible to tell a pose from an angle)
;;   R                    back to the front view
;;
;; Note on what you see: K switches between dual-quaternion skinning (the default)
;; and the engine's linear blend. Linear is exactly what assimp/glTF define — this
;; port's posed vertices match a from-scratch glTF-spec skinner to within float
;; noise — but on a long-limbed rig a clip that folds an arm across the body
;; collapses the elbow, the classic "candy wrapper". Dual quaternions keep the
;; joint's volume. If a pose still looks odd in both, try another clip (UP / DOWN)
;; before blaming the loader.
;;
;; Arrow keys reach a sketch as (key-down? 1) .. (key-down? 4) — JUCE's arrow key
;; codes are too large for the key-down table, so the message thread maps them into
;; the unused control-char slots 1=left 2=right 3=up 4=down (see CLAUDE.md). Click
;; the window first: key-down? reads global OS state, so an unfocused window still
;; "works" but the arrows also beep in whatever app IS focused.

(retained)
(clear)
(background (vector 0.05 0.06 0.08))

;; The held-key poll only runs while the editor is HIDDEN — otherwise the editor
;; owns the keyboard and (key-down? …) never sees anything. Ctrl+E brings the
;; editor back when you want to change this file.
(hide-editor)

;; models carry real materials, so they need a light to be anything but silhouettes
(define key-light (make-light 'point 'free))
(light-diffuse key-light (vector 1 0.97 0.92))
(light-position key-light (vector 8 12 10))
(define fill-light (make-light 'point 'free))
(light-diffuse fill-light (vector 0.25 0.3 0.4))
(light-position fill-light (vector -10 2 -6))

;; ---- where the models live -------------------------------------------------
;; The .app has no useful current directory, so try the usual spots and take the
;; first that exists. Add your own path here if you keep models elsewhere.
(define *candidate-roots*
  (list "assets/models"
        (string-append (path->string (find-system-path 'home-dir))
                       "work/bauhouse/juce-test/fluxus-port/assets/models")
        "/Users/manticore/work/bauhouse/juce-test/fluxus-port/assets/models"))

(define *model-exts* (list ".fbx" ".gltf" ".glb" ".dae" ".ply" ".stl" ".obj" ".3ds"))

(define (has-model-ext? name)
  (let loop ((es *model-exts*))
    (cond ((null? es) #f)
          ((let* ((e (car es)) (n (string-length name)) (m (string-length e)))
             (and (> n m) (string-ci=? (substring name (- n m) n) e))) #t)
          (else (loop (cdr es))))))

(define *root*
  (let loop ((rs *candidate-roots*))
    (cond ((null? rs) #f)
          ((directory-exists? (car rs)) (car rs))
          (else (loop (cdr rs))))))

;; one entry per subfolder: (name . absolute-path-of-its-model-file)
(define *models*
  (if (not *root*)
      '()
      (let loop ((dirs (sort (map path->string (directory-list *root*)) string<?)) (out '()))
        (if (null? dirs)
            (reverse out)
            (let* ((d (car dirs))
                   (full (string-append *root* "/" d)))
              (if (not (directory-exists? full))
                  (loop (cdr dirs) out)
                  (let inner ((fs (sort (map path->string (directory-list full)) string<?)))
                    (cond ((null? fs) (loop (cdr dirs) out))
                          ((has-model-ext? (car fs))
                           (loop (cdr dirs)
                                 (cons (cons d (string-append full "/" (car fs))) out)))
                          (else (inner (cdr fs)))))))))))

;; ---- state (retained mode: it lives in these top-level boxes) ---------------
(define *index*   (box 0))
(define *current* (box -1))      ; model handle
(define *spin*    (box 0.0))
(define *paused*  (box #f))
(define *t0*      (box 0.0))     ; animation clock offset, so pausing works
(define *label*   (box -1))
(define *clip*    (box 0))
(define *spinning* (box #f))
(define *draw-modes* (list 'fill 'points 'wireframe 'hidden-line))
(define *draw*    (box 0))
(define *skin*    (box 'dual))
(define *fit-scale* (box 1.0))
(define *fit-mid*   (box (vector 0 0 0)))
(define *prev-keys* (box (list #f #f #f #f #f #f #f)))  ; left right space up down mode skin

;; ---- fitting ---------------------------------------------------------------
;; Models arrive in whatever units the file used (the fox is ~157 long, the druid
;; ~3), so measure the bounds and scale to a fixed on-screen size.
;;
;; Measured straight from pdata rather than (get-bb): the engine's bounding box is
;; lazily computed and comes back EMPTY for a freshly built primitive, which made
;; the fit divide by ~0 and blow the model up to 2600x.
(define (prim-bounds mn mx)
  (let ((n (pdata-size)))
    (let loop ((i 0) (mn mn) (mx mx))
      (if (>= i n)
          (list mn mx)
          (let ((p (pdata-ref "p" i)))
            (loop (+ i 1)
                  (if mn (vector (min (vx mn) (vx p)) (min (vy mn) (vy p)) (min (vz mn) (vz p))) p)
                  (if mx (vector (max (vx mx) (vx p)) (max (vy mx) (vy p)) (max (vz mx) (vz p))) p)))))))

(define (model-bounds h)
  (let loop ((ps (model-prims h)) (mn #f) (mx #f))
    (if (null? ps)
        (list (or mn (vector -1 -1 -1)) (or mx (vector 1 1 1)))
        (let ((b (with-primitive (car ps) (prim-bounds mn mx))))
          (loop (cdr ps) (car b) (cadr b))))))

;; The fit is recomputed into the root transform EVERY frame together with the
;; spin, rather than accumulating (rotate …) on top of it. Ops apply in REVERSE
;; order to the geometry, so "(rotate)(scale)(translate -mid)" means centre first,
;; then scale, then spin — i.e. the model turns about its own middle. Accumulating
;; a rotate on the fitted transform instead spins it about the model's file origin
;; (its feet), which walks it out of frame and looks like the camera is wrong.
(define (fit! h)
  (let* ((b   (model-bounds h))
         (lo  (car b)) (hi (cadr b))
         (size (vsub hi lo))
         (ext (max 0.001 (max (vx size) (max (vy size) (vz size)))))
         (s   (/ 2.6 ext))
         (mid (vmul (vadd lo hi) 0.5)))
    (set-box! *fit-scale* s)
    (set-box! *fit-mid* mid)
    (place! h)
    (display (list 'model-viewer 'extent ext 'scale s 'centre mid)) (newline) (flush-output)))

(define (place! h)
  (let ((s (unbox *fit-scale*)) (mid (unbox *fit-mid*)))
    (with-model h
      (identity)
      (rotate (vector 0 (unbox *spin*) 0))
      (scale (vector s s s))
      (translate (vmul mid -1)))))

;; ---- labels ----------------------------------------------------------------
;; "<name>  [3/6]  clip 1/3" pinned to the camera node, i.e. drawn in eye space
(define (relabel!)
  (when (>= (unbox *label*) 0) (destroy (unbox *label*)) (set-box! *label* -1))
  (when (not (null? *models*))
    (let* ((h (unbox *current*))
           (entry (list-ref *models* (unbox *index*)))
           (clips (if (model-ok? h) (model-anim-count h) 0))
           (txt (string-append (car entry)
                               "  [" (number->string (+ 1 (unbox *index*)))
                               "/" (number->string (length *models*)) "]"
                               (if (> clips 0)
                                   (string-append "  clip " (number->string (+ 1 (unbox *clip*)))
                                                  "/" (number->string clips))
                                   "  (no animation)")
                               "  " (symbol->string (list-ref *draw-modes* (unbox *draw*)))
                               (if (> clips 0)
                                   (string-append "  " (symbol->string (unbox *skin*)))
                                   ""))))
      (set-box! *label*
                (with-state
                  (parent (camera-node))
                  (hint-unlit)
                  (colour (vector 0.9 0.95 1))
                  ;; eye space: -Z is forward, so this sits top-left of the view.
                  ;; The fov is telephoto (~12°), so the visible extent at Z=-1 is
                  ;; small and these offsets look tiny next to normal world units.
                  (translate (vector -0.155 0.085 -1.0))
                  (scale (vector 0.0075 0.0075 0.0075))
                  (build-text txt))))))

;; ---- loading ---------------------------------------------------------------
(define (show! i)
  (when (not (null? *models*))
    (let ((n (length *models*)))
      (set-box! *index* (modulo i n))
      ;; destroying the root takes the meshes AND the skeleton locators with it
      (when (>= (unbox *current*) 0)
        (destroy (model-root (unbox *current*)))
        (model-free (unbox *current*))
        (set-box! *current* -1))
      (let* ((entry (list-ref *models* (unbox *index*)))
             (h (load-model (cdr entry))))
        (set-box! *current* h)
        (set-box! *spin* 0.0)
        (set-box! *clip* 0)
        (set-box! *t0* (time))
        (if (model-ok? h)
            (begin
              ;; fit the FIRST ANIMATED pose, not the bind pose: a rigged model's
              ;; rest layout (arms out, staff held away from the body) can be twice
              ;; the size of anything the clip actually shows, and fitting that
              ;; leaves the animation looking tiny
              (when (model-animated? h) (model-play h 0 0))
              (fit! h)
              (model-draw-mode h (list-ref *draw-modes* (unbox *draw*))))
            (display (string-append "model-viewer: " (cdr entry) " failed: " (model-error) "\n")))
        (relabel!)))))

;; ---- input -----------------------------------------------------------------
;; key-down? is a LEVEL, so remember the previous state and act on the edge —
;; otherwise one press steps through every model in the folder.
(define (edge! slot which)
  (let* ((now (key-down? slot))
         (prev (list-ref (unbox *prev-keys*) which))
         (fired (and now (not prev))))
    (set-box! *prev-keys*
              (let loop ((i 0) (ks (unbox *prev-keys*)) (out '()))
                (if (null? ks) (reverse out)
                    (loop (+ i 1) (cdr ks) (cons (if (= i which) now (car ks)) out)))))
    fired))

(define (cycle-clip! d)
  (let ((h (unbox *current*)))
    (when (model-animated? h)
      (let ((n (model-anim-count h)))
        (set-box! *clip* (modulo (+ (unbox *clip*) d) n))
        (set-box! *t0* (time))
        (relabel!)))))

(define (handle-keys)
  (when (edge! 1 0) (show! (- (unbox *index*) 1)))      ; left arrow
  (when (edge! 2 1) (show! (+ (unbox *index*) 1)))      ; right arrow
  (when (edge! 3 3) (cycle-clip! -1))                   ; up arrow
  (when (edge! 4 4) (cycle-clip!  1))                   ; down arrow
  (when (edge! 32 2)                                    ; space
    (set-box! *paused* (not (unbox *paused*))))
  (when (edge! 109 5)                                   ; M: cycle the draw mode
    (set-box! *draw* (modulo (+ 1 (unbox *draw*)) (length *draw-modes*)))
    (when (model-ok? (unbox *current*))
      (model-draw-mode (unbox *current*) (list-ref *draw-modes* (unbox *draw*))))
    (relabel!))
  (when (edge! 107 6)                                   ; K: dual <-> linear skinning
    (set-box! *skin* (if (eq? (unbox *skin*) 'dual) 'linear 'dual))
    (model-skinning (unbox *skin*))
    (relabel!))
  (when (key-down? 115)                                 ; S: turntable on/off
    (set-box! *spinning* (not (unbox *spinning*))))
  (when (key-down? 114)                                 ; R: back to the front view
    (set-box! *spin* 0.0)
    (when (model-ok? (unbox *current*)) (place! (unbox *current*)))))

;; ---- frame -----------------------------------------------------------------
(define (animate)
  (handle-keys)
  (let ((h (unbox *current*)))
    (when (model-ok? h)
      (when (and (model-animated? h) (not (unbox *paused*)))
        (model-play h (unbox *clip*) (- (time) (unbox *t0*))))
      ;; Spin is OFF by default (S toggles it). A turntable makes a rigged model
      ;; much harder to read — you cannot tell a pose from a camera angle, and
      ;; every skinning question turns into "is that the animation or the spin?".
      (when (and (unbox *spinning*) (not (unbox *paused*)))
        (set-box! *spin* (+ (unbox *spin*) 0.35))
        (place! h)))))

(if (null? *models*)
    (begin
      (display "model-viewer: no models found — put some under assets/models/<name>/\n")
      (with-state (hint-solid #f) (hint-wire) (hint-unlit)
                  (wire-colour (vector 1 0.3 0.2)) (build-cube)))
    (show! 0))

(every-frame (animate))
