; video-texture.scm — play a movie file as a live GL texture.
;
; AVFoundation (VideoHost.mm) decodes the file on its own threads; each frame the
; GL thread pulls the current frame into a GL texture. (video-texture) returns that
; texture id (0 until the first frame), which you bind like any texture: (texture id).
; The clip loops. Commands: (video-open path) (video-texture) (video-width/height)
; (video-play) (video-pause) (video-seek secs) (video-duration) (video-close).
;
; Set VIDEO to your own .mp4/.mov/.m4v. A quick test clip:
;   ffmpeg -f lavfi -i testsrc=size=640x480:rate=30:duration=6 -pix_fmt yuv420p /tmp/testvid.mp4

(clear)
(set-window-size 540 960)
(define VIDEO "/tmp/testvid.mp4")

; open once — immediate mode re-evals this buffer every frame, and (video-open) is
; idempotent for the same path, so this is safe to leave at top level.
(video-open VIDEO)

(every-frame
  (begin
    (let ((vt (video-texture)))
      ; full-frame backdrop showing the movie
      (with-state
        (colour (vector 1 1 1))
        (texture vt)
        (translate (vector 0 0 6))
        (scale (vector 7 9 1))
        (build-plane))
      ; a spinning cube wrapped in the same live frame
      (with-state
        (colour (vector 1 1 1))
        (texture vt)
        (rotate (vector (* (time) 22) (* (time) 34) 0))
        (scale (vector 2.2 2.2 2.2))
        (build-cube)))))
