; supershape computed in the VERTEX SHADER (GPU).
; The mesh is a plain sphere (built once, retained). Each vertex's direction is
; reshaped by the Gielis superformula in the vertex shader, with the parameters
; passed as uniforms and morphed by audio + time. Zero CPU per-vertex work, so it
; runs real-time at very high resolution.

(retained)
(start-audio "system:capture_1" 512 44100)

(define RES 160)                    ; ~150k verts — GPU does all the shaping
(define S (build-nurbs-sphere RES RES))

(define vert "
uniform float time; uniform float audio;
uniform float m1; uniform float m2;
uniform float n1; uniform float n2; uniform float n3;
varying vec3 N; varying vec3 P;
float superr(float a, float m, float e1, float e2, float e3){
  float t = m * a / 4.0;
  return pow(pow(abs(cos(t)), e2) + pow(abs(sin(t)), e3), -1.0 / e1);
}
vec3 ss(float th, float ph){                       // supershape position
  float r1 = superr(th, m1, n1, n2, n3);
  float r2 = superr(ph, m2, n1, n2, n3);
  float cp = r2 * cos(ph);
  return vec3(r1 * cos(th) * cp, r1 * sin(th) * cp, r2 * sin(ph)) * 2.4;
}
void main(){
  vec3 d = normalize(gl_Vertex.xyz);
  float th = atan(d.y, d.x);
  float ph = asin(clamp(d.z, -1.0, 1.0));
  float e = 0.01;
  vec3 pos = ss(th, ph);
  vec3 dt  = ss(th + e, ph) - pos;                 // tangents by finite difference
  vec3 dp  = ss(th, ph + e) - pos;
  vec3 nrm = normalize(cross(dt, dp));             // analytic surface normal
  P = pos;
  N = gl_NormalMatrix * nrm;
  gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);
}")

(define frag "
uniform float time; uniform float audio;
varying vec3 N; varying vec3 P;
void main(){
  vec3 n = normalize(N);
  float rim = pow(1.0 - abs(n.z), 2.0);
  float b = 0.5 + 0.5 * sin(length(P) * 3.0 - time * 2.0);
  vec3 col = vec3(0.5 + 0.5 * n.x, b, 0.6 + 0.4 * n.z);
  col = col * (0.45 + audio * 2.5) + rim * vec3(0.7, 0.4, 1.0) * (0.4 + audio * 3.0);
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (with-primitive S
    (identity)
    (rotate (vector (* (time) 8) (* (time) 12) 0))
    (shader-source vert frag)
    (shader-set-float! "time"  (time))
    (shader-set-float! "audio" (gain))
    ; morph the superformula params with audio + a slow drift
    (shader-set-float! "m1" (+ 3.0 (* 8.0 (gh 1)) (* 2.0 (sin (* 0.3 (time))))))
    (shader-set-float! "m2" (+ 2.0 (* 6.0 (gh 5)) (* 2.0 (cos (* 0.2 (time))))))
    (shader-set-float! "n1" (+ 0.3 (* 0.8 (gh 3))))
    (shader-set-float! "n2" (+ 0.6 (* 2.5 (gh 7))))
    (shader-set-float! "n3" (+ 0.6 (* 2.5 (gh 9))))))
