; static supershape, retained mode — built ONCE, per frame only rotates + shades.
; No per-frame vertex work at all (the mesh persists), so it should be perfectly
; smooth even at high resolution. Good test that retained mode itself is cheap.

(retained)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)

(define RES 140)                        ; ~118k verts, built ONCE
(define S (build-sphere RES RES))

(define (superr angle m n1 n2 n3)
  (let ((t (/ (* m angle) 4.0)))
    (expt (+ (expt (abs (cos t)) n2) (expt (abs (sin t)) n3)) (/ -1.0 n1))))

; compute the supershape into the mesh ONCE
(with-primitive S
  (let ((NV (pdata-size)) (m1 7.0) (m2 4.0) (n1 0.2) (n2 1.7) (n3 1.7))
    (let loop ((i 0))
      (when (< i NV)
        (let* ((o  (pdata-ref "p" i))
               (d  (vnormalise o))
               (th (atan (vy d) (vx d)))
               (ph (asin (max -1.0 (min 1.0 (vz d)))))
               (r1 (superr th m1 n1 n2 n3))
               (r2 (superr ph m2 n1 n2 n3))
               (cp (* r2 (cos ph))))
          (pdata-set! "p" i (vector (* 2.5 r1 (cos th) cp)
                                    (* 2.5 r1 (sin th) cp)
                                    (* 2.5 r2 (sin ph)))))
        (loop (+ i 1)))))
  (recalc-normals))

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
  float b = 0.5 + 0.5 * sin(length(P) * 3.0 - time * 2.0);
  vec3 col = vec3(0.5 + 0.5 * n.x, b, 0.6 + 0.4 * n.z);
  col = col * (0.5 + audio * 2.0) + rim * vec3(0.9, 0.4, 1.0) * (0.4 + audio * 3.0);
  gl_FragColor = vec4(col, 1.0);
}")

; per frame: only orient + shade the persistent supershape (no deform)
(every-frame
  (with-primitive S
    (identity)
    (scale (vector 2.5 2.5 2.5))
    (rotate (vector (* (time) 8) (* (time) 12) 0))
    (shader-source vert frag)
    (shader-set-float! "time"  (time))
    (shader-set-float! "audio" (gain))))
