;; (load-model …) — import a 3D model through assimp
;;
;; Handles fbx / gltf / glb / dae / ply / stl / 3ds / obj. Each mesh in the file
;; becomes one indexed poly primitive with the node transforms already baked in,
;; and all of them are parented to a single locator: grab (model-root m) to move,
;; scale or spin the whole thing.
;;
;; Point *model-path* at a file you have. Test assets ship with openFrameworks
;; under examples/3d/*/bin/data/ (securityCamera.fbx, Druid/druid.gltf,
;; Astroboy/astroBoy_walk.dae, lofi-bunny.ply).

(define *model-path* "/tmp/openFrameworks/examples/3d/ofxAssimpAdvancedExample/bin/data/Druid/druid.gltf")

(retained)
(clear)
(background (vector 0.06 0.06 0.09))

;; a light, so a textured/material model is not flat
(define l (make-light 'point 'free))
(light-diffuse l (vector 1 1 1))
(light-position l (vector 20 30 20))

(define m (load-model *model-path*))

(when (model-ok? m)
  ;; models arrive in their own units — normalise by hand until it fits the view
  (with-model m
    (scale (vector 0.6 0.6 0.6))
    (translate (vector 0 -1.5 0))))

;; nothing loaded? draw a wire cube as a marker instead of a black window
(define fallback
  (if (model-ok? m)
      -1
      (with-state
        (hint-solid #f)
        (hint-wire)
        (hint-unlit)
        (wire-colour (vector 1 0.3 0.2))
        (build-cube))))

(define (animate)
  (if (model-ok? m)
      (with-model m (rotate (vector 0 0.6 0)))
      (with-primitive fallback (rotate (vector 0.5 0.7 0)))))

(every-frame (animate))

;; Other things to try:
;;   (model-mesh-count m)                      how many meshes came back
;;   (model-mesh-names m)                      their names, in file order
;;   (model-vert-count m)                      total verts
;;   (model-hint m (lambda () (hint-wire) (hint-unlit)))   wireframe the whole model
;;   (model-apply m (lambda (p) (colour (vector 1 0.5 0))))  per-mesh state
;;   (model-error)                             why a load failed
