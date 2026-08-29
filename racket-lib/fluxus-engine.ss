#lang racket/base
;; fluxus->JUCE port: the "fluxus-engine" module the fluxus .ss library requires.
;;
;; The real engine commands are wired to the C engine via FFI (flux_* symbols
;; exported by the app). Each get-ffi-obj has a FAILURE-THUNK fallback, so this
;; module ALSO loads standalone on the racket CLI (where flux_* don't exist) —
;; there the commands are harmless stubs, which is all the .ss-compat test needs.
(require ffi/unsafe ffi/vector)
;; building-blocks.ss defines vx/vy/vz + with-state/with-primitive itself.
(provide (except-out (all-defined-out) vx vy vz vw with-state with-primitive))

(define (->fl x) (exact->inexact x))

;; get a C function, or a stub if the symbol isn't present (CLI / no engine)
(define (cfun cname type stub) (get-ffi-obj cname #f type (lambda () stub)))

;; vector component accessors
(define (vx v) (vector-ref v 0))
(define (vy v) (vector-ref v 1))
(define (vz v) (vector-ref v 2))
(define (vw v) (vector-ref v 3))

;; vector/matrix maths the real building-blocks.ss re-exports from the engine
(define (vadd a b) (list->vector (map + (vector->list a) (vector->list b))))
(define (vsub a b) (list->vector (map - (vector->list a) (vector->list b))))
(define (vdot a b) (apply + (map * (vector->list a) (vector->list b))))
(define (vmag v)   (sqrt (vdot v v)))
(define (vdist a b) (vmag (vsub a b)))
(define (vcross a b)
  (vector (- (* (vy a) (vz b)) (* (vz a) (vy b)))
          (- (* (vz a) (vx b)) (* (vx a) (vz b)))
          (- (* (vx a) (vy b)) (* (vy a) (vx b)))))
(define (vnormalise v) (let ((m (vmag v))) (if (zero? m) v (list->vector (map (lambda (x) (/ x m)) (vector->list v))))))
(define vnormalize vnormalise)
(define (mmul . _) (vector 1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1))
(define (madd . _) (mmul)) (define (msub . _) (mmul)) (define (mdiv . _) (mmul))

;; ---- real engine commands (FFI, vector-shaped like fluxus) -----------------
(define _cube  (cfun "flux_build_cube"   (_fun -> _int) (lambda () 0)))
(define _plane (cfun "flux_build_plane"  (_fun -> _int) (lambda () 0)))
(define _segplane (cfun "flux_build_seg_plane" (_fun _int _int -> _int) (lambda (a b) 0)))
(define _sph   (cfun "flux_build_sphere" (_fun _int _int -> _int) (lambda (a b) 0)))
(define _tor   (cfun "flux_build_torus"  (_fun _double _double _int _int -> _int) (lambda (a b c d) 0)))
(define (build-cube) (_cube))
(define (build-plane) (_plane))
(define (build-seg-plane (x 10) (y 10)) (_segplane x y))
(define (build-sphere (sl 10) (st 10)) (_sph sl st))
(define (build-torus (i 0.5) (o 1.0) (sl 12) (st 12)) (_tor (->fl i) (->fl o) sl st))
;; fluxus draw-* : immediate draw of a template shape at the current state. This
;; immediate-mode port has no retained template, so they just build the shape
;; (cleared next frame like everything else).
(define (draw-cube)   (_cube))
(define (draw-plane)  (_plane))
(define (draw-sphere) (_sph 10 10))
(define (draw-torus)  (_tor 0.5 1.0 12 12))

(define _bg  (cfun "flux_background" (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _col (cfun "flux_colour"     (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _tr  (cfun "flux_translate"  (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _rot (cfun "flux_rotate"     (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _scl (cfun "flux_scale"      (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define (background v) (_bg  (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (colour v)     (_col (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (color v)      (colour v))
(define (translate v)  (_tr  (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (rotate v)     (_rot (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (scale v)      (_scl (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))

(define identity (cfun "flux_identity" (_fun -> _void) (lambda () (void))))
(define push     (cfun "flux_push"     (_fun -> _void) (lambda () (void))))
(define pop      (cfun "flux_pop"      (_fun -> _void) (lambda () (void))))

(define _hw (cfun "flux_hint_wire"  (_fun _int -> _void) (lambda (x) (void))))
(define _hs (cfun "flux_hint_solid" (_fun _int -> _void) (lambda (x) (void))))
(define _lw (cfun "flux_line_width" (_fun _double -> _void) (lambda (x) (void))))
(define (hint-wire  (on #t)) (_hw (if on 1 0)))
(define (hint-solid (on #t)) (_hs (if on 1 0)))
(define (line-width w) (_lw (->fl w)))

(define _time  (cfun "flux_time"  (_fun -> _double) (lambda () 0.0)))
(define _frame (cfun "flux_frame" (_fun -> _int)    (lambda () 0)))
(define _delta (cfun "flux_delta" (_fun -> _double) (lambda () 0.0)))
(define (time)  (_time))
(define (frame) (_frame))
(define (delta) (_delta))

;; audio-reactive (FFI): (gh n) harmonic band, (gain) overall level
(define _gh   (cfun "flux_audio_harmonic" (_fun _int -> _double) (lambda (n) 0.0)))
(define _gain (cfun "flux_audio_gain"     (_fun -> _double)      (lambda () 0.0)))
(define (gh n) (_gh n))
(define (gain) (_gain))

;; mouse (FFI) — camera orbit is host-side; scripts can still read the mouse
(define _mx (cfun "flux_mouse_x"      (_fun -> _double) (lambda () 0.0)))
(define _my (cfun "flux_mouse_y"      (_fun -> _double) (lambda () 0.0)))
(define _mb (cfun "flux_mouse_button" (_fun -> _int)    (lambda () 0)))
(define (mouse-x) (_mx))
(define (mouse-y) (_my))
(define (mouse-button) (_mb))
;; mouse-driven camera params (wheel dolly + drag orbit), readable by scripts
(define _cdist (cfun "flux_camera_dist"  (_fun -> _double) (lambda () 10.0)))
(define _cyaw  (cfun "flux_camera_yaw"   (_fun -> _double) (lambda () 0.0)))
(define _cpit  (cfun "flux_camera_pitch" (_fun -> _double) (lambda () 0.0)))
(define (camera-dist)  (_cdist))
(define (camera-yaw)   (_cyaw))
(define (camera-pitch) (_cpit))

;; fluxus (with-state ...) — save/run/restore transform+colour
(define-syntax-rule (with-state body ...) (begin (push) (let ((r (begin body ...))) (pop) r)))

;; ---- still stubbed (no FFI yet) --------------------------------------------
(define-syntax-rule (stub-void name ...) (begin (define (name . _) (void)) ...))
(define-syntax-rule (stub-id   name ...) (begin (define (name . _) 0) ...))

(define _rib (cfun "flux_build_ribbon"    (_fun _int -> _int) (lambda (n) 0)))
(define _par (cfun "flux_build_particles" (_fun _int -> _int) (lambda (n) 0)))
(define _nsp (cfun "flux_build_nurbs_sphere" (_fun _int _int -> _int) (lambda (a b) 0)))
(define (build-ribbon n)    (_rib n))
(define (build-particles n) (_par n))
(define (build-nurbs-sphere (h 10) (r 10)) (_nsp h r))
(stub-id build-nurbs
         build-line build-extrusion build-type)
(define _nplane (cfun "flux_build_nurbs_plane" (_fun _int _int -> _int) (lambda (a b) 0)))
(define (build-nurbs-plane (u 5) (v 5)) (_nplane u v))

;; grabbed-prim state (FFI)
(define _op  (cfun "flux_opacity"      (_fun _double -> _void) (lambda (x) (void))))
(define _wo  (cfun "flux_wire_opacity" (_fun _double -> _void) (lambda (x) (void))))
(define _wc  (cfun "flux_wire_colour"  (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _bfc (cfun "flux_backfacecull" (_fun _int -> _void) (lambda (x) (void))))
(define (opacity o) (_op (->fl o)))
(define (wire-opacity o) (_wo (->fl o)))
(define (wire-colour v) (_wc (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (backfacecull on) (_bfc (if (and on (not (zero? on))) 1 0)))

(stub-void concat shader-set! shader
           hint-wire-stippled
           apply-transform clear-colour texture-params)
;; (clear): wipe the scene graph (real, not a stub) — needed at the top of a
;; retained sketch's per-frame thunk so it can rebuild without re-parsing.
(define _scene-clear (cfun "flux_scene_clear" (_fun -> _void) (lambda () (void))))
(define (clear) (_scene-clear))
;; (destroy id): remove one primitive by id — retained mode keeps static geometry
;; alive and destroys+rebuilds only animated prims each frame (persistent scene).
(define destroy (cfun "flux_destroy" (_fun _int -> _void) (lambda (x) (void))))

;; every-frame: registers the body as a thunk AND runs it once. In immediate mode
;; the whole buffer re-evals each frame, so this runs the body every frame (as
;; before). In retained mode the host commits the buffer once and then calls the
;; stored thunk (flux-run-frame) each frame — no rebuild.
(define frame-callback (box (lambda () (void))))
(define-syntax-rule (every-frame body ...)
  (begin (set-box! frame-callback (lambda () body ...))
         ((unbox frame-callback))))
(define (start-audio . _) (void))

;; retained mode opt-in: build once, then per-frame run only the every-frame thunk
(define _retained (cfun "flux_set_retained" (_fun _int -> _void) (lambda (x) (void))))
(define (retained (on #t)) (_retained (if on 1 0)))

;; ---- pdata (FFI, grabbed primitive) ----------------------------------------
(define grab   (cfun "flux_grab"   (_fun _int -> _void) (lambda (x) (void))))
(define ungrab (cfun "flux_ungrab" (_fun -> _void) (lambda () (void))))
(define _psize (cfun "flux_pdata_size" (_fun -> _int) (lambda () 0)))
(define _pget  (cfun "flux_pdata_get"  (_fun _string _int _int -> _double) (lambda (a b c) 0.0)))
(define _pset  (cfun "flux_pdata_set"  (_fun _string _int _int _double -> _void) (lambda (a b c d) (void))))
(define _rn    (cfun "flux_recalc_normals" (_fun -> _void) (lambda () (void))))
(define _padd (cfun "flux_pdata_add"  (_fun _string _string -> _void) (lambda (a b) (void))))
(define _pcpy (cfun "flux_pdata_copy" (_fun _string _string -> _void) (lambda (a b) (void))))
(define (pdata-size) (_psize))
(define (pdata-ref name i) (vector (_pget name i 0) (_pget name i 1) (_pget name i 2)))
(define (pdata-set! name i v) (_pset name i 0 (->fl (vx v))) (_pset name i 1 (->fl (vy v))) (_pset name i 2 (->fl (vz v))))
(define (pdata-add name type) (_padd name type))
(define (pdata-copy src dst) (_pcpy src dst))
(define (recalc-normals) (_rn))
;; native audio deform of the grabbed prim (p = ori + n*disp), whole loop in C++
(define _deform (cfun "flux_deform_audio" (_fun _double _double _double _double _int -> _void)
                      (lambda (a b c d e) (void))))
(define (deform-audio (band-scale 1.0) (wobble 0.0) (freq 6.0) (speed 1.0) (recalc #t))
  (_deform (->fl band-scale) (->fl wobble) (->fl freq) (->fl speed) (if recalc 1 0)))
;; shape cache: snapshot an expensive base shape once, deform cheaply from it
(define _cacheshape (cfun "flux_cache_shape"  (_fun _string -> _void) (lambda (a) (void))))
(define _shapecached (cfun "flux_shape_cached" (_fun _string -> _int) (lambda (a) 0)))
(define _deformc   (cfun "flux_deform_cached" (_fun _string _double _double _double _double _int -> _void)
                        (lambda (a b c d e f) (void))))
(define (cache-shape name) (_cacheshape name))
(define (shape-cached? name) (= 1 (_shapecached name)))
(define (deform-cached name (band-scale 1.0) (wobble 0.0) (freq 6.0) (speed 1.0) (recalc #t))
  (_deformc name (->fl band-scale) (->fl wobble) (->fl freq) (->fl speed) (if recalc 1 0)))
(define-syntax-rule (with-primitive id body ...)
  (begin (grab id) (let ((r (begin body ...))) (ungrab) r)))
(stub-void pdata-op)

(define (mident) (vector 1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1))
(define (get-global-transform . _) (mident))
(define (get-transform . _) (mident))
;; get-camera-transform is wired to the engine via FFI (see camera section above)
(define (get-inv-camera-transform . _) (minverse (get-camera-transform)))
(define (vtransform v . _) v)
(define (vtransform-rot v . _) v)
(define (mtranslate . _) (mident))
(define (mrotate . _) (mident))
(define (mscale . _) (mident))
(define (minverse m) m)
(define (mtranspose m) m)
(define (flxtime) 0.0)
(define (mmul2 . _) (void))
(define (madd2 . _) (void))
(define (msub2 . _) (void))
(define (mdiv2 . _) (void))
(define (poly-type-enum . _) (void)) ;; auto-stub
(define (poly-indexed? . _) (void)) ;; auto-stub
(define (poly-indices . _) (void)) ;; auto-stub
(define (pdata-names . _) (void)) ;; auto-stub
(define (maim . _) (void)) ;; auto-stub
(define (poly-set-index . _) (void)) ;; auto-stub

;; ---- stubs for engine prims used by the loaded .ss libs (not wired to libfluxus)
;; camera.ss quaternion helpers (engine prims)
(define (qmul . _) (vector 0 0 0 1))
(define (qnormalise q) q)
(define (qconjugate q) q)
(define (qtomatrix . _) (mident))
;; ---- script-driven camera (FFI, real engine) -------------------------------
;; matrices marshalled as 16-double f64vectors (column-major, fluxus order)
(define _set-cam-tx (cfun "flux_set_camera_transform" (_fun _f64vector -> _void) (lambda (m) (void))))
(define _get-cam-tx (cfun "flux_get_camera_transform" (_fun _f64vector -> _void) (lambda (m) (void))))
(define _set-cam-pos (cfun "flux_set_camera_position" (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _cam-reset  (cfun "flux_camera_reset"    (_fun -> _void) (lambda () (void))))
(define _set-fov    (cfun "flux_set_fov"         (_fun _double -> _void) (lambda (x) (void))))
(define _set-aspect (cfun "flux_set_aspect"      (_fun _double -> _void) (lambda (x) (void))))
(define _win-size   (cfun "flux_request_window_size" (_fun _int _int -> _void) (lambda (w h) (void))))
(define _screenshot (cfun "flux_screenshot"      (_fun _string -> _void) (lambda (p) (void))))
(define _ed-visible (cfun "flux_set_editor_visible"    (_fun _int -> _void) (lambda (v) (void))))
(define _ed-full    (cfun "flux_set_editor_full_width" (_fun _int -> _void) (lambda (f) (void))))
(define _set-frustum (cfun "flux_set_frustum"    (_fun _double _double _double _double -> _void) (lambda (a b c d) (void))))
(define _set-ortho  (cfun "flux_set_ortho"       (_fun _int -> _void) (lambda (x) (void))))
(define _set-ozoom  (cfun "flux_set_ortho_zoom"  (_fun _double -> _void) (lambda (x) (void))))
(define _set-clip   (cfun "flux_set_clip"        (_fun _double _double -> _void) (lambda (a b) (void))))
(define _set-vp     (cfun "flux_set_viewport"    (_fun _double _double _double _double -> _void) (lambda (a b c d) (void))))
(define _screen-sz  (cfun "flux_get_screen_size" (_fun _f64vector -> _void) (lambda (v) (void))))

(define (set-camera-transform m)
  (_set-cam-tx (list->f64vector (map ->fl (vector->list m)))))
(define (get-camera-transform)
  (let ((v (make-f64vector 16 0.0))) (_get-cam-tx v) (list->vector (f64vector->list v))))
(define set-camera set-camera-transform)   ;; fluxus alias
(define get-camera get-camera-transform)
(define (set-camera-position v) (_set-cam-pos (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (camera-reset) (_cam-reset))
(define (set-fov f) (_set-fov (->fl f)))
(define (set-aspect r) (_set-aspect (->fl r)))
(define (set-window-size w h) (_win-size (inexact->exact (round w)) (inexact->exact (round h))))
(define (screenshot p) (_screenshot p))
(define (show-editor) (_ed-visible 1))
(define (hide-editor) (_ed-visible 0))
(define (editor-full-width (f #t)) (_ed-full (if f 1 0)))
(define (frustum l r b t) (_set-frustum (->fl l) (->fl r) (->fl b) (->fl t)))
(define (ortho (on #t)) (_set-ortho (if on 1 0)))
(define (set-ortho-zoom z) (_set-ozoom (->fl z)))
(define (clip f b) (_set-clip (->fl f) (->fl b)))
(define (viewport x y w h) (_set-vp (->fl x) (->fl y) (->fl w) (->fl h)))
(define (get-screen-size)
  (let ((v (make-f64vector 2 0.0))) (_screen-sz v) (vector (f64vector-ref v 0) (f64vector-ref v 1) 0)))

;; ---- persistent script state (FFI) -----------------------------------------
;; survives the per-frame buffer re-eval, so scripts can keep mutable state
;; (damping/inertia) like retained-mode fluxus. Values are numeric vectors.
(define _state-get   (cfun "flux_state_get"   (_fun _string _f64vector _int -> _int)  (lambda (a b c) 0)))
(define _state-set   (cfun "flux_state_set"   (_fun _string _f64vector _int -> _void) (lambda (a b c) (void))))
(define _state-clear (cfun "flux_state_clear" (_fun -> _void) (lambda () (void))))
(define (persist! key v)
  (_state-set key (list->f64vector (map ->fl (vector->list v))) (vector-length v)))
(define (persist key default)               ; default: a numeric vector
  (let* ((n (vector-length default))
         (buf (make-f64vector n 0.0)))
    (if (= 1 (_state-get key buf n))
        (list->vector (f64vector->list buf)) ; stored value from a previous frame
        (begin (persist! key default) default))))
(define (clear-state) (_state-clear))

;; ---- GLSL shaders (FFI) ----------------------------------------------------
(define _shader-src (cfun "flux_shader_source"    (_fun _string _string -> _void) (lambda (a b) (void))))
(define _shader-off (cfun "flux_shader_clear"     (_fun -> _void) (lambda () (void))))
(define _shader-f   (cfun "flux_shader_set_float" (_fun _string _double -> _void) (lambda (a b) (void))))
(define _shader-v   (cfun "flux_shader_set_vec"   (_fun _string _double _double _double -> _void) (lambda (a b c d) (void))))
(define (shader-source vert frag) (_shader-src vert frag))   ; compile from source strings
(define (shader-off) (_shader-off))
(define (shader-set-float! name x) (_shader-f name (->fl x)))
(define (shader-set-vec! name v) (_shader-v name (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define _shader-i (cfun "flux_shader_set_int" (_fun _string _int -> _void) (lambda (a b) (void))))
(define (shader-set-int! name v) (_shader-i name v))

;; blend-mode: GL source/destination factors. Accepts symbols or raw GL ints.
(define (blend-enum s)
  (cond ((number? s) s)
        ((eq? s 'zero) 0) ((eq? s 'one) 1)
        ((eq? s 'src-color) 768) ((eq? s 'one-minus-src-color) 769)
        ((eq? s 'src-alpha) 770) ((eq? s 'one-minus-src-alpha) 771)
        ((eq? s 'dst-alpha) 772) ((eq? s 'one-minus-dst-alpha) 773)
        ((eq? s 'dst-color) 774) ((eq? s 'one-minus-dst-color) 775)
        (else 1)))
(define _blend (cfun "flux_blend_mode" (_fun _int _int -> _void) (lambda (a b) (void))))
(define (blend-mode src dst) (_blend (blend-enum src) (blend-enum dst)))
(define _mtex (cfun "flux_multitexture" (_fun _int _int -> _void) (lambda (a b) (void))))
(define (multitexture unit id) (_mtex unit id))
;; full-screen post-processing (FBO): (post-shader fragsrc) / (post-off)
(define _post-src (cfun "flux_post_shader" (_fun _string -> _void) (lambda (a) (void))))
(define _post-off (cfun "flux_post_off"    (_fun -> _void) (lambda () (void))))
(define (post-shader frag) (_post-src frag))
(define (post-off) (_post-off))
(define _blur (cfun "flux_blur" (_fun _double -> _void) (lambda (a) (void))))
(define (blur amt) (_blur (->fl amt)))   ; feedback motion-blur (0..~0.97)
(define _aa (cfun "flux_set_antialias" (_fun _int -> _void) (lambda (x) (void))))
(define (anti-alias (on #t)) (_aa (if on 1 0)))
(define (hint-anti-alias (on #t)) (_aa (if on 1 0)))

;; ---- more builders (real engine, FFI) --------------------------------------
(define _cyl (cfun "flux_build_cylinder" (_fun _double _double _int _int -> _int) (lambda (a b c d) 0)))
(define (build-cylinder (h 1.0) (r 1.0) (hs 10) (rs 10)) (_cyl (->fl h) (->fl r) hs rs))
(define (poly-type-num t)
  (cond ((number? t) t)
        ((eq? t 'triangle-strip) 0) ((eq? t 'quad-list) 1)
        ((eq? t 'triangle-list) 2)  ((eq? t 'triangle-fan) 3)
        ((eq? t 'polygon) 4)        (else 0)))
(define _polys (cfun "flux_build_polygons" (_fun _int _int -> _int) (lambda (a b) 0)))
(define (build-polygons type nverts) (_polys (poly-type-num type) nverts))
(define _copy (cfun "flux_build_copy" (_fun _int -> _int) (lambda (a) 0)))
(define (build-copy id) (_copy id))
(define _loc (cfun "flux_build_locator" (_fun -> _int) (lambda () 0)))
(define (build-locator) (_loc))

;; ---- material (grabbed prim) -----------------------------------------------
(define _spec (cfun "flux_specular"      (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _amb  (cfun "flux_ambient"       (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _emi  (cfun "flux_emissive"      (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _shin (cfun "flux_shinyness"     (_fun _double -> _void) (lambda (a) (void))))
(define _ncol (cfun "flux_normal_colour" (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _pw   (cfun "flux_point_width"   (_fun _double -> _void) (lambda (a) (void))))
(define (specular v)      (_spec (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (ambient v)       (_amb  (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (emissive v)      (_emi  (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (shinyness s)     (_shin (->fl s)))
(define (normal-colour v) (_ncol (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (point-width w)   (_pw   (->fl w)))

;; ---- render hints ----------------------------------------------------------
(define _hnone (cfun "flux_hint_none" (_fun -> _void) (lambda () (void))))
(define (hint-none) (_hnone))
(define-syntax-rule (hint-def name cname)
  (begin (define f (cfun cname (_fun _int -> _void) (lambda (x) (void))))
         (define (name (on #t)) (f (if on 1 0)))))
(hint-def hint-normal       "flux_hint_normal")
(hint-def hint-points       "flux_hint_points")
(hint-def hint-unlit        "flux_hint_unlit")
(hint-def hint-vertcols     "flux_hint_vertcols")
(hint-def hint-depth-sort   "flux_hint_depth_sort")
(hint-def hint-cull-ccw     "flux_hint_cull_ccw")
(hint-def hint-origin       "flux_hint_origin")
(hint-def hint-cast-shadow  "flux_hint_cast_shadow")
(hint-def hint-ignore-depth "flux_hint_ignore_depth")
(hint-def hint-nozwrite     "flux_hint_nozwrite")
(hint-def hint-sphere-map   "flux_hint_sphere_map")

;; ---- lights ----------------------------------------------------------------
(define (light-type-num t)
  (cond ((number? t) t) ((eq? t 'point) 0) ((eq? t 'directional) 1) ((eq? t 'spot) 2) (else 0)))
(define _mklight (cfun "flux_make_light" (_fun _int -> _int) (lambda (a) 0)))
(define (make-light (type 'point) . _) (_mklight (light-type-num type)))
(define _lpos (cfun "flux_light_position"  (_fun _int _double _double _double -> _void) (lambda (a b c d) (void))))
(define _ldif (cfun "flux_light_diffuse"   (_fun _int _double _double _double -> _void) (lambda (a b c d) (void))))
(define _lamb (cfun "flux_light_ambient"   (_fun _int _double _double _double -> _void) (lambda (a b c d) (void))))
(define _lspc (cfun "flux_light_specular"  (_fun _int _double _double _double -> _void) (lambda (a b c d) (void))))
(define _ldir (cfun "flux_light_direction" (_fun _int _double _double _double -> _void) (lambda (a b c d) (void))))
(define _lspa (cfun "flux_light_spot_angle" (_fun _int _double -> _void) (lambda (a b) (void))))
(define (light-position id v)  (_lpos id (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (light-diffuse id v)   (_ldif id (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (light-ambient id v)   (_lamb id (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (light-specular id v)  (_lspc id (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (light-direction id v) (_ldir id (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (light-spot-angle id a) (_lspa id (->fl a)))

;; ---- fog / parent / select / shadows ---------------------------------------
(define _fog (cfun "flux_fog" (_fun _double _double _double _double _double _double -> _void) (lambda (a b c d e f) (void))))
(define (fog col density start end)
  (_fog (->fl (vx col)) (->fl (vy col)) (->fl (vz col)) (->fl density) (->fl start) (->fl end)))
(define _parent (cfun "flux_parent" (_fun _int -> _void) (lambda (a) (void))))
(define (parent id) (_parent id))
(define _select (cfun "flux_select" (_fun _int _int _int -> _int) (lambda (a b c) 0)))
(define (select x y (size 5)) (_select (inexact->exact (round x)) (inexact->exact (round y)) size))
(define _shl (cfun "flux_shadow_light" (_fun _int -> _void) (lambda (a) (void))))
(define (shadow-light i) (_shl i))
(define _shlen (cfun "flux_shadow_length" (_fun _double -> _void) (lambda (a) (void))))
(define (shadow-length l) (_shlen (->fl l)))

;; ---- textures (image decode in TextureLoader.cpp via JUCE) ------------------
(define _loadtex (cfun "flux_load_texture" (_fun _string -> _uint) (lambda (a) 0)))
(define _tex     (cfun "flux_texture"      (_fun _int -> _void) (lambda (a) (void))))
(define (load-texture path) (_loadtex path))   ; -> GL id (0 on failure)
(define (texture id) (_tex id))
;; ---- video texture (AVFoundation decode in VideoHost.mm) --------------------
(define _vopen (cfun "flux_video_open"     (_fun _string -> _int)    (lambda (a) 0)))
(define _vtex  (cfun "flux_video_texture"  (_fun -> _uint)           (lambda () 0)))
(define _vw    (cfun "flux_video_width"    (_fun -> _int)            (lambda () 0)))
(define _vh    (cfun "flux_video_height"   (_fun -> _int)            (lambda () 0)))
(define _vdur  (cfun "flux_video_duration" (_fun -> _double)         (lambda () 0.0)))
(define _vplay (cfun "flux_video_play"     (_fun -> _void)           (lambda () (void))))
(define _vpause(cfun "flux_video_pause"    (_fun -> _void)           (lambda () (void))))
(define _vseek (cfun "flux_video_seek"     (_fun _double -> _void)   (lambda (a) (void))))
(define _vclose(cfun "flux_video_close"    (_fun -> _void)           (lambda () (void))))
(define (video-open path) (_vopen path))   ; -> 1 ok / 0 fail; starts looping
(define (video-texture)   (_vtex))         ; -> GL id (0 until first frame)
(define (video-width)     (_vw))
(define (video-height)    (_vh))
(define (video-duration)  (_vdur))
(define (video-play)      (_vplay))
(define (video-pause)     (_vpause))
(define (video-seek s)    (_vseek s))
(define (video-close)     (_vclose))
;; ---- webcam capture (AVCaptureSession in VideoHost.mm) ----------------------
(define _copen (cfun "flux_camera_open"    (_fun _int -> _int)  (lambda (a) 0)))
(define _ctex  (cfun "flux_camera_texture" (_fun -> _uint)      (lambda () 0)))
(define _cw    (cfun "flux_camera_width"   (_fun -> _int)       (lambda () 0)))
(define _ch    (cfun "flux_camera_height"  (_fun -> _int)       (lambda () 0)))
(define _cclose(cfun "flux_camera_close"   (_fun -> _void)      (lambda () (void))))
(define (camera-open (device 0)) (_copen device))   ; -> 1 ok; first launch prompts
(define (camera-texture) (_ctex))                   ; -> GL id (0 until first frame)
(define (camera-width)   (_cw))
(define (camera-height)  (_ch))
(define (camera-close)   (_cclose))
;; mouse.ss: C fmod (engine prim) — real impl
(define (fmod a b) (if (zero? b) 0.0 (- a (* b (truncate (/ a b))))))
;; pixels-tools.ss engine prims (pixels-index/pixels-texcoord are library defs)
(define _pxw (cfun "flux_pixels_width"  (_fun -> _int) (lambda () 0)))
(define _pxh (cfun "flux_pixels_height" (_fun -> _int) (lambda () 0)))
(define _pxup (cfun "flux_pixels_upload" (_fun -> _void) (lambda () (void))))
(define (pixels-width) (_pxw))
(define (pixels-height) (_pxh))
(define (pixels-upload) (_pxup))
(define _mktext (cfun "flux_build_text"   (_fun _string -> _int) (lambda (a) 0)))
(define _mkpix  (cfun "flux_build_pixels" (_fun _int _int -> _int) (lambda (a b) 0)))
(define (build-text str) (_mktext str))
(define (build-pixels w h) (_mkpix w h))
;; planetarium.ss engine prims
(define (current-camera . _) 0)
(define (pixels->texture . _) 0)
(define (build-camera . _) 0)
(define (set-screen-size . _) (void))
(define (set-camera-update . _) (void))
;; collada-import.ss engine prims
(define (hide . _) (void))
(define (fullpath p) p)
;; viewport / ortho are wired via FFI (see camera section above)
(define (lock-camera . _) (void))
(define (camera-lag . _) (void))
;; building-blocks.ss with-pixels-renderer macro (engine prims)
(define (renderer-grab . _) (void))
(define (renderer-ungrab . _) (void))
(define (vdist-sq a b) (let ((d (vsub a b))) (vdot d d)))
;; voxels-tools.ss engine prims
(define (voxels-width) 0)
(define (voxels-height) 0)
(define (voxels-depth) 0)
