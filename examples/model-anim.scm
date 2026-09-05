;; skeletal animation from a model file — (load-model) + (model-play)
;;
;; A skinned model brings its own skeleton: two locator trees (live + bindpose)
;; mirroring the file's node hierarchy, per-vertex bone weights in "w0".."wN", and
;; the bind pose kept in "pref"/"nref". (model-play h anim t) poses the skeleton at
;; t seconds and runs the engine's skinning over every skinned mesh.
;;
;; Test assets ship with openFrameworks: Fox/Fox_05.fbx, Astroboy/astroBoy_walk.dae,
;; Druid/druid.gltf are all rigged.

(define *model-path* "/tmp/openFrameworks/examples/3d/ofxAssimpBoneControlExample/bin/data/Fox/Fox_05.fbx")
(define *speed* 1.0)

(retained)
(clear)
(background (vector 0.06 0.07 0.09))

(define l (make-light 'point 'free))
(light-diffuse l (vector 1 1 1))
(light-position l (vector 20 30 20))

(define m (load-model *model-path*))

(when (model-ok? m)
  (with-model m (scale (vector 0.02 0.02 0.02)) (translate (vector 0 -20 0))))

(define fallback
  (if (model-ok? m)
      -1
      (with-state (hint-solid #f) (hint-wire) (hint-unlit)
                  (wire-colour (vector 1 0.3 0.2)) (build-cube))))

(define (animate)
  (cond
    ((model-animated? m)
     (model-play m 0 (time) *speed*)          ; pose + skin, once per frame
     (with-model m (rotate (vector 0 0.4 0))))
    ((model-ok? m) (with-model m (rotate (vector 0 0.5 0))))
    (else (with-primitive fallback (rotate (vector 0.5 0.7 0))))))

(every-frame (animate))

;; Other things to try:
;;   (model-anim-count m)                 how many clips the file has
;;   (model-anim-duration m 0)            clip length in seconds
;;   (model-play m 1 (time))              a different clip
;;   (model-bone-count m)                 skeleton size (nodes, not just bones)
;;   (model-bone-name m 5)                that node's name in the file
;;   (with-primitive (model-bone-named m "head") (rotate (vector 0 30 0)))
;;      ^ override one joint; the next model-play call overwrites it again, so do
;;        this AFTER model-play inside the same frame to make it stick
