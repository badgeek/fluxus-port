; isometric audio "plan" rendered through a CRT post shader.
; A subdivided grid whose cells rise as flat-topped blocks (each cell's 4 verts
; share one height -> vertical walls = an isometric city plan). Heights = a fixed
; hash layout * audio band. Viewed near-orthographic (far camera + narrow FOV).

(clear)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)                    ; smooth the wireframe lines

(define GX 40)
(define GY 40)
(define (fract q) (- q (floor q)))
(define (h i) (- (* 2 (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453))) 1))
(define (h01 i) (abs (h i)))
(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))
(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))

(define grid (build-seg-plane GX GY))
(with-primitive grid
  (rotate (vector -90 0 0))          ; lay flat (Z normal -> world +Y = up)
  (scale (vector 30 30 30))
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))

; rotating isometric view: keep the 2:1 iso elevation (~35.26 deg), spin azimuth.
; far camera + narrow fov ~= orthographic. MOUSE WHEEL zooms (camera-dist).
(define (iso-camera)
  (set-fov 11)
  (let* ((el   0.6155)                  ; iso elevation
         (az   (* (time) 0.35))         ; spin
         (zoom (/ (camera-dist) 10))    ; wheel out -> larger dist -> zoom out
         (d    (* 150 zoom)) (ce (cos el)) (se (sin el))
         (eye (vector (* d ce (sin az)) (* d se) (* d ce (cos az)))))
    (set-camera-transform (look-at eye (vector 0 0 0) (vector 0 1 0)))))

; smooth wireframe grid heightfield (displace by ORIGINAL position so shared quad
; corners stay welded = a continuous grid, not blocks). Spectrum ridges + waves.
(define (grid-plane)
  (with-primitive grid
    (hint-solid #f)
    (hint-wire)
    (line-width 1.3)
    (wire-opacity 1)
    (wire-colour (vector 0.2 1.0 0.5))         ; phosphor green grid
    (colour (vector 0.02 0.06 0.03))
    (pdata-index-map!
      (lambda (index val)
        (let* ((o    (pdata-ref "ori" index))
               (n    (pdata-ref "n" index))
               (ox   (vx o)) (oy (vy o))       ; plane coords in [-0.5,0.5]
               (band (modulo (inexact->exact (floor (* (+ ox 0.5) 16))) 16))
               (aud  (* (gh band) 0.9))
               (wav  (+ (* 0.10 (sin (+ (* 16 ox) (* 1.6 (time)))))
                        (* 0.10 (cos (+ (* 14 oy) (* 1.3 (time))))))))
          (vadd o (v* n (+ aud wav)))))
      "p")))

; CRT: curvature, chromatic aberration, scanlines, RGB grille, vignette, flicker
(define crt "
uniform sampler2D tex;
uniform float time;
uniform float audio;
uniform vec2 resolution;
varying vec2 uv;
vec2 curve(vec2 p) {
  p = p * 2.0 - 1.0;
  vec2 o = abs(p.yx) / vec2(5.0, 4.0);
  p = p + p * o * o;
  return p * 0.5 + 0.5;
}
void main() {
  vec2 u = curve(uv);
  if (u.x < 0.0 || u.x > 1.0 || u.y < 0.0 || u.y > 1.0) { gl_FragColor = vec4(0.0,0.0,0.0,1.0); return; }
  float ca = 0.0012 + audio * 0.004;                       // chromatic aberration
  float r = texture2D(tex, u + vec2(ca, 0.0)).r;
  float g = texture2D(tex, u).g;
  float b = texture2D(tex, u - vec2(ca, 0.0)).b;
  vec3 col = vec3(r, g, b);
  col *= 0.85 + 0.15 * sin(u.y * 650.0);                    // scanlines
  float m = mod(gl_FragCoord.x, 3.0);                       // aperture grille
  vec3 mask = vec3(m < 1.0 ? 1.15 : 0.75,
                   (m >= 1.0 && m < 2.0) ? 1.15 : 0.75,
                   m >= 2.0 ? 1.15 : 0.75);
  col *= mask;
  float vig = 16.0 * u.x * u.y * (1.0 - u.x) * (1.0 - u.y); // vignette
  col *= pow(vig, 0.25);
  col *= 1.08 + 0.03 * sin(time * 8.0) + audio * 0.4;       // flicker + audio glow
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (begin
    (iso-camera)
    (grid-plane)
    (post-shader crt)))
