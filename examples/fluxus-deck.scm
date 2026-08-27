; fluxus-deck.scm — a 4-slide black & white Bauhaus deck (9:16) with a generative
; ridgeline-terrain twist. Slide 0 is a title card with big generative art; slides
; 1-3 carry the fluxus history, each a left-set title + a typed paragraph over a
; terrain band. Auto-forwards and loops. Strictly monochrome: white on black.

(start-audio "system:capture_1" 512 44100)
(set-window-size 540 960)             ; 9:16 vertical (grabs at 1080x1920 on retina)
(background (vector 0 0 0))
; retained mode: compile the program ONCE, then per frame run only the every-frame
; thunk (which clears + rebuilds). Avoids re-reading/re-compiling the whole file
; every frame — the big CPU cost in immediate mode.
(retained)

;; ---- config ----------------------------------------------------------------
(define FOV 50.0)
(define CAM-DIST 10.0)
(define DEG (/ 3.141592653589793 180.0))
(define TWO-PI 6.283185307179586)
(define CW 0.44)
(define WHITE (vector 1 1 1))
(define WRAP-B 30)          ; body column width (chars) — default text slides
(define CPS 46.0)           ; typing speed
(define GAP 6)
(define LH 1.20)
(define AT-MIN 0.02) (define AT-MAX 0.85) (define AT-DRIFT 0.10)

; per slide: (index title body-or-#f caption)  body #f = title card
(define slides (list
  (list "00" "FLUXUS LIVES" #f "LIVE CODING / 2005 - 2025")
  (list "01" "FLUXUS"
        (string-append
         "FLUXUS is a live-coding environment for 3D graphics created by Dave "
         "Griffiths and first released in 2005. Written initially in Scheme and "
         "later ported to Racket, it allows a program to be modified during "
         "execution, with the scene re-rendered after each evaluation.")
        "DAVE GRIFFITHS")
  (list "02" "TOPLAP"
        (string-append
         "The system developed in parallel with TOPLAP, a collective founded in "
         "2004, the Temporary Organisation for the Promotion of Live Algorithm "
         "Programming, that established live coding as an artistic practice, with "
         "a manifesto calling for performers screens to be projected so audiences "
         "could watch the code being written. FLUXUS was among the earlier tools "
         "built for this practice, treating the running program as the instrument.")
        "LIVE CODING AS PERFORMANCE")
  (list "03" "ALGORAVES"
        (string-append
         "FLUXUS later featured at algoraves, events named around 2012 by Alex "
         "McLean and Nick Collins, where dance music and visuals are generated "
         "from algorithms written live on stage. Development concluded around "
         "2015; this project restores the engine for use on contemporary "
         "platforms.")
        "RESTORATION")))
(define NS (length slides))

(define D-TITLE 6.5)        ; title-card seconds
(define D-TEXT  17.0)       ; text-slide seconds
(define TRANS   1.4)

;; ---- helpers ---------------------------------------------------------------
(define (clamp01 x) (max 0.0 (min 1.0 x)))
(define (ease x) (let ((u (clamp01 x))) (- 1.0 (* (- 1.0 u) (- 1.0 u) (- 1.0 u)))))
(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))

(define (line ax ay bx by th)
  (let ((rb (build-ribbon 2)))
    (with-primitive rb (identity) (hint-unlit) (colour WHITE)
      (pdata-index-map! (lambda (i v) (if (= i 0) (vector ax ay 0) (vector bx by 0))) "p")
      (pdata-index-map! (lambda (i w) (vector th th th)) "w"))))
(define (rect cx cy w h)
  (with-state (translate (vector cx cy 0)) (scale (vector w h 0.01))
    (hint-unlit) (colour WHITE) (build-cube)))
(define (circle cx cy r th)
  (let ((rb (build-ribbon 65)))
    (with-primitive rb (identity) (hint-unlit) (colour WHITE)
      (pdata-index-map! (lambda (i v) (let ((a (* TWO-PI (/ i 64.0))))
                          (vector (+ cx (* r (cos a))) (+ cy (* r (sin a))) 0))) "p")
      (pdata-index-map! (lambda (i w) (vector th th th)) "w"))))
; left-anchored text: x = left edge, y = vertical centre, h = cap height
(define (ltext str x y h)
  (when (> (string-length str) 0)
    (let ((tp (build-text str)) (sc (/ h 0.9)))
      (with-primitive tp (identity) (hint-unlit)
        (translate (vector x (- y (* 0.5 h)) 0)) (scale (vector sc sc sc)) (colour WHITE)))))
(define (text-w str h) (* CW (string-length str) (/ h 0.9)))

;; ---- wrap + typing ---------------------------------------------------------
(define (split-words str)
  (let loop ((i 0) (start 0) (acc '()) (len (string-length str)))
    (cond ((>= i len) (reverse (if (> i start) (cons (substring str start i) acc) acc)))
          ((char=? (string-ref str i) #\space)
           (loop (+ i 1) (+ i 1) (if (> i start) (cons (substring str start i) acc) acc) len))
          (else (loop (+ i 1) start acc len)))))
(define (wrap str n)
  (let loop ((ws (split-words str)) (cur "") (acc '()))
    (cond ((null? ws) (reverse (if (> (string-length cur) 0) (cons cur acc) acc)))
          ((= (string-length cur) 0) (loop (cdr ws) (car ws) acc))
          ((<= (+ (string-length cur) 1 (string-length (car ws))) n)
           (loop (cdr ws) (string-append cur " " (car ws)) acc))
          (else (loop (cdr ws) (car ws) (cons cur acc))))))
(define (typed full off n)
  (let* ((len (string-length full)) (shown (max 0 (min len (- n off))))
         (blink (odd? (inexact->exact (floor (* (time) 3.0)))))
         (cur (if (and (> shown 0) (< shown len) blink) "_" "")))
    (if (> shown 0) (string-append (substring full 0 shown) cur) "")))

;; left-aligned auto-scaled body block: wrapped `lines`, typed to budget `n`,
;; scaled by a single persisted value so the whole paragraph fits (colw,colh).
(define (body-block key lines n x ytop colw colh atmax)
  (let* ((bw (let loop ((ls lines) (m 0.001))
               (if (null? ls) m (loop (cdr ls) (max m (* CW (string-length (car ls))))))))
         (bh (* LH (length lines)))
         (target (max AT-MIN (min atmax (min (/ colw bw) (/ colh (max 0.001 bh))))))
         (cur0 (vx (persist key (vector target))))
         (sc (max AT-MIN (min atmax (+ cur0 (* (- target cur0) AT-DRIFT))))))
    (persist! key (vector sc))
    (let loop ((ls lines) (off 0) (row 0))
      (when (pair? ls)
        (let ((vis (typed (car ls) off n)))
          (when (> (string-length vis) 0)
            (ltext vis x (- ytop (* row LH sc)) (* 0.9 sc)))
          (loop (cdr ls) (+ off (string-length (car ls)) GAP) (+ row 1)))))))

;; ---- generative audio-reactive SUPERSHAPE (B&W wireframe) -------------------
;; a superformula solid rendered as a white wireframe; spikes push out per FFT
;; band and the whole form pulses with loudness. One form per slide.
(define (superf a m n1 n2 n3)
  (let* ((t (* 0.25 m a)) (c (expt (abs (cos t)) n2)) (s (expt (abs (sin t)) n3)) (bb (+ c s)))
    (if (<= bb 0.0) 0.0 (min 3.0 (expt bb (/ -1.0 n1))))))
(define supers (list
  (vector  8.0 0.30 1.0 1.0   4.0 0.40 1.0 1.0)    ; star
  (vector  6.0 0.30 1.7 1.7   6.0 0.30 1.0 1.0)    ; flower
  (vector  3.0 4.50 10.0 10.0 3.0 4.50 10.0 10.0)  ; rounded cube
  (vector 10.0 0.22 1.5 1.5   5.0 0.30 0.6 2.0)))  ; twisty
(define (supershape si cx cy sc audio)
  (let* ((p  (list-ref supers (modulo si NS)))
         (m1 (vector-ref p 0)) (a1 (vector-ref p 1)) (b1 (vector-ref p 2)) (c1 (vector-ref p 3))
         (m2 (vector-ref p 4)) (a2 (vector-ref p 5)) (b2 (vector-ref p 6)) (c2 (vector-ref p 7))
         (prim (build-nurbs-sphere 18 28)))
    (with-primitive prim
      (pdata-add "ori" "v") (pdata-copy "p" "ori")
      (identity)
      (translate (vector cx cy 2.0))
      (rotate (vector (* (time) 17) (* (time) 29) (* (time) 11)))
      (let ((s (* sc (+ 1.0 (* 0.5 audio)))))
        (scale (vector s s s)))
      (hint-solid #f) (hint-wire) (hint-unlit)
      (line-width 1.2)
      (colour WHITE) (wire-opacity 1) (wire-colour WHITE)
      (pdata-index-map!
        (lambda (i v)
          (let* ((d   (vnormalise (pdata-ref "ori" i)))
                 (lat (asin (max -1.0 (min 1.0 (vy d)))))
                 (lon (atan (vz d) (vx d)))
                 (r1  (superf lon m1 a1 b1 c1))
                 (r2  (superf lat m2 a2 b2 c2))
                 (av  (+ 1.0 (* 2.2 (gh (modulo i 16))))))
            (vector (* r1 (cos lon) r2 (cos lat) av)
                    (* r2 (sin lat) av)
                    (* r1 (sin lon) r2 (cos lat) av))))
        "p"))))

;; ---- per-slide timing ------------------------------------------------------
(define (dur-of s) (if (caddr s) D-TEXT D-TITLE))
(define (total-dur) (let loop ((ls slides) (s 0.0)) (if (null? ls) s (loop (cdr ls) (+ s (dur-of (car ls)))))))
; which slide + phase at wrapped time tt
(define (locate tt)
  (let loop ((ls slides) (i 0) (acc 0.0))
    (let ((d (dur-of (car ls))))
      (if (< tt (+ acc d)) (list i (- tt acc) d)
          (if (null? (cdr ls)) (list i (- tt acc) d) (loop (cdr ls) (+ i 1) (+ acc d)))))))

;; ---- layout ----------------------------------------------------------------
(every-frame
  (clear) (background (vector 0 0 0))    ; retained: wipe + repaint bg each frame
  (set-fov FOV) (ortho #f)
  (let* ((sz (get-screen-size))
         (asp (if (> (vy sz) 0) (/ (vx sz) (vy sz)) 0.5625))
         (hh (* CAM-DIST (tan (* 0.5 FOV DEG))))
         (hw (* hh asp))
         (ml (* -0.82 hw)) (mr (* 0.82 hw))
         (tot (total-dur))
         (tt (- (time) (* tot (floor (/ (time) tot)))))
         (loc (locate tt))
         (si (car loc)) (ph (cadr loc)) (dur (caddr loc))
         (s (list-ref slides si))
         (idx (car s)) (title (cadr s)) (body (caddr s)) (cap (cadddr s))
         (card (not body))
         (surge (clamp01 (- 1.0 (/ ph TRANS))))
         (out (clamp01 (/ (- dur ph) 0.7)))
         (tfade (* (clamp01 (ease (/ (- ph (* 0.5 TRANS)) 0.7))) out))
         (n (inexact->exact (floor (* (max 0.0 (- ph (* 0.7 TRANS))) CPS)))))

    ; --- frame furniture (all slides) ---
    (ltext idx ml (* 0.90 hh) (* 0.04 hh))
    (ltext cap (* 0.16 hw) (* 0.90 hh) (* 0.028 hh))
    (line ml (* 0.84 hh) mr (* 0.84 hh) 0.03)
    (line ml (* -0.86 hh) mr (* -0.86 hh) 0.018)
    (ltext "LIVE CODED / SCHEME" ml (* -0.91 hh) (* 0.028 hh))
    ; progress marks bottom-right
    (let loop ((k 0))
      (when (< k NS)
        (if (= k si) (rect (- mr (* (- NS k 1) 0.06 hh)) (* -0.91 hh) (* 0.03 hh) (* 0.03 hh))
            (circle (- mr (* (- NS k 1) 0.06 hh)) (* -0.91 hh) (* 0.014 hh) 0.008))
        (loop (+ k 1))))

    (if card
        (begin
          ; --- slide 0: big title + big generative supershape ---
          (ltext title ml (* 0.60 hh) (* 0.15 hh))
          (supershape si 0.0 (* -0.18 hh) (* 0.34 hh) (gain)))
        (begin
          ; --- text slides: title + typed paragraph + terrain band ---
          (ltext title ml (* 0.68 hh) (* 0.12 hh))
          (line ml (* 0.55 hh) (+ ml (* 0.55 (text-w title (* 0.12 hh)))) (* 0.55 hh) 0.012)
          (if (= si 2)
              (begin   ; TOPLAP: longest paragraph, wider column + more height
                (body-block "body" (wrap body 34) n ml (* 0.48 hh) (* 1.62 hw) (* 0.92 hh) 0.85)
                (supershape si 0.0 (* -0.55 hh) (* 0.045 hh) (gain)))
              (begin   ; other text slides: original layout
                (body-block "body" (wrap body WRAP-B) n ml (* 0.46 hh) (* 1.55 hw) (* 0.66 hh) 0.6)
                (supershape si 0.0 (* -0.44 hh) (* 0.14 hh) (gain))))))))
