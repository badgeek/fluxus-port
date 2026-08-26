; supershape, retained-style via the shape cache.
;
; The superformula is expensive per-vertex, so we compute it ONCE into the mesh
; and snapshot it with (cache-shape). After that every frame only does a native
; (deform-cached ...) — p = cachedShape + normal*disp, all in C++, no per-vertex
; FFI and no superformula recompute. The shape relaxes back to the cached base
; when the audio is silent. This mimics fluxus's retained build-once behaviour.

(clear)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)

(define RES 72)                              ; high res is fine now (72*72*6 ~= 31k)
(define S (build-sphere RES RES))
(with-primitive S
  (scale (vector 2.5 2.5 2.5))
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))
(define NV (with-primitive S (pdata-size)))

(define (superr angle m n1 n2 n3)
  (let ((t (/ (* m angle) 4.0)))
    (expt (+ (expt (abs (cos t)) n2) (expt (abs (sin t)) n3)) (/ -1.0 n1))))

; ---- build the supershape ONCE, then cache it ------------------------------
(when (not (shape-cached? "ss"))
  (let ((m1 6.0) (m2 3.0) (n1 0.3) (n2 0.5) (n3 0.5))
    (with-primitive S
      (let loop ((i 0))
        (when (< i NV)
          (let* ((o  (pdata-ref "ori" i))
                 (d  (vnormalise o))
                 (th (atan (vy d) (vx d)))
                 (ph (asin (max -1.0 (min 1.0 (vz d)))))
                 (r1 (superr th m1 n1 n2 n3))
                 (r2 (superr ph m2 n1 n2 n3))
                 (cp (* r2 (cos ph))))
            (pdata-set! "p" i (vector (* 2.5 r1 (cos th) cp)
                                      (* 2.5 r1 (sin th) cp)
                                      (* 2.5 r2 (sin ph)))))
          (loop (+ i 1))))
      (recalc-normals)
      (cache-shape "ss"))))               ; snapshot positions + normals in C++

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

; ---- cheap per-frame deform of the cached supershape -----------------------
(every-frame
  (with-primitive S
    (shader-source vert frag)
    (shader-set-float! "time"  (time))
    (shader-set-float! "audio" (gain))
    (rotate (vector (* (time) 8) (* (time) 12) 0))
    (deform-cached "ss" 0.8 0.08 4.0 1.5 #t)))   ; p = cached + n*(band*0.8 + wobble)
