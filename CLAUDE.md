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

## Performance (measure before "optimizing")
- **Startup was ~25s; it's now ~4s — don't undo the fix.** The embedded Racket
  boot did not point `current-compiled-file-roots`/`use-compiled-file-paths` at
  the install's separate compiled-collects root, so it recompiled the whole
  collects tree from source every launch. `RacketScriptHost::init` now sets them
  to match the `racket` CLI. `make precompile` builds `racket-lib/compiled/*.zo`
  (gitignored) for the last ~1s.
- **Immediate mode re-`read`s + re-compiles the WHOLE script every frame** (all
  top-level `define`s, `string-append`s, lists — not just the drawing). For a
  heavy sketch that dominates CPU (profile: the "OpenGL Renderer" thread sits in
  `RacketScriptHost::eval → Scall2`, GL draw is a few %). Fix: **retained mode +
  real `(clear)`** — `(retained)` compiles the buffer once and per frame runs
  only the every-frame thunk; put `(clear)` (now a genuine `Renderer::Clear`) and
  `(background …)` at the top of that thunk to wipe + rebuild. Same visuals, big
  CPU drop (the deck went ~96% → ~20%). `(clear)` at the top of an immediate-mode
  sketch stays harmless (the host already clears before each eval).
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
  draw count.

## Self-calibration + new script commands
- `(set-window-size w h)` resizes the GL content (e.g. `1080 1920` for vertical
  IG; a `540 960` window grabs at `1080x1920` on retina). `(screenshot "path")`
  writes the finished framebuffer to a PNG, **once per path** (safe to call every
  frame). `(set-aspect ratio)` letterboxes to a locked AR. Use these + the
  **fluxus-calibrate skill** to draw → screenshot → Read → adjust in a loop.
- **The grab genuinely captures the on-screen frame** (post-shader + MSAA
  included). If a screenshot is black it's almost always gotcha #5 (`eval` wiped
  the sketch), NOT the capture. Correct loop: put `(screenshot "/tmp/x.png")`
  inside the every-frame thunk of the sketch FILE, `cli/fluxus load <file>`, wait
  a beat, `Read /tmp/x.png`, adjust the file, reload. Do NOT `eval` the screenshot.
- `(key-poll)` returns the last-pressed char code (0 if none), consumed once —
  poll it in the thunk for hotkeys. Keys reach scripts even with `(hide-editor)`
  (the component grabs focus). `(set-export on "path" fps)` does an offline
  frame-locked MP4 render; drive start/stop from a hotkey + a rendered-FRAME
  counter (frame-locked `(time)`-based auto-stop is unreliable), and keep the
  window visible while it renders.

## Building optimized visual sketches (patterns that worked)
Reference impl: `examples/iso-city.scm` (an audio-agnostic, self-evolving generative
piece). Reuse these patterns; they keep it cheap AND readable.
- **Retained + persistent scene.** `(retained)`, build streets/static geometry
  ONCE at top level, and in the every-frame thunk only `(destroy)` + rebuild the
  ANIMATED prims (track their ids in a `*dyn*` list; `clear-dyn!` each frame).
  Never rebuild static geometry per frame.
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
Skip `scheme/class`-based files (frisbee/gui/drflux/itchy/joylisten/tricks).

## Conventions
- Racket paths are currently hardcoded (`RACKET_DIR`, `RACKET_LIB_DIR` in CMake) —
  not portable; bundling is a TODO.
- Verify visual changes by launching the app + screenshotting; verify `.ss` changes
  with the racket CLI (fast, no rebuild — `.ss` files load at runtime).
- Commit only when asked; end commit messages with:
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`
