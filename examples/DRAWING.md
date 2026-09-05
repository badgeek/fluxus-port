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

**10. Y-rotating a bar to a heading: use `atan2(dx, dz)`, NOT the compass
bearing.** To orient a cube-as-line along a direction `d=(dx,0,dz)`, a `(rotate
(vector 0 θ 0))` maps the prim's local **+Z** axis to world `(sinθ, 0, cosθ)`.
So the angle that makes local +Z point along `d` is `θ = (deg (atan dx dz))`
(Racket `atan` is 2-arg `atan2`). The trap: a *navigational bearing* is
`atan2(dx, -dz)` (north = −Z = 0°, clockwise) — reusing that for the rotate
**mirrors the bar across the X-axis**. Axis-aligned and symmetric segments hide
the bug (a bar and its X-mirror occupy the same line), so a grid/border looks
perfect while every **diagonal** heading leader renders at the reflected angle —
reads as "pointing the wrong way / almost perpendicular". Keep the two separate:
`bearing` (display compass number) uses `atan2(dx,-dz)`; the geometry rotate uses
`atan2(dx,dz)`. Calibrate with ONE diagonal (e.g. a leader for `d=(+,+)` must
slope down-right when +X=right and +Z=down), never only the axes. Even simpler
when you can: build the bar from its two explicit endpoints via a segment helper
that only needs to *span* `a→b` — direction sign then can't bite you.

**11. `State.Cull` defaults ON — a curved / double-sided mesh renders "rim-only".**
Every prim's `State.Cull` is `true` by default (back-face culling). A FLAT prim
hides this: its one face points at the camera, so it draws whether cull is on or
off. Wrap that same grid onto a **sphere** (or any closed/curved surface) and the
near hemisphere is back-facing → culled → you see the *far* wall through it, with
anything you lifted "outward" now behind it. The signature is **only the silhouette
rim draws, the front face is blank** (it reads as a depth/extrude bug — it is NOT).
Fix: make the prim double-sided — `s->Cull = false` in C, or `(backfacecull #f)` in
script — before blaming winding or z-fighting. (This is why the terminal-on-sphere
showed an empty teal globe until Cull was cleared.) Burned several rebuild cycles.

**12. Frustum aspect ≠ pixel viewport → a true sphere renders as an EGG.** The
camera builds its frustum from an aspect that does NOT auto-track the window
(default ~`720/576 ≈ 1.25`). On a *square* window that squishes X by ~1.25, so a
geometrically perfect sphere (verify: `x²+y²+z² = r²`) draws as a vertical ellipse.
It is a projection artefact, not your geometry, and **`(set-aspect 1.0)` did NOT fix
it** in practice. The reliable cure is to counter it in the transform: widen X by the
mismatch, e.g. `(scale (vector (* s 1.25) s s))` — tune the factor by eye against a
round reference. Any circle/sphere on a non-matching window aspect needs this.

**13. `hint-wire` on a SOLID prim z-fights → the grid is invisible (not "wire is
broken").** Turning on `(hint-wire)` while the solid fill is still on draws the wire
at the *same* depth as the face; the fill wins the depth test and you see no lines —
raising `wire-opacity`/`line-width` does nothing. Two real fixes: (a) pure wireframe
— `(hint-solid #f)(hint-wire)` (the iso-city look), but then the wire is a single
`wire-colour`, NOT the per-vertex `"c"` gradient; or (b) **grid-on-fill** — build a
SECOND prim of the same geometry, wire-only, lifted just off the surface so it can't
z-fight (`fps-terrain.scm`: `+LIFT` on the height, ~0.01 local ≈ 0.1 world). You
cannot get a per-vertex-coloured wireframe over a gradient fill from one prim.

**14. A translucent prim blends against what's ALREADY in the framebuffer → build
the backdrop FIRST.** `(terminal-bg-alpha 0.4)` (or any alpha vertcols) is a normal
`SRC_ALPHA/ONE_MINUS_SRC_ALPHA` blend: it mixes with whatever was drawn *before* it,
not with prims built later. Build a translucent terminal/plane before the bright
object behind it and the "tint" blends over BLACK → reads as solid black, looking
like the alpha did nothing. Build the backdrop earlier in the sketch (lower prim id
= drawn first) and the tint shows correctly. Alpha `0` (bg quads skipped) is
immune — that path is a branch, not a blend.

**15. Custom FPS / fly camera: feed `set-camera-transform` a gluLookAt VIEW matrix
(world→eye), column-major.** `dMatrix` stores translation at `arr[12,13,14]` — the
standard OpenGL column-major layout — so the canonical gluLookAt array drops straight
in (`fps-terrain.scm` `look-at`): rows `s|u|-f`, translation `(-dot(s,eye)
-dot(u,eye) dot(f,eye) 1)`, where `f=normalize(tgt-eye)`, `s=normalize(f×up)`,
`u=s×f`. Default cam is `eye=(0,0,10)` looking −Z, which yields `translate(0,0,-10)`
— check any hand-built matrix against that. To lay a `build-seg-plane` (local XY,
+Z normal) flat as ground, `(rotate (vector -90 0 0))`: local **z becomes world Y**
(height), local y becomes world −Z (depth) — displace height in pdata `"p"` z.

**16. Particle clouds: sparse 1px points are invisible, and a point-source spawn
won't disperse.** Two traps porting a particle system: (a) a few thousand 1px
`GL_POINTS` spread through a volume individually vanish — billboard them into soft
sprites (a geometry shader: point → camera-facing quad + radial-falloff FS) and use
ADDITIVE blend (`(blend-mode 'src-alpha 'one)` + `(hint-nozwrite)`) so faint
overlaps accumulate into a visible glow; budget the per-particle alpha DOWN (≈0.1)
or a dense additive cloud blows out to white. (b) Spawning every particle at one
tiny point makes them all sample near-identical noise → they advect as one coherent
CLUMP and never disperse; spawn across a VOLUME (a box) so each samples a different
part of the field and the cloud actually churns. (The original's plume look needs
100k+ continuously-spawned particles to read; at a few thousand, volume-spawn looks
better.) See `examples/particle-cloud.scm` / `particle-cloud-gpu.scm`.

**17. `set-camera-transform` view built as `mmul(mrotate(vector pitch yaw 0),
mtranslate)`: NEGATIVE pitch looks DOWN, and a single `rotxyz(pitch,yaw,0)` TILTS the
horizon ("miring").** `rotxyz` bakes pitch(X) and yaw(Y) into one matrix, which
leaves a roll component when both are non-zero → the grid horizon comes out slanted.
Split them and apply YAW FIRST, then pitch, so the horizon stays level — and
remember `mmul` follows the engine convention where the RIGHTMOST factor applies
FIRST (see the header comment on `mmul2` in `racket-lib/fluxus-engine.ss`), so the
chain reads right-to-left:
`(mmul (mrotate (vector pitch 0 0)) (mrotate (vector 0 yaw 0)) (mtranslate v))`.
Sign trap: POSITIVE pitch tilts the view UP (content slides off the top) — use a
negative pitch to look down at the ground. (Feeding a gluLookAt matrix, gotcha 15,
sidesteps both.) A hand-laid grid of thin unlit bars is a reliable ground reference
(`quaternion-demo.scm`) when you just want a level floor plane.

**18. `build-text` faces +Z and the app camera looks toward −Z, so a bare
`(build-text …)` already reads correctly — do NOT rotate it 180° to "face the
camera".** The 180°-about-Y flip you'd reach for makes the text VANISH: the glyph
quads render through the builtin text shader and the flipped winding is dropped even
with `(backfacecull #f)`. Draw text with no Y flip. It is LEFT-anchored (origin =
left edge), so centre by shifting `-0.5·width`, `width = CW·len·h` (CW=0.44). Upright
text seen from a pitched-down camera leans a little; tilt it back by the camera pitch
if you need it dead-flat. (See `quaternion-demo.scm`.)

**19. `node-look-at` / `node-orbit` OVERWRITE the node's whole transform → any scale
is lost.** They write a pure rotation+translation from the aim quaternion. If the
aimed object has a scale (a long arrow, a flat fin, a stretched cube), make the
SCALED mesh a CHILD of a bare `(build-node)` locator and aim the LOCATOR — the child
keeps its scale, the locator carries the orientation. Same locator-parent trick the
`(camera-node)` HUD uses.

**20. Look-at has an up-vector singularity — a target passing near straight
overhead/below makes the aim FLIP.** A ring or field of `node-look-at` arrows
tracking a target that moves through the ±up cone snaps ~180° as each arrow's look
direction crosses vertical — reads as chaotic jitter/scatter. Keep the target OUT of
that cone: orbit it in the arrows' OWN plane (same height, no latitude) so they only
yaw, or clamp its elevation. (`node-look-at` uses world-up `(0,1,0)`.)

---

## 4. Verify visually, every step

Positioning is deterministic — calibrate by construction, not by eye. But
composition, colour, and "does it read as X" are NOT derivable from code. The loop:
edit → `cli/fluxus load <file>` (never `eval`, which replaces the whole buffer) →
look at the live window (or `(screenshot)` + Read). When the user is watching, ask
one concrete question per round ("top tick above the cube's top edge? by how
much?") instead of Reading PNGs yourself.
