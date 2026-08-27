; fluxus-lives.scm
;
; a four-slide presentation, typed out one line at a time, with a wireframe
; object turning behind the words and crossfading as each slide gives way to the
; next. Immediate mode: every frame the slide is rebuilt from (time), so the
; whole thing loops on its own with no state to keep.
;
; the text is a short historical note on fluxus: what it was, its place in the
; livecoding movement, why it is an important piece of software, and that this
; project rebuilds the engine so it keeps running on current machines.

(start-audio "system:capture_1" 512 44100)
(background (vector 0.03 0.03 0.05))

;; ---- timing / look ---------------------------------------------------------
(define N-SLIDES  3)
(define SLIDE-DUR 18.0)     ; seconds per slide (paragraphs need reading time)
(define TRANS     1.6)      ; transition window at each cut (supershape bloom)
(define CPS       42.0)     ; characters typed per second
(define GAP       6)        ; phantom chars of pause between typed lines
(define CW        0.44)     ; build-text pen advance per char (matches engine ADV)
(define FOV       50.0)     ; vertical field of view (deg)
(define CAM-DIST  10.0)     ; default camera sits at z=-10, text lives at z=0
(define DEG       (/ 3.141592653589793 180.0))
(define WRAP-T    16)       ; title column width (chars) — narrower = bigger title
(define WRAP-B    26)       ; body column width (chars)  — narrower = bigger body

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

;; ---- small helpers ---------------------------------------------------------
(define (frac q) (- q (floor q)))
(define (clamp01 x) (max 0.0 (min 1.0 x)))
(define (ease x) (let ((u (clamp01 x))) (- 1.0 (* (- 1.0 u) (- 1.0 u) (- 1.0 u)))))  ; out-cubic
(define (hash i) (- (* 2.0 (frac (* (sin (* (+ i 1.0) 91.73)) 3758.53))) 1.0))
(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))

;; word wrap: split on spaces, greedily pack words into lines of at most `n`
;; characters. A narrow column means short lines, which lets the shared
;; auto-focus scale the whole block up bigger.
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

;; ---- ONE auto-focus control for the whole text block -----------------------
;; like the GLEditor scratchpad: the entire buffer shares a single scale that
;; drifts so its bounding box fits the view. As text is typed the box grows and
;; that one scale eases down (and back up on a new slide) with a soft settle.
;; persist carries the single scale between per-frame re-evals.
(define AT-MIN 0.03) (define AT-MAX 1.30) (define AT-DRIFT 0.09)
(define LH 1.18)        ; line height in block-local units (per unit of rel size)

;; reveal `full` given a global typed budget `n` and this line's start offset;
;; adds a blinking cursor while this line is mid-type. "" until it starts.
(define (typed full off n)
  (let* ((len   (string-length full))
         (shown (max 0 (min len (- n off))))
         (blink (odd? (inexact->exact (floor (* (time) 3.0)))))
         (cur   (if (and (> shown 0) (< shown len) blink) "_" "")))
    (if (> shown 0) (string-append (substring full 0 shown) cur) "")))

;; items: list of (full-string . rel-size). Returns the visible ones as
;; (visible-string . rel), typed in order with GAP pauses between lines.
(define (assemble items n)
  (let loop ((its items) (off 0) (acc '()))
    (if (null? its) (reverse acc)
        (let* ((full (caar its)) (rel (cdar its))
               (vis  (typed full off n)))
          (loop (cdr its) (+ off (string-length full) GAP)
                (if (> (string-length vis) 0) (cons (cons vis rel) acc) acc))))))

;; render `items` ((string . rel)…) as one block, centred on (cx,cy), the whole
;; thing scaled by a single drifting auto-focus value keyed by `key` so it fits
;; (tw,th). rel gives each line its relative size (title bigger, body smaller).
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
      ; walk the lines top to bottom; positions live in block-local space and are
      ; scaled by the one `cur`, so the block moves and zooms as a single unit.
      (let loop ((is items) (yy (* 0.5 bh)))
        (when (pair? is)
          (let* ((line (caar is)) (rel (cdar is))
                 (lh   (* LH rel))
                 (sc   (* cur rel))
                 (w    (* CW (string-length line) sc)))
            (let ((tp (build-text line)))
              (with-primitive tp
                (identity)
                (hint-unlit)
                (translate (vector (- cx (* 0.5 w))
                                   (+ cy (* cur (- yy (* 0.5 lh)))) 0))
                (scale (vector sc sc sc))
                (colour col)))
            (loop (cdr is) (- yy lh))))))))

;; ---- the transition object: a morphing SUPERSHAPE (superformula) --------------
;; only shown around each cut (never while reading, so the text stays clean). Each
;; slide has its own superformula parameters, and across the cut the shape morphs
;; from the previous slide's form to the incoming one.

; superformula radius for angle `a` (A=B=1). Clamped so thin lobes don't explode.
(define (superf a m n1 n2 n3)
  (let* ((t  (* 0.25 m a))
         (c  (expt (abs (cos t)) n2))
         (s  (expt (abs (sin t)) n3))
         (bb (+ c s)))
    (if (<= bb 0.0) 0.0
        (min 3.4 (expt bb (/ -1.0 n1))))))       ; looser clamp = wilder spikes

; one parameter set per slide: (m1 n11 n12 n13  m2 n21 n22 n23). Cranked to
; extremes (high symmetry m, tiny n1) so each is a very different, spiky form.
(define supers (list
  (vector 14.0 0.14 0.8 1.6   7.0 0.20 1.2 0.7)    ; screaming sea-urchin star
  (vector 10.0 0.20 3.4 0.4   12.0 0.16 0.5 2.2)   ; jagged asymmetric gear
  (vector  6.0 0.10 6.0 0.9    3.0 4.5 12.0 12.0)  ; spiked box / caltrop
  (vector 16.0 0.18 1.9 1.9    5.0 0.30 0.3 3.1))) ; chaotic twisting bloom

(define (lerp a b t) (+ a (* (- b a) t)))
(define (plerp pa pb t)                          ; lerp two 8-param vectors
  (let ((o (make-vector 8 0.0)))
    (let loop ((i 0))
      (when (< i 8) (vector-set! o i (lerp (vector-ref pa i) (vector-ref pb i) t)) (loop (+ i 1))))
    o))

; morph supershape from slide `pi` to slide `ci` (t 0..1), wire fading with `fade`.
(define (draw-super pi ci t fade halfh)
  (when (> fade 0.03)                               ; skip once basically invisible
  (let* ((pa (list-ref supers (modulo pi N-SLIDES)))
         (pb (list-ref supers (modulo ci N-SLIDES)))
         (p  (plerp pa pb t))
         ; wobble the symmetry counts over time so the form writhes while it blooms
         (wob (* 2.0 (sin (* 1.7 (time)))))
         (m1 (+ (vector-ref p 0) wob)) (a1 (vector-ref p 1)) (b1 (vector-ref p 2)) (c1 (vector-ref p 3))
         (m2 (- (vector-ref p 4) wob)) (a2 (vector-ref p 5)) (b2 (vector-ref p 6)) (c2 (vector-ref p 7))
         (prim (build-nurbs-sphere 30 44)))                    ; more res for the spikes
    (with-primitive prim
      (pdata-add "ori" "v") (pdata-copy "p" "ori")
      (identity)
      (translate (vector 0 (* 0.10 halfh) 2.0))
      (rotate (vector (* (time) 23) (* (time) 37) (* (time) 13)))   ; faster tumble
      (let ((s (* 0.40 halfh (+ 1.0 (* 0.6 (gain))))))              ; pulse with loudness
        (scale (vector s s s)))
      (hint-solid #f) (hint-wire) (hint-unlit)
      (line-width 1.5)
      (wire-opacity fade)
      (wire-colour (v* (vector 1.0 1.0 1.0) fade))               ; white wire
      (pdata-index-map!
        (lambda (i v)
          (let* ((d   (vnormalise (pdata-ref "ori" i)))
                 (lat (asin (max -1.0 (min 1.0 (vy d)))))       ; -pi/2..pi/2
                 (lon (atan (vz d) (vx d)))                     ; -pi..pi
                 (r1  (superf lon m1 a1 b1 c1))
                 (r2  (superf lat m2 a2 b2 c2))
                 ; push spikes out by the audio band this vertex maps to
                 (av  (+ 1.0 (* 2.6 (gh (modulo i 16))))))
            (vector (* r1 (cos lon) r2 (cos lat) av)
                    (* r2 (sin lat) av)
                    (* r1 (sin lon) r2 (cos lat) av))))
        "p")))))

;; ---- progress dots ---------------------------------------------------------
(define (draw-dots si halfh halfw)
  (let* ((gap (* 0.09 halfw))
         (x0  (* -0.5 gap (- N-SLIDES 1)))
         (yy  (* -0.78 halfh))
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

;; ---- post: a soft presentation grade + a light sweep across each cut --------
(define post "
uniform sampler2D tex; uniform sampler2D prev;
uniform float time; uniform float audio; uniform vec2 resolution;
varying vec2 uv;
void main(){
  vec3 col = texture2D(tex, uv).rgb;
  col = max(col, texture2D(prev, uv).rgb * 0.62);          // gentle phosphor trail
  float vig = 16.0*uv.x*uv.y*(1.0-uv.x)*(1.0-uv.y);
  col *= pow(vig, 0.28);
  float g = fract(sin(dot(uv*resolution, vec2(12.9898,78.233))+time)*43758.5453);
  col += (g-0.5)*0.05;                                     // fine grain
  float scan = 0.97 + 0.03*sin(uv.y*resolution.y*1.6);
  col *= scan;
  gl_FragColor = vec4(col, 1.0);
}")

;; ---- the loop --------------------------------------------------------------
(every-frame
  (let* ((sz    (get-screen-size))
         (asp   (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 1.3333))
         (halfh (* CAM-DIST (tan (* 0.5 FOV DEG))))        ; visible half-height at z=0
         (halfw (* halfh asp))                             ; visible half-width
         (tt   (- (time) (* (* SLIDE-DUR N-SLIDES)
                            (floor (/ (time) (* SLIDE-DUR N-SLIDES))))))
         (si   (inexact->exact (floor (/ tt SLIDE-DUR))))
         (ph   (- tt (* si SLIDE-DUR)))
         (out  (clamp01 (/ (- SLIDE-DUR ph) 0.7)))         ; slide easing out
         ; text only comes up AFTER the supershape has gone (no overlap)
         (tfade (* (clamp01 (ease (/ (- ph (* 0.8 TRANS)) 0.8))) out))
         (slide (list-ref slides (modulo si N-SLIDES)))
         (title (car slide))
         (lines (cadr slide))
         (n     (inexact->exact (floor (* (max 0.0 (- ph TRANS)) CPS))))
         ; the whole slide is ONE text block: title (bigger) then the body, both
         ; wrapped to a narrow column so lines are short and the shared auto-focus
         ; scale grows the text large. Tune WRAP-T / WRAP-B for column width.
         (t-items (map (lambda (w) (cons w 1.7)) (wrap title WRAP-T)))
         (b-items (apply append
                    (map (lambda (l) (map (lambda (w) (cons w 1.0)) (wrap l WRAP-B)))
                         lines)))
         (items (append t-items b-items))
         (vis   (assemble items n))
         ; the supershape bloom only lives in the first 0.8*TRANS of the slide,
         ; and is fully gone before the text is readable (no black wire ghost over
         ; the words). Perspective during the bloom, orthographic while reading.
         (show   (* 0.8 TRANS))
         (persp? (< ph show)))

    (if persp?
        (begin (ortho #f) (set-fov FOV))
        (begin (frustum (- asp) asp -1.0 1.0) (set-ortho-zoom halfh) (ortho #t)))

    ; --- transition: a morphing supershape blooms only around the cut (never
    ;     while reading), morphing from the previous slide's form to this one and
    ;     fading out before the text arrives.
    (when (< ph show)
      (let ((tr (ease (/ ph show))))
        (draw-super (- si 1) si tr (- 1.0 tr) halfh)))

    ; --- text: one auto-focus frame for the entire slide (GLEditor scratchpad) ---
    (text-frame "slide" vis 0.0 (* 0.02 halfh)
                (* 1.85 halfw) (* 1.78 halfh)
                (v* (vector 0.95 0.98 1.0) tfade))

    (draw-dots si halfh halfw))
  (post-shader post))
