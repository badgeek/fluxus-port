#lang racket/base
;; fluxus->JUCE port: the "fluxus-engine" module the fluxus .ss library requires.
;;
;; The real engine commands are wired to the C engine via FFI (flux_* symbols
;; exported by the app). Each get-ffi-obj has a FAILURE-THUNK fallback, so this
;; module ALSO loads standalone on the racket CLI (where flux_* don't exist) —
;; there the commands are harmless stubs, which is all the .ss-compat test needs.
(require ffi/unsafe)
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
(define _sph   (cfun "flux_build_sphere" (_fun _int _int -> _int) (lambda (a b) 0)))
(define _tor   (cfun "flux_build_torus"  (_fun _double _double _int _int -> _int) (lambda (a b c d) 0)))
(define (build-cube) (_cube))
(define (build-plane) (_plane))
(define (build-sphere (sl 10) (st 10)) (_sph sl st))
(define (build-torus (i 0.5) (o 1.0) (sl 12) (st 12)) (_tor (->fl i) (->fl o) sl st))

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
(stub-id build-cylinder build-polygons build-nurbs build-nurbs-plane
         build-line build-locator build-copy
         build-extrusion build-type build-text build-pixels)

;; grabbed-prim state (FFI)
(define _op  (cfun "flux_opacity"      (_fun _double -> _void) (lambda (x) (void))))
(define _wo  (cfun "flux_wire_opacity" (_fun _double -> _void) (lambda (x) (void))))
(define _wc  (cfun "flux_wire_colour"  (_fun _double _double _double -> _void) (lambda (a b c) (void))))
(define _bfc (cfun "flux_backfacecull" (_fun _int -> _void) (lambda (x) (void))))
(define (opacity o) (_op (->fl o)))
(define (wire-opacity o) (_wo (->fl o)))
(define (wire-colour v) (_wc (->fl (vx v)) (->fl (vy v)) (->fl (vz v))))
(define (backfacecull on) (_bfc (if (and on (not (zero? on))) 1 0)))

(stub-void concat shader-set! shader texture multitexture
           hint-none hint-normal hint-points hint-anti-alias
           hint-unlit hint-vertcols hint-depth-sort hint-cull-ccw hint-wire-stippled
           point-width blend-mode specular ambient emissive shinyness
           normal-colour parent apply-transform clear clear-colour texture-params)

;; every-frame: our model re-evals the whole buffer each frame, so the arg is
;; already run each frame — just accept it. start-audio: JUCE audio auto-starts.
(define (every-frame . _) (void))
(define (start-audio . _) (void))

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
(define-syntax-rule (with-primitive id body ...)
  (begin (grab id) (let ((r (begin body ...))) (ungrab) r)))
(stub-void pdata-op)

(define (mident) (vector 1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1))
(define (get-global-transform . _) (mident))
(define (get-transform . _) (mident))
(define (get-camera-transform . _) (mident))
(define (get-inv-camera-transform . _) (mident))
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
