; supershape in RETAINED mode (ported fluxus build-once model).
; (retained) tells the host to eval this buffer ONCE: the sphere is built and the
; expensive superformula is computed a single time into "ori". After that only the
; (every-frame ...) thunk runs each frame — a native deform + shader, no rebuild,
; no per-vertex FFI, no superformula recompute. High res stays smooth.

(retained)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)

(define RES 110)                        ; ~72k verts, built ONCE
(define S (build-sphere RES RES))

(define (superr angle m n1 n2 n3)
  (let ((t (/ (* m angle) 4.0)))
    (expt (+ (expt (abs (cos t)) n2) (expt (abs (sin t)) n3)) (/ -1.0 n1))))

; build the supershape into the mesh ONCE (top level runs once in retained mode)
(with-primitive S
  (pdata-add "ori" "v")
  (let ((NV (pdata-size)) (m1 6.0) (m2 3.0) (n1 0.3) (n2 0.5) (n3 0.5))
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
  (recalc-normals)
  (pdata-copy "p" "ori"))               ; ori = the supershape base to deform from

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
  col = col * (0.4 + audio * 3.0) + rim * vec3(0.9, 0.4, 1.0) * (0.4 + audio * 3.0);
  gl_FragColor = vec4(col, 1.0);
}")

; per-frame: only deform + orient + shade the persistent mesh (no rebuild)
(every-frame
  (with-primitive S
    (identity)                          ; retained: transform persists -> rebuild it each frame
    (scale (vector 2.5 2.5 2.5))
    (rotate (vector (* (time) 8) (* (time) 12) 0))
    (shader-source vert frag)
    (shader-set-float! "time"  (time))
    (shader-set-float! "audio" (gain))
    (deform-audio 0.6 0.08 4.0 1.5 #t)))   ; p = ori(supershape) + n*disp, native
