# Drawing & positioning helpers (from `calib-3d.scm`)

Reference for the small 2D/3D mark helpers used by the calibration sketch, and —
more importantly — **how positioning actually works** on this engine. Every number
below was verified live in a screenshot-and-Read calibration session; the gotchas
are the ones that actually bit.

---

## 1. The coordinate frame

The camera looks down **+Z**. By default it sits at `(0,0,-10)` and is *tilted*
(orbit yaw/pitch ≈ 0.3 rad). For any layout where you compare 2D overlay marks
against 3D geometry you MUST pin it head-on first, or the 3D projection shifts
while your z=0 marks stay put:

```scheme
(set-camera-position (vector 0 0 10))   ; eye on-axis, looking at origin
```

`R` (`camera-reset`) releases it back to the mouse orbit.

### Visible extent at the z=0 plane

Everything drawn at `z=0` lives inside a rectangle whose half-size you compute
from the FOV and camera distance:

```
hh = CAM-DIST * tan(FOV/2 in radians)     ; half-height, world units
hw = hh * aspect                          ; half-width
aspect = screen_w / screen_h              ; from (get-screen-size) -> (vector w h)
```

So visible `x ∈ [-hw, hw]`, `y ∈ [-hh, hh]`. **Position everything as a fraction
of `hw`/`hh`** and the layout reframes correctly at any window size. Recompute
`hw`/`hh` every frame from `(get-screen-size)` — never hard-code pixels.

At `FOV 50 / CAM-DIST 10`: `hh = 10*tan(25°) ≈ 4.663`, and at 9:16
`hw ≈ 4.663 * 0.5625 ≈ 2.62`. Handy conversions at that setting:
`1 world unit ≈ 0.214 of the frame HEIGHT`, so a 2-unit cube spans ~21% tall.

### Retina capture

`(set-window-size 540 960)` renders a 540×960 window but `(screenshot …)` grabs
it at **2× → 1080×1920** (exact Instagram-vertical). Iterate small, export is
already full-res.

---

## 2. Helpers

### `(line-col ax ay az bx by bz col w)`
A single straight 2-point line as a ribbon, `col` colour, `w` **half-width**
(see gotcha below). Build any tick/rule/segment with this.

### `(grid-line ax ay az bx by bz)`
Thin green (`GRID`) `line-col`, width `0.008`. Used for the ground grid and the
centre cross.

### `(circle cx cy r col w)`
Outline ring on the z=0 plane: a closed ribbon of 48 segments, centre `(cx,cy)`,
radius `r`, colour `col`, half-width `w`. Outline only — no fill.

### `(wire-cube x y z s col)`
A see-through wireframe cube, side `s`, centred at `(x,y,z)`, colour `col`.
Wraps `wire-look` (below) so it never draws a solid fill.

### `(wire-look col)`
`(hint-solid #f)(hint-wire)(hint-unlit)(backfacecull #f)(wire-colour col)`.
Required on every freshly built prim you want as wireframe — see gotcha #1.

### `(ltext str x y h col)`
Left-anchored text on z=0. `x` is the LEFT edge, `y` the cap line, `h` the cap
height. Glyph advance is `CW = 0.44` per char, so a string's world width is
`CW * (string-length str) * (h/0.9)`:

```scheme
(define (text-w str h) (* 0.44 (string-length str) (/ h 0.9)))
;; centre on cx:  (ltext str (- cx (* 0.5 (text-w str h))) y h col)
;; right edge xr: (ltext str (- xr (text-w str h))        y h col)
```
Placing short strings at a fixed `x` makes them drift — always anchor by width.

---

## 3. Positioning gotchas (each cost real time this session)

**1. `hint-solid` defaults ON.** A freshly built prim draws a solid fill in the
current colour unless you `(hint-solid #f)`. For wireframe use `wire-look`.

**2. Ribbon `w` is a HALF-width, and geometry is the CENTRELINE.** A ribbon
extends `w` to *each* side of its path. Two ribbons tangent at their paths visibly
overlap by a full stroke. For an outline shape resting *on* a rule, offset by the
sum of both half-widths, not half of each.

**3. Near-face projection — a face toward the camera projects LARGER.** A cube of
half-size 1 centred at the origin has its near face at `z=+1`, i.e. distance
`CAM-DIST-1 = 9` from the eye, not 10. Its silhouette (the near face) projects to
the screen height that a z=0 mark at

```
y = world_y * CAM-DIST / (CAM-DIST - face_z)      ; = 1 * 10/9 here
```

would occupy. So the calibration ticks that touch the cube's on-screen top/bottom
edges sit at `y = ±10/9`, NOT `±1`. Generalise: multiply by `CAM-DIST/(CAM-DIST -
face_z)` for any near-face offset.

**4. Corner marks at a 0.9 margin get CLIPPED if the mark has extent.** A circle
centred at `(0.9·hw, 0.9·hh)` runs off-screen by its own radius. Inset the centre
by the extent so the *edge* lands on the margin:

```scheme
(let* ((r (* 0.08 hh)) (mx (- (* 0.9 hw) r)) (my (- (* 0.9 hh) r)))
  (circle (- mx) my r col w) ...)     ; ring edge sits at 0.9, whole ring visible
```
Same for text: a caption's right edge, not its origin, is what hits the margin.

**5. `(screenshot …)` grabs the FINISHED framebuffer.** Immediate mode does
`Clear → eval → Render`. If you call `(screenshot)` *inside* the every-frame body,
`Render()` for THIS frame hasn't run yet — you capture the PREVIOUS frame. So you
can't reliably screenshot "the frame where my blink is on" by gating the shot on
the blink flag. Capture across several frames (distinct filenames) instead, or
accept it lags one frame.

**6. Blink / animation is a pure function of `(time)`.** Immediate mode re-evals
the whole buffer every frame, so `(time)` advances and any `(time)`-driven value
animates:

```scheme
(even? (inexact->exact (floor (* (time) 3))))   ; ~1.5 Hz on/off toggle
```
`floor` returns an inexact real; `inexact->exact` before `even?`.

**7. Legibility beats correctness-in-the-dark.** A thin red mark placed over other
red geometry is invisible even when perfectly positioned. Give calibration marks a
distinct colour (e.g. `CYAN`), extra width, and clear space away from the geometry
they measure — otherwise you can't verify the alignment you just computed.

**8. `wire-cube` (any `hint-wire` prim) LEAKS its wire state into later prims.**
The nastiest one this session: build a wireframe cube, then every ribbon built
*after* it comes out red — regardless of the `(colour …)` you set. Two facts
combine:
- A ribbon's SOLID pass uses `State.Colour` **only when `HINT_WIRE` is off**;
  with `HINT_WIRE` on it instead draws a wire in `State.WireColour`.
- `wire-look` sets `(hint-solid #f)(hint-wire)(wire-colour …)` on the *global*
  build context, and `with-state` does **not** restore hints / wire-colour on
  exit — so `HINT_WIRE` and the last red `WireColour` persist into the next prim.

So `line-col`/`circle` inherited `HINT_WIRE` + red `WireColour` and rendered as a
red wire; the ground grid escaped only because it's built *before* the cubes. Fix:
every ribbon pins its own render mode **before** `(colour …)`:

```scheme
(hint-solid #t) (hint-wire #f) (hint-unlit) (colour col)
```

Diagnostic tells: same helper renders one colour early in the frame and a
different colour later; the wrong colour matches an *earlier* prim's colour. Note
also that a ribbon's per-vertex `"c"` pdata is ignored for solid/wire fill unless
`(hint-vertcols)` is on — setting `"c"` does nothing by itself; `State.Colour`
(via `(colour …)`) is what a plain solid ribbon uses.

**9. No `concat` → no easy HUD billboard.** `concat` (multiply a matrix into the
current transform) is a **no-op stub** in this port (`fluxus-engine.ss`). So the
usual screen-pinned-HUD trick — `(concat (get-inv-camera-transform))` then place
text in eye space — silently does nothing; the text stays on whatever plane you
built it. `get-camera-transform` / `get-inv-camera-transform` (and
`camera-yaw`/`camera-pitch`/`camera-dist`) ARE readable, so a true billboard is
possible but must be built by hand: compute eye + right/up/fwd from the orbit
angles and position/orient each label yourself (the iso-city HUD pattern). For an
on-axis calibration pose, a plain z=0 overlay is screen-correct and far simpler —
just don't call it "billboarded", and know it swims if the camera orbits.

---

## 4. Verify visually, every step

Positioning is deterministic — calibrate by construction, not by eye. But
composition, colour, and "does it read as X" are NOT derivable from code. The loop:
edit → `cli/fluxus load <file>` (never `eval`, which replaces the whole buffer) →
look at the live window (or `(screenshot)` + Read). When the user is watching, ask
one concrete question per round ("top tick above the cube's top edge? by how
much?") instead of Reading PNGs yourself.
