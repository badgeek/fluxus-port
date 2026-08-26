; audio-deformed NURBS sphere. build-nurbs-sphere's control points are pdata "p";
; push each one radially (out along its own direction — NURBS has no "n") by an
; audio band + a slow wobble, and GLU re-tessellates a SMOOTH bulging surface.
; Far fewer points to move than a poly sphere, and smoother.

(retained)
(start-audio "system:capture_1" 512 44100)

(define s (build-nurbs-sphere 16 22))
(with-primitive s
  (pdata-add "ori" "v")
  (pdata-copy "p" "ori"))

; glitch + CRT + feedback-blur post shader
(define post "
uniform sampler2D tex;
uniform sampler2D prev;
uniform float time; uniform float audio; uniform vec2 resolution;
varying vec2 uv;
float hash(float n){ return fract(sin(n) * 43758.5453); }
vec2 curve(vec2 p){ p = p*2.0-1.0; vec2 o = abs(p.yx)/vec2(6.0,5.0); p = p + p*o*o; return p*0.5+0.5; }
void main(){
  vec2 u = curve(uv);
  if (u.x<0.0||u.x>1.0||u.y<0.0||u.y>1.0){ gl_FragColor=vec4(0.0,0.0,0.0,1.0); return; }
  float band = floor(u.y*20.0);
  float on = step(0.65, hash(band*3.1 + floor(time*10.0)));
  u.x += (hash(band + floor(time*20.0)) - 0.5) * on * (0.006 + audio*0.14);   // block tear
  float ca = 0.0015 + audio*0.03;                                             // chroma split
  vec3 col = vec3(texture2D(tex,u+vec2(ca,0.0)).r, texture2D(tex,u).g, texture2D(tex,u-vec2(ca,0.0)).b);
  col *= 0.86 + 0.14*sin(u.y*650.0);                                          // scanlines
  float m = mod(gl_FragCoord.x, 3.0);                                         // RGB grille
  col *= vec3(m<1.0?1.12:0.8, (m>=1.0&&m<2.0)?1.12:0.8, m>=2.0?1.12:0.8);
  float vig = 16.0*u.x*u.y*(1.0-u.x)*(1.0-u.y); col *= pow(vig,0.25);         // vignette
  col = max(col, texture2D(prev, uv).rgb * (0.4 + audio*0.08));               // feedback blur (subtle)
  col *= 1.05 + 0.03*sin(time*8.0);                                           // flicker
  gl_FragColor = vec4(col, 1.0);
}")

(every-frame
  (with-primitive s
    (identity)
    (rotate (vector 0 (* (time) 18) (* (time) 6)))
    (scale (vector 3 3 3))
    (hint-solid #f)                    ; wireframe only, no fill
    (hint-wire)
    (line-width 1.4)
    (wire-opacity 1)
    (wire-colour (vector 1 1 1))
    (pdata-index-map!
      (lambda (i v)
        (let* ((o (pdata-ref "ori" i))
               (d (vnormalise o))
               (r (+ 1.0
                     (* 2.5 (gh (modulo i 16)))
                     (* 0.25 (sin (+ (* 5 (vx o)) (* 3 (time))))))))
          (vmul d r)))                 ; radial: p = dir * radius
      "p"))
  (post-shader post))                  ; glitch + CRT + feedback blur over the frame
