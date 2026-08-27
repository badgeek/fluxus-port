; fluxus-flow.scm
;
; same readable slide style as fluxus-lives (one auto-focus text block, typed out,
; front-facing, clean while reading) but the generative visual is a TERRAIN FLYBY:
; a scrolling wireframe landscape that recedes into fog below the text and reacts
; to audio (relief + brightness pulse with the music). Only the terrain scrolls and
; the text types; the camera is fixed, so the words stay put and readable.

(start-audio "system:capture_1" 512 44100)
(background (vector 0.02 0.02 0.04))
(fog (vector 0.02 0.02 0.04) 0.035 14 46)     ; fade the far terrain into the dark

;; ---- timing / look ---------------------------------------------------------
(define N-SLIDES  3)
(define SLIDE-DUR 18.0)
(define TRANS     1.6)
(define CPS       42.0)
(define GAP       6)
(define CW        0.44)
(define FOV       50.0)
(define CAM-DIST  10.0)
(define DEG       (/ 3.141592653589793 180.0))
(define WRAP-T    16)
(define WRAP-B    26)

(define slides (list
  (list "FLUXUS"
        (list (string-append
               "FLUXUS is a live-coding environment for 3D graphics created by "
               "Dave Griffiths and first released in 2005. Written initially in "
               "Scheme and later ported to Racket, it lets a program be modified "
               "during execution, with the scene re-rendered after each "
               "evaluation.")))
  (list "TOPLAP and live coding"
        (list (string-append
               "The system developed in parallel with TOPLAP, a collective "
               "founded in 2004, the Temporary Organisation for the Promotion of "
               "Live Algorithm Programming, which established live coding as an "
               "artistic practice, with a manifesto calling for performers' "
               "screens to be projected so audiences could watch the code being "
               "written. FLUXUS was among the earlier tools built for this "
               "practice, treating the running program itself as the instrument.")))
  (list "algoraves and restoration"
        (list (string-append
               "FLUXUS later featured at algoraves, events named around 2012 by "
               "Alex McLean and Nick Collins, where dance music and visuals are "
               "generated from algorithms written live on stage. Development "
               "concluded around 2015; this project restores the engine for use "
               "on contemporary platforms.")))))

;; ---- helpers ---------------------------------------------------------------
(define (frac q) (- q (floor q)))
(define (clamp01 x) (max 0.0 (min 1.0 x)))
(define (ease x) (let ((u (clamp01 x))) (- 1.0 (* (- 1.0 u) (- 1.0 u) (- 1.0 u)))))
(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))

(define (split-words str)
  (let loop ((i 0) (start 0) (acc '()) (len (string-length str)))
    (cond ((>= i len) (reverse (if (> i start) (cons (substring str start i) acc) acc)))
          ((char=? (string-ref str i) #\space)
           (loop (+ i 1) (+ i 1)
                 (if (> i start) (cons (substring str start i) acc) acc) len))
          (else (loop (+ i 1) start acc len)))))
(define (wrap str n)
  (let loop ((ws (split-words str)) (cur "") (acc '()))
    (cond ((null? ws) (reverse (if (> (string-length cur) 0) (cons cur acc) acc)))
          ((= (string-length cur) 0) (loop (cdr ws) (car ws) acc))
          ((<= (+ (string-length cur) 1 (string-length (car ws))) n)
           (loop (cdr ws) (string-append cur " " (car ws)) acc))
          (else (loop (cdr ws) (car ws) (cons cur acc))))))

;; ---- one auto-focus text block ---------------------------------------------
(define AT-MIN 0.03) (define AT-MAX 1.30) (define AT-DRIFT 0.09)
(define LH 1.18)

(define (typed full off n)
  (let* ((len   (string-length full))
         (shown (max 0 (min len (- n off))))
         (blink (odd? (inexact->exact (floor (* (time) 3.0)))))
         (cur   (if (and (> shown 0) (< shown len) blink) "_" "")))
    (if (> shown 0) (string-append (substring full 0 shown) cur) "")))

(define (assemble items n)
  (let loop ((its items) (off 0) (acc '()))
    (if (null? its) (reverse acc)
        (let* ((full (caar its)) (rel (cdar its))
               (vis  (typed full off n)))
          (loop (cdr its) (+ off (string-length full) GAP)
                (if (> (string-length vis) 0) (cons (cons vis rel) acc) acc))))))

(define (text-frame key items cx cy tw th col)
  (when (pair? items)
    (let* ((bw (let loop ((is items) (m 0.001))
                 (if (null? is) m
                     (loop (cdr is) (max m (* CW (string-length (caar is)) (cdar is)))))))
           (bh (let loop ((is items) (s 0.0))
                 (if (null? is) s (loop (cdr is) (+ s (* LH (cdar is)))))))
           (target (max AT-MIN (min AT-MAX (min (/ tw bw) (/ th (max 0.001 bh))))))
           (cur0 (vx (persist key (vector target))))
           (cur  (max AT-MIN (min AT-MAX (+ cur0 (* (- target cur0) AT-DRIFT))))))
      (persist! key (vector cur))
      (let loop ((is items) (yy (* 0.5 bh)))
        (when (pair? is)
          (let* ((line (caar is)) (rel (cdar is))
                 (lh   (* LH rel))
                 (sc   (* cur rel))
                 (w    (* CW (string-length line) sc)))
            (let ((tp (build-text line)))
              (with-primitive tp
                (identity) (hint-unlit)
                (translate (vector (- cx (* 0.5 w))
                                   (+ cy (* cur (- yy (* 0.5 lh)))) 0))
                (scale (vector sc sc sc))
                (colour col)))
            (loop (cdr is) (- yy lh))))))))

;; ---- the generative visual: JOY DIVISION / RUTT-ETRA RIDGELINES ------------
;; a stack of horizontal scan-lines, one per depth row, each a bright waveform of
;; the (windowed, scrolling) heightfield. Every row is backed by a BLACK skirt
;; down to a baseline, so nearer rows hide the ones behind (hidden-line look).
;; The whole field scrolls toward the viewer (flyby) and swells with the audio.
(define ROWS  46)      ; number of stacked lines
(define NX    84)      ; samples across each line
(define GW    30.0)    ; world width
(define GD    46.0)    ; world depth (front to fog)
(define GY   -4.6)     ; base height (below the text)
(define GZ    3.0)     ; nearest row, just in front of the camera
(define SKY  -16.0)    ; skirt baseline (well below)
(define SCROLL 4.0)    ; flyby speed

; central amplitude window: flat at the edges, peaked in the middle (pulsar plot)
(define (win fx) (let ((w (- 1.0 (* 4.2 fx fx)))) (if (> w 0.0) (* w w) 0.0)))
; a row's height at normalised x (fx -0.5..0.5), world depth d, scaled by amp
(define (ridge-h fx d amp)
  (let ((wx (* GW fx)))
    (* amp (win fx)
       (+ 0.35
          (* 1.5 (abs (sin (+ (* 0.85 wx) (* 0.6 d)))))
          (* 0.9 (abs (sin (+ (* 1.9 wx) (* 0.3 d) 1.7))))
          (* 0.5 (abs (sin (+ (* 3.3 wx) (* 0.9 d)))))))))

(define (ridges audio surge)
  (let ((amp (+ 1.3 (* 3.4 audio) (* 1.8 surge)))
        (scr (* SCROLL (time))))
    ; draw FAR -> NEAR so nearer black skirts paint over farther lines
    (let loop ((r (- ROWS 1)))
      (when (>= r 0)
        (let* ((d  (* GD (/ (exact->inexact r) ROWS)))
               (z  (+ GZ d))
               (sk (build-seg-plane NX 1))       ; black occluding skirt
               (ln (build-ribbon (+ NX 1))))     ; bright top line
          (with-primitive sk
            (identity) (hint-unlit) (hint-solid #t)
            (colour (vector 0 0 0))
            (pdata-index-map!
              (lambda (i v)
                (let* ((o  (pdata-ref "ori" i))
                       (fx (- (vx o) 0.5))
                       (top (> (vy o) 0.5)))
                  (vector (* GW fx)
                          (if top (+ GY (ridge-h fx (+ d scr) amp)) SKY)
                          z)))
              "p"))
          (with-primitive ln
            (identity) (hint-unlit)
            (line-width 1.7)
            (colour (v* (vector 1.0 1.0 1.0) (+ 0.55 (* 0.45 audio))))
            (pdata-index-map!
              (lambda (i v)
                (let ((fx (- (/ i (exact->inexact NX)) 0.5)))
                  (vector (* GW fx)
                          (+ GY (ridge-h fx (+ d scr) amp))
                          (- z 0.05))))            ; nudge toward camera over its skirt
              "p")
            (pdata-index-map! (lambda (i w) (vector 0.008 0.008 0.008)) "w")))
        (loop (- r 1))))))

;; ---- progress dots ---------------------------------------------------------
(define (draw-dots si halfh halfw)
  (let* ((gap (* 0.09 halfw))
         (x0  (* -0.5 gap (- N-SLIDES 1)))
         (yy  (* -0.86 halfh))
         (r   (* 0.02 halfh)))
    (let loop ((i 0))
      (when (< i N-SLIDES)
        (let ((on (= i (modulo si N-SLIDES))))
          (with-state
            (translate (vector (+ x0 (* i gap)) yy 0))
            (scale (if on (vector (* 1.8 r) (* 1.8 r) (* 1.8 r)) (vector r r r)))
            (hint-unlit)
            (colour (if on (vector 0.9 0.95 1.0) (vector 0.28 0.32 0.4)))
            (build-cube)))
        (loop (+ i 1))))))

;; ---- grade + soft trail ----------------------------------------------------
(define post "
uniform sampler2D tex; uniform sampler2D prev;
uniform float time; uniform vec2 resolution;
varying vec2 uv;
void main(){
  vec3 col = texture2D(tex, uv).rgb;
  col = max(col, texture2D(prev, uv).rgb * 0.60);
  float vig = 16.0*uv.x*uv.y*(1.0-uv.x)*(1.0-uv.y);
  col *= pow(vig, 0.26);
  gl_FragColor = vec4(col, 1.0);
}")

;; ---- the loop --------------------------------------------------------------
(every-frame
  ; perspective the whole time so the terrain has depth; front-facing text at z=0
  ; shows no foreshortening anyway, so it stays crisp and centred.
  (set-fov FOV)
  (ortho #f)

  (let* ((sz    (get-screen-size))
         (asp   (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 1.3333))
         (halfh (* CAM-DIST (tan (* 0.5 FOV DEG))))
         (halfw (* halfh asp))
         (tt   (- (time) (* (* SLIDE-DUR N-SLIDES)
                            (floor (/ (time) (* SLIDE-DUR N-SLIDES))))))
         (si   (inexact->exact (floor (/ tt SLIDE-DUR))))
         (ph   (- tt (* si SLIDE-DUR)))
         (out  (clamp01 (/ (- SLIDE-DUR ph) 0.7)))
         (tfade (* (clamp01 (ease (/ (- ph (* 0.6 TRANS)) 0.8))) out))
         ; a brief non-audio "surge" of the terrain right at each cut
         (surge (clamp01 (- 1.0 (/ ph TRANS))))
         (slide (list-ref slides (modulo si N-SLIDES)))
         (title (car slide))
         (lines (cadr slide))
         (n     (inexact->exact (floor (* (max 0.0 (- ph (* 0.6 TRANS))) CPS))))
         (t-items (map (lambda (w) (cons w 1.7)) (wrap title WRAP-T)))
         (b-items (apply append
                    (map (lambda (l) (map (lambda (w) (cons w 1.0)) (wrap l WRAP-B)))
                         lines)))
         (items (append t-items b-items))
         (vis   (assemble items n)))

    ; generative ridgelines (Joy Division / Rutt-Etra), audio-reactive + cut surge
    (ridges (gain) surge)

    ; the slide text, front and centre
    (text-frame "slide" vis 0.0 (* 0.06 halfh)
                (* 1.85 halfw) (* 1.55 halfh)
                (v* (vector 0.95 0.98 1.0) tfade))

    (draw-dots si halfh halfw))
  (post-shader post))
