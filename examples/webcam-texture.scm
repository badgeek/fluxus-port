; webcam-texture.scm — live camera feed as a GL texture.
;
; AVCaptureSession (VideoHost.mm) captures frames on a background queue; the GL
; thread pulls the latest into a GL texture. (camera-texture) returns that texture
; id (0 until the first frame + permission grant). The FIRST launch shows the macOS
; camera-permission prompt — approve it, then the feed appears.
; Commands: (camera-open [device]) (camera-texture) (camera-width/height) (camera-close).

(clear)
(set-window-size 540 960)
(camera-open 0)                     ; default camera; idempotent (safe every frame)

(every-frame
  (begin
    (let ((cam (camera-texture)))
      ; full-frame mirror of the camera
      (with-state
        (colour (vector 1 1 1))
        (texture cam)
        (translate (vector 0 0 6))
        (scale (vector 7 9 1))
        (build-plane))
      ; a spinning cube wrapped in the live feed
      (with-state
        (colour (vector 1 1 1))
        (texture cam)
        (rotate (vector (* (time) 20) (* (time) 32) 0))
        (scale (vector 2.2 2.2 2.2))
        (build-cube)))))
