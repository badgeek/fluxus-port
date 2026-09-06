;; astroboy-android.scm — assimp load + skeletal animation on the Android spike
;; (spikes/android-racket-apk). Same shape as examples/model-anim.scm; only the
;; path differs — the model ships inside the APK's runtime asset (build.sh) and
;; is extracted to <filesDir>/models/astroboy/ on first launch, same mechanism
;; as racket-lib/*.ss.
;;
;; adb forward tcp:8020 tcp:8020
;; cli/fluxus load examples/astroboy-android.scm
;; cli/fluxus watch examples/astroboy-android.scm   ; live-code from a laptop

(retained)
(clear)
(background (vector 0.05 0.06 0.08))

;; Inert on this GLES spike — GLESBackend has one hard-coded headlight and
;; setLightPosition/setLightFloat/setLightEnabled are no-ops — kept only for
;; parity with the desktop example.
(define l (make-light 'point 'free))
(light-diffuse l (vector 1 1 1))
(light-position l (vector 20 30 20))

(define *model-path*
  "/data/user/0/cc.fluxus.racket/files/models/astroboy/astroBoy_walk.dae")

(define m (load-model *model-path*))

;; astroBoy's own units are tiny — model_test measures the whole rig at 0.16
;; units across (verified: `./build/model_test assets/models/astroboy/*.dae`
;; prints "a model 0.16 across"), nothing like the ~1-unit scale the
;; druid.gltf/Fox.fbx examples assume. Scaled up to roughly fill the default
;; camera's view at its dist=11 orbit.
(when (model-ok? m)
  (with-model m (scale (vector 12 12 12)) (translate (vector 0 -0.4 0))))

(define fallback
  (if (model-ok? m) -1
      (with-state (hint-solid #f) (hint-wire) (hint-unlit)
                  (wire-colour (vector 1 0.3 0.2)) (build-cube))))

(define (animate)
  (cond
    ((model-animated? m) (model-play m 0 (time)) (with-model m (rotate (vector 0 0.3 0))))
    ((model-ok? m)       (with-model m (rotate (vector 0 0.3 0))))
    (else (with-primitive fallback (rotate (vector 0.5 0.7 0))))))

(every-frame (animate))
