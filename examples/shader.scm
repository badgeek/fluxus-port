; GLSL shader demo — a sphere shaded by a fragment program, pulsing with audio.
; shader-source compiles from source strings (compiled once, cached); uniforms
; (time, audio) are pushed each frame. Audio = default INPUT (mic).

(clear)
(start-audio "system:capture_1" 512 44100)

(define vert "
varying vec3 N;
varying vec3 P;
void main() {
  N = gl_NormalMatrix * gl_Normal;
  P = gl_Vertex.xyz;
  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}")

(define frag "
uniform float time;
uniform float audio;
varying vec3 N;
varying vec3 P;
void main() {
  vec3 n = normalize(N);
  float band = 0.5 + 0.5 * sin(P.y * 4.0 + time * 2.0 + audio * 12.0);
  float rim  = pow(1.0 - abs(n.z), 2.0);
  vec3 col = vec3(0.5 + 0.5 * n.x, band, 0.6 + 0.4 * n.y);
  col = col * (0.35 + audio * 4.0) + rim * vec3(1.0, 0.4, 0.1) * (0.4 + audio * 3.0);
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (begin
    (shader-source vert frag)              ; cached after first frame
    (shader-set-float! "time"  (time))
    (shader-set-float! "audio" (gain))
    (with-state
      (rotate (vector (* (time) 12) (* (time) 20) 0))
      (let ((s (+ 1.6 (* 4 (gain)))))
        (scale (vector s s s)))
      (build-sphere 48 48))))             ; sphere inherits the shader
