; "the algorithmic mirror" — deforming wireframe NURBS sphere under glitch/CRT/
; feedback, with scattered fragments of a dystopian AI-critique poem typing in and
; out. Aesthetic follows jnbgo: minimalist monochrome (white/grey) text with the
; occasional pale accent, scattered short fragments, glitch.

(start-audio "system:capture_1" 512 44100)

;; ---- tweakables ------------------------------------------------------------
(define NUM-QUOTES 6)
(define PERIOD     6.5)
(define TYPE-SPEED 24)
(define TEXT-SCALE 0.24)
(define SPREAD-X   13.0)
(define SPREAD-Y   7.5)
(define SPREAD-Z   3.5)

(define quotes (list
  "we built systems that predict us until we become predictable"
  "the algorithm sees your patterns  not your potential"
  "between data points lies the unquantifiable essence of being human"
  "personalization is the comfortable prison we build around ourselves"
  "when machines anticipate your desires  do those desires remain yours"
  "the echo chamber has perfect acoustics but no windows"
  "your future self is being designed without your permission"
  "not intelligence but its shadow"
  "correlation without comprehension"
  "we perform for the algorithms that perform us back to ourselves"))

;; monochrome palette, biased to white/grey, occasional pale accent
(define palette (vector (vector 1 1 1) (vector 1 1 1) (vector 1 1 1)
                        (vector 0.72 0.72 0.72) (vector 0.53 0.53 0.53)
                        (vector 0.67 0.67 1.0) (vector 0.67 1.0 0.67)
                        (vector 1.0 0.67 0.67) (vector 1.0 1.0 0.4)))

(define GLITCH-RATE 0.12)                 ; fraction of chars corrupted to symbols
(define glitch-chars "#@%&*/\\|~^<>=+$?!")

(define (frac q) (- q (floor q)))
(define (h i) (- (* 2 (frac (* (sin (* (+ i 1) 91.73)) 3758.53))) 1))

;; randomly replace some chars with glitch symbols, flickering ~12x/sec
(define (corrupt s)
  (let ((tq (floor (* (time) 12))))
    (list->string
      (for/list ((c (in-string s)) (i (in-naturals)))
        (let ((r (frac (* (sin (+ (* i 12.9898) (* tq 7.777))) 43758.5453))))
          (if (and (not (char=? c #\space)) (< r GLITCH-RATE))
              (string-ref glitch-chars
                (modulo (inexact->exact (floor (* (frac (* r 97.0)) 1000)))
                        (string-length glitch-chars)))
              c))))))

(define (slot-text j)
  (let* ((t   (+ (time) (* j 1.9)))
         (k   (inexact->exact (floor (/ t PERIOD))))
         (idx (modulo (+ (* j 3) k) (length quotes)))
         (ph  (- t (* PERIOD (floor (/ t PERIOD)))))
         (q   (list-ref quotes idx))
         (len (string-length q))
         (td  (/ len TYPE-SPEED))
         (n   (cond ((< ph td) (inexact->exact (floor (* ph TYPE-SPEED))))
                    ((< ph (- PERIOD 1.3)) len)
                    (else 0)))
         (cur (if (and (> n 0) (< ph (- PERIOD 1.3))
                       (odd? (inexact->exact (floor (* t 4))))) "_" "")))
    (values (string-append (substring q 0 (min len n)) cur) k)))

(define (draw-slot j)
  (let-values (((txt k) (slot-text j)))
    (when (> (string-length txt) 0)
      (let ((tp (build-text (corrupt txt))))
        (with-primitive tp
          (identity)
          (translate (vector (* SPREAD-X (h (+ (* j 3) (* k 5))))
                             (* SPREAD-Y (h (+ (* j 9) (* k 7) 2)))
                             (* SPREAD-Z (h (+ (* j 5) k 4)))))
          (scale (vector TEXT-SCALE TEXT-SCALE TEXT-SCALE))
          (colour (vector-ref palette (modulo (+ (* j 3) (* k 5)) (vector-length palette)))))))))

(define (draw-text j) (when (< j NUM-QUOTES) (draw-slot j) (draw-text (+ j 1))))

(define post "
uniform sampler2D tex;
uniform sampler2D prev;
uniform float time; uniform float audio; uniform vec2 resolution;
varying vec2 uv;
float hash(float n){ return fract(sin(n) * 43758.5453); }
vec2 curve(vec2 p){ p = p*2.0-1.0; vec2 o = abs(p.yx)/vec2(6.0,5.0); p = p + p*o*o; return p*0.5+0.5; }
void main(){
  vec2 u = curve(uv);
  if (u.x<0.0||u.x>1.0||u.y<0.0||u.y>1.0){ gl_FragColor=vec4(0.0,0.0,0.0,1.0); return; }
  float band = floor(u.y*22.0);
  float on = step(0.62, hash(band*3.1 + floor(time*9.0)));
  u.x += (hash(band + floor(time*18.0)) - 0.5) * on * (0.006 + audio*0.13);
  float ca = 0.0012 + audio*0.028;
  vec3 col = vec3(texture2D(tex,u+vec2(ca,0.0)).r, texture2D(tex,u).g, texture2D(tex,u-vec2(ca,0.0)).b);
  col *= 0.9 + 0.1*sin(u.y*680.0);
  float m = mod(gl_FragCoord.x, 3.0);
  col *= vec3(m<1.0?1.1:0.82, (m>=1.0&&m<2.0)?1.1:0.82, m>=2.0?1.1:0.82);
  float vig = 16.0*u.x*u.y*(1.0-u.x)*(1.0-u.y); col *= pow(vig,0.28);
  col = max(col, texture2D(prev, uv).rgb * (0.42 + audio*0.08));
  col *= 1.04 + 0.03*sin(time*7.0);
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (let ((s (build-nurbs-sphere 16 12)))
    (with-primitive s
      (pdata-add "ori" "v")
      (pdata-copy "p" "ori")
      (identity)
      (rotate (vector 0 (* (time) 16) (* (time) 5)))
      (scale (vector 3 3 3))
      (hint-solid #f)
      (hint-wire)
      (line-width 2.5)
      (wire-opacity 1)
      (wire-colour (vector 0.9 0.9 0.9))
      (pdata-index-map!
        (lambda (i v)
          (let* ((o (pdata-ref "ori" i))
                 (d (vnormalise o))
                 (r (+ 1.0 (* 2.2 (gh (modulo i 16)))
                       (* 0.25 (sin (+ (* 5 (vx o)) (* 3 (time))))))))
            (vmul d r)))
        "p")))

  (draw-text 0)
  (post-shader post))
