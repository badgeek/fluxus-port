;; the wizard, plain — one model, one clip, nothing else
;;
;; This is the minimal correct case: load the model, leave its root transform
;; ALONE, and call (model-play …) once per frame. No auto-fit, no turntable, no
;; camera-pinned label — so what you see is exactly what the file's animation and
;; the engine's skinning produce, with nothing of the sketch's own on top.
;;
;; Use this as the reference when a richer sketch (examples/model-viewer.scm)
;; looks wrong: if it looks right here and wrong there, the difference is the
;; sketch, not the importer.
;;
;;   cli/fluxus load examples/model-wizard.scm
;;
;; NOTE the model path is absolute: a packaged .app has no useful working
;; directory. Edit it if the repo lives somewhere else.

(retained)
(clear)
(background (vector 0.05 0.06 0.08))

(define *model*
  "/Users/manticore/work/bauhouse/juce-test/fluxus-port/assets/models/druid/druid.gltf")

(define m (load-model *model*))

(when (not (model-ok? m))
  (display (string-append "model-wizard: load failed: " (model-error) "\n"))
  (flush-output))

;; clip 0 = "PortalOpen", 1 = "Still", 2 = "Waiting" — change the 0 below to try
;; another one; (model-anim-count m) says how many the file has.
(define (animate)
  (when (model-animated? m)
    (model-play m 0 (time))))

(every-frame (animate))
