; generative skyscraper city on an audio grid.
; The subdivided plane's cells don't share vertices, so setting all 4 verts of a
; cell to ONE height makes a flat-topped block with vertical walls to its
; neighbours = buildings. Heights are a fixed hash of the cell (the "city
; layout"); audio makes the towers jump. Mouse WHEEL zooms out.

(clear)
(start-audio "system:capture_1" 512 44100)

(define GX 44)   ; grid columns
(define GY 44)   ; grid rows

(define (v* v s) (vector (* (vx v) s) (* (vy v) s) (* (vz v) s)))
(define (fract x) (- x (floor x)))
(define (h i) (- (* 2 (fract (* (sin (* (+ i 1) 12.9898)) 43758.5453))) 1)) ; [-1,1]
(define (h01 i) (abs (h i)))                                                 ; [0,1]
(define (look-at eye target up)
  (let* ((f (vnormalise (vsub target eye)))
         (s (vnormalise (vcross f up)))
         (u (vcross s f)))
    (vector (vx s) (vx u) (- (vx f)) 0
            (vy s) (vy u) (- (vy f)) 0
            (vz s) (vz u) (- (vz f)) 0
            (- (vdot s eye)) (- (vdot u eye)) (vdot f eye) 1)))
(define (vlerp a b s) (vadd a (v* (vsub b a) s)))

; grid in XY (+Z normals) -> lay flat so Z becomes up
(define city (build-seg-plane GX GY))

(with-primitive city
  (rotate (vector -90 0 0))
  (scale (vector 40 40 40))
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))

; height for grid cell (cx,cy): fixed generative layout * audio pulse
(define (tower-height cx cy)
  (let* ((base (h01 (+ (* cx 131) (* cy 7))))          ; city skyline shape
         (tall (if (> (h01 (+ (* cx 17) (* cy 91) 5)) 0.80) 2.4 1.0)) ; rare skyscrapers
         (band (modulo (+ cx (* 2 cy)) 16))
         (aud  (+ 0.45 (* 2.6 (gh band)))))            ; towers jump with their band
    (* base tall aud 0.30)))

(define (buildings)
  (with-primitive city
    (hint-solid #f)
    (hint-wire)
    (line-width 1.3)
    (wire-opacity 1)
    (wire-colour (vector 0.1 (+ 0.5 (gh 4)) 1))
    (colour (vector 0.02 0.02 0.05))
    (pdata-index-map!
      (lambda (index val)
        (let* ((o    (pdata-ref "ori" index))
               (n    (pdata-ref "n" index))
               (cell (floor (/ index 4)))              ; 4 verts per grid cell
               (cx   (floor (/ cell GY)))
               (cy   (- cell (* cx GY)))
               (hh   (tower-height cx cy)))
          (vadd o (v* n hh))))                          ; all 4 corners -> flat roof
      "p")))

; damped fly-through camera; MOUSE WHEEL scales the distance (zoom out)
(define (camera)
  (let* ((t      (time))
         (bass   (gh 0))
         (zoom   (/ (camera-dist) 10))                 ; wheel out -> >1 -> further
         (s      (floor (* t 0.4)))
         (way    (vector (* (h s) 22) (+ 10 (* 8 (h01 (+ s 5)))) (* (h (+ s 3)) 22)))
         (target (v* (v* way (- 1 (* 0.4 bass))) zoom))
         (cur    (persist "citycam" target))
         (nxt    (vlerp cur target 0.05))
         (shk    (* 1.2 (gain)))
         (jit    (vector (* shk (h (frame))) 0 (* shk (h (+ (frame) 20))))))
    (persist! "citycam" nxt)
    (set-camera-transform (look-at (vadd nxt jit) (vector 0 0 0) (vector 0 1 0)))))

(define (renderchain)
  (camera)
  (buildings))

(every-frame (renderchain))
