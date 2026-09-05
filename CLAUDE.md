# CLAUDE.md — fluxus → JUCE port

The [fluxus](https://github.com/zzkt/fluxus) C++ 3D live-coding engine (`libfluxus`)
re-hosted in JUCE, driven by a pluggable script host (**s7 Scheme** or **real
Racket CS**) through a shared command layer. Four apps; live-code 3D from a
transparent code overlay; Ctrl+E / Shift+Enter to run.

Read `README.md` (overview), `DESIGN.md` (analysis + seams), `ROADMAP.md` (next),
`racket-lib/README.md` (.ss library), `spikes/README.md` (proofs).

## Build & run
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FluxusApp        # s7 + JUCE editor
#                              FluxusGLApp     # s7 + fluxus's own GLEditor
#                              FluxusRacketApp # real Racket + JUCE editor
#                              FluxusGLRacketApp
open build/FluxusApp_artefacts/Release/FluxusApp.app
```
First configure fetches JUCE (+ FreeType for GL editor). Racket targets need
`brew install minimal-racket` and are skipped otherwise.

**Platform layer** — `cmake/platform.cmake` is the only file that knows the OS:
the system-OpenGL target (`fluxus_gl`), the whole-archive helper
(`fluxus_whole_archive`, `-force_load` vs `--whole-archive`), which backend TU
implements each optional subsystem, and the Racket link extras. `app/GLHeaders.h`
is the matching include-side switch (never include `<OpenGL/gl.h>` directly in a
JUCE-free TU). Optional subsystems each have a REAL backend and a null stub with
the same `flux_*` surface, so script bindings — and every sketch — are identical
whatever is built in:
```sh
-DFLUXUS_ENABLE_NTSC=OFF    # NTSCEffectNull; also drops the whole Rust/cargo dep
-DFLUXUS_ENABLE_VIDEO=OFF   # VideoHostNull  (video-*/camera-* become no-ops)
-DFLUXUS_ENABLE_HAND=OFF    # HandHostNull
```
VIDEO/HAND are macOS-only (AVFoundation/Vision) and default OFF elsewhere. The
null TUs warn ONCE on stderr rather than failing silently. Porting checklist and
the open GPU questions (Mesa v3d has no geometry shaders; `glPolygonMode`,
float FBOs unproven) are in `ROADMAP.md`.

## Architecture — the seams (keep them clean)
```
editor (JUCE TextEditor | fluxus GLEditor)
  → IScriptHost (S7ScriptHost | RacketScriptHost)   # swap the language
    → FluxusCommands (shared extern "C" flux_* layer)
      → libfluxus → IRenderBackend/GLBackend → JUCE OpenGLContext
```
- **Add a command once**: implement `flux_*` in `app/FluxusCommands.{h,cpp}`, then
  bind it in BOTH hosts — `app/S7ScriptHost.cpp` (`f_*` + `def(...)`) and
  `racket-lib/fluxus-engine.ss` (`get-ffi-obj` with a failure-thunk). It reaches
  all four apps.
- External inputs (audio, mouse, and future MIDI/OSC) follow the `IAudioHost` /
  `app/AudioHost.{h,cpp}` pattern: a JUCE object pushes values into mutex-protected
  `FluxusCommands` state; scripts read via bound commands.

## CRITICAL gotchas (these caused real bugs)
1. **Script engines are bound to the GL thread and are NOT thread-safe.** Racket CS
   / s7 must only be called from the thread they were init'd on. JUCE renders the
   OpenGLContext synchronously on the MESSAGE thread during move/resize/fullscreen —
   `FluxusScene::renderFrame` guards against this (`glThread` check). Any host
   callback (audio/mouse/MIDI/OSC) runs on OTHER threads: it may ONLY touch the
   mutex-protected `FluxusCommands` state, NEVER call the script engine.
2. **JUCE's `juce_opengl` puts GL symbols in `juce::gl` and guards `<OpenGL/gl.h>`.**
   Engine code that uses system GL must live in JUCE-free TUs (`FluxusScene`,
   `FluxusCommands`, `EditorOverlay`, the hosts) — never include `juce_opengl` there.
   JUCE components talk to the engine through forward-declared handles.
3. **Per-frame immediate model**: `FluxusScene::renderFrame` does `Clear()` → eval
   the committed script (which rebuilds the scene) → `Render()`. So primitives are
   rebuilt every frame; there is no persistent scene yet (fluxus's `every-frame`
   is a no-op because the whole buffer re-evals each frame). The orbit camera and
   audio/mouse state persist because they live outside the scene graph.
4. **State commands are grab-aware**: inside `(with-primitive id …)`,
   `colour`/`rotate`/`hint-*`/`opacity`/etc. modify the GRABBED prim's `State`;
   otherwise they set the build context for the next-built primitive
   (see `grabbedState()` in FluxusCommands.cpp).
5. **`cli/fluxus eval '(expr)'` REPLACES the whole running buffer** (it calls
   `loadSource`, same as `load`). So `eval '(screenshot "/x.png")'` swaps the live
   sketch for just that one call → the scene is EMPTY → a black PNG; `eval
   '(start-record)'` kills the every-frame thunk mid-record → black frames + no
   auto-stop. This burned an entire session chasing a non-existent "MSAA/occlusion
   capture bug." The grab is FINE. To run something WITHOUT destroying the sketch:
   (a) bind it to a **hotkey** read by `(key-poll)` in the thunk (see `V`=record,
   `R`=reset), or (b) add the call INTO the sketch file + `cli/fluxus load <file>`
   (never `eval`). Only use `eval` for throwaway one-liners you're happy to replace
   the buffer with.
6. **`hint-solid` defaults ON for a freshly built prim.** For a see-through
   wireframe you MUST `(hint-solid #f)` explicitly — otherwise a solid fill draws
   in the current `(colour …)` (looked like "black/orange blobs" not wireframe).
7. **[FIXED in 220f4da] `with-state` used to restore only transform+colour — hints,
   wire-colour, `(parent id)`, texture, shader etc. all LEAKED into later prims.**
   `flux_push/flux_pop` now save/restore the WHOLE build state (upstream parity), so
   a `(hint-wire)` or `(parent id)` inside `(with-state …)` no longer escapes. The
   historical symptoms — wire hints bleeding into ribbons (cyan/yellow marks
   rendering red), and labels/prims silently becoming CHILDREN of the last gizmo
   node and following its rotation (burned the gimbal-lock session; the tell is a
   global position = your local one PLUS another node's — check `(get-parent)`
   before suspecting matrix math) — are gone. Defensive pinning like
   `(hint-solid #t)(hint-wire #f)` stays harmless; a sketch that RELIED on the leak
   would change. Related: `concat` is a **no-op stub** here, so `(concat (get-inv-
   camera-transform))` HUD billboards silently do nothing. The clean fix now is to
   parent HUD prims to `(camera-node)` (eye-space, see the camera-as-node note
   below); `camera-yaw`/`pitch`/`dist` manual basis math still works as a fallback.
   Full writeup + 21 drawing/positioning gotchas in `examples/DRAWING.md`.

## Performance (measure before "optimizing")
- **Startup was ~25s; it's now ~4s — don't undo the fix.** The embedded Racket
  boot did not point `current-compiled-file-roots`/`use-compiled-file-paths` at
  the install's separate compiled-collects root, so it recompiled the whole
  collects tree from source every launch. `RacketScriptHost::init` now sets them
  to match the `racket` CLI. `make precompile` builds `racket-lib/compiled/*.zo`
  (gitignored) for the last ~1s.
- **Immediate mode now compiles ONCE per buffer** (`flux-run-guarded` in
  RacketScriptHost caches compiled forms keyed on the exact buffer string; an
  unchanged buffer just re-evals compiled code objects — the expander, which
  dominated the frame cost, is skipped; ~2× steady-state CPU on a 100-cube
  immediate sketch). The re-eval still RE-RUNS every top-level define each
  frame, so **retained mode + real `(clear)`** stays the bigger win for heavy
  sketches — `(retained)` runs only the every-frame thunk; put `(clear)` and
  `(background …)` at the top of that thunk to wipe + rebuild (the deck went
  ~96% → ~20%). `(clear)` at the top of an immediate-mode sketch stays harmless.
  Cache internals: first pass compiles+evals form-by-form (sketch-local
  `define-syntax` works) and splices top-level `(begin …)`; any error drops the
  cache.
- **pdata script access is FAST now — don't hand-roll around it.** Three layers
  (commit 8b62bbc/48f3930): a C-side channel cache (no per-call string+map
  lookups), bulk `flux_pdata_get3/set3` (one FFI crossing per element), and
  whole-channel `pdata-map!`/`pdata-index-map!` (channels read/written ONCE per
  map via `flux_pdata_read_all/write_all`; procs see a snapshot). Net 4.1× on a
  24k-vert `pdata-map!`. Semantics note: `pdata-ref` on ANY float channel
  returns a number now (was only "w"/"s" by name). `./build/pdata_bench` is a
  headless correctness test + benchmark for this layer (no GL, no JUCE) — run
  it after touching pdata code.
- **Measure steady-state, not startup.** ~99% CPU right after launch is Racket
  still loading — wait for full load before judging. An idle/empty sketch is
  ~10%. The JUCE-editor apps render at a 30 Hz timer (not vsync-continuous).
- **The GL bottleneck is per-draw STATE DISPATCH, not vertex upload** (profile:
  `gldUpdateDispatch`/`gleDoDrawDispatchCore`/`gleUpdateDeferredState`, NOT
  `glDrawArrays_IMM`). On Apple's Metal-emulated GL each prim = ~2 draws (a
  hidden-line prim does solid + wire passes) + heavy state churn (polygon-mode,
  lighting/texture enable, colour). Things that DON'T help (all tried, all
  within-noise or worse): **display lists** (`glCallList` still runs every draw),
  **VBOs** (upload isn't the cost; behind `-DFLUXUS_ENABLE_VBO`, default ON but
  marginal), **state-sorting the ImmediateMode record** (retained already groups
  static/dynamic). The ONLY real lever is **fewer draws** — merge geometry, or cut
  prim count. What actually won (95%→~30%): retained mode; a persistent scene
  (build static ONCE, `(destroy)`+rebuild only the animated prims); and pooling
  churny prims (build once, mutate via `grab` — see the smoke pool + caption text
  pool in `examples/iso-city.scm`). Cubes (QUADS, 24 verts) are much cheaper per
  prim than cylinders/spheres (TRILIST, 40–140) — but that cuts vertex cost, not
  draw count. **`(build-merged id-list)` is the direct tool for this**: bakes N
  same-type static polys (transforms + colours per-vertex) into ONE prim = one
  draw; render it with `(hint-vertcols)`, then `(for-each destroy ids)` the
  sources (400 cubes: draw cost ~6× down). One state for the merged prim —
  per-prim hints/opacity/textures don't survive, so it's for uniform-look
  static geometry; vertex-colour lighting is marginally brighter than material
  diffuse (invisible with `hint-unlit`/wire looks).

## Self-calibration + new script commands
- `(set-window-size w h)` resizes the GL content (e.g. `1080 1920` for vertical
  IG; a `540 960` window grabs at `1080x1920` on retina). `(screenshot "path")`
  writes the finished framebuffer to a PNG, **once per path** (safe to call every
  frame). `(set-aspect ratio)` letterboxes to a locked AR. Use these + the
  **fluxus-calibrate skill** to calibrate layout. When the user is present,
  prefer the skill's **human-verdict mode** (they watch the live window and
  answer one-line questions; `cli/fluxus load` reloads in place) — the
  screenshot → Read → adjust loop is the fallback for unattended work.
- **The grab genuinely captures the on-screen frame** (post-shader + MSAA
  included). If a screenshot is black it's almost always gotcha #5 (`eval` wiped
  the sketch), NOT the capture. Correct loop: put `(screenshot "/tmp/x.png")`
  inside the every-frame thunk of the sketch FILE, `cli/fluxus load <file>`, wait
  a beat, `Read /tmp/x.png`, adjust the file, reload. Do NOT `eval` the screenshot.
- **macOS THROTTLES an occluded window — `(time)` nearly freezes, so a screenshot
  A/B of motion looks broken when it isn't.** Two grabs 1.5 s apart come back
  near-identical and an animated sketch reads as frozen. Bring the window forward
  first: `osascript -e 'tell application "System Events" to set frontmost of (first
  process whose unix id is (do shell script "pgrep -n FluxusRacketApp") as integer)
  to true'`. Cheap tell while debugging: keep a `(* k (sin (time)))` probe cube in
  the scene — if it barely moves between grabs it's the throttle, not the sketch.
  (Synthetic `osascript … key code` presses are unreliable too: the held-key poll
  is 30 Hz and a synthetic tap can fall between polls. Human presses are fine.)
- **When the eye can't referee — skinning, matrix chains, projection — write an
  INDEPENDENT reference and diff numbers, and make sure it's independent.** A wrong
  result still looks smooth and plausible, and "it looks off" never says which
  stage. But a checker derived from the same library shares its bugs: this port's
  animation sampler and its first checker both defaulted a partially-animated
  channel to identity, agreed to 0.0000, and were both wrong — the error only
  surfaced when a model rendered 50x too big. A reference written from the SPEC (or
  another library) is worth ten written from the call under test.
  `spikes/model-load/main.cpp` is the pattern: recompute per vertex, assert the
  worst delta.
- `(key-poll)` returns the last-pressed char code (0 if none), consumed once —
  poll it in the thunk for hotkeys. Keys reach scripts even with `(hide-editor)`
  (the component grabs focus). **For hold-to-move (WASD flight etc) use
  `(key-down? c)`, NOT `(key-poll)`** — `key-poll` is discrete and rides OS
  key-repeat (a ~0.5 s gap after the first press → stutter). `(key-down? c)` (c =
  char / 1-char string / code) is the LIVE physical up/down state: the message
  thread polls `KeyPress::isKeyCurrentlyDown` each frame into a JUCE-free atomic
  array (`flux_*_key_down` in FluxusCommands), read every frame for smooth
  continuous input. Polled only when the editor is hidden (sketch owns the
  keyboard); `handleKey` consumes keys then so macOS doesn't beep. Note: on the
  Racket JUCE-editor app the key *events* route to the GL view (script-side
  `key-poll` may not fire with the editor hidden), but `key-down?` reads global OS
  state so it works regardless. Wired in FluxusComponent only — the GLEditor apps
  still lack the held-key poll. See `examples/fps-terrain.scm`.
- **Arrow keys**: JUCE's arrow key codes exceed the 256-slot `g_keyDown` array, so
  the timer poll maps `leftKey/rightKey/upKey/downKey` into the low unused
  control-char slots **1/2/3/4**. Scripts read them as `(key-down? 1)`…`(key-down? 4)`
  (define `(key-left?)` etc. wrappers). See `examples/camera-follow.scm` (arrow-driven
  car with velocity + steering + chase cam).
- **`key-down?` reads GLOBAL OS key state (focus-free) → hold-to-move works even when
  the app window is NOT frontmost, but the arrow key-DOWN events then land in whatever
  IS frontmost (e.g. the terminal), which BEEPs on unhandled arrows ("tick tick tick").**
  It's not a bug in the sketch — the car moving while another app beeps is the tell.
  Fix: click the app window so it's key; then `keyPressed` consumes the arrows
  (`return !editorVisible`) and macOS stays quiet. Burned a few minutes reading it as
  a key-handling bug when it's just window focus.
- **s7 immediate host wraps each frame's sketch in `(catch (lambda () …))` → a
  plain top-level `(define *x* …)` is LOCAL to that lambda and RESETS every frame.**
  Cross-frame state (steer, accumulators) silently reverts to its init each frame;
  `(time)`-driven motion still works, which masks it. Fix on s7: bind into the
  global rootlet once — `(if (not (defined? '*x* (rootlet))) (varlet (rootlet) '*x*
  …))` then `vector-set!` the persistent object. The RACKET host has no lambda wrap
  (it `eval`s each form at namespace top level), and the clean answer there is
  `(retained)` + `every-frame`: state lives in the thunk's CLOSURE for free, and
  geometry builds once instead of re-parsing the whole script each frame
  (`fps-terrain.scm`). Burned a session chasing "why does drift snap back".
- `(set-export on "path" fps)` does an offline
  frame-locked MP4 render; drive start/stop from a hotkey + a rendered-FRAME
  counter (frame-locked `(time)`-based auto-stop is unreliable), and keep the
  window visible while it renders.
- **`(tweak "name" default lo hi)` returns a live slider value** — an ImGui panel
  (`app/ImguiOverlay`, vendored `vendor/imgui`, OpenGL2 backend) lists every
  registered tweak top-right. The registry (`flux_tweak*` in FluxusCommands) is
  mutex-guarded file-scope state, so a dialled-in value SURVIVES the per-frame
  re-eval and Ctrl+E; only `loadScript` (File → Open) clears it. Workflow: drag
  until it looks right, then bake the numbers back into the source. The panel
  draws only when a sketch declares tweaks (otherwise ImGui is skipped entirely —
  zero GL calls), renders AFTER `renderFrame` returns so it never lands in
  screenshots/recordings/exports, and swallows mouse events while hovered so a
  slider drag doesn't also orbit the camera. Show/hide it from View → Show Tweaks
  or `(show-tweaks)`/`(hide-tweaks)`; there is deliberately **no built-in hotkey**
  — sketches bind their own via `(key-poll)` (see `examples/tweak-demo.scm`), which
  keeps every letter free for typing in the editor. Menu and script share one
  state (`flux_*_tweaks_visible`, polled on the message thread like
  `flux_get_editor`) and the menu writes back into it, or the poll would undo the
  toggle a tick later. `(key-poll)` now works in the GLEditor apps too.
  **Measured cost** (324-cube sketch, `sample` on the GL thread): panel open = 28
  of 698 samples ≈ **4%**, hidden ≈ 0.15%. Of that 4%, the OpenGL2 backend's draw
  is only ~0.4% — the cost is ImGui's CPU-side layout in `NewFrame`, NOT the
  legacy-GL state churn you'd expect from the `glPushAttrib` bracket. So don't
  bother swapping in a GL3/shader backend to "fix" state dispatch; it isn't the
  problem. `ps` alone can't see this (run-to-run spread swamps it) — profile.
- **Camera is now a scene-graph node (openFrameworks `ofCamera : ofNode` parity).**
  `(camera-parent id)` makes the camera RIDE a node (follow-cam), `(camera-lag 0..1)`
  smooths it, `(camera-node)` returns an invisible locator tracking the inverse view
  so prims parented to it render in EYE SPACE — a real screen-pinned HUD that
  **retires gotcha #7's manual `camera-yaw/pitch/dist` billboard math** (`concat` is
  still a no-op stub; use `(parent (camera-node))` instead). `ofNode` ergonomics:
  `(build-node)`/`(with-node)` = locator + grab, `(node-look-at id target [up])`,
  `(node-orbit id lon lat r [c])`, `(node-global-transform/pos id)`, `(set-transform m)`.
  Two hard-won impl notes (both burned a session):
  1. **The port bakes the follow-cam ITSELF** (`computeAndApplyCamera` in
     FluxusCommands, engine `Camera::LockCamera` left at 0). Reason: the HUD anchor
     must invert the EXACT applied view, and doing the follow in-port lets us know it
     precisely at any lag. It's recomputed in `flux_camera_finalize()` — called by
     `FluxusScene::renderFrame` AFTER the script eval, BEFORE `Render()` — because at
     frame-begin (`applyCamera`) the followed node still holds last frame's transform;
     using it there makes the HUD drift by one frame (looked like "jitter").
  2. **Engine `dQuat` is partly BROKEN — don't route quats through it.** `dQuat::dot`
     (`dada.h:1031`) has a typo (`z*q.x`), and `dQuat::renorm` divides by |q|² not |q|,
     so `Fluxus::slerp` is doubly wrong. The `flux_q_*` shims implement correct math in
     C++ and only reuse the SAFE `dQuat::toMatrix` / from-matrix ctor (which DO agree
     with scheme `qtomatrix` — both m[row][col], row-vector `v' = v*M`). GL reads our
     row-major `dMatrix.arr()` as column-major (= transpose), so the modelview base is
     `locked*view`; invert accordingly. See `examples/camera-node.scm`.

## GPU: shaders, geometry shaders, and GPGPU (capabilities + hard gotchas)
The context is **legacy OpenGL 2.1 (Apple "Metal - 88.1"), GLSL 1.20** — but it's
more capable than that sounds. PROBE at runtime before assuming a feature is missing
(`glGetString`, `glGetIntegerv`, a trial compile/link) — I was wrong twice this way.
- **Three shader stages exist.** `(shader-source vert frag)` binds a custom
  vertex+fragment program to the grabbed prim (GLSL 1.20 compat builtins:
  `gl_ModelViewProjectionMatrix`, `gl_Vertex`, `gl_Color`, `gl_MultiTexCoord0`…).
  `(shader-source-geom vert geom frag in-type out-type max-verts)` adds a GEOMETRY
  stage — **`GL_EXT_geometry_shader4` really works on this Metal-backed 2.1 context**
  (verified: compile+link+render), set in/out prim type + max verts via
  `glProgramParameteriEXT` before linking (`gl-points`/`gl-lines`/`gl-triangles` in,
  `gl-line-strip`/`gl-triangle-strip` out). Examples: `grass-gpu.scm`,
  `noise-grid-3d.scm` grow geometry per input primitive on the GPU. `(shader-set-*!)`
  sets uniforms on the grabbed prim's program (Apply first).
- **GPGPU / GPU particles: render-to-texture works, with three sharp edges.**
  `(build-gpu-particles w h init-frag)` + `(gpu-update! update-frag)` +
  `(gpu-draw-shaders …)` ping-pong particle state (pos+age) in RGBA32F FBO textures
  and advect all w*h particles in a fragment shader (`particle-cloud-gpu.scm`, 65536
  particles). Hard-won gotchas baked into the impl:
  1. **Apple 2.1-Metal advertises 16 vertex texture units but CANNOT vertex-
     texture-fetch a FLOAT texture** (`"unit 0 … unloadable … sampler Float …
     using zero texture"`). So the draw pass can't sample the position texture in
     its VS — instead `glReadPixels` the updated state back and fill the draw prim's
     pdata. The heavy per-particle work still runs on the GPU; only a cheap readback
     is CPU-side.
  2. **An FBO/GPGPU texture has no mipmaps, but fluxus's default `TextureState.Min`
     is `LINEAR_MIPMAP_LINEAR`** → the texture renders INCOMPLETE ("unloadable") and
     samples as zero. Pin `GL_NEAREST` (+ `CLAMP_TO_EDGE`) when a shader samples a
     non-mipmapped texture.
  3. **Render-to-FBO needs `glDrawBuffer(GL_COLOR_ATTACHMENT0)` then restore
     `glDrawBuffer(GL_BACK)`**, and save/restore the viewport — the update runs mid-
     eval, before the main `Render()`; leaking that state blanks the scene.
  4. **`addPrim` STOMPS the state you set on a freshly-`new`ed primitive** — it calls
     `Renderer::AddPrimitive`, which copies the renderer's build State over the prim's,
     then ORs in the build-context hints. So `p->GetState()->Hints = …` BEFORE
     `addPrim(p)` is silently discarded and you inherit the default `HINT_SOLID`
     (gotcha 6 above), unlit off, no vertcols, and `g_ctx.lineWidth`. Set engine-owned
     prim state AFTER `addPrim` (its own comment says so). This burned a session: the
     GPU velocity-STREAK prim lost `HINT_VERTCOLS|HINT_UNLIT`, so instead of 16k
     coloured 2-vertex segments it drew one lit white `TriStrip` ribbon through every
     particle — read as "blown-white vs invisible density", not as a state bug.
     Compounding it, `PolyPrimitive::Render`'s HINT_SOLID switch had no `LINES` case
     and fell through to `default: TriStrip`; both had to be fixed.
- **Embedded Racket's `(random)` returns a CONSTANT here** (not seeded/varying) — a
  particle system seeded with it spawns every particle identically. Use a per-index
  `sin`-hash (`frac(sin(i*12.9898)*43758.5453)`) for pseudo-randomness instead.
- **Curl noise lives in `racket-lib/gpu-noise.ss`, not in the sketches.**
  `simplex-curl-glsl` (4D simplex WITH analytic derivatives, ported from
  NoiseWorkshop's `SimplexNoiseDerivatives4D.glslinc`) gives `curlNoise(p, t)` and
  `curlNoise(p, t, octaves, persistence)`; `value-curl-glsl` is the ~5x cheaper
  value-noise version. `string-append` one into a fragment shader between the uniforms
  and `main()`. Analytic gradients make a curl 3 noise evaluations instead of 18 finite
  differences AND divergence-free to float precision, so particles never pool in sinks.
  The 4th axis is TIME — pass `u_time`, don't scroll the field along z.

## Building optimized visual sketches (patterns that worked)
Reference impl: `examples/iso-city.scm` (an audio-agnostic, self-evolving generative
piece). Reuse these patterns; they keep it cheap AND readable.
- **Retained + persistent scene.** `(retained)`, build streets/static geometry
  ONCE at top level, and in the every-frame thunk only `(destroy)` + rebuild the
  ANIMATED prims (track their ids in a `*dyn*` list; `clear-dyn!` each frame).
  Never rebuild static geometry per frame. Then collapse the static set with
  `(build-merged ids)` + `(hint-vertcols)` + destroy the sources — one draw for
  the whole static scene (see Performance section for caveats).
- **Rebuild-on-change, not per-frame,** for generative structure. Quantise the
  driver (e.g. a growth `evo-step`) and rebuild the static set only when the step
  advances; track the built ids so you can `(destroy)` them on the next flip.
- **Pool churny prims.** Anything you'd `build-*` every frame (smoke puffs, HUD
  glyphs): build ONCE into a reused pool, then per frame `grab` + reset transform/
  opacity. Park unused pooled prims off-screen (`(translate (vector 0 -9999 0))`)
  or they linger as stale ghosts when the active count drops.
- **Wireframe aesthetic:** `(hint-solid #f)(hint-wire)(hint-unlit)(backfacecull #f)`
  + a bright `(wire-colour …)`; occlude with a near-black solid fill only if you
  want hidden-line. Cubes compose blocky/voxel forms cheaply.
- **Screen-pinned HUD:** capture the camera basis (eye + right/up/fwd) in the
  camera fn, then place text at `eye + fwd*D + right*X + up*Y` and yaw-billboard it.
  Note fov ~12° is telephoto — visible extent at D≈3 is only ~±0.18 wide, so HUD
  offsets/scale are ~5× smaller than intuition; right-align by estimating text
  width (`nchars * CW * scale/0.9`).
- **Grid-routed motion:** route vehicles along cell EDGES (roads), never cell
  centres (buildings); use short PERPENDICULAR half-cell stubs into a source/dest
  cell. Ribbons FOLD at sharp corners ("kelepit") — draw each segment as its own
  straight 2-pt ribbon and overlap the ends to fill the joint.
- **Determinism:** seed everything from `(hsh id)`; make animation a pure function
  of a virtual clock you advance yourself (so you can scale speed live / compress
  for a recording without the value jumping) rather than raw `(time)`.
- **CRT/glow output:** one `(post-shader …)` over the whole frame (bloom +
  scanlines + grille + curvature). HUD text drawn before it inherits the CRT look;
  keep per-element flicker SUBTLE (a few % brightness breathe), not glitchy.
- **Verify visually every step** (screenshot→Read, per the calibration loop above)
  — composition, colour, and "does it read as X" are not derivable from code.

## Model import = assimp (`(load-model …)`) — optional dep, `brew install assimp`
`app/FluxusCommandsModel.cpp` imports fbx/gltf/glb/dae/ply/stl/3ds/obj: one
**indexed** `PolyPrimitive(TRILIST)` per aiMesh (node transform baked into the
verts, `p`/`n`/`t`/`c` filled, material colours + diffuse texture applied), all
parented to one locator. `(load-model path)` returns a HANDLE, not a prim —
`racket-lib/model.ss` wraps it: `(model-ok? m)`, `(model-prims m)`, `(with-model m …)`,
`(model-apply m proc)`, `(model-play m anim (time))`, `(model-bone-named m "head")`.
Examples: `examples/model-load.scm`, `examples/model-anim.scm`. Headless test +
regression guard: `./build/model_test [model…]` (also self-contained with no args).
- **assimp is OPTIONAL** (`find_package(assimp CONFIG)`): without it the apps still
  build and the Racket bindings fall back to their failure thunks (`load-model` → -1,
  `(model-error)` says so). The TU lives in **`fluxus_render`, not `fluxus_core`** —
  `pdata_bench` links `fluxus_core` and must not grow an assimp dependency; the
  frame-begin hook into it is `#ifdef FLUXUS_HAVE_ASSIMP` and pdata_bench stubs it.
  Release bundling of the dylib is NOT done yet (brew ships no static lib).
- **Matrix order:** assimp composes `world = parentWorld * local` in ITS
  column-vector convention. Transposed into fluxus's row-vector `dMatrix` that is the
  standard product `local·parentWorld`, and because the engine's `operator*` is
  reversed the code reads **`world = parentXf * aiToD(node)`** — parent first. Verified
  against assimp's own product across astroBoy's rig; the other order is exact only
  for a FLAT hierarchy (a two-node fbx renders fine either way) and is 42 units off
  on a real skeleton.
- **The bind pose is the bones' `mOffsetMatrix`, NOT the node rest transforms.** In
  FBX the two routinely disagree — the rest pose is just the pose the file was saved
  in. Building the bindpose locator tree from node transforms produced an animation
  that *looked* plausible but was **35.9 units** off assimp's own
  `globalAnim*offset*v` (fox; astroBoy 0.11 — small enough to miss by eye). So
  `bindLocals()` derives each bind global as `meshWorld * offset^-1` and converts to
  locals. `model_test` compares against that reference formula on every skinned
  vertex — currently 0.0000 on fox/astroBoy/druid. Keep that check.
- **Two skinning paths, selected by `(model-skinning 'dual|'linear)`.**
  `'linear` is the ENGINE's `SkinningPrimFunc`: two locator trees (live + bindpose)
  + one `w<n>` float channel per skeleton NODE in `SceneGraph::GetNodes` order
  (**pre-order DFS**, zero-filled for nodes that deform nothing) + `pref`/`nref`.
  Both trees hang off the model root so a transform on the root cancels out of
  `skeleton*bindpose^-1` (verified: same pose with and without a scaled root).
  `(make-pfunc 'genskinweights)` is unrelated and looks broken — never needed here,
  the file supplies real weights.
  `'dual` (the DEFAULT) is a C-side dual-quaternion skinner in the same TU. Linear
  blending averages MATRICES, so a joint folded by a large rotation loses volume —
  the "candy wrapper" that makes an arm crossing the body look tangled. Blending
  unit dual quaternions interpolates the rigid motion instead. It reads the pose
  back out of the LIVE locators, so script bone overrides still drive it, and it is
  sparse (≤4 influences/vertex instead of every node). Measured against linear:
  0.9–2.4% of model size — a local joint correction, same overall pose. Bones with
  non-uniform scale/shear fall back to matrix blending per vertex.
  `model_test` checks `'linear` against assimp's reference formula (must stay
  0.0000) and separately checks `'dual` stays finite and near it — don't "fix" a
  dual-vs-linear difference, that difference IS the feature.
- **A pfunc writes pdata behind the pdata layer's back, so the VBO cache goes
  stale**: `flux_model_set_anim_time` calls `BumpPDataVersion()` after each
  `pfunc-run` or `PolyPrimitive::UpdateVBO` keeps drawing the bind pose in retained
  mode. Any other per-frame pfunc user needs the same bump.
- Cost: import 5-6 ms (cached by path+flags — an immediate-mode sketch re-BUILDS
  prims each frame but does not re-parse), prim build ~0.1 ms, indexed geometry is
  ~4x fewer verts than unindexed. Skinning runs the engine's DENSE loop (every node ×
  every vertex): 0.15 ms/frame at 65 nodes × 2073 verts, but **4.9 ms at 50k verts** —
  a sparse per-bone skinner is ~9x faster if a heavy model ever needs it.
- Embedded textures (glTF/FBX `*0`) decode through `flux_load_texture_mem`
  (`app/TextureLoader.cpp`); external ones resolve relative to the model's folder.
  UVs are flipped in V at import because `flux_load_texture` uploads flipped.

## NTSC filter = ntsc-rs (Rust) — build needs cargo
`(ntsc …)` runs the vendored [ntsc-rs](https://github.com/ntsc-rs/ntsc-rs) signal
simulation (`vendor/ntsc-rs`: core crate + C-FFI staticlib, built by cargo via
`cmake/ntsc_rs.cmake`, linked into all four apps). Needs rustc ≥ 1.89 — **this
machine's nix rustc 1.84 shadows rustup's**; the cmake fragment pins the rustup
cargo + RUSTC from `~/.cargo/bin`, and manual builds need
`RUSTC="$HOME/.cargo/bin/rustc" "$HOME/.cargo/bin/cargo" build --release`.
`(ntsc-preset json)` loads a full ntsc-rs GUI / ntscQT JSON preset (VHS tape
speed, head switching, tracking noise, edge wave, …) — one string = total
control; `""` resets. Classic knobs still work: noise/hue regenerate the signal
settings (n=12 = stock ntsc-rs); saturation/brightness/contrast/scanlines/
monochrome/blend are a GPU monitor pass (`kMonFrag` in `app/NTSCEffect.cpp`,
small-res FBO ping-pong; blend = a real IIR trail; identity settings skip the
pass — ntsc-rs models the signal, not the monitor). use_field defaults to
Upper (half cost, Bob deinterlace).
- **Pipeline** (all in NTSCEffect.cpp, each stage profile-driven): GPU
  downsample to a ≤480-row internal res (decim = ceil(h/480)) → PBO ping-pong
  ASYNC readback (no glReadPixels GPU-flush stall; 1 frame latency) → ntsc-rs
  pass on a dedicated worker thread (GL thread NTSC cost ~12ms → sub-ms;
  1 more frame latency, 2 total — invisible at 25 fps) → GPU monitor pass →
  LINEAR upscale blit. `RAYON_NUM_THREADS` is forced to 1 in `ntscrs_new()`:
  the vendored `thread_pool.rs` then builds NO rayon pool and runs the filter
  inline on our worker — with ≥2 workers the per-row-pass scope() fan-out/join
  wake churn cost ~24 CPU points, dwarfing the ~10-point filter (headless
  bench: `vendor/ntsc-rs/ffi/examples/bench.rs`, args `w h frames [json]`,
  env `RAYON_NUM_THREADS`/`BENCH_BLACK`).
- The ntsc setters (`ntsc-preset`/`ntsc-noise`/`ntsc-hue`) bump `presetRev`
  only on a real VALUE change — an immediate-mode sketch re-runs its top-level
  ntsc calls every frame, and an unconditional bump re-parsed the JSON and
  rebuilt the whole effect per frame.

### Measuring CPU here (all of these burned real time — read before profiling)
- **%CPU across launch paths is apples-to-oranges.** `open`/Finder launch =
  darwin spawn role `ui` → macOS runs the sustained-busy filter thread on
  E-cores: SAME work, SAME 28 fps, ~2.5× the CPU TIME (app ~44% vs ~21% as a
  terminal child, which has no spawn role). Nothing app-side overrides it
  (QoS USER_INTERACTIVE, RT time-constraint policy, NSActivity, taskpolicy -B,
  PRIO_DARWIN_ROLE — all tried, all null; list in `app/AppActivity.mm`). And
  it's the RIGHT call: measured `powermetrics`, the E-core path draws ~725 mW
  vs ~1027 mW on P-cores — `open` is ~30% more battery-efficient despite the
  2× scarier top number. Compare fps/frame-time, never %CPU, across launch
  paths; on battery prefer `open`, for max headroom launch the binary from a
  terminal.
- `ps -o %cpu` is a decaying average (~2× spread across instances); `sample`
  counts parked/blocked threads (cvwait, iokit traps) as if busy. True numbers:
  `top -pid X -l 2 -stats cpu | tail -1` (second sample), paired A/B within
  ONE instance (e.g. `(ntsc #f)` vs `#t`), and real fps via a `(frame)`-to-file
  probe. Simulate E-core cost headlessly: `taskpolicy -c background`.

## Vendored fluxus (`vendor/fluxus/`)
Minimally edited for the port — find every change with `grep -rn "fluxus->JUCE port"`.
Key edits: `OpenGL.h` (mac GL shim, no GLEW/GLUT), `RenderBackend`/`GLBackend` (the
backend seam), `GlutKeys.h` (GLUT key constants for GLEditor), `building-blocks.ss`
provide reconciliation. `cmake/libfluxus_min.cmake` compiles the minimal engine
(OpenGL framework only — no ODE/FreeType/libpng). Vendored include dirs are marked
`SYSTEM` so their legacy warnings don't leak (don't reintroduce warnings in our code).

## Racket `.ss` library (`racket-lib/`)
Real fluxus `.ss` files run on the engine via FFI (`fluxus-engine.ss` wires `flux_*`
with failure-thunk fallbacks so the files also load standalone on the racket CLI).
**Recipe to add a file**: copy it + its deps into `racket-lib/`, add to
`RacketScriptHost::requireLibForm()`, then iterate `racket -e '(require (file …))'`
— on `already required X in Y.ss` drop `X` from `Y.ss`'s provide; on unbound `X`
add a stub to `fluxus-engine.ss`; on `already defined` add to its `except-out`.
- **`racket/class` WORKS — the class system was never the blocker, the legacy module
  PATH was.** brew `minimal-racket` ships no `scheme` collection (that's
  `compatibility-lib`), so upstream's `(require scheme/class)` dies with
  *"collection not found … collection: `scheme`"*; swap it to `racket/class` and
  classes run fine, in library files AND in sketches (`(require racket/class)` at
  the top of a sketch works — `flux-run-guarded` compiles form-by-form, so the
  require is live before later forms expand; verified live with
  `class`/`define/public`/`define/override`/`super`/`init-field`). Caveat: immediate
  mode re-evals the buffer each frame, so class definitions are REBUILT every frame
  and instances held across frames go stale against their own class — use
  `(retained)` + the every-frame thunk when objects must live. s7 has no class
  system, so a class sketch is Racket-only.
  `tricks.ss` is loaded now (its `scheme/class` require was unused). Still skipped:
  `frisbee.ss` (needs the `frtime` package), `gui/drflux/itchy/joylisten` (real
  `scheme/class` GUI code — portable in principle, no engine payoff).
- **After editing ANY `.ss`, `make precompile` before running a Racket app — a stale
  `.zo` silently shadows your source.** The host pins `use-compiled-file-check` to
  `'exists` (the startup win), so Racket loads `racket-lib/compiled/<name>_ss.zo`
  whenever it exists, IGNORING mtime. Symptom that burned a session: a freshly added
  binding reports `undefined` at runtime though the `.ss` clearly defines it. Add any
  NEW `.ss` to the Makefile `LIBSS` list so it gets a `.zo` (and bundles).
- **Sketch source is UTF-8 → the host decodes it with `Sstring_utf8`, NOT `Sstring`.**
  Chez `Sstring` reads a `char*` as Latin-1, so any non-ASCII in a sketch (box-drawing/
  block/emoji glyphs, accents) mangles to per-byte U+FFFD BEFORE your command sees it.
  If unicode arrives as a run of `ef bf bd`, this is why — the fix is at the eval seam
  (`RacketScriptHost::eval`/`eval_cstr`), not in your command. Runtime-built strings are
  fine once the source decodes correctly (`_string` FFI marshals back to UTF-8).

## Conventions
- **`fluxus_core`/`fluxus_render` static libs MUST stay `-force_load` (whole-archive).**
  The Racket host resolves every `flux_*` command via `get-ffi-obj` (dlsym) at RUNTIME —
  invisible to the linker, which would otherwise drop unreferenced archive members, and
  every binding in a dropped TU silently falls back to its failure-thunk stub (commands
  become no-ops, no error). Any future lib factoring of command TUs needs the same
  whole-archive link. Canary check: `nm <app binary> | grep flux_vadd`.
- **Racket runtime: dev builds point at the brew install; release builds bundle it.**
  Dev (default) bakes the absolute `RACKET_DIR` / `RACKET_LIB_DIR` into the binary —
  fast, but the `.app` only runs on this machine. Configure with
  `-DFLUXUS_BUNDLE_RACKET=ON` (CI does) and each Racket `.app` gets a self-contained
  copy in `Contents/Resources/racket/` (~+90 MB): boot files, the collects tree with
  the install's separate compiled root merged back IN-TREE, a relative
  `etc/racket/config.rktd` with `compiled-file-roots '(same)`, and `racket-lib/`.
  `RacketScriptHost::bundleRoot()` finds it relative to the executable and prefers
  it; absent, it falls back to `RACKET_DIR`. The boot line in stderr says which.
  Run `make precompile` BEFORE a bundling build or the `.zo` won't be in the bundle.
  See `cmake/bundle_racket.cmake`.
- **Adding files to an .app AFTER the link breaks its ad-hoc signature**, and a
  DOWNLOADED broken-signature app is rejected by Gatekeeper as *"is damaged and
  can't be opened"* — which reads like a corrupt download but is not, and
  `xattr -dr com.apple.quarantine` does NOT fix it (`codesign --verify` says
  "code has no resources but signature indicates they must be present"). So
  `bundle_racket.cmake` re-signs `--force --deep --sign -` after bundling, on BOTH
  the fresh and already-bundled paths (a relink re-signs the binary and breaks the
  seal again). It also chmods the copied tree u+w — brew ships collects 444, and
  read-only files make the user's `xattr -dr` fail with "Permission denied".
- Verify visual changes by launching the app + screenshotting; verify `.ss` changes
  with the racket CLI (fast, no rebuild — `.ss` files load at runtime).
- Commit only when asked; end commit messages with:
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`
