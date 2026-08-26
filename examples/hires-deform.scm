; high-res audio deform, done natively.
; The mesh is dense (build-sphere 90 90 -> ~48k verts). The deformation runs
; entirely in C++ via (deform-audio ...) — it reads the audio bands directly and
; writes p = ori + n*disp per vertex, so there is NO per-vertex FFI. That's what
; makes high resolution cheap here (the per-frame rebuild in C++ is fast; the old
; slowness was the Racket pdata-index-map! FFI loop).

(clear)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)

(define S (build-sphere 90 90))
(with-primitive S
  (scale (vector 2.5 2.5 2.5))
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))

(define vert "
varying vec3 N; varying vec3 P;
void main(){ N = gl_NormalMatrix * gl_Normal; P = gl_Vertex.xyz;
  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }")
(define frag "
uniform float time; uniform float audio;
varying vec3 N; varying vec3 P;
void main(){
  vec3 n = normalize(N);
  float rim = pow(1.0 - abs(n.z), 2.0);
  float b = 0.5 + 0.5 * sin(length(P) * 4.0 - time * 2.0);
  vec3 col = vec3(0.5 + 0.5 * n.x, b, 0.6 + 0.4 * n.z);
  col = col * (0.4 + audio * 3.0) + rim * vec3(1.0, 0.5, 0.2) * (0.4 + audio * 3.0);
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (with-primitive S
    (shader-source vert frag)
    (shader-set-float! "time"  (time))
    (shader-set-float! "audio" (gain))
    (rotate (vector (* (time) 8) (* (time) 12) 0))
    ; native: p = ori + n * (band*2.5 + wobble-wave); recalc normals for shading
    (deform-audio 2.5 0.15 5.0 1.5 #t)))
