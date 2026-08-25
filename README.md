# fluxus → JUCE

The [fluxus](https://github.com/zzkt/fluxus) C++ 3D live-coding engine (`libfluxus`),
re-hosted in a **JUCE** app and driven by a **pluggable script host** — either
**s7 Scheme** or **real Racket (CS)** — through one shared command layer. Renders
with the original fixed-function OpenGL under JUCE's `OpenGLContext`.

Live-code 3D from a transparent code overlay on the scene; edit, press **Ctrl+E**
(or Shift+Enter) to run. Animation via `(time)` keeps running between edits.

## The four apps

Editor and script host are independent axes — any combination works:

| App | Code editor | Script host |
|---|---|---|
| **FluxusApp** | JUCE `TextEditor` overlay | s7 Scheme |
| **FluxusGLApp** | fluxus's own `GLEditor` + `PolyGlyph` (real fluxus code) | s7 Scheme |
| **FluxusRacketApp** | JUCE `TextEditor` overlay | real Racket CS |
| **FluxusGLRacketApp** | fluxus's own `GLEditor` | real Racket CS |

Racket apps need `brew install minimal-racket`; they load fluxus's actual
`.ss` Scheme library (incl. the real `building-blocks.ss`) via FFI.

## Architecture — the seams

```
 editor (JUCE TextEditor | fluxus GLEditor)
        │  code buffer
        ▼
 IScriptHost  ── S7ScriptHost | RacketScriptHost        (swap the language)
        │  calls
        ▼
 FluxusCommands  (shared extern "C" flux_* command layer)
        │
        ▼
 libfluxus engine  ── IRenderBackend / GLBackend         (swap the GL backend)
        │
        ▼
 JUCE OpenGLContext
```

- **`IScriptHost`** — scripting seam. `S7ScriptHost` (tiny, embedded s7) and
  `RacketScriptHost` (boots Racket CS in-process, binds via `ffi/unsafe`). Chosen
  per app by a factory passed to the component.
- **`FluxusCommands`** — one `extern "C"` command layer (`flux_build_cube`,
  `flux_translate`, `flux_pdata_set`, …) that BOTH hosts drive. Add a primitive
  here + bind it in each host → all four apps get it.
- **`IRenderBackend` / `GLBackend`** — rendering seam in the engine (matrix stack,
  material, geometry). GL today; kept as a swap point (raylib was evaluated and
  skipped — GL already renders).
- Engine (system GL) lives in JUCE-free TUs (`FluxusScene`, `FluxusCommands`,
  hosts) so its GL symbols don't clash with JUCE's `juce::gl`.

## Command surface

```scheme
; build
(build-cube) (build-sphere s h) (build-torus i o s h) (build-plane)
(build-ribbon n) (build-particles n)
; state / transform
(colour v) (background v) (translate v) (rotate v) (scale v)
(push) (pop) (with-state …) (identity)
(hint-wire) (hint-solid) (line-width w)
; pdata — vertex-level (grab a primitive first)
(grab id) (ungrab) (with-primitive id …)
(pdata-size) (pdata-ref "p" i) (pdata-set! "p" i v) (recalc-normals)
; time
(time) (frame)
```
`v` is `(vector x y z)`. On Racket, the real fluxus `building-blocks.ss` adds
`pdata-map!`, `pdata-index-map!`, `pdata-fold`, `vx`/`vy`/`vz`, etc; `maths.ss`
and `shapes.ss` add `vmix`, `build-circle-points`, …

## Build & run

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FluxusApp          # or FluxusGLApp / FluxusRacketApp / FluxusGLRacketApp
open build/FluxusApp_artefacts/Release/FluxusApp.app
```
First configure fetches JUCE (+ FreeType for the GL editor). Racket targets are
skipped if `minimal-racket` isn't installed.

## Layout

```
CMakeLists.txt            all four app targets
cmake/libfluxus_min.cmake minimal libfluxus static lib (no external deps but OpenGL)
app/                      host seam + components + script hosts + shared commands
racket-lib/               fluxus .ss library wired to the engine via FFI (see its README)
vendor/fluxus/            vendored fluxus source (minimally edited; grep "fluxus->JUCE port")
spikes/                   proof spikes: s7, Lua/sol2, embedded Racket (embed + Racket-calls-C)
DESIGN.md                 full analysis, seams, phased plan, GL-porting playbook
ROADMAP.md                what's next + per-feature library/dependency notes
```

## Status

Working: 4 apps · s7 + real-Racket hosts · transparent code overlay + fluxus GLEditor ·
IRenderBackend/GLBackend · full command surface incl. **pdata** · particles/ribbon ·
real fluxus `maths.ss` / `shapes.ss` / `building-blocks.ss` running on the engine.

Not done: audio/MIDI/OSC (JUCE-provided, no new deps), physics (needs ODE),
textures, mouse-orbit camera, more `.ss` library files. See `ROADMAP.md`.
