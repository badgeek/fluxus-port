;; fluxus->JUCE port: ergonomics over the assimp model importer.
;;
;; The engine command (load-model path [flags]) returns a HANDLE, because a model
;; is a SET of primitives — one indexed poly prim per mesh, node transforms baked
;; in — all parented to a single locator. These wrappers hand back the pieces in
;; the shapes a sketch actually wants (a list of prim ids, the root to grab).
;;
;; Typical use — retained mode, load ONCE:
;;   (retained)
;;   (define m (load-model "/path/model.gltf"))
;;   (when (model-ok? m)
;;     (with-primitive (model-root m) (scale (vector 0.1 0.1 0.1))))
;;   (every-frame (with-primitive (model-root m) (rotate (vector 0 1 0))))
;;
;; In immediate mode this still works — the parsed scene is cached C-side, so only
;; the primitives are rebuilt each frame — but retained is the right way for
;; anything heavy.

#lang racket/base
(require "fluxus-modules.ss")
(require "building-blocks.ss")

(provide model-ok?
         model-prims
         model-mesh-names
         with-model
         model-apply
         model-hint
         model-vert-count
         model-animated?
         model-play
         model-bones
         model-bone-named)

;; #t when the load succeeded. On failure (model-error) says why.
(define (model-ok? h) (and (integer? h) (>= h 0)))

;; the mesh primitives, in file order
(define (model-prims h)
  (if (model-ok? h)
      (let loop ((i (- (model-mesh-count h) 1)) (acc '()))
        (if (< i 0) acc (loop (- i 1) (cons (model-prim h i) acc))))
      '()))

(define (model-mesh-names h)
  (if (model-ok? h)
      (let loop ((i (- (model-mesh-count h) 1)) (acc '()))
        (if (< i 0) acc (loop (- i 1) (cons (model-mesh-name h i) acc))))
      '()))

;; grab the model's ROOT locator: transforms applied inside move the whole model.
(define-syntax-rule (with-model h body ...)
  (with-primitive (model-root h) body ...))

;; run proc with each mesh primitive GRABBED — the way to touch per-mesh state
;; (colour, hints, texture, pdata) without knowing how many meshes there are.
(define (model-apply h proc)
  (for-each (lambda (p) (with-primitive p (proc p))) (model-prims h)))

;; convenience: apply a hint-style thunk to every mesh of the model, e.g.
;;   (model-hint m (lambda () (hint-wire) (hint-unlit)))
(define (model-hint h thunk)
  (for-each (lambda (p) (with-primitive p (thunk))) (model-prims h)))

;; total vertices across the model's meshes — cheap sanity check after a load
(define (model-vert-count h)
  (let loop ((ps (model-prims h)) (n 0))
    (if (null? ps) n (loop (cdr ps) (+ n (with-primitive (car ps) (pdata-size)))))))

;; ---- skeletal animation ----------------------------------------------------

(define (model-animated? h)
  (and (model-ok? h) (> (model-anim-count h) 0) (> (model-bone-count h) 0)))

;; play clip `anim` at wall-clock time t, optionally rate-scaled. Call once per
;; frame — it poses the skeleton AND re-skins the meshes.
;;   (every-frame (model-play m 0 (time)))
(define (model-play h anim t . speed)
  (when (model-animated? h)
    (model-set-anim-time h anim (* t (if (null? speed) 1.0 (car speed))))))

;; the skeleton's locators, in the order the skinning weights use
(define (model-bones h)
  (if (model-ok? h)
      (let loop ((i (- (model-bone-count h) 1)) (acc '()))
        (if (< i 0) acc (loop (- i 1) (cons (model-bone h i) acc))))
      '()))

;; find a bone locator by the name it has in the file — grab it to read or
;; override that joint (an override lasts until the next model-play call).
(define (model-bone-named h name)
  (let loop ((i 0))
    (cond ((>= i (model-bone-count h)) -1)
          ((string=? (model-bone-name h i) name) (model-bone h i))
          (else (loop (+ i 1))))))
