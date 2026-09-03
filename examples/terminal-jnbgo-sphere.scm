;; terminal-jnbgo-sphere.scm — the jnbgo INTRO scene mapped onto a SPHERE.
;; Same 1:1 intro data field + scattered poem as terminal-jnbgo-intro.scm, but the
;; terminal grid is wrapped around a globe via (terminal-shape 1) and spun. Press N
;; toggles NTSC. Col -> longitude (wraps 360), row -> latitude (top row = north pole).
(retained)
(clear)
(hide-editor)
(set-window-size 720 720)

(define W 84)
(define H 30)
(define t (build-terminal W H))
(with-primitive t
  (terminal-shape 1)                 ; wrap the grid onto a sphere (auto radius)
  (terminal-bg-alpha 0.0)            ; see-through: skip bg quads, glyphs float on the globe
  (scale (vector 0.62 0.62 0.62))    ; uniform — frustum now tracks window aspect, stays round
  (rotate (vector 16 0 0)))          ; slight tilt so the poles aren't dead-on

;; --- rng helpers -----------------------------------------------------------
(define (rf) (random))                         ; float [0,1)
(define (ri n) (if (<= n 0) 0 (random n)))     ; int [0,n)
(define (pickc s) (string-ref s (ri (string-length s))))   ; random char of a string
(define (bit) (if (< (rf) 0.5) #\0 #\1))
(define (i x) (inexact->exact (floor x)))

;; --- poem -> scattered fragments (like intro.go NewIntro) -------------------
(define poem-text
  (string-append
   "Our creations now reflect us back to ourselves-amplified, distorted, optimized. "
   "They show not who we are but who the data says we should be. They predict our "
   "behaviors before we choose them. They anticipate our desires before we feel them. "
   "This is not consciousness but its simulation. Not intelligence but its shadow. In "
   "this hall of digital mirrors, we lose sight of where the reflection ends and reality "
   "begins. The algorithms feed on fragments of our expressions, reassembling them into "
   "a composite self we barely recognize yet increasingly obey. We have built systems "
   "that reduce the infinite complexity of human experience to patterns and probabilities. "
   "These reflections appear to know us, yet they know only the traces we leave behind. "
   "The mirror does not just reflect-it refracts. We become less ourselves and more what "
   "the mirror expects us to be. What appears as intelligence is merely correlation "
   "without comprehension. The mirror cannot see beyond its frame, cannot wonder at the "
   "stars. Yet we increasingly defer to its judgment. The ultimate question is not whether "
   "machines will think like humans, but whether humans will forget how to think."))
(define (string-join-sp lst)   ; join words with spaces (no racket/string dep)
  (cond [(null? lst) ""]
        [(null? (cdr lst)) (car lst)]
        [else (string-append (car lst) " " (string-join-sp (cdr lst)))]))
(define words (regexp-split #px" +" poem-text))
(define poem
  (let loop ((ws words) (acc '()) (cur '()) (cap (+ 3 (ri 10))))
    (cond
      [(null? ws) (reverse (if (null? cur) acc (cons (string-join-sp (reverse cur)) acc)))]
      [(>= (length cur) cap)
       (loop ws (cons (string-join-sp (reverse cur)) acc) '() (+ 3 (ri 10)))]
      [else (loop (cdr ws) acc (cons (car ws) cur) cap)])))
(define npoem (length poem))
(define poemv (list->vector poem))

(define title
  (vector ""
          "              ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀"
          ""))

;; --- mutable data field (H rows of W chars) --------------------------------
(define fld (make-vector H))
(define (row y) (vector-ref fld y))
(define (fset! y x c) (when (and (>= x 0) (< x W) (>= y 0) (< y H)) (string-set! (row y) x c)))

(define (init-fields!)
  (for ([y (in-range H)]) (vector-set! fld y (make-string W #\space)))
  ;; 1. grid sections
  (for ([s (in-range (+ 1 (ri 3)))])
    (define gw (+ (quotient W 6) (ri (quotient W 4))))
    (define gh (+ (quotient H 6) (ri (quotient H 4))))
    (when (and (>= gw 5) (>= gh 5) (< gw W) (< gh H))
      (define sx (ri (- W gw))) (define sy (ri (- H gh)))
      (define dens (+ 0.1 (* 0.3 (rf))))
      (for* ([y (in-range gh)] [x (in-range gw)])
        (cond
          [(and (= (modulo y 2) 0) (= (modulo x 2) 0) (< (rf) dens))
           (fset! (+ sy y) (+ sx x) (pickc "█▓▒░01"))]
          [(and (or (= y 0) (= y (- gh 1)) (= x 0) (= x (- gw 1))) (< (rf) 0.3))
           (fset! (+ sy y) (+ sx x) #\·)]))))
  ;; 2. horizontal data streams
  (for ([s (in-range (+ 2 (ri 4)))])
    (define y (ri H))
    (define len (+ (quotient W 4) (ri (quotient W 2))))
    (when (< len W)
      (define sx (ri (- W len))) (define ty (ri 3)) (define sp (+ 1 (ri 3)))
      (for ([x (in-range sx (+ sx len))])
        (when (and (< x W) (= (modulo x sp) 0))
          (fset! y x (cond [(= ty 0) (bit)]
                           [(= ty 1) (pickc "-=·")]
                           [else (if (< (rf) 0.7) #\█ #\░)]))))))
  ;; 3. vertical data columns
  (for ([c (in-range (+ 2 (ri 3)))])
    (define x (ri W))
    (define len (+ 5 (ri (quotient H 2))))
    (when (< len H)
      (define sy (ri (- H len))) (define ty (ri 3)) (define sp (+ 1 (ri 2)))
      (for ([y (in-range sy (+ sy len))])
        (when (and (< y H) (= (modulo y sp) 0))
          (fset! y x (cond
                       [(= ty 0) (bit)]
                       [(= ty 1) (pickc "|│┃║")]
                       [else (let ((v (/ (exact->inexact (- y sy)) len)))
                               (cond [(< v 0.2) #\▁][(< v 0.4) #\▃][(< v 0.6) #\▅][(< v 0.8) #\▇][else #\█]))]))))))
  ;; 4. frame border
  (when (and (> H 10) (> W 20))
    (for ([x (in-range W)]) (when (= (modulo x 2) 0) (fset! 0 x #\─) (fset! (- H 1) x #\─)))
    (for ([y (in-range H)]) (when (= (modulo y 2) 0) (fset! y 0 #\│) (fset! y (- W 1) #\│)))
    (fset! 0 0 #\┌) (fset! 0 (- W 1) #\┐) (fset! (- H 1) 0 #\└) (fset! (- H 1) (- W 1) #\┘))
  ;; 5. binary header/footer
  (when (> H 5)
    (define p (ri 2))
    (for ([x (in-range W)])
      (if (= p 0) (fset! 1 x (integer->char (+ (modulo x 2) 48)))
          (when (< (rf) 0.7) (fset! 1 x (bit)))))
    (when (> H 10)
      (define p2 (ri 2))
      (for ([x (in-range W)])
        (if (= p2 0) (fset! (- H 2) x (integer->char (+ (modulo x 2) 48)))
            (when (< (rf) 0.7) (fset! (- H 2) x (bit))))))))

(define *tick* 0) (define *ptick* 0) (define *ppos* 0)

(define (update-fields!)
  ;; 1. scan line every 5 ticks
  (when (and (= (modulo *tick* 5) 0) (> H 5))
    (define sy (modulo (quotient *tick* 5) H))
    (for ([x (in-range W)]) (when (= (modulo x 3) 0) (fset! sy x (pickc "─═▓▒01")))))
  ;; 2. structured row shifting every 3 ticks
  (when (= (modulo *tick* 3) 0)
    (for ([y (in-range H)])
      (when (= (modulo y 5) (modulo (quotient *tick* 3) 5))
        (define r (row y))
        (if (= (modulo y 2) 0)
            (let ((f (string-ref r 0)))            ; shift left
              (for ([x (in-range (- W 1))]) (string-set! r x (string-ref r (+ x 1))))
              (string-set! r (- W 1) f))
            (let ((l (string-ref r (- W 1))))       ; shift right
              (for ([x (in-range (- W 1) 0 -1)]) (string-set! r x (string-ref r (- x 1))))
              (string-set! r 0 l))))))
  ;; 3. binary pulse every 10 ticks
  (when (= (modulo *tick* 10) 0)
    (when (> H 5)
      (for ([x (in-range W)]) (fset! 1 x (if (= (modulo x 2) (modulo *tick* 2)) #\1 #\0))))
    (when (> H 10)
      (for ([x (in-range W)]) (fset! (- H 2) x (if (not (= (modulo x 2) (modulo *tick* 2))) #\1 #\0)))))
  ;; 4. random burst every 20 ticks
  (when (and (= (modulo *tick* 20) 0) (> W 10) (> H 5))
    (define bw (+ 5 (ri 10))) (define bh (+ 1 (ri 3)))
    (when (and (< bw W) (< bh H))
      (define bx (ri (- W bw))) (define by (ri (- H bh)))
      (for* ([y (in-range bh)] [x (in-range bw)])
        (fset! (+ by y) (+ bx x) (if (= (modulo (+ x y) 2) 0) #\1 #\0)))))
  ;; 5. keep corners
  (when (and (> H 10) (> W 20))
    (fset! 0 0 #\┌) (fset! 0 (- W 1) #\┐) (fset! (- H 1) 0 #\└) (fset! (- H 1) (- W 1) #\┘)))

;; fading glitch rectangles
(define glitches '())
(define (add-glitch!)
  (when (and (>= W 10) (>= H 5))
    (define gw (+ 5 (ri 20))) (define gh (+ 1 (ri 3)))
    (when (< gw W)
      (when (> (- H gh) 0)
        (define gx (ri (- W gw))) (define gy (ri (- H gh)))
        (define ch (for/vector ([r (in-range gh)])
                     (build-string gw (lambda (c)
                       (define rr (rf))
                       (cond [(< rr 0.4) (pickc "█▓▒░▀▄▌▐")]
                             [(< rr 0.7) (bit)]
                             [(< rr 0.9) (pickc "|─┃━│┄┅┆┇┈┉┊┋")]
                             [else (pickc "/\\+=-*.:;<>[]()")])))))
        (define life (+ 3 (ri 8) (if (< (rf) 0.2) 10 0)))
        (set! glitches (cons (list gx gy gw gh ch life life) glitches))))))

(define (step-tick!)
  (set! *tick* (+ *tick* 1))
  (set! *ptick* (+ *ptick* 1))
  (when (and (>= *ptick* 8) (< *ppos* npoem)) (set! *ppos* (+ *ppos* 1)) (set! *ptick* 0))
  (when (< (rf) 0.35) (add-glitch!))
  (set! glitches (filter (lambda (g) (> (list-ref g 5) 0))
                         (map (lambda (g) (list (car g) (cadr g) (caddr g) (cadddr g)
                                                (list-ref g 4) (- (list-ref g 5) 1) (list-ref g 6)))
                              glitches)))
  (update-fields!))

;; simple LCG for the slowly-changing fragment placement (seeded by tick/50)
(define (lcg-make seed) (box (bitwise-and (+ (* seed 2654435761) 12345) #xffffffff)))
(define (lcg-i! b n) (if (<= n 0) 0
  (let ((v (bitwise-and (+ (* (unbox b) 1103515245) 12345) #x7fffffff)))
    (set-box! b v) (modulo (arithmetic-shift v -8) n))))

(define (build-frame)
  (define bg (for/vector ([y (in-range H)]) (string-copy (row y))))
  (define (bset! y x c) (when (and (>= x 0) (< x W) (>= y 0) (< y H)) (string-set! (vector-ref bg y) x c)))
  ;; top/bottom data strips (redrawn each frame)
  (when (> H 5)  (for ([x (in-range W)]) (when (< (rf) 0.7) (bset! 1 x (bit)))))
  (when (> H 10) (for ([x (in-range W)]) (when (< (rf) 0.7) (bset! (- H 2) x (bit)))))
  ;; occasional horizontal scan line every 7 ticks
  (when (and (= (modulo *tick* 7) 0) (> H 3))
    (define sy (+ 1 (ri (- H 2))))
    (for ([x (in-range W)]) (when (< (rf) 0.8) (bset! sy x (pickc "─━═")))))
  ;; title bar (glitched)
  (define titleY (quotient H 8))
  (for ([idx (in-range (vector-length title))])
    (define line (vector-ref title idx))
    (define gl (string-copy line))
    (define gi (+ 0.01 (/ (modulo *tick* 40) 500.0)))
    (for ([j (in-range (string-length gl))])
      (when (and (< (rf) gi) (not (char=? (string-ref gl j) #\space)))
        (string-set! gl j (pickc "▓▒░█▄▀01|-="))))
    (when (< (+ titleY idx) H)
      (define ts (max 0 (quotient (- W (string-length gl)) 2)))
      (for ([j (in-range (string-length gl))])
        (when (< (+ ts j) W) (bset! (+ titleY idx) (+ ts j) (string-ref gl j))))))
  ;; poem fragments (scattered, typewriter)
  (define das (+ titleY (vector-length title) 2))
  (define das* (if (>= das H) (quotient H 2) das))
  (define dah (max 1 (- H das* 3)))
  (define vis (min npoem (+ *ppos* 1)))
  (when (> vis 0)
    (define fr (lcg-make (quotient *tick* 50)))
    (for ([fidx (in-range vis)])
      (define frag (vector-ref poemv fidx))
      (define xo (if (> W 4) (let ((rng (quotient W 4))) (if (> rng 0) (- (lcg-i! fr rng) (quotient rng 2)) 0)) 0))
      (define xo* (max -10 xo))
      (define yp0 (+ das* (quotient (* fidx dah) vis)))
      (define yp1 (if (> dah 3) (+ yp0 (- (lcg-i! fr 3) 1)) yp0))
      (define yp (max das* (min (- H 3) yp1)))
      (define cur? (= fidx *ppos*))
      (define gf (string-copy frag))
      (define gi (if cur? 0.001 (+ 0.003 (* (/ fidx npoem) 0.02))))
      (for ([j (in-range (string-length gf))])
        (when (and (< (rf) gi) (not (char=? (string-ref gf j) #\space)))
          (string-set! gf j (pickc "▒░01.|-"))))
      (when cur?
        (define show (min (string-length frag) (quotient (* *ptick* (string-length frag)) 8)))
        (for ([j (in-range show (string-length gf))]) (string-set! gf j #\space))
        (when (and (< show (string-length frag)) (= (modulo *tick* 2) 0))
          (string-set! gf show #\█)))
      (define fs0 (+ (quotient (- W (string-length gf)) 2) xo*))
      (define fs (max 0 (min (- W 10) fs0)))
      (for ([j (in-range (string-length gf))])
        (when (< (+ fs j) W) (bset! yp (+ fs j) (string-ref gf j))))))
  ;; glitches on top (fading)
  (for ([g (in-list glitches)])
    (define gx (car g)) (define gy (cadr g)) (define gw (caddr g)) (define gh (cadddr g))
    (define chs (list-ref g 4)) (define alpha (/ (list-ref g 5) (list-ref g 6)))
    (for* ([yy (in-range gh)] [xx (in-range gw)])
      (when (< (rf) alpha) (bset! (+ gy yy) (+ gx xx) (string-ref (vector-ref chs yy) xx)))))
  ;; "-> NEXT" hint bottom-right (blinking)
  (when (> H 2)
    (define hint (if (< (modulo *tick* 30) 15) "→ NEXT" "      "))
    (define ns (- W (string-length hint) 2))
    (when (>= ns 0) (for ([j (in-range (string-length hint))]) (bset! (- H 1) (+ ns j) (string-ref hint j)))))
  ;; emit: white text on a dark phosphor bg so the GLOBE surface is visible (a pure
  ;; black bg would make the sphere invisible — only the flat version stays black).
  (define out (open-output-string))
  (write-string (ansi-bg 6 24 20) out)
  (write-string (ansi-fg 235 255 245) out)
  (for ([y (in-range H)])
    (write-string (ansi-move (+ y 1) 1) out)
    (write-string (vector-ref bg y) out))
  (get-output-string out))

(init-fields!)

;; press N to toggle the NTSC/CRT filter (raw jnbgo output when off).
(define *ntsc-on* #f)
(define (apply-ntsc!)
  (if *ntsc-on*
      (begin (ntsc) (ntsc-scanlines) (ntsc-noise 4) (ntsc-saturation 12)
             (ntsc-brightness 26) (ntsc-contrast 200))
      (ntsc #f)))
(define (poll-keys!)
  (let ((k (key-poll)))
    (when (or (= k 78) (= k 110))          ; N / n
      (set! *ntsc-on* (not *ntsc-on*))
      (apply-ntsc!))))

(every-frame
  (poll-keys!)
  ;; keep the sphere ROUND on window resize: (set-fov) rebuilds the frustum from the
  ;; live window aspect each frame (73.7397 = the engine's default vertical fov).
  (set-fov 73.7397)
  ;; advance the 80ms tick clock (cap catch-up so a pause doesn't spike)
  (let ((target (i (/ (time) 0.08))))
    (let loop ((n 0)) (when (and (< *tick* target) (< n 3)) (step-tick!) (loop (+ n 1))))
    (when (< *tick* target) (set! *tick* target)))
  (with-primitive t
    (rotate (vector 0 0.6 0))          ; spin the globe around its axis
    (terminal-clear)
    (terminal-write (build-frame))
    (terminal-draw)
    (screenshot "/tmp/sphT.png")))
