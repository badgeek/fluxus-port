; supershape — Gielis superformula morphing with audio (optimised).
;
; PERF NOTES: build-sphere N N makes N*N*6 unshared verts (60 -> 21600!),
; and pdata-index-map! costs ~9 FFI crossings/vertex. So: (1) modest resolution,
; (2) precompute each vertex's (theta,phi) ONCE and cache across frames with
; persist, (3) the per-frame loop only does the superformula + one pdata-set!.

(clear)
(start-audio "system:capture_1" 512 44100)
(anti-alias #t)

(define RES 26)                              ; 26*26*6 = 4056 verts
(define S (build-sphere RES RES))
(with-primitive S
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))
(define NV (with-primitive S (pdata-size)))

; --- one-time: cache per-vertex angles (theta,phi) across frames -------------
(when (= 0 (vx (persist "ss-init" (vector 0))))
  (let ((th (make-vector NV 0.0)) (ph (make-vector NV 0.0)))
    (with-primitive S
      (let loop ((i 0))
        (when (< i NV)
          (let* ((o (pdata-ref "ori" i)) (d (vnormalise o)))
            (vector-set! th i (atan (vy d) (vx d)))
            (vector-set! ph i (asin (max -1.0 (min 1.0 (vz d))))))
          (loop (+ i 1)))))
    (persist! "ss-th" th)
    (persist! "ss-ph" ph)
    (persist! "ss-init" (vector 1))))
(define TH (persist "ss-th" (make-vector NV 0.0)))   ; cached angle tables
(define PH (persist "ss-ph" (make-vector NV 0.0)))

; 2D superformula radius
(define (superr angle m n1 n2 n3)
  (let ((t (/ (* m angle) 4.0)))
    (expt (+ (expt (abs (cos t)) n2) (expt (abs (sin t)) n3)) (/ -1.0 n1))))

(define vert "
varying vec3 N; varying vec3 P;
void main(){ N = gl_NormalMatrix * gl_Normal; P = gl_Vertex.xyz;
  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }")
(define frag "
uniform float time; uniform float audio;
varying vec3 N; varying vec3 P;
void main(){
  vec3 n = normalize(N);
  float rim = pow(1.0 - abs(n.z), 2.5);
  float band = 0.5 + 0.5 * sin(P.y * 3.0 + time * 1.5);
  vec3 col = vec3(0.4 + 0.6 * n.x, band, 0.6 + 0.4 * n.y);
  col = col * (0.4 + audio * 3.0) + rim * vec3(0.2, 0.7, 1.0) * (0.5 + audio * 3.0);
  gl_FragColor = vec4(col, 1.0);
}")

(define (supershape)
  (let* ((m1  (+ 3.0 (* 9.0 (gh 1))))
         (m2  (+ 2.0 (* 8.0 (gh 5))))
         (n11 (+ 0.3 (* 1.6 (gh 3))))
         (n12 (+ 0.4 (* 3.0 (gh 7))))
         (n13 (+ 0.4 (* 3.0 (gh 9))))
         (sc  (+ 2.0 (* 4.0 (gain)))))
    (with-primitive S
      (shader-source vert frag)
      (shader-set-float! "time"  (time))
      (shader-set-float! "audio" (gain))
      (rotate (vector (* (time) 10) (* (time) 16) 0))
      ; hot loop: cached angles -> superformula -> one pdata-set! (no pdata-ref)
      (let loop ((i 0))
        (when (< i NV)
          (let* ((th (vector-ref TH i)) (ph (vector-ref PH i))
                 (r1 (superr th m1 n11 n12 n13))
                 (r2 (superr ph m2 n11 n12 n13))
                 (cp (* r2 (cos ph))))
            (pdata-set! "p" i (vector (* sc r1 (cos th) cp)
                                      (* sc r1 (sin th) cp)
                                      (* sc r2 (sin ph)))))
          (loop (+ i 1))))
      (recalc-normals))))

(every-frame (supershape))
