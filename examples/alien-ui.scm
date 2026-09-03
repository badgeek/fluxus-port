; ALIEN (1979) — Nostromo UI, three screens. Ron Cobb retro-futurist:
; monochrome phosphor, chunky wire graphics, ALL-CAPS readouts, CRT curvature.
;   1 = MU/TH/UR 6000 console   2 = NAV / ORBITAL APPROACH   3 = EMERGENCY DESTRUCT
; Switch live with keys 1/2/3 (key-poll). Retained mode; the whole UI is rebuilt
; each frame into *dyn* (a flat 2D panel on the XZ plane, top-down camera).
; Needs the Racket host.

(clear)
(anti-alias #t)
(set-window-size 540 960)                     ; 9:16 vertical
(ntsc #t) (ntsc-noise 5) (ntsc-saturation 14) (ntsc-scanlines #f)

(define PI 3.14159265358979)
(define *screen* 1)                            ; default screen (sed/keys switch)
(define *ntsc* #t)                              ; NTSC composite filter (key n toggles)
(define *fc* 0)                                 ; frame counter (force window size early)

;; ---- 2D helpers (XZ plane, Y up, flat panel) --------------------------------
(define (v2 x z) (vector x 0.02 z))
(define (v+ a b) (vector (+ (vx a)(vx b)) 0.02 (+ (vz a)(vz b))))
(define (v- a b) (vector (- (vx a)(vx b)) 0.02 (- (vz a)(vz b))))
(define (vs a s) (vector (* (vx a) s) 0.02 (* (vz a) s)))
(define (vlen a) (sqrt (+ (* (vx a)(vx a)) (* (vz a)(vz a)))))
(define (vnrm a) (let ((l (vlen a))) (if (> l 1e-6) (vs a (/ 1.0 l)) (v2 0 0))))
(define (deg r) (* r 57.2957795))
(define (pad n w) (let ((s (number->string n))) (string-append (make-string (max 0 (- w (string-length s))) #\0) s)))
(define (clampf x lo hi) (max lo (min hi x)))

(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye))) (s (vnormalise (vcross f up))) (u (vcross s f)))
    (vector (vx s)(vx u)(- (vx f)) 0 (vy s)(vy u)(- (vy f)) 0
            (vz s)(vz u)(- (vz f)) 0 (- (vdot s eye))(- (vdot u eye))(vdot f eye) 1)))

;; plot half-extents
(define HW 8.4)
(define HH 15.0)

;; ---- palette (phosphor) -----------------------------------------------------
(define C-GRN  (vector 0.55 1.00 0.60))        ; primary green phosphor
(define C-GRND (vector 0.20 0.55 0.28))        ; dim green
(define C-AMB  (vector 1.00 0.78 0.28))        ; amber
(define C-AMBD (vector 0.45 0.34 0.12))        ; dim amber
(define C-RED  (vector 1.00 0.32 0.26))        ; hazard red
(define C-WHT  (vector 0.90 1.00 0.92))

;; ---- primitive builders (return prim id; caller wraps in dyn!) --------------
(define (mk-line a b col w)
  (let* ((mid (vs (v+ a b) 0.5)) (d (v- b a)) (len (max 0.001 (vlen d))))
    (let ((p (build-cube)))
      (with-primitive p (hint-solid)(hint-unlit)(colour col)
        (translate mid) (rotate (vector 0 (deg (atan (vx d) (vz d))) 0))
        (scale (vector (* 0.05 w) 0.02 len)))
      p)))
(define (mk-box pos sx sz rot col lw)
  (let ((p (build-cube)))
    (with-primitive p (hint-solid #f)(hint-wire)(hint-unlit)(backfacecull #f)
      (line-width lw)(wire-colour col)(colour (vector 0 0 0))
      (translate pos)(rotate (vector 0 rot 0))(scale (vector sx 0.01 sz)))
    p))
(define (mk-fill pos sx sz col)                 ; solid filled rect (panels)
  (let ((p (build-cube)))
    (with-primitive p (hint-solid)(hint-unlit)(colour col)
      (translate pos)(scale (vector sx 0.01 sz)))
    p))
(define (mk-dot pos size col)
  (let ((p (build-cube)))
    (with-primitive p (hint-solid)(hint-unlit)(colour col)
      (translate pos)(scale (vector size 0.02 size)))
    p))
(define (mk-text s pos scl col)
  (if (= (string-length s) 0) -1
    (let ((p (build-text s)))
      (with-primitive p (hint-unlit)(colour col)
        (translate (vector (vx pos) 0.12 (vz pos)))    ; lift above panel fills (y=0.02) so text is never occluded
        (rotate (vector -90 0 0))(scale (vector scl scl scl)))
      p)))

;; ---- dynamic prim tracking --------------------------------------------------
(define *dyn* '())
(define (dyn! id) (when (>= id 0) (set! *dyn* (cons id *dyn*))) id)
(define (clear-dyn!) (for-each (lambda (id) (when (>= id 0) (destroy id))) *dyn*) (set! *dyn* '()))
(define (line! a b col w) (dyn! (mk-line a b col w)))
(define (box! p sx sz r col w) (dyn! (mk-box p sx sz r col w)))
(define (fill! p sx sz col) (dyn! (mk-fill p sx sz col)))
(define (dot! p s col) (dyn! (mk-dot p s col)))
(define (txt! s p scl col) (dyn! (mk-text s p scl col)))

;; N-gon ring (optionally squashed to an ellipse via rz), from line segments
(define (ring! cx cz rx rz n col w)
  (let loop ((i 0) (prev #f))
    (when (<= i n)
      (let* ((a (* 2 PI (/ i n)))
             (p (vector (+ cx (* rx (cos a))) 0.02 (+ cz (* rz (sin a))))))
        (when prev (line! prev p col w))
        (loop (+ i 1) p)))))

;; screen frame + corner brackets (shared chrome)
(define (frame! col)
  (line! (vector (- HW) 0 (- HH)) (vector HW 0 (- HH)) col 2.4)
  (line! (vector (- HW) 0 HH)     (vector HW 0 HH)     col 2.4)
  (line! (vector (- HW) 0 (- HH)) (vector (- HW) 0 HH) col 2.4)
  (line! (vector HW 0 (- HH))     (vector HW 0 HH)     col 2.4)
  (let ((cl 1.3))                               ; heavier corner brackets
    (for-each (lambda (sx sz)
      (let ((cx (* sx (- HW 0.05))) (cz (* sz (- HH 0.05))))
        (line! (vector cx 0 cz) (vector (- cx (* sx cl)) 0 cz) col 3.4)
        (line! (vector cx 0 cz) (vector cx 0 (- cz (* sz cl))) col 3.4)))
      (list -1 1 -1 1) (list -1 -1 1 1))))

;; ================================================================
;; SCREEN 1 — MU/TH/UR 6000
;; ================================================================
(define muthur-body
  (list "MU/TH/UR 6000  MU-TH-6000 MAINFRAME"
        "INTERFACE 2037 READY FOR INQUIRY"
        ""
        "> REROUTE COMMAND OVERRIDE"
        "  SPECIAL ORDER 937"
        "  SCIENCE OFFICER EYES ONLY"
        ""
        "NOSTROMO 180924609"
        "CREW 007  ACTIVE 001"
        "STATUS ......... EN ROUTE"
        "COURSE ......... CORRECTED"
        "ROUTE .......... ZETA-2 RETICULI"
        ""
        "PRIORITY ONE"
        "INSURE RETURN OF ORGANISM"
        "FOR ANALYSIS."
        "ALL OTHER CONSIDERATIONS"
        "SECONDARY."
        "CREW EXPENDABLE."))

(define (draw-muthur t)
  (frame! C-GRND)
  ;; top status band
  (fill! (vector 0 0 (+ (- HH) 0.95)) (* 2 (- HW 0.2)) 1.6 (vector 0.06 0.14 0.07))
  (line! (vector (- HW) 0 (+ (- HH) 1.9)) (vector HW 0 (+ (- HH) 1.9)) C-GRN 1.4)
  (txt! "MU/TH/UR 6000" (vector (+ (- HW) 0.6) 0 (+ (- HH) 1.15)) 0.62 C-GRN)
  (txt! (string-append "T+" (pad (inexact->exact (floor t)) 5))
        (vector (- HW 3.2) 0 (+ (- HH) 1.15)) 0.42 C-GRND)
  ;; body text lines, reveal progressively (typewriter)
  (let* ((n (length muthur-body))
         (shown (clampf (inexact->exact (floor (* t 2.2))) 0 n)))
    (let loop ((ls muthur-body) (i 0))
      (unless (null? ls)
        (when (< i shown)
          (let* ((z (+ (- HH) 3.1 (* i 1.28)))
                 (s (car ls))
                 (col (if (< i 2) C-GRN
                          (if (and (> (string-length s) 0) (char=? (string-ref s 0) #\>)) C-WHT C-GRN))))
            (txt! s (vector (+ (- HW) 0.7) 0 z) 0.4 col)))
        (loop (cdr ls) (+ i 1))))
    ;; blinking block cursor on the line after the last shown
    (when (< shown n)
      (let ((z (+ (- HH) 3.1 (* shown 1.28))))
        (when (< (modulo (inexact->exact (floor (* t 2))) 2) 1)
          (fill! (vector (+ (- HW) 0.9) 0 (- z 0.1)) 0.5 0.55 C-GRN)))))
  ;; bottom prompt
  (line! (vector (- HW) 0 (- HH 1.9)) (vector HW 0 (- HH 1.9)) C-GRND 1.0)
  (txt! "REQUEST: ________________________" (vector (+ (- HW) 0.6) 0 (- HH 1.0)) 0.42 C-GRND))

;; ================================================================
;; SCREEN 2 — NAV / ORBITAL APPROACH
;; ================================================================
(define (draw-nav t)
  (frame! C-GRND)
  ;; faint reticle grid
  (let loop ((i -3)) (when (<= i 3)
    (line! (vector (* i 2.6) 0 (- HH)) (vector (* i 2.6) 0 HH) (vector 0.08 0.20 0.10) 0.6)
    (line! (vector (- HW) 0 (* i 2.6)) (vector HW 0 (* i 2.6)) (vector 0.08 0.20 0.10) 0.6)
    (loop (+ i 1))))
  ;; header
  (txt! "NAV/APPROACH" (vector (+ (- HW) 0.6) 0 (+ (- HH) 1.0)) 0.5 C-GRN)
  (txt! "LV-426" (vector (- HW 2.6) 0 (+ (- HH) 1.0)) 0.5 C-GRN)
  ;; --- schematic planet (centre) ---
  (let ((cx 0.0) (cz -1.0) (rot (* t 18)))
    (ring! cx cz 4.4 4.4 44 C-GRN 1.4)                         ; limb
    ;; latitude bands (squashed) — slow vertical wobble
    (let loop ((k -2)) (when (<= k 2)
      (let* ((yy (* k 1.5)) (rr (sqrt (max 0.0 (- (* 4.4 4.4) (* yy yy))))))
        (ring! cx (+ cz (* yy 0.0)) rr (* rr 0.28) 40 C-GRND 0.8)
        (loop (+ k 1)))))
    ;; meridians (rotate)
    (let loop ((m 0)) (when (< m 6)
      (let* ((a (+ (* rot 0.0174) (* m (/ PI 6))))
             (rx (* 4.4 (cos a))))
        (ring! cx cz (abs rx) 4.4 40 C-GRND 0.8)
        (loop (+ m 1)))))
    ;; rotating radar sweep
    (let* ((sa (* rot 0.0174)) (ex (+ cx (* 4.4 (cos sa)))) (ez (+ cz (* 4.4 (sin sa)))))
      (line! (vector cx 0 cz) (vector ex 0 ez) C-GRN 1.6))
    ;; orbit ring + ship marker
    (ring! cx cz 6.6 6.6 60 C-AMBD 0.8)
    (let* ((oa (* t 0.6)) (ox (+ cx (* 6.6 (cos oa)))) (oz (+ cz (* 6.6 (sin oa)))))
      (box! (vector ox 0 oz) 0.5 0.5 45 C-AMB 2.0)
      (line! (vector cx 0 cz) (vector ox 0 oz) C-AMBD 0.7)
      (txt! "NOSTROMO" (vector (+ ox 0.5) 0 oz) 0.34 C-AMB)))
  ;; --- corner telemetry ---
  (let* ((alt (+ 1200 (inexact->exact (floor (* 40 (sin (* t 0.7)))))))
         (vel (+ 8420 (inexact->exact (floor (* 60 (sin (* t 1.3)))))))
         (fuel (clampf (- 96 (/ (floor t) 30.0)) 0 100))
         (o2  (clampf (- 88 (/ (floor t) 55.0)) 0 100)))
    (fill! (vector (+ (- HW) 2.0) 0 (- HH 3.0)) 3.4 4.4 (vector 0.05 0.12 0.06))
    (box!  (vector (+ (- HW) 2.0) 0 (- HH 3.0)) 3.4 4.4 0 C-GRND 1.0)
    (txt! (string-append "ALT  " (pad alt 5)) (vector (+ (- HW) 0.6) 0 (- HH 4.4)) 0.36 C-GRN)
    (txt! (string-append "VEL  " (pad vel 5)) (vector (+ (- HW) 0.6) 0 (- HH 3.6)) 0.36 C-GRN)
    (txt! (string-append "FUEL " (number->string (/ (round (* fuel 10)) 10.0))) (vector (+ (- HW) 0.6) 0 (- HH 2.8)) 0.36 C-AMB)
    (txt! (string-append "O2   " (number->string (/ (round (* o2 10)) 10.0)))   (vector (+ (- HW) 0.6) 0 (- HH 2.0)) 0.36 C-AMB)
    ;; approach vector callout, lower-right
    (txt! "APPROACH VECTOR" (vector (- HW 4.6) 0 (- HH 2.7)) 0.34 C-GRN)
    (txt! "LOCKED" (vector (- HW 2.2) 0 (- HH 2.0)) 0.4 C-GRN)))

;; ================================================================
;; SCREEN 3 — EMERGENCY DESTRUCT
;; ================================================================
(define (draw-destruct t)
  ;; pulsing red hazard border
  (let ((pulse (< (modulo (inexact->exact (floor (* t 2))) 2) 1)))
    (frame! (if pulse C-RED (vector 0.78 0.24 0.20)))
    ;; chevron hazard stripes top + bottom
    (let loop ((i -4)) (when (<= i 4)
      (let ((x (* i 2.0)))
        (line! (vector x 0 (+ (- HH) 0.2)) (vector (+ x 1.0) 0 (+ (- HH) 1.2)) (if pulse C-AMB (vector 0.7 0.5 0.2)) 2.4)
        (line! (vector x 0 (- HH 0.2)) (vector (+ x 1.0) 0 (- HH 1.2)) (if pulse C-AMB (vector 0.7 0.5 0.2)) 2.4)
        (loop (+ i 1)))))
    ;; header (main line always bright; sub line blinks)
    (txt! "EMERGENCY DESTRUCT" (vector (+ (- HW) 0.7) 0 (+ (- HH) 2.4)) 0.6 C-RED)
    (txt! "SYSTEM ACTIVATED"   (vector (+ (- HW) 0.7) 0 (+ (- HH) 3.4)) 0.5 (if pulse C-RED C-AMB))
    ;; --- top-down Nostromo deck schematic (blocky amber wireframe) ---
    (let ((cz 0.5))
      (box! (vector 0 0 cz)        2.2 6.4 0 C-AMB 1.6)          ; spine/hull
      (box! (vector 0 0 (- cz 3.4)) 3.0 1.4 0 C-AMB 1.6)         ; bridge (fwd)
      (box! (vector -2.6 0 cz)    2.0 2.2 0 C-AMB 1.4)           ; port pod
      (box! (vector  2.6 0 cz)    2.0 2.2 0 C-AMB 1.4)           ; stbd pod
      (box! (vector 0 0 (+ cz 3.2)) 3.4 2.0 0 C-AMB 1.6)         ; engineering (aft)
      (line! (vector -2.6 0 cz) (vector -1.1 0 cz) C-AMBD 1.0)   ; struts
      (line! (vector  2.6 0 cz) (vector  1.1 0 cz) C-AMBD 1.0)
      ;; blinking detonation core
      (when (< (modulo (inexact->exact (floor (* t 4))) 2) 1)
        (fill! (vector 0 0 (+ cz 3.2)) 1.0 1.0 C-RED)))
    ;; --- countdown timer ---
    (let* ((remain (max 0 (- 300 (inexact->exact (floor t)))))
           (mm (quotient remain 60)) (ss (modulo remain 60)))
      (fill! (vector 0 0 (- HH 3.6)) 8.4 2.6 (vector 0.05 0.012 0.010))
      (box!  (vector 0 0 (- HH 3.6)) 8.4 2.6 0 C-RED 2.4)
      (txt! "DETONATION IN" (vector -3.4 0 (- HH 4.5)) 0.42 C-AMB)
      (txt! (string-append "T-00:" (pad mm 2) ":" (pad ss 2))
            (vector -3.9 0 (- HH 3.0)) 1.1 (vector 1.0 0.55 0.45))
      (when (< (modulo (inexact->exact (floor (* t 2))) 2) 1)
        (txt! "*** NO OVERRIDE — 5 MIN ***" (vector -4.4 0 (- HH 1.4)) 0.4 C-AMB)))))

;; ---- CRT post shader (curvature + scanline + vignette, hue-neutral) ---------
(define crt "
uniform sampler2D tex;
uniform float time;
uniform vec2 resolution;
varying vec2 uv;
vec2 curve(vec2 p){ p=p*2.0-1.0; vec2 o=abs(p.yx)/vec2(6.2,5.4); p=p+p*o*o; return p*0.5+0.5; }
void main(){
  vec2 u=curve(uv);
  if(u.x<0.0||u.x>1.0||u.y<0.0||u.y>1.0){ gl_FragColor=vec4(0.0,0.0,0.0,1.0); return; }
  float ca=0.0011;
  vec3 col=vec3(texture2D(tex,u+vec2(ca,0.0)).r, texture2D(tex,u).g, texture2D(tex,u-vec2(ca,0.0)).b);
  col*=2.2;
  col=pow(col, vec3(0.9));
  col*=0.86+0.14*sin(u.y*1500.0);                         // strong scanlines
  col*=1.02+0.03*sin(time*3.0+u.y*40.0);                  // roll flicker
  float d=length(u-0.5); col*=1.0-d*d*0.34;               // vignette
  gl_FragColor=vec4(col,1.0);
}")

;; ---- camera: flat top-down panel -------------------------------------------
(define (panel-camera)
  (set-fov 12)
  (let* ((zoom (/ (camera-dist) 10)) (d (* 162 zoom)))
    (set-camera-transform (look-at (vector 0 d 0) (vector 0 0 0) (vector 0 0 -1)))))

(retained)
(every-frame
  (begin
    (clear)                                     ; full wipe each frame (whole UI is rebuilt; no cross-load leftovers)
    (set! *fc* (+ *fc* 1))
    (when (< *fc* 24) (set-window-size 540 960)) ; force 9:16 for the first frames after load
    (let ((k (key-poll)))
      (cond ((= k 49) (set! *screen* 1)) ((= k 50) (set! *screen* 2)) ((= k 51) (set! *screen* 3))
            ((= k 110) (set! *ntsc* (not *ntsc*)) (ntsc *ntsc*))))   ; n = toggle NTSC
    (let ((t (time)))
      (cond ((= *screen* 1) (draw-muthur t))
            ((= *screen* 2) (draw-nav t))
            ((= *screen* 3) (draw-destruct t))))
    (panel-camera)
    (post-shader crt)))
