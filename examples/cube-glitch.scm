; cube cloud + GLSL glitch shader. Audio tears the cubes apart (blocky vertex
; displacement) with scanlines / flicker / chroma boost. Draw the cubes at top
; level (NOT inside with-primitive) so turtle state applies; each build-cube
; inherits the current shader. Audio = default INPUT (mic).

(clear)
(start-audio "system:capture_1" 512 44100)

(define N 90)
(define (fract q) (- q (floor q)))
(define (h i) (- (* 2 (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453))) 1)) ; [-1,1]

(define vert "
uniform float time;
uniform float audio;
varying vec3 vcol;
float hash(float n){ return fract(sin(n)*43758.5453); }
void main() {
  vec4 p = gl_Vertex;
  // blocky horizontal tearing, gated by audio + a time-stepped hash
  float g = step(0.6, hash(floor(time*18.0) + floor(p.y*4.0)));
  p.x += g * (hash(p.z + floor(time*12.0)) * 2.0 - 1.0) * (0.3 + audio * 8.0);
  p.y += (hash(floor(time*24.0) + p.x) - 0.5) * audio * 3.0;
  vcol = gl_Color.rgb;
  gl_Position = gl_ModelViewProjectionMatrix * p;
}")

(define frag "
uniform float time;
uniform float audio;
varying vec3 vcol;
void main() {
  float sl    = 0.6 + 0.4 * sin(gl_FragCoord.y * 0.9);          // scanlines
  float flick = step(0.93, fract(sin(floor(time * 15.0)) * 99.0)); // random frame flicker
  vec3 c = vcol * (0.35 + audio * 5.0);
  c.r *= 1.0 + audio * 3.0 * flick;                            // chroma push on flicker
  c.b *= 1.0 + audio * 1.5;
  c  *= sl;
  gl_FragColor = vec4(c, 1.0);
}")

(define (cube i)
  (let* ((pos (vector (* 22 (h i)) (* 22 (h (+ i 7))) (* 22 (h (+ i 13)))))
         (a   (gh (modulo i 8)))
         (s   (+ 0.4 (* 5 a))))
    (with-state
      (rotate (vector (* (time) 8) (* (time) 13) 0))
      (translate pos)
      (colour (vector (+ 0.3 a) (+ 0.2 (* 0.6 a)) (+ 0.7 (- 1 a))))
      (scale (vector s s s))
      (build-cube))))                          ; solid; inherits the glitch shader

(define (draw-all i) (when (< i N) (cube i) (draw-all (+ i 1))))

; full-screen post glitch: RGB split + block displacement + scanlines + noise,
; all pushed by audio. Runs over the rendered scene via an FBO.
(define post "
uniform sampler2D tex;
uniform sampler2D prev;   // previous frame's output (feedback / motion blur)
uniform float time;
uniform float audio;
uniform vec2 resolution;
varying vec2 uv;
float hash(float n){ return fract(sin(n) * 43758.5453); }
void main() {
  vec2 u = uv;
  float band = floor(u.y * 24.0);
  float jump = (hash(band + floor(time * 20.0)) - 0.5) * 2.0;
  float on   = step(0.6, hash(band * 3.1 + floor(time * 10.0)));
  u.x += jump * on * (0.01 + audio * 0.18);                 // horizontal block tear
  float sep = 0.002 + audio * 0.03;                          // chroma separation
  float r = texture2D(tex, u + vec2(sep, 0.0)).r;
  float g = texture2D(tex, u).g;
  float b = texture2D(tex, u - vec2(sep, 0.0)).b;
  vec3 col = vec3(r, g, b);
  col *= 0.94 + 0.06 * sin(u.y * 700.0);                     // subtle scanlines (fixed freq)
  float nl = step(0.997, hash(floor(u.y * 220.0) + floor(time * 24.0)));
  col += nl * (0.3 + audio * 2.0);                           // occasional flash lines
  // feedback motion-blur: keep decayed trails of the previous frame
  vec3 trail = texture2D(prev, uv).rgb * (0.82 + audio * 0.12);
  col = max(col, trail);
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (begin
    (shader-source vert frag)                  ; per-cube vertex glitch (cached)
    (shader-set-float! "time"  (time))
    (shader-set-float! "audio" (gain))
    (draw-all 0)
    (post-shader post)))                       ; screen-space glitch over the whole frame
