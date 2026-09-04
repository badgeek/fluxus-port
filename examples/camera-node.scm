;; camera-node.scm — camera as a scene-graph node (openFrameworks ofCamera : ofNode).
;;
;; Demonstrates all three new capabilities:
;;   1. (camera-parent id)  — the camera RIDES a moving node (follow-cam), smoothed
;;                            with (camera-lag).
;;   2. (camera-node)       — an invisible node tracking the inverse view; a marker
;;                            parented to it stays pinned on screen (HUD) with no
;;                            manual camera-basis math (retires the old billboard hack).
;;   3. (node-look-at id t) — a turret node aims itself at the moving ship every
;;                            frame via the C-side quaternion/basis path.
;;
;; Retained mode: static geometry + the ship/turret/HUD are built ONCE; the thunk
;; only mutates them. No (clear) in the thunk (that is for the rebuild-everything
;; style) — here the scene persists and we animate via (with-primitive).

(clear)
(retained)

;; --- static ground (built once) ---------------------------------------------
(with-state
  (hint-solid #t) (hint-wire #f)
  (colour (vector 0.18 0.22 0.28))
  (translate (vector 0 -1.2 0))
  (scale (vector 30 0.1 30))
  (build-cube))

;; --- the ship the camera follows --------------------------------------------
(define *ship*
  (with-state
    (hint-solid #t) (hint-wire #f)
    (colour (vector 1.0 0.6 0.2))
    (build-cube)))
(camera-parent *ship*)   ;; camera now rides the ship
(camera-lag 0.12)        ;; smooth the follow (HUD still pins exactly)

;; --- a turret that always aims at the ship ----------------------------------
(define *turret*
  (with-state
    (hint-solid #t) (hint-wire #f)
    (colour (vector 0.4 1.0 0.7))
    (translate (vector 8 0 0))
    (scale (vector 0.6 0.6 2.0))   ;; long in local +Z so the aim reads clearly
    (build-cube)))

;; --- HUD marker, parented to the camera-node (built lazily, once) -----------
(define *hud* #f)

(every-frame
  (let ((t (time)))
    ;; move the ship in a circle
    (with-primitive *ship*
      (identity)
      (translate (vector (* 7 (sin t)) (* 0.5 (sin (* 2 t))) (* 7 (cos t)))))
    ;; the turret aims at the ship's world position (quaternion/basis, C-side)
    (node-look-at *turret* (node-global-pos *ship*))
    ;; HUD: parent a small quad to the camera-node so it holds its screen slot
    (let ((cn (camera-node)))
      (when (and cn (>= cn 0) (not *hud*))
        (set! *hud*
          (with-state
            (parent cn)
            (hint-solid #t) (hint-wire #f) (hint-unlit)
            (colour (vector 1 1 1))
            (translate (vector 0.6 0.42 -1.5))   ;; top-right, 1.5 units in front of eye
            (scale (vector 0.05 0.05 0.05))
            (build-cube)))))))
