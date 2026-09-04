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
(define (vreflect a n) (let ((d (* 2.0 (vdot a n)))) (vector (- (vx a) (* d (vx n))) (- (vy a) (* d (vy n))) (- (vz a) (* d (vz n))))))
(define (vdist-sq a b) (let ((d (vsub a b))) (vdot d d)))
;; componentwise matrix ops (rarely used; building-blocks has the list variants)
(define (madd a b) (build-vector 16 (lambda (n) (+ (vector-ref a n) (vector-ref b n)))))
(define (msub a b) (build-vector 16 (lambda (n) (- (vector-ref a n) (vector-ref b n)))))
(define (mdiv a s) (build-vector 16 (lambda (n) (/ (vector-ref a n) s))))

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
;; Upstream fluxus lets these take a NUMBER as shorthand: (scale 2) is uniform,
;; (colour 0.5) is grey. Many vendor/fluxus/examples rely on it — without this
;; they die on `vector-ref: contract violation, given: 1`.
(define (->v3 v) (if (number? v) (vector v v v) v))
(define (background v) (let ((v (->v3 v))) (_bg  (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
(define (colour v)     (let ((v (->v3 v))) (_col (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
(define (color v)      (colour v))
(define (translate v)  (let ((v (->v3 v))) (_tr  (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
(define (rotate v)     (let ((v (->v3 v))) (_rot (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
(define (scale v)      (let ((v (->v3 v))) (_scl (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))

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
;; upstream: (mouse-button n) -> is button n currently down. The port's engine
;; call returns WHICH button is down (0 = none), so compare. No arg keeps this
;; port's older "which button" reading.
(define (mouse-button . n) (if (null? n) (_mb) (= (_mb) (car n))))
;; (key-poll): consume the last-pressed char code (0 if none) — simple hotkeys.
(define _keypoll (cfun "flux_get_key" (_fun -> _int) (lambda () 0)))
(define (key-poll) (_keypoll))
;; (key-down? c): live physical held state (c = char / 1-char string / code) — poll
;; every frame for smooth hold-to-move controls (no OS key-repeat stutter).
(define _keydown (cfun "flux_key_is_down" (_fun _int -> _int) (lambda (c) 0)))
(define (key-down? c)
  (> (_keydown (cond ((char? c) (char->integer c))
                     ((string? c) (if (> (string-length c) 0) (char->integer (string-ref c 0)) 0))
                     (else c))) 0))
;; (set-export on path fps): offline frame-locked render straight to MP4.
(define _setexport (cfun "flux_set_export" (_fun _int _string _int -> _void) (lambda (a b c) (void))))
(define (set-export on path fps) (_setexport (if on 1 0) path fps))
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
(define (wire-colour v) (let ((v (->v3 v))) (_wc (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
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
;; Some pdata channels are SCALAR, not vec3 — ribbon width "w", particle size
;; "s". Upstream reads/writes those as plain numbers, so accept and return one
;; (a vec3 channel is unchanged). Without this a (pdata-map! (lambda (w) 0.5) "w")
;; dies on `vector-ref: contract violation, given: 0.5`.
(define (scalar-pdata? name) (member name '("w" "s")))
(define (pdata-ref name i)
  (if (scalar-pdata? name)
      (_pget name i 0)
      (vector (_pget name i 0) (_pget name i 1) (_pget name i 2))))
(define (pdata-set! name i v)
  (if (number? v)
      (_pset name i 0 (->fl v))
      (begin (_pset name i 0 (->fl (vx v)))
             (_pset name i 1 (->fl (vy v)))
             (_pset name i 2 (->fl (vz v))))))
(define (pdata-add name type) (_padd name type))
(define (pdata-copy src dst) (_pcpy src dst))
;; upstream: (recalc-normals smooth) — 1 arg, 0=faceted 1=smooth. The port's
;; engine call has no smooth flag, so the argument is accepted and ignored.
(define (recalc-normals (smooth 1)) (_rn))
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
;; ---- pdata-op / poly-index / scene-graph / primitive-io (real engine, FFI) --
(define (vec->f64 v) (list->f64vector (map ->fl (vector->list v))))
(define (f64->vec v) (list->vector (f64vector->list v)))
(define _pop-num (cfun "flux_pdata_op_num"   (_fun _string _string _double _f64vector -> _int) (lambda a 0)))
(define _pop-vec (cfun "flux_pdata_op_vec"   (_fun _string _string _f64vector _int _f64vector -> _int) (lambda a 0)))
(define _pop-pd  (cfun "flux_pdata_op_pdata" (_fun _string _string _string _f64vector -> _int) (lambda a 0)))
(define (pdata-op op name operand)
  (let ((out (make-f64vector 3 0.0)))
    (let ((n (cond ((string? operand) (_pop-pd op name operand out))
                   ((vector? operand) (_pop-vec op name (vec->f64 operand) (vector-length operand) out))
                   (else (_pop-num op name (->fl operand) out)))))
      (if (= n 3) (f64->vec out) (void)))))
(define _ptype (cfun "flux_poly_type"    (_fun -> _int) (lambda () -1)))
(define _pidxd (cfun "flux_poly_indexed" (_fun -> _int) (lambda () 0)))
(define _picnt (cfun "flux_poly_index_count" (_fun -> _int) (lambda () 0)))
(define _pidcs (cfun "flux_poly_indices" (_fun _u32vector _int -> _void) (lambda (a b) (void))))
(define _psidx (cfun "flux_poly_set_index" (_fun _u32vector _int -> _void) (lambda (a b) (void))))
(define _pc2i  (cfun "flux_poly_convert_to_indexed" (_fun -> _void) (lambda () (void))))
(define (poly-type-enum) (_ptype))
(define (poly-indexed?) (not (zero? (_pidxd))))
(define (poly-indices) (let ((n (_picnt))) (if (<= n 0) '() (let ((v (make-u32vector n 0))) (_pidcs v n) (u32vector->list v)))))
(define (poly-set-index lst) (let ((v (list->u32vector (map (lambda (x) (inexact->exact (floor x))) lst)))) (_psidx v (length lst))))
(define (poly-convert-to-indexed) (_pc2i))
(define _getbb (cfun "flux_get_bb" (_fun _f64vector _f64vector -> _int) (lambda (a b) 0)))
(define _getpar (cfun "flux_get_parent" (_fun -> _int) (lambda () -1)))
(define _getcc  (cfun "flux_get_children_count" (_fun -> _int) (lambda () 0)))
(define _getch  (cfun "flux_get_children" (_fun _s32vector _int -> _void) (lambda (a b) (void))))
(define _rcbb   (cfun "flux_recalc_bb" (_fun -> _void) (lambda () (void))))
(define (get-bb) (let ((mn (make-f64vector 3 0.0)) (mx (make-f64vector 3 0.0))) (if (= 1 (_getbb mn mx)) (list (f64->vec mx) (f64->vec mn)) '())))
(define (get-parent) (_getpar))
(define (get-children) (let ((n (_getcc))) (if (<= n 0) '() (let ((v (make-s32vector n 0))) (_getch v n) (s32vector->list v)))))
(define (recalc-bb) (_rcbb))
(define _loadp (cfun "flux_load_primitive" (_fun _string -> _int) (lambda (p) -1)))
(define _savep (cfun "flux_save_primitive" (_fun _string -> _void) (lambda (p) (void))))
(define (load-primitive path) (_loadp path))
(define (save-primitive path) (_savep path))

;; ---- matrices: flat length-16, row-major, point as ROW vector (v' = v·M);
;; translation lives in the last row (indices 12 13 14), matching the engine's
;; dMatrix::transform. Replaces the old always-identity stubs.
(define (mident) (vector 1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1))
(define (m@ m i j) (vector-ref m (+ (* i 4) j)))
(define (deg->rad d) (* d 0.017453292519943295))
(define (mmul a b)
  (build-vector 16 (lambda (n)
    (let ((i (quotient n 4)) (j (remainder n 4)))
      (+ (* (m@ a i 0) (m@ b 0 j)) (* (m@ a i 1) (m@ b 1 j))
         (* (m@ a i 2) (m@ b 2 j)) (* (m@ a i 3) (m@ b 3 j)))))))
(define (mtranslate v)
  (vector 1 0 0 0  0 1 0 0  0 0 1 0  (->fl (vx v)) (->fl (vy v)) (->fl (vz v)) 1))
(define (mscale v)
  (vector (->fl (vx v)) 0 0 0  0 (->fl (vy v)) 0 0  0 0 (->fl (vz v)) 0  0 0 0 1))
(define (mrotate v)
  (let ((rx (deg->rad (vx v))) (ry (deg->rad (vy v))) (rz (deg->rad (vz v))))
    (let ((Rx (vector 1 0 0 0  0 (cos rx) (sin rx) 0  0 (- (sin rx)) (cos rx) 0  0 0 0 1))
          (Ry (vector (cos ry) 0 (- (sin ry)) 0  0 1 0 0  (sin ry) 0 (cos ry) 0  0 0 0 1))
          (Rz (vector (cos rz) (sin rz) 0 0  (- (sin rz)) (cos rz) 0 0  0 0 1 0  0 0 0 1)))
      (mmul (mmul Rx Ry) Rz))))
(define (mtranspose m) (build-vector 16 (lambda (n) (m@ m (remainder n 4) (quotient n 4)))))
;; affine/rigid inverse (transpose the 3x3, invert the translation) — exact for
;; the rotation+translation matrices sketches build (e.g. the camera transform).
(define (minverse m)
  (let ((tx (m@ m 3 0)) (ty (m@ m 3 1)) (tz (m@ m 3 2)))
    (let ((nx (- (+ (* tx (m@ m 0 0)) (* ty (m@ m 0 1)) (* tz (m@ m 0 2)))))
          (ny (- (+ (* tx (m@ m 1 0)) (* ty (m@ m 1 1)) (* tz (m@ m 1 2)))))
          (nz (- (+ (* tx (m@ m 2 0)) (* ty (m@ m 2 1)) (* tz (m@ m 2 2))))))
      (vector (m@ m 0 0) (m@ m 1 0) (m@ m 2 0) 0
              (m@ m 0 1) (m@ m 1 1) (m@ m 2 1) 0
              (m@ m 0 2) (m@ m 1 2) (m@ m 2 2) 0
              nx ny nz 1))))
(define (vtransform v m)
  (let ((x (->fl (vx v))) (y (->fl (vy v))) (z (->fl (vz v))))
    (vector (+ (* x (m@ m 0 0)) (* y (m@ m 1 0)) (* z (m@ m 2 0)) (m@ m 3 0))
            (+ (* x (m@ m 0 1)) (* y (m@ m 1 1)) (* z (m@ m 2 1)) (m@ m 3 1))
            (+ (* x (m@ m 0 2)) (* y (m@ m 1 2)) (* z (m@ m 2 2)) (m@ m 3 2)))))
(define (vtransform-rot v m)
  (let ((x (->fl (vx v))) (y (->fl (vy v))) (z (->fl (vz v))))
    (vector (+ (* x (m@ m 0 0)) (* y (m@ m 1 0)) (* z (m@ m 2 0)))
            (+ (* x (m@ m 0 1)) (* y (m@ m 1 1)) (* z (m@ m 2 1)))
            (+ (* x (m@ m 0 2)) (* y (m@ m 1 2)) (* z (m@ m 2 2))))))
(define _gtx  (cfun "flux_get_transform"        (_fun _f64vector -> _void) (lambda (m) (void))))
(define _ggtx (cfun "flux_get_global_transform" (_fun _f64vector -> _void) (lambda (m) (void))))
(define (get-transform)        (let ((v (make-f64vector 16 0.0))) (_gtx v)  (f64->vec v)))
(define (get-global-transform) (let ((v (make-f64vector 16 0.0))) (_ggtx v) (f64->vec v)))
;; ---- primitive functions (pfunc) + skinning (real engine, FFI) -------------
(define _pfmake (cfun "flux_pfunc_make"      (_fun _string -> _int) (lambda (n) -1)))
(define _pfstr  (cfun "flux_pfunc_set_str"   (_fun _int _string _string -> _void) (lambda a (void))))
(define _pfint  (cfun "flux_pfunc_set_int"   (_fun _int _string _int -> _void) (lambda a (void))))
(define _pfflt  (cfun "flux_pfunc_set_float" (_fun _int _string _double -> _void) (lambda a (void))))
(define _pfvec  (cfun "flux_pfunc_set_vec"   (_fun _int _string _double _double _double -> _void) (lambda a (void))))
(define _pfcol  (cfun "flux_pfunc_set_col"   (_fun _int _string _double _double _double _double -> _void) (lambda a (void))))
(define _pfrun  (cfun "flux_pfunc_run"       (_fun _int -> _void) (lambda (i) (void))))
(define (as-str x) (cond ((symbol? x) (symbol->string x)) ((string? x) x) (else (format "~a" x))))
(define (make-pfunc name) (_pfmake (as-str name)))
(define (pfunc-set! id args)
  (let loop ((a args))
    (when (and (pair? a) (pair? (cdr a)))
      (let ((k (as-str (car a))) (v (cadr a)))
        (cond ((or (symbol? v) (string? v)) (_pfstr id k (as-str v)))
              ((and (integer? v) (exact? v)) (_pfint id k v))
              ((number? v) (_pfflt id k (->fl v)))
              ((and (vector? v) (>= (vector-length v) 4)) (_pfcol id k (->fl (vector-ref v 0)) (->fl (vector-ref v 1)) (->fl (vector-ref v 2)) (->fl (vector-ref v 3))))
              ((vector? v) (_pfvec id k (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))))
      (loop (cddr a)))))
(define (pfunc-run id) (_pfrun id))
;; get-camera-transform is wired to the engine via FFI (see camera section above)
(define (get-inv-camera-transform . _) (minverse (get-camera-transform)))
(define (flxtime) 0.0)
(define (mmul2 . _) (void))
(define (madd2 . _) (void))
(define (msub2 . _) (void))
(define (mdiv2 . _) (void))
(define (pdata-names . _) (void)) ;; auto-stub
(define (maim . _) (mident)) ;; simple stub (rare)

;; ---- quaternions (x y z w), consistent with the row-vector matrices above ---
(define (qaxisangle axis angle)
  (let ((a (deg->rad angle)) (n (vnormalise axis)))
    (let ((s (sin (/ a 2.0))))
      (vector (* (vx n) s) (* (vy n) s) (* (vz n) s) (cos (/ a 2.0))))))
(define (qmul a b)
  (let ((ax (vx a)) (ay (vy a)) (az (vz a)) (aw (vw a))
        (bx (vx b)) (by (vy b)) (bz (vz b)) (bw (vw b)))
    (vector (+ (* aw bx) (* ax bw) (* ay bz) (- (* az by)))
            (+ (* aw by) (- (* ax bz)) (* ay bw) (* az bx))
            (+ (* aw bz) (* ax by) (- (* ay bx)) (* az bw))
            (- (* aw bw) (* ax bx) (* ay by) (* az bz)))))
(define (qnormalise q)
  (let ((m (sqrt (+ (* (vx q) (vx q)) (* (vy q) (vy q)) (* (vz q) (vz q)) (* (vw q) (vw q))))))
    (if (zero? m) q (vector (/ (vx q) m) (/ (vy q) m) (/ (vz q) m) (/ (vw q) m)))))
(define (qconjugate q) (vector (- (vx q)) (- (vy q)) (- (vz q)) (vw q)))
(define (qtomatrix q)
  (let* ((n (qnormalise q)) (x (vx n)) (y (vy n)) (z (vz n)) (w (vw n)))
    (vector (- 1 (* 2 (+ (* y y) (* z z)))) (* 2 (+ (* x y) (* w z)))     (* 2 (- (* x z) (* w y)))     0
            (* 2 (- (* x y) (* w z)))     (- 1 (* 2 (+ (* x x) (* z z)))) (* 2 (+ (* y z) (* w x)))     0
            (* 2 (+ (* x z) (* w y)))     (* 2 (- (* y z) (* w x)))     (- 1 (* 2 (+ (* x x) (* y y)))) 0
            0 0 0 1)))
;; noise: real Perlin/simplex via the engine (FFI); harmless 0.0 on the bare CLI
(define _noise   (cfun "flux_noise"        (_fun _double _double _double -> _double) (lambda (x y z) 0.0)))
(define _snoise  (cfun "flux_snoise"       (_fun _double _double _double -> _double) (lambda (x y z) 0.0)))
(define _nseed   (cfun "flux_noise_seed"   (_fun _int -> _void) (lambda (s) (void))))
(define _ndetail (cfun "flux_noise_detail" (_fun _int _double -> _void) (lambda (o f) (void))))
(define (noise x (y 0.0) (z 0.0))  (_noise  (->fl x) (->fl y) (->fl z)))
(define (snoise x (y 0.0) (z 0.0)) (_snoise (->fl x) (->fl y) (->fl z)))
(define (noise-seed s) (_nseed s))
(define (noise-detail o (f 0.0)) (_ndetail o (->fl f)))
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
;; vertex + GEOMETRY + fragment shader (GL_EXT_geometry_shader4). in-type/out-type are
;; GL primitive enums (use gl-points / gl-lines / gl-triangles / gl-line-strip /
;; gl-triangle-strip below); max-verts = most vertices the geometry shader emits.
(define _shader-src-g (cfun "flux_shader_source_geom"
                            (_fun _string _string _string _int _int _int -> _void)
                            (lambda (a b c d e f) (void))))
(define (shader-source-geom vert geom frag in-type out-type max-verts)
  (_shader-src-g vert geom frag in-type out-type max-verts))
(define gl-points 0) (define gl-lines 1) (define gl-line-strip 3)
(define gl-triangles 4) (define gl-triangle-strip 5)
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
;; live-tweakable variable: (tweak "radius" 1.0 0.0 5.0) registers a slider in the
;; tweak panel and returns whatever it holds, so an edit survives the re-eval.
(define _tweak (cfun "flux_tweak" (_fun _string _double _double _double -> _double)
                     (lambda (n d lo hi) d)))
(define (tweak name def lo hi) (_tweak name (->fl def) (->fl lo) (->fl hi)))
(define _tweak-vis (cfun "flux_set_tweaks_visible" (_fun _int -> _void) (lambda (v) (void))))
(define (show-tweaks) (_tweak-vis 1))
(define (hide-tweaks) (_tweak-vis 0))
;; full-screen post-processing (FBO): (post-shader fragsrc) / (post-off)
(define _post-src (cfun "flux_post_shader" (_fun _string -> _void) (lambda (a) (void))))
(define _post-off (cfun "flux_post_off"    (_fun -> _void) (lambda () (void))))
(define (post-shader frag) (_post-src frag))
(define (post-off) (_post-off))
(define _blur (cfun "flux_blur" (_fun _double -> _void) (lambda (a) (void))))
(define (blur amt) (_blur (->fl amt)))   ; feedback motion-blur (0..~0.97)
;; final-stage software NTSC/CRT filter (LMP88959/NTSC-CRT). (ntsc #t/#f) +
;; monitor knobs; runs over the finished frame, captured by screenshots too.
(define (->i x) (inexact->exact (round x)))
(define _ntsc      (cfun "flux_ntsc"            (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-noi  (cfun "flux_ntsc_noise"      (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-hue  (cfun "flux_ntsc_hue"        (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-sat  (cfun "flux_ntsc_saturation" (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-bri  (cfun "flux_ntsc_brightness" (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-con  (cfun "flux_ntsc_contrast"   (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-scan (cfun "flux_ntsc_scanlines"  (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-mono (cfun "flux_ntsc_monochrome" (_fun _int -> _void) (lambda (a) (void))))
(define _ntsc-blnd (cfun "flux_ntsc_blend"      (_fun _int -> _void) (lambda (a) (void))))
(define (ntsc (on #t))          (_ntsc (if on 1 0)))
(define (ntsc-noise n)          (_ntsc-noi (->i n)))
(define (ntsc-hue deg)          (_ntsc-hue (->i deg)))
(define (ntsc-saturation s)     (_ntsc-sat (->i s)))
(define (ntsc-brightness b)     (_ntsc-bri (->i b)))
(define (ntsc-contrast c)       (_ntsc-con (->i c)))
(define (ntsc-scanlines (on #t)) (_ntsc-scan (if on 1 0)))
(define (ntsc-monochrome (on #t)) (_ntsc-mono (if on 1 0)))
(define (ntsc-blend (on #t))    (_ntsc-blnd (if on 1 0)))
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
;; Upstream fluxus is (build-polygons COUNT 'type); this port's own sketches
;; (examples/iso-city.scm) and the C layer use (type count). Accept both: a
;; SYMBOL in either slot names the type, and the number is the vertex count.
;; Two bare numbers keep the port's historical (type count) meaning.
(define (build-polygons a b)
  (cond ((symbol? b) (_polys (poly-type-num b) a))     ; upstream: (count 'type)
        ((symbol? a) (_polys (poly-type-num a) b))     ; (type-symbol count)
        (else        (_polys (poly-type-num a) b))))   ; legacy: (type-num count)
(define _copy (cfun "flux_build_copy" (_fun _int -> _int) (lambda (a) 0)))
(define (build-copy id) (_copy id))
;; ---- turtle builder (real engine, FFI) -------------------------------------
(define _tprim (cfun "flux_turtle_prim"     (_fun _int -> _void)    (lambda (t) (void))))
(define _tvert (cfun "flux_turtle_vert"     (_fun -> _void)         (lambda () (void))))
(define _tbld  (cfun "flux_turtle_build"    (_fun -> _int)          (lambda () -1)))
(define _tmove (cfun "flux_turtle_move"     (_fun _double -> _void) (lambda (d) (void))))
(define _tturn (cfun "flux_turtle_turn"     (_fun _double _double _double -> _void) (lambda (x y z) (void))))
(define _tpush (cfun "flux_turtle_push"     (_fun -> _void)         (lambda () (void))))
(define _tpop  (cfun "flux_turtle_pop"      (_fun -> _void)         (lambda () (void))))
(define _trst  (cfun "flux_turtle_reset"    (_fun -> _void)         (lambda () (void))))
(define _tatt  (cfun "flux_turtle_attach"   (_fun _int -> _void)    (lambda (i) (void))))
(define _tskip (cfun "flux_turtle_skip"     (_fun _int -> _void)    (lambda (n) (void))))
(define _tpos  (cfun "flux_turtle_position" (_fun -> _int)          (lambda () 0)))
(define _tseek (cfun "flux_turtle_seek"     (_fun _int -> _void)    (lambda (p) (void))))
(define _gtt   (cfun "flux_get_turtle_transform" (_fun _f64vector -> _void) (lambda (m) (void))))
(define (turtle-prim (t 'triangle-strip)) (_tprim (poly-type-num t)))
(define (turtle-vert) (_tvert))
(define (turtle-build) (_tbld))
(define (turtle-move d) (_tmove (->fl d)))
(define (turtle-turn v) (_tturn (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (turtle-push) (_tpush))
(define (turtle-pop) (_tpop))
(define (turtle-reset) (_trst))
(define (turtle-attach id) (_tatt id))
(define (turtle-skip n) (_tskip n))
(define (turtle-position) (_tpos))
(define (turtle-seek p) (_tseek p))
(define (get-turtle-transform) (let ((v (make-f64vector 16 0.0))) (_gtt v) (list->vector (f64vector->list v))))
(define _loc (cfun "flux_build_locator" (_fun -> _int) (lambda () 0)))
(define (build-locator) (_loc))

;; ---- material (grabbed prim) -----------------------------------------------
(define _spec (cfun "flux_specular"      (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _amb  (cfun "flux_ambient"       (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _emi  (cfun "flux_emissive"      (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _shin (cfun "flux_shinyness"     (_fun _double -> _void) (lambda (a) (void))))
(define _ncol (cfun "flux_normal_colour" (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _pw   (cfun "flux_point_width"   (_fun _double -> _void) (lambda (a) (void))))
(define (specular v) (let ((v (->v3 v))) (_spec (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
(define (ambient v) (let ((v (->v3 v))) (_amb  (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
(define (emissive v) (let ((v (->v3 v))) (_emi  (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
(define (shinyness s)     (_shin (->fl s)))
(define (normal-colour v) (let ((v (->v3 v))) (_ncol (->fl (vx v)) (->fl (vy v)) (->fl (vz v)))))
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
(define _lspe (cfun "flux_light_spot_exponent" (_fun _int _double -> _void) (lambda (a b) (void))))
(define _latt (cfun "flux_light_attenuation" (_fun _int _int _double -> _void) (lambda (a b c) (void))))
(define (light-spot-exponent id e) (_lspe id (->fl e)))
(define (light-attenuation id type v)
  (_latt id (cond ((eq? type 'linear) 1) ((eq? type 'quadratic) 2) (else 0)) (->fl v)))
;; colour mode + hsv/rgb
(define _cmode (cfun "flux_colour_mode" (_fun _int -> _void) (lambda (m) (void))))
(define _h2r   (cfun "flux_hsv_to_rgb"  (_fun _f64vector _f64vector -> _void) (lambda (a b) (void))))
(define _r2h   (cfun "flux_rgb_to_hsv"  (_fun _f64vector _f64vector -> _void) (lambda (a b) (void))))
(define (colour-mode m) (_cmode (if (eq? m 'hsv) 1 0)))
(define (color-mode m) (colour-mode m))
(define (hsv->rgb v) (let ((o (make-f64vector 3 0.0))) (_h2r (vec->f64 v) o) (f64->vec o)))
(define (rgb->hsv v) (let ((o (make-f64vector 3 0.0))) (_r2h (vec->f64 v) o) (f64->vec o)))
;; ---- MIDI + OSC (real engine, FFI) -----------------------------------------
(define _mcc  (cfun "flux_midi_cc"             (_fun _int _int -> _int)    (lambda (a b) 0)))
(define _mccn (cfun "flux_midi_ccn"            (_fun _int _int -> _double) (lambda (a b) 0.0)))
(define _mnt  (cfun "flux_midi_note"           (_fun -> _int) (lambda () -1)))
(define _mnv  (cfun "flux_midi_note_velocity"  (_fun -> _int) (lambda () 0)))
(define (midi-cc chan ctrl) (_mcc chan ctrl))
(define (midi-ccn chan ctrl) (_mccn chan ctrl))
(define (midi-note) (_mnt))
(define (midi-note-velocity) (_mnv))
(define _oscsrc (cfun "flux_osc_source"      (_fun _int -> _void) (lambda (p) (void))))
(define _oscget (cfun "flux_osc_get"         (_fun _string _int -> _double) (lambda (a i) 0.0)))
(define _oscdst (cfun "flux_osc_destination" (_fun _string _int -> _void) (lambda (h p) (void))))
(define _oscsnd (cfun "flux_osc_send"        (_fun _string _f64vector _int -> _void) (lambda (a v n) (void))))
(define _oscmsg (cfun "flux_osc_msg"         (_fun _bytes _int -> _int) (lambda (b c) 0)))
;; upstream passes the port as a STRING ("4444"); accept either.
(define (osc-source port)
  (_oscsrc (if (string? port) (or (string->number port) 0) port)))
(define (osc addr (index 0)) (_oscget addr index))
;; upstream takes a single liblo URL ("osc.udp://localhost:4444"); this port
;; takes host + port. Accept both.
(define (osc-destination host (port #f))
  (if port
      (_oscdst host port)
      (let ((m (regexp-match #rx"//([^:/]+):([0-9]+)" host)))
        (if m
            (_oscdst (cadr m) (or (string->number (caddr m)) 0))
            (_oscdst host 0)))))
(define (osc-send addr . args) (_oscsnd addr (list->f64vector (map ->fl args)) (length args)))
(define (osc-msg)
  (let ((buf (make-bytes 256 0)))
    (let ((n (_oscmsg buf 256))) (bytes->string/utf-8 (subbytes buf 0 (max 0 (min n 255))) #\?))))

;; ---- hand tracking (HandHost: Apple Vision on macOS, null elsewhere) --------
(define _hcount (cfun "flux_hand_count"    (_fun -> _int)                 (lambda () 0)))
(define _hjoint (cfun "flux_hand_joint"    (_fun _int _int _int -> _double) (lambda (h j a) 0.0)))
(define _hpinch (cfun "flux_hand_pinch"    (_fun _int -> _double)         (lambda (h) 0.0)))
(define _htrack (cfun "flux_hand_tracking" (_fun _int -> _void)           (lambda (o) (void))))
(define (hand-count) (_hcount))
(define (hand-joint h j axis) (_hjoint h j axis))
(define (hand-pinch h) (_hpinch h))
(define (hand-tracking (on #t)) (_htrack (if on 1 0)))
(define (hand h j) (vector (_hjoint h j 0) (_hjoint h j 1) (_hjoint h j 2)))  ; landmark -> #(x y z)

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
;; upstream: (build-pixels w h [renderer? [num-textures]]) — the port has no
;; render-to-texture, so the optional args are accepted and ignored (see the
;; PixelPrimitive note in ROADMAP.md).
(define (build-pixels w h (renderer #f) (ntex 1)) (_mkpix w h))
;; terminal (libvterm): author ANSI in Racket, render as a glyph-atlas cell grid.
;; See ansi.ss for string helpers. Grab-aware like pixels-upload.
(define _mkterm  (cfun "flux_build_terminal" (_fun _int _int -> _int)  (lambda (c r) 0)))
(define _twrite  (cfun "flux_terminal_write" (_fun _string -> _void)   (lambda (s) (void))))
(define _tclear  (cfun "flux_terminal_clear" (_fun -> _void)           (lambda () (void))))
(define _tdraw   (cfun "flux_terminal_draw"  (_fun -> _void)           (lambda () (void))))
(define _tcols   (cfun "flux_terminal_cols"  (_fun -> _int)            (lambda () 0)))
(define _trows   (cfun "flux_terminal_rows"  (_fun -> _int)            (lambda () 0)))
(define _tshape  (cfun "flux_terminal_shape" (_fun _int _double -> _void) (lambda (m r) (void))))
(define (build-terminal (cols 40) (rows 20)) (_mkterm cols rows))
(define (terminal-write str) (_twrite str))
(define (terminal-clear) (_tclear))
(define (terminal-draw) (_tdraw))
(define (terminal-cols) (_tcols))
(define (terminal-rows) (_trows))
;; (terminal-shape mode [radius]) — 0 flat, 1 sphere (radius<=0 auto-fits the grid).
(define (terminal-shape mode (radius 0.0)) (_tshape mode (exact->inexact radius)))
;; (terminal-bg-alpha a) — bg opacity: 1 opaque (default), 0 see-through, between = tint.
(define _tbga (cfun "flux_terminal_bg_alpha" (_fun _double -> _void) (lambda (a) (void))))
(define (terminal-bg-alpha a) (_tbga (exact->inexact a)))
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
;; voxels-tools.ss engine prims — real voxel/blobby engine (FFI)
(define _bvox  (cfun "flux_build_voxels" (_fun _int _int _int -> _int) (lambda (w h d) 0)))
(define _vxw   (cfun "flux_voxels_width"  (_fun -> _int) (lambda () 0)))
(define _vxh   (cfun "flux_voxels_height" (_fun -> _int) (lambda () 0)))
(define _vxd   (cfun "flux_voxels_depth"  (_fun -> _int) (lambda () 0)))
(define _vcg   (cfun "flux_voxels_calc_gradient" (_fun -> _void) (lambda () (void))))
(define _vsi   (cfun "flux_voxels_sphere_influence" (_fun _double _double _double _double _double _double _double -> _void) (lambda a (void))))
(define _vss   (cfun "flux_voxels_sphere_solid"    (_fun _double _double _double _double _double _double _double -> _void) (lambda a (void))))
(define _vbs   (cfun "flux_voxels_box_solid"       (_fun _double _double _double _double _double _double _double _double _double -> _void) (lambda a (void))))
(define _vth   (cfun "flux_voxels_threshold" (_fun _double -> _void) (lambda (v) (void))))
(define _vpl   (cfun "flux_voxels_point_light" (_fun _double _double _double _double _double _double -> _void) (lambda a (void))))
(define _v2b   (cfun "flux_voxels_to_blobby" (_fun _int -> _int) (lambda (i) -1)))
(define _v2p   (cfun "flux_voxels_to_poly" (_fun _int _double -> _int) (lambda (i l) -1)))
(define _bblob (cfun "flux_build_blobby" (_fun _int _double _double _double _double _double _double -> _int) (lambda a -1)))
(define _b2p   (cfun "flux_blobby_to_poly" (_fun _int -> _int) (lambda (i) -1)))
(define (build-voxels w h d) (_bvox w h d))
(define (voxels-width) (_vxw))
(define (voxels-height) (_vxh))
(define (voxels-depth) (_vxd))
(define (voxels-calc-gradient) (_vcg))
(define (voxels-sphere-influence pos col pow) (_vsi (->fl (vx pos)) (->fl (vy pos)) (->fl (vz pos)) (->fl (vx col)) (->fl (vy col)) (->fl (vz col)) (->fl pow)))
(define (voxels-sphere-solid pos col radius) (_vss (->fl (vx pos)) (->fl (vy pos)) (->fl (vz pos)) (->fl (vx col)) (->fl (vy col)) (->fl (vz col)) (->fl radius)))
(define (voxels-box-solid top bot col) (_vbs (->fl (vx top)) (->fl (vy top)) (->fl (vz top)) (->fl (vx bot)) (->fl (vy bot)) (->fl (vz bot)) (->fl (vx col)) (->fl (vy col)) (->fl (vz col))))
(define (voxels-threshold v) (_vth (->fl v)))
(define (voxels-point-light pos col) (_vpl (->fl (vx pos)) (->fl (vy pos)) (->fl (vz pos)) (->fl (vx col)) (->fl (vy col)) (->fl (vz col))))
(define (voxels->blobby id) (_v2b id))
(define (voxels->poly id (isolevel 1.0)) (_v2p id (->fl isolevel)))
(define (build-blobby count dim size) (_bblob count (->fl (vx dim)) (->fl (vy dim)) (->fl (vz dim)) (->fl (vx size)) (->fl (vy size)) (->fl (vz size))))
(define (blobby->poly id) (_b2p id))

;; --- util / compatibility shims -------------------------------------------
;; Commands the upstream vendor/fluxus/examples call that this port has no
;; engine-side equivalent for. Defining them here (rather than leaving them
;; unbound) is what lets those sketches LOAD; where the port cannot honour the
;; command it is a documented no-op rather than a silent lie about behaviour.

;; flxrnd/flxseed: upstream's seeded rand()/RAND_MAX in [0,1). Implemented in
;; Scheme over a Racket pseudo-random-generator (same contract, no C round-trip).
(define _flxrnd-gen (make-pseudo-random-generator))
(define (flxrnd) (real->double-flonum (random _flxrnd-gen)))
(define (flxseed n)
  (parameterize ((current-pseudo-random-generator _flxrnd-gen))
    (random-seed (bitwise-and (inexact->exact (floor n)) #x7fffffff))))

;; Debug overlays / frame pacing the port does not implement. The apps render on
;; a fixed timer and have no axis/fps overlay, so these are accepted and ignored.
(define (show-axis . a) (void))
(define (show-fps . a) (void))
(define (desiredfps . a) (void))
(define (shadow-debug . a) (void))

;; Pre-bang aliases: older fluxus spelled these without the !.
(define (pdata-set name i v) (pdata-set! name i v))
(define (pdata-get name i) (pdata-ref name i))

;; Audio: this port takes its input from the JUCE AudioHost (see (gh)/(gain)),
;; not from fluxa/JACK, so the upstream device-setup calls are accepted no-ops.
;; The FFT band count is fixed host-side.
(define (set-num-frequency-bins . a) (void))
(define (get-num-frequency-bins) 16)
(define (smoothing-bias . a) (void))
(define (gain-audio . a) (void))

;; ODE physics is not ported (see ROADMAP.md). These are defined so a physics
;; sketch LOADS and its non-physics geometry still draws; the bodies simply do
;; not move. Anything that must return an id returns 0.
(define (collisions . a) (void))
(define (ground-plane . a) (void))
(define (gravity . a) (void))
(define (set-max-physical . a) (void))
(define (active-box . a) 0)
(define (active-sphere . a) 0)
(define (active-cylinder . a) 0)
(define (passive-box . a) 0)
(define (passive-sphere . a) 0)
(define (passive-cylinder . a) 0)
(define (surface-params . a) (void))
(define (kick . a) (void))
(define (twist . a) (void))
(define (has-collided . a) #f)
(define (build-balljoint . a) 0)
(define (build-hingejoint . a) 0)
(define (build-sliderjoint . a) 0)
(define (build-hinge2joint . a) 0)
(define (build-amotorjoint . a) 0)
(define (build-fixedjoint . a) 0)
(define (joint-param . a) (void))
(define (joint-angle . a) (void))
(define (joint-slide . a) (void))

;; Other upstream commands with no equivalent in this port yet — defined so the
;; sketches that touch them still LOAD (see ROADMAP.md for what each needs):
;;   fluxus-init      - upstream's engine bring-up; the host already did it
;;   selectable      - marks a prim pickable (this port's (select) is FFI-real)
;;   camera-hide      - per-camera visibility (single camera only)
;;   pixels-download  - PixelPrimitive readback (no render-to-texture)
;;   geo/line-intersect - the geometry addon, not vendored
(define (fluxus-init . a) (void))
(define (fluxus-reshape . a) (void))
(define (selectable . a) (void))
(define (camera-hide . a) (void))
(define (pixels-download . a) (void))
(define (geo/line-intersect . a) #f)
(define (set-physics-debug . a) (void))
(define (passive-mesh . a) 0)
(define (active-mesh . a) 0)
(define (pixels-render-to . a) (void))
(define (pixels-display-to . a) (void))
(define (ffgl-load . a) 0)
(define (ffgl-get-info . a) '())
(define (ffgl-get-parameters . a) '())
(define (ffgl-set-parameter! . a) (void))
(define (ffgl-activate . a) (void))
(define (ffgl-process . a) (void))
(define-syntax-rule (with-ffgl id body ...) (begin body ...))
(define (pixels-display . a) (void))
