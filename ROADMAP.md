# fluxus → JUCE — Roadmap

Current state: engine renders in JUCE; `IRenderBackend`/`GLBackend` seam;
s7 fluxus command API; two editor variants (JUCE overlay + fluxus `GLEditor`);
fluxus eval model (Ctrl+E / Shift+Enter, animation on committed script).

Below: what's left, prioritized. Each item notes value, effort (S/M/L), and the
files it touches. Recommended order at the bottom.

---

## A. Command-API breadth (toward fluxus parity)

The single highest-leverage area — every command added makes the tool more
capable with little architectural risk. All land in `app/S7ScriptHost.cpp`
(bindings) + a scheme prelude.

- **A1. More primitives** (S). `build-cylinder`, `build-plane xseg yseg`,
  `build-torus` variants, `build-icosphere`, `build-line`/`build-ribbon`. Builders
  already exist in `GraphicsUtils` / `RibbonPrimitive`. ~1 binding each.
- **A2. More state** (S). `wire-colour`, `line-width`, `point-width`, `hint-*`
  (wireframe/points/unlit/none), `opacity`, `specular`, `shininess`, `blend-mode`.
  All are `State` fields already routed through `IRenderBackend`.
- **A3. Turtle / grab** (M). fluxus `(grab id) … (ungrab)` to mutate an existing
  primitive's state/pdata after creation; `(with-primitive id …)`. Needs a
  "current grabbed prim" in the build context.
- **A4. Maths helpers** (S). `vmul`, `vadd`, `vsub`, `vcross`, `vnormalise`,
  `vdist`, `vlerp`, `crndvec`, `srndvec` (fluxus random vectors). Pure scheme
  prelude + a few C bindings for RNG.
- **A5. Persistent scene mode** (M). fluxus also supports non-immediate scenes
  where prims persist across frames and you animate via `(grab)`. Add an
  `(every-frame proc)` registration so a scene builds once and a proc runs each
  frame — the true fluxus model alongside the current whole-buffer re-eval.

## B. pdata — vertex-level access (fluxus signature)

- **B1. pdata read/write** (M). `(pdata-ref "p" n)`, `(pdata-set! "p" n v)`,
  `(pdata-size)`, `(pdata-add "mydata")`. This is what makes fluxus fluxus —
  deform geometry per-vertex from script. `PDataContainer` is already GL-free;
  bind its typed arrays. Reference: fluxus `PDataFunctions.cpp`.
- **B2. pdata-map / foldl** (M). Scheme-side iterate helpers over pdata.

## C. Live-coding UX (fluxus scratchpad features)

- **C1. REPL/console for FluxusGLApp** (M). Port fluxus `Repl` (subclass of
  GLEditor) as a second overlay for interactive one-liners + output. FluxusApp
  already has a console; GLApp has none.
- **C2. Editor show/hide + multiple workspaces** (M). fluxus: Ctrl+H hide,
  digits 0-8 switch 9 buffers, 9 = repl. Store N buffers, a current index.
- **C3. Save / load scripts** (S). JUCE `FileChooser` → read/write the buffer.
- **C4. Scheme syntax colouring** (M). fluxus greys comments, highlights parens.
  GLEditor has paren-match already; add token colouring.
- **C5. Auto-focus toggle** (S). `m_DoAutoFocus` on/off (the zoom-to-cursor).

## D. Audio-reactive (no JACK)

- **D1. IAudioHost seam + JuceAudioHost** (M). `AudioDeviceManager` input →
  ring buffer → FFT. Mirrors the existing IScriptHost/IRenderBackend seam style.
- **D2. Audio bindings** (S). fluxus `(gh n)` harmonic, `(ga)` amplitude,
  `(gain)`; feed FFT bands to the script each frame. Enables sound-driven visuals.

## H. External control — MIDI / OSC (no new libraries)

fluxus has `fluxus-midi` + `fluxus-osc` modules. JUCE covers both, so this is
pure binding work behind seams (same pattern as `IAudioHost`).

- **H1. MIDI in** (M). `IMidiHost` + `JuceMidiHost` (`juce::MidiInput`, CoreMIDI).
  Bind fluxus getters: `(midi-cc channel ctrl)`, `(midi-ccn …)` normalised,
  `(midi-note)`, `(midi-program)`. Ref: `modules/fluxus-midi`. Lib: **none** —
  `juce_audio_devices` (already available).
- **H2. OSC in/out** (M). `IOscHost` + `JuceOscHost` using the **`juce_osc`**
  module (built into JUCE — just link it). Bind `(osc-source "port")`,
  `(osc "/addr")` to read args, `(osc-destination)` + `(osc-send)` to send.
  Ref: `modules/fluxus-osc`. Lib: **none** (fluxus used liblo; JUCE replaces it).

## E. Interaction

- **E1. Mouse-orbit / zoom camera** (M). JUCE mouse events → `Camera` transform
  (orbit, pan, dolly). Currently the camera is fixed at (0,0,-10).
- **E2. Picking** (L). fluxus `(mouse-over)` returns the prim under the cursor.
  `GL_SELECT` is gone in modern GL → colour-pick or CPU ray test (`Geometry.cpp`
  has ray/triangle already). Defer.

## F. Rendering fidelity

- **F1. Textures** (M). `(texture "file.png")`. Needs re-enabling PNGLoader
  (drop `FLUXUS_MINIMAL_NO_PNG`, link libpng) + `TexturePainter` already compiled.
- **F2. Lights API** (S). `(make-light)`, `(light-position)`, ambient/diffuse —
  `Light` API exists; just bind it.
- **F3. More prim types compiled in** (M). Re-add TypePrimitive (text, FreeType
  already fetched for the editor!), ParticlePrimitive, Blobby as builders.

## G. Infrastructure / polish

- **G1. Bundle the font** (S). Copy DejaVuSansMono.ttf into the .app Resources
  instead of an absolute compile-time path (portable builds).
- **G2. Error surfacing in FluxusGLApp** (S). Draw the s7 error string as a GL
  overlay line (GLApp has no console yet).
- **G3. A Makefile / run targets** (S). Like the root project's, for both apps.
- **G4. Smoke tests** (M). Headless: eval a script → assert scene prim count /
  no error. Guards the command API as it grows.

---

## Recommended order

1. **A1 + A2** (S) — cheap primitives + state; biggest capability-per-effort.
2. **B1** (M) — pdata access; the defining fluxus feature.
3. **A5** (M) — `every-frame` / persistent scene; unlocks real fluxus animation
   patterns beyond whole-buffer re-eval.
4. **G1 + G2 + G3** (S) — portability + GLApp error overlay + run targets.
5. **D1 + D2** (M) — audio-reactive; high wow-factor, seam pattern already known.
6. **E1** (M) — mouse-orbit camera; makes exploring scenes natural.
7. **C1–C4** (M) — scratchpad UX (repl, workspaces, save/load, syntax).
8. **F1 / F3** (M) — textures + more prim types.
9. **E2 / G4** (L) — picking + tests; last.

**Fastest path to "feels like fluxus":** A1 → A2 → B1 → A5 → D1/D2.

---

## External libraries — what each feature needs

Current deps: **OpenGL framework** (both apps), **s7** (vendored source),
**JUCE** (fetched), **FreeType** (fetched, FluxusGLApp only).

| Feature | Library | Status |
|---|---|---|
| A (commands), B (pdata), A5 (every-frame) | **none** | engine already compiled — pure binding work |
| Audio (D) | **none new** | JUCE gives CoreAudio input + FFT (`juce_dsp`, `juce_audio_devices`) |
| Text primitive / `build-text` (F3) | **FreeType** | already fetched for the editor — just re-add `TypePrimitive.cpp` |
| Particle / Blobby prims (F3) | **none** | pure engine; excluded only to trim — re-add the .cpp files |
| NURBS | **GLU** | already in the OpenGL framework |
| Textures / `(texture "x.png")` (F1) | **libpng** *or none* | avoid it: decode via JUCE `ImageFileFormat`, push pixels to `TexturePainter`. Skips the dep. DDS already works (no dep). |
| **MIDI in** (`midi-cc`, `midi-note`) | **none new** | `juce_audio_devices` — `MidiInput`/CoreMIDI (fluxus used ALSA) |
| **OSC in/out** (`osc-source`, `osc-send`) | **none new** | **`juce_osc`** module, built into JUCE (fluxus used liblo) |
| **Physics** (`active-box`, gravity, collisions) | **ODE** (Open Dynamics Engine) | NOT yet included — the one real new lib |
| Picking (E2) | **none** | colour-pick or CPU ray (`Geometry.cpp` has ray/triangle) |

### Physics = the one that needs a library (ODE)

fluxus physics is built on **ODE**. To add it:
1. Fetch ODE (has CMake: `github.com/thomasmarsh/ODE` mirror, or odedevs/ode).
2. Re-add `Physics.cpp` (+ `PixelPrimitive` is separate) to `libfluxus_min`, link `ode`.
3. Tick it each frame in `FluxusScene::renderFrame` (`Physics::Tick()`).
4. Bind fluxus physics commands in `S7ScriptHost.cpp`: `(active-box)`,
   `(active-sphere)`, `(passive-box)`, `(set-gravity (vector …))`, `(kick id …)`,
   `(collisions #t)`, etc. Reference: `modules/fluxus-engine/PhysicsFunctions.cpp`.

Effort: **M–L** (ODE build + integration). No other roadmap item needs a new
external dependency — everything else is binding work against already-compiled code.

---

## Raspberry Pi / Linux port

**Done** (the build no longer assumes macOS):
- `cmake/platform.cmake` — the single OS-aware file: `fluxus_gl` (framework
  OpenGL vs `OpenGL::GL` + `OpenGL::GLU`; libfluxus tessellates NURBS through
  GLU, a separate lib off macOS), `fluxus_whole_archive()` (`-force_load` vs
  `--whole-archive`), the backend-TU selection, the Racket link extras.
- `app/GLHeaders.h` — the include-side switch (`<OpenGL/gl.h>` vs `<GL/gl.h>`
  \+ `GL_GLEXT_PROTOTYPES`), used by every JUCE-free TU.
- Null backends with the same `flux_*` surface as the real ones, so bindings and
  sketches don't change: `AppActivityNull`, `VideoHostNull`, `NTSCEffectNull`
  (+ the existing `HandHostNull`). Switches: `FLUXUS_ENABLE_{NTSC,VIDEO,HAND}`.
- `.app` bundling steps and the mac-only `pthread_set_qos_class_self_np` guarded.

**Next, in order:**
1. Build on an aarch64 Linux desktop/VM first — cheap loop, and it separates
   platform bugs from GPU ones. Milestone: `pdata_bench` + `math_test`, both
   headless (no GL context, no JUCE). Then add a `ubuntu-24.04-arm` CI job.
2. Racket CS on the Pi is second; target **s7 (FluxusApp) first** — startup here
   is ~4 s on Apple silicon, expect several times that on a Pi.
3. Then GL, where the real unknowns are. Probe before assuming (this context is
   Mesa **v3d** on VideoCore VI/VII, GL/GLES 3.1 — not Apple's 2.1):
   - **no geometry shaders** on v3d → `shader-source-geom`, `grass-gpu.scm`,
     `noise-grid-3d.scm` are dead there;
   - `glPolygonMode` drives the wireframe/hidden-line look (`PolyPrimitive`,
     `Renderer`, `GLEditor`) — unproven on v3d;
   - GPGPU needs float-renderable FBOs (`RGBA32F`) — unproven;
   - immediate mode (`glBegin`) and display lists are everywhere in the engine:
     supported in a compat profile, but on Mesa's slow paths.
4. Only after those probes: a single `flux_gl_caps()` runtime probe replacing
   today's scattered assumptions (`OpenGL.h` hardcodes `GLEW_* = 1`, `GLSLShader`
   calls `glProgramParameteriEXT` unconditionally, `FluxusCommandsGpu` assumes
   `RGBA32F`), so an unsupported command reports instead of drawing black.
   Deliberately NOT designed in advance — the shape should follow the hardware.
5. Linux packaging: examples currently only ship via `.app` bundling; needs an
   `install(DIRECTORY …)` rule.

---

## Android

A different class of work from the Pi: there the port is a build-system job,
here the renderer has to be rewritten.

**The blocker is GLES, and it is not a guess.** JUCE selects the GLES symbol set
on Android — `modules/juce_opengl/juce_opengl.h:72`:

```cpp
#if JUCE_IOS || JUCE_ANDROID
 #define JUCE_OPENGL_ES 1
 #include "opengl/juce_gles2.h"
```

`juce_gles2.h` has no `glBegin`, `glPushMatrix`, `glMatrixMode`, `glPolygonMode`,
`glLightfv`, `glVertexPointer` or `glEnableClientState` — all of which
`libfluxus` uses, ~170 call sites in total. Unlike Mesa on the Pi there is no
compatibility profile to fall back on. So Android needs the `IRenderBackend`
seam finished (primitives still call GL directly today) plus a real
`GLESBackend`: own matrix stack, lighting as shaders, QUADS split to triangles,
wireframe as index lines or a barycentric shader. GLU is absent too, so
`NURBSPrimitive` goes. **Effort: L, and it is the whole story — every other
Android item is small next to it.**

Build plumbing is lighter than it first looks: JUCE's CMake API does not support
Android (`docs/CMake API.md:6`), but the Android Studio project the Projucer
emits is itself Gradle + `externalNativeBuild` → CMake, so a hand-written Gradle
shell around our existing CMake is viable. Effort: M.

**Racket CS on Android is proven to work** — see `spikes/racket-android/`, which
cross-builds it and runs it on an arm64 emulator. Native `tarm64le` code (no
`pb` fallback), no W^X problem, ~77 ms to boot, and `(get-ffi-obj … #f)` reaches
our C symbols under every loading shape including `System.loadLibrary`'s, so
`racket-lib/fluxus-engine.ss` needs no change. One real catch: the default
cross-build's `libracketcs.a` is non-PIC and cannot be linked into a `.so` —
build with `CFLAGS+="-fPIC"`. Effort: M, and now de-risked.

**A GLES backend is proven to render the engine's own geometry** —
`spikes/gles-cube/` draws a real `PolyPrimitive` (built by `MakeCube`) offscreen
on an Android device: solid, **wireframe without `glPolygonMode`**, and
hidden-line, with no GL errors and **no edits under `vendor/`**. So the look our
sketches depend on survives the move to GLES. What it does not cover:
`Ribbon`/`Particle`/`Voxel`/`Blobby` still draw with `glBegin` and render
nothing there — moving them behind `IRenderBackend` IS the remaining work — and
wireframe on triangulated meshes needs edge dedup or a barycentric shader,
because per-face edges on a TRILIST would expose every triangulation diagonal.

Suggested order: finish the `IRenderBackend` seam while doing the Pi work (a
GLES backend can be tested on desktop via ANGLE), and only then touch Gradle.
The realistic first target is a player-style APK — bundled sketches, touch/OSC
control — not live-coding on a touchscreen keyboard.

