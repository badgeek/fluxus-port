; deforming wireframe NURBS sphere + glitch/CRT/feedback post, with MULTIPLE
; Neuromancer lines scattered around the screen, each typing out, holding, then
; vanishing and re-appearing elsewhere. Immediate mode so the per-frame text
; meshes are cleared (no leak).

(start-audio "system:capture_1" 512 44100)

;; ---- tweakables ------------------------------------------------------------
(define NUM-QUOTES 5)     ; how many lines on screen at once
(define PERIOD     6.0)   ; seconds each line lives (type + hold + gap)
(define TYPE-SPEED 22)    ; characters typed per second
(define TEXT-SCALE 0.26)  ; line size
(define SPREAD-X   12.0)  ; scatter range
(define SPREAD-Y   7.0)
(define SPREAD-Z   3.0)

(define quotes (list
  "the sky above the port was the color of a dead channel"
  "cyberspace  a consensual hallucination"
  "the matrix has its roots in primitive arcade games"
  "information death at the hands of the ice"
  "he'd operated on an almost permanent adrenaline high"
  "a deranged experiment in social darwinism"
  "the future had already happened"
  "he was looking for the exit that wasn't there"))

(define (frac q) (- q (floor q)))
(define (h i) (- (* 2 (frac (* (sin (* (+ i 1) 91.73)) 3758.53))) 1))   ; hash [-1,1]

;; text for slot j: which quote, how much is typed, and a blinking cursor
(define (slot-text j)
  (let* ((t   (+ (time) (* j 1.7)))                 ; desync the slots
         (k   (inexact->exact (floor (/ t PERIOD))))
         (idx (modulo (+ j k) (length quotes)))
         (ph  (- t (* PERIOD (floor (/ t PERIOD)))))
         (q   (list-ref quotes idx))
         (len (string-length q))
         (td  (/ len TYPE-SPEED))
         (n   (cond ((< ph td) (inexact->exact (floor (* ph TYPE-SPEED))))
                    ((< ph (- PERIOD 1.2)) len)
                    (else 0)))
         (cur (if (and (> n 0) (< ph (- PERIOD 1.2))
                       (odd? (inexact->exact (floor (* t 4))))) "_" "")))
    (values (string-append (substring q 0 (min len n)) cur) k)))

(define (draw-slot j)
  (let-values (((txt k) (slot-text j)))
    (when (> (string-length txt) 0)
      (let ((tp (build-text txt)))                  ; new random spot each cycle (hash of j,k)
        (with-primitive tp
          (identity)
          (translate (vector (* SPREAD-X (h (+ (* j 3) (* k 5))))
                             (* SPREAD-Y (h (+ (* j 9) (* k 7) 2)))
                             (* SPREAD-Z (h (+ (* j 5) k 4)))))
          (scale (vector TEXT-SCALE TEXT-SCALE TEXT-SCALE))
          (colour (vector (+ 0.3 (* 0.3 (h (+ j k)))) 1.0 (+ 0.4 (* 0.3 (h (+ j k 1)))))))))))

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
  float band = floor(u.y*20.0);
  float on = step(0.65, hash(band*3.1 + floor(time*10.0)));
  u.x += (hash(band + floor(time*20.0)) - 0.5) * on * (0.006 + audio*0.14);
  float ca = 0.0015 + audio*0.03;
  vec3 col = vec3(texture2D(tex,u+vec2(ca,0.0)).r, texture2D(tex,u).g, texture2D(tex,u-vec2(ca,0.0)).b);
  col *= 0.96 + 0.14*sin(u.y*650.0);
  float m = mod(gl_FragCoord.x, 3.0);
  col *= vec3(m<1.0?1.12:0.8, (m>=1.0&&m<2.0)?1.12:0.8, m>=2.0?1.12:0.8);
  float vig = 16.0*u.x*u.y*(1.0-u.x)*(1.0-u.y); col *= pow(vig,0.25);
  col = max(col, texture2D(prev, uv).rgb * (0.4 + audio*0.08));
  col *= 1.05 + 0.03*sin(time*8.0);
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (let ((s (build-nurbs-sphere 16 12)))
    (with-primitive s
      (pdata-add "ori" "v")
      (pdata-copy "p" "ori")
      (identity)
      (rotate (vector 0 (* (time) 18) (* (time) 6)))
      (scale (vector 3 3 3))
      (hint-solid #f)
      (hint-wire)
      (line-width 3)
      (wire-opacity 1)
      (wire-colour (vector 1 1 1))
      (pdata-index-map!
        (lambda (i v)
          (let* ((o (pdata-ref "ori" i))
                 (d (vnormalise o))
                 (r (+ 1.0 (* 2.5 (gh (modulo i 16)))
                       (* 0.25 (sin (+ (* 5 (vx o)) (* 3 (time))))))))
            (vmul d r)))
        "p")))

  (draw-text 0)
  (post-shader post))
