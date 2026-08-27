; auto-text.scm
;
; the GLEditor "auto-focus" feeling as a reusable VISUAL text primitive: a line
; that types itself out and, as it grows, its scale drifts to keep filling a
; target width. Short text zooms up big, long text eases back down, always with
; that soft settle. Reproduced in scheme (no engine fork) so it runs in every
; app, using persist!/persist to carry the drifting scale between frames.
;
; auto-text is self-contained: copy the block below into any sketch. Give each
; instance its OWN key so their scales don't fight over the same persisted slot.

(background (vector 0.02 0.02 0.04))

;; ---- the reusable auto-focus text primitive --------------------------------
(define AT-CW    0.44)   ; build-text pen advance per char (matches engine ADV)
(define AT-MIN   0.05)   ; scale clamp (like GLEditor m_AutoFocusMin/MaxScale)
(define AT-MAX   1.20)
(define AT-DRIFT 0.10)   ; how fast the scale eases toward the fit (0..1 per frame)

; render `str` centred at (cx,cy), coloured `col`, its scale drifting so the line
; fills ~target-w world units. `key` names the persisted scale for this instance.
(define (auto-text key str cx cy target-w col)
  (let* ((len    (max 1 (string-length str)))
         (target (max AT-MIN (min AT-MAX (/ target-w (* AT-CW len)))))
         (cur0   (vx (persist key (vector target))))     ; last frame's scale
         (cur    (max AT-MIN (min AT-MAX (+ cur0 (* (- target cur0) AT-DRIFT))))))
    (persist! key (vector cur))
    (when (> (string-length str) 0)
      (let ((tp (build-text str))
            (w  (* AT-CW (string-length str) cur)))
        (with-primitive tp
          (identity)
          (hint-unlit)
          (translate (vector (- cx (* 0.5 w)) cy 0))
          (scale (vector cur cur cur))
          (colour col))))))

;; ---- demo: a sentence typed one word-ish at a time, looping ----------------
(define lines (list
  "fluxus"
  "livecoding"
  "the program is the performance"
  "type"
  "and the machine answers back"
  "media archaeology"
  "old software, made to breathe again"))

(define DWELL 2.6)          ; seconds per line (type + hold)
(define CPS   16.0)         ; characters per second

(define (frac q) (- q (floor q)))
(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))

(define post "
uniform sampler2D tex; uniform sampler2D prev;
uniform float time; uniform vec2 resolution;
varying vec2 uv;
void main(){
  vec3 col = texture2D(tex, uv).rgb;
  col = max(col, texture2D(prev, uv).rgb * 0.55);
  float vig = 16.0*uv.x*uv.y*(1.0-uv.x)*(1.0-uv.y); col *= pow(vig,0.3);
  float g = fract(sin(dot(uv*resolution, vec2(12.9898,78.233))+time)*43758.5453);
  col += (g-0.5)*0.045;
  gl_FragColor = vec4(col,1.0);
}")

(every-frame
  (set-fov 50)
  (let* ((sz    (get-screen-size))
         (asp   (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 1.3333))
         (halfh (* 10.0 (tan (* 0.5 50.0 (/ 3.141592653589793 180.0)))))
         (halfw (* halfh asp))
         (tot   (* DWELL (length lines)))
         (t     (- (time) (* tot (floor (/ (time) tot)))))
         (i     (inexact->exact (floor (/ t DWELL))))
         (ph    (- t (* i DWELL)))
         (full  (list-ref lines (modulo i (length lines))))
         (nshow (min (string-length full)
                     (inexact->exact (floor (* ph CPS)))))
         (typing (< nshow (string-length full)))
         (blink (odd? (inexact->exact (floor (* (time) 3.0)))))
         (cur   (if (and typing blink) "_" ""))
         (shown (string-append (substring full 0 nshow) cur))
         ; fade the line out in its last 0.4s so the next zoom reads as a change
         (fade  (max 0.0 (min 1.0 (/ (- DWELL ph) 0.4)))))
    (auto-text "line" shown 0.0 0.0
               (* 1.7 halfw)                       ; target width = ~85% of view
               (v* (vector 1.0 1.0 1.0) fade)))
  (post-shader post))
