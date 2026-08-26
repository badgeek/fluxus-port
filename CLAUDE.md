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
