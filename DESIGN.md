# Fluxus → JUCE Port — Design

Port the fluxus C++ 3D engine (`libfluxus`) to a JUCE-hosted app, rendering via
OpenGL first and **raylib** later, keeping the engine untouched behind clean
adapter seams.

Source analyzed: `https://github.com/zzkt/fluxus.git` (fixed-function OpenGL,
~2005-era). This doc captures the analysis + the phased plan + chosen decisions.

---

## 1. What fluxus is — 3 clean tiers

```
src/        HOST      GLUT window + main loop + embedded Racket VM + GL text editor   → REPLACE
modules/    BINDINGS  Racket <-> C++ glue. ALL Scheme lives here. Engine::Get() singleton → REPLACE
libfluxus/  ENGINE    scene graph + primitives + renderer. RACKET-FREE. raw fixed-func GL → KEEP
```

Verified facts (from source analysis):

- `libfluxus` is **Racket-free** and **host-agnostic**. Nothing in it must change to re-host.
- The host never calls the renderer directly: each frame it evals the string
  `(fluxus-frame-callback)`; Scheme calls down through the **`Engine::Get()`
  global singleton** into `Fluxus::Renderer::Render()`.
- `Primitive::Render()` is the **single virtual draw call** per primitive
  (`Primitive.h:32`). Geometry lives in GL-free `PData` arrays (`dVector`/`dColour`).
  Math (`dada.h`) is standalone. Procedural mesh builders (`GraphicsUtils.cpp`,
  cube/sphere/torus/…) are **zero-GL**.
- ~55% of engine files port untouched (math, PData, scene tree, evaluators, IO, physics sim).
- It is **fixed-function GL**: ~226 immediate-mode calls, ~230 fixed-func state
  calls, GLU/GLUT deps, `GL_SELECT` picking, GLSL 1.10 shaders (no `#version`).

---

## 2. The core insight — THREE seams, not one

"Port to JUCE + use raylib + replace Racket" is really three *independent* swaps.
Keep them decoupled or the port drowns.

| Seam | Swap | Difficulty | Interface |
|---|---|---|---|
| **Host** (top) | Racket/GLUT window+loop+input → JUCE | Easy | `FluxusComponent` (JUCE) |
| **Script** (side) | Racket → none / s7 / Lua | Easy–Med | `IScriptHost` |
| **Backend** (bottom) | raw fixed-func GL → raylib | **Hard** | `IRenderBackend` |
| **Audio** (side) | JACK / fluxa → JUCE audio | Easy | `IAudioHost` |

The engine is edited in exactly ONE structural way: its raw GL calls are routed
through `IRenderBackend`. Everything else is host/script code *around* the engine.

---

## 3. Host seam — what the engine expects (the whole contract)

A new host must supply only these five things:

1. **GL context** — double-buffered RGBA + **depth + stencil**. JUCE `OpenGLContext`.
2. **Per-frame tick** — call `Engine::Get()->Render()` + `Physics()->Tick()`
   (direct), or eval `(fluxus-frame-callback)` (if scripted). Engine computes its
   own time/delta from `gettimeofday` and self-throttles — **host supplies no clock**.
3. **Resize** → `Renderer::SetResolution(w,h)`.
4. **Input** — keyboard/mouse with modifier bitmask (shift=1, ctrl=2, alt=4).
   Direct host calls camera/pick APIs; scripted host builds `(fluxus-input-callback …)`.
5. **Script eval channel** — only if scripting kept.

Discardable host cruft (all in `src/`): GLUT, `GLEditor`/`Repl`/`GLFileDialog`/
`PolyGlyph` (the GL live-coding editor), `Recorder`, fullscreen toggles,
`fluxus-scratchpad-*` prefs. Replace the editor with a JUCE `CodeEditorComponent`
whose only job is to push strings into the script host and show captured output.

---

## 4. Adapter design (clean-code)

### 4a. Bottom seam — `IRenderBackend` (GL ⇄ raylib without touching primitives)

```cpp
struct IRenderBackend {
  virtual ~IRenderBackend() = default;

  // matrix stack — State::Apply, SceneGraph walk, Camera call these
  virtual void pushMatrix() = 0;
  virtual void popMatrix()  = 0;
  virtual void multMatrix(const dMatrix&) = 0;
  virtual void setProjection(const dMatrix&) = 0;

  // frame + fixed-function state
  virtual void clear(const dColour&, bool depth, bool stencil) = 0;
  virtual void viewport(int x, int y, int w, int h) = 0;
  virtual void setMaterial(const Material&) = 0;      // was glMaterialfv
  virtual void setLight(int i, const LightData&) = 0; // was glLightfv
  virtual void setBlend(BlendMode) = 0;
  virtual void setCull(CullMode) = 0;
  virtual void setLineWidth(float) = 0;

  // hot path — PData is already contiguous dVector/dColour arrays
  virtual void drawArrays(PrimType, const PDataView&, const uint32_t* idx, size_t n) = 0;

  // immediate-mode decorations (normals, wireframe, axes, particle billboards)
  virtual void begin(PrimType) = 0;
  virtual void vertex(const dVector&) = 0;
  virtual void normal(const dVector&) = 0;
  virtual void colour(const dColour&) = 0;
  virtual void texCoord(float s, float t) = 0;
  virtual void end() = 0;

  // texture + shader
  virtual TexHandle    uploadTexture(const ImageData&) = 0;
  virtual void         bindTexture(int unit, TexHandle) = 0;
  virtual ShaderHandle buildShader(const std::string& vert, const std::string& frag) = 0;
  virtual void         useShader(ShaderHandle) = 0;
};

struct GLBackend     : IRenderBackend { /* wraps EXISTING raw GL — mechanical 1:1 extract */ };
struct RaylibBackend : IRenderBackend { /* rlPushMatrix / rlBegin+rlVertex3f / DrawMesh */ };
```

Engine edit: replace inline `gl*` in `Renderer.cpp`, `SceneGraph.cpp`, `State.cpp`,
`Camera.cpp`, `Light.cpp`, and each `Primitive::Render()` with `backend->…` calls.
A process-wide `IRenderBackend* g_backend` (or inject via `Engine`) selects impl.

### 4b. Top seam — JUCE host

```cpp
class FluxusComponent : public juce::Component, private juce::OpenGLRenderer {
  juce::OpenGLContext ctx;                    // depth+stencil, continuous repaint
public:
  FluxusComponent() { ctx.setRenderer(this); ctx.setContinuousRepainting(true);
                      ctx.attachTo(*this); }
  void newOpenGLContextCreated() override {   // glewInit(); pick backend
    backend = std::make_unique<GLBackend>();  // Phase 2: swap for RaylibBackend
    Engine::Get()->SetBackend(backend.get());
  }
  void renderOpenGL() override {              // = GLUT DisplayCallback
    Engine::Get()->Render();                  // (or scriptHost->tick())
    Engine::Get()->Physics()->Tick();
  }
  void resized() override { Engine::Get()->Renderer()->SetResolution(getWidth(), getHeight()); }
  // key/mouse listeners -> camera calls (direct) or scriptHost input strings
};
```

### 4c. Side seam — `IScriptHost` (pluggable scripting; see §6)

```cpp
struct IScriptHost {
  virtual ~IScriptHost() = default;
  virtual void   init() = 0;
  virtual void   eval(const std::string& code) = 0;   // live-coding entry
  virtual void   frameTick() = 0;                      // per-frame user callback
  virtual std::string drainOutput() = 0;               // stdout/err for the console
};
// impls: NullScriptHost (Phase 0), S7ScriptHost, LuaScriptHost
```

### 4d. Side seam — `IAudioHost` (audio backend; **no JACK**)

Fluxus audio (`fluxa`, `modules/fluxus-audio`) is JACK-based — **dropped**. Audio
goes behind an adapter so it too is portable to another host later; **JUCE audio
(CoreAudio) is the first impl**.

```cpp
struct IAudioHost {
  virtual ~IAudioHost() = default;
  virtual void  start() = 0;
  virtual void  stop()  = 0;
  virtual int   fftBands(float* out, int n) = 0;   // scripts read audio -> drive visuals
  virtual float gain() const = 0;
};
// impls: NullAudioHost (Phase 0), JuceAudioHost (AudioDeviceManager + FFT)
```

fluxus's audio-reactive API (`gh`/`ga` harmonic getters) maps onto `fftBands()`;
the engine itself has no audio dependency, so this seam is purely host-side.

---

## 5. Backend feasibility (raylib) — the hard part

`rlgl` maps geometry + matrix stack near 1:1. Per-subsystem:

| Subsystem | Rating | Note |
|---|---|---|
| Matrix/transform stack | Easy | `rlPushMatrix`/`rlMultMatrixf`; only matrix *read-backs* (`glGetFloatv`) need CPU-side tracking |
| Poly / Particle / Ribbon `Render()` | Easy | `rlBegin`/`rlVertex3f` shim |
| Type/text meshes | Medium | display lists gone; per-glyph draw OK |
| Textures | Medium | bind/param maps; mipmap-gen / `glTexEnv` need rework |
| Fixed-func **lighting/materials** | **Hard** | no `glLight*`/`glMaterial*` in raylib — move to a lit übershader |
| **Shaders** (13 GLSL-1.10 pairs) | **Hard** | remap `gl_*` built-ins, add `#version`, `varying`→`in/out` |
| NURBS | Hard / skip-v1 | GLU tessellator gone → CPU tessellate |
| Pixel/FBO render-to-texture | Hard | reimplement on raylib `RenderTexture2D` |
| `GL_SELECT` picking | Skip-v1 | removed from modern GL — needs color-pick or CPU raycast |
| Stencil shadows, accum motion-blur, fog, stipple | Skip-v1 | fixed-function-only effects |

**Biggest risks:**
1. **Lighting + legacy-shader stack** — cross-cutting; gates visual fidelity.
2. **raylib window ownership** — raylib's high-level API (`InitWindow`) owns its
   own GLFW window/context. Embedding *inside* JUCE's `OpenGLContext` means using
   the low-level **`rlgl`** layer against JUCE's context, not `InitWindow`. This is
   the main integration friction. (Alt: separate raylib window, JUCE = control surface.)
3. `GL_SELECT` picking removal — structural rewrite of `Renderer::Select`.

---

## 6. Racket replacement analysis

Fluxus embeds the **deprecated mzscheme BC C-API** (`scheme_basic_env`, `base.c`
bytecode) — hard to build today; modern Racket CS embeds heavier, not easier.
Not worth chasing an "easier Racket." Bindings only convert values then call
`Engine::Get()`, so **any** language can wrap the same C++ calls.

| Option | Build pain | Scheme feel | Verdict |
|---|---|---|---|
| **None (pure C++)** | zero | — | Phase-0 default. Drive `Engine`/`Renderer` directly. |
| **s7 Scheme** | tiny (1 `.c` + `.h`) | yes | Keeps fluxus identity + live-coding. Rebind, not drop-in. |
| **Lua + sol2** | tiny (header-only) | no | Cleanest C++ binding, JUCE-friendly. Syntax differs. |
| Chibi / Guile / Chicken | med–high | yes | Heavier than s7, no real win. |

Existing `.ss` fluxus scripts will NOT run unchanged under s7 (bindings rewritten
regardless), so language choice is essentially free. **Recommendation:** ship
Phase-0 with `NullScriptHost` (no scripting), add **s7** (Scheme live-coding, 1-file
build) or **Lua/sol2** (easiest) behind `IScriptHost` later.

### 6a. Spike results (PROVEN — `spikes/`)

Both candidates were spiked against an identical fake engine (`SpikeEngine`,
mirroring the `Engine::Get()` singleton + a command log). Each proved: embed,
bind C++ calls, eval a **live** code string, **hot re-eval** (redefine `frame`
at runtime → engine output changes), capture console output, catch a script
error without crashing the host.

| Criterion | s7 Scheme | Lua + sol2 |
|---|---|---|
| Source | radiganm/s7 (1× `s7.c`+`s7.h`) | walterschell/Lua (5.4.7) + ThePhD/sol2 |
| Binding boilerplate | manual `Scheme_Object` marshalling | **auto** (`lua.set_function(name, lambda)`) |
| Config time | ~30s (74k-line `s7.c` compile) | fast |
| Live re-eval | ✅ | ✅ |
| Console capture | ✅ (`s7_open_output_string`) | ✅ (override `print`) |
| Error isolation | ✅ (`(catch #t …)`) | ✅ (`SOL_ALL_SAFETIES_ON` + `safe_script`) |
| Scheme syntax (fluxus feel) | ✅ | ✗ |
| gotcha found | top-level `define` must NOT be wrapped in a catch-lambda (scope leak) | none |

**Verdict:** both viable. **Lua/sol2** = least binding code, cleanest C++
integration (auto-marshalling), fastest build — pick if Scheme syntax isn't
sacred. **s7** = keeps fluxus's Scheme identity + live-coding, one-file build,
more manual glue. Decision deferred to Phase 3; `IScriptHost` makes it swappable.

---

## 7. Chosen decisions (this port)

- **Scripting:** pluggable `IScriptHost`. Phase-0 = none (pure C++). Both s7 and Lua/sol2 spiked & proven (§6a); pick in Phase 3. *(open question resolved: replace Racket, don't chase a Racket port.)*
- **Backend:** GL under JUCE `OpenGLContext` via `IRenderBackend`/`GLBackend`. **raylib SKIPPED** — GLBackend already renders real libfluxus; the seam stays for a possible future swap but no RaylibBackend is planned.
- **Fidelity:** core primitives first (Poly/Particle/Ribbon + procedural meshes + basic material), full later.
- **Audio:** **no JACK.** JUCE audio (CoreAudio) behind `IAudioHost`, portable later.
- **Deliverable:** this doc before code.

---

## 8. Phased plan

- **Phase 0 — re-host as-is.** JUCE `OpenGLContext` + keep raw GL + no scripting.
  Drive `Engine::Get()->Render()` directly. Prove the host seam. **Zero engine edits.**
  Milestone: a spinning cube from `libfluxus` inside a JUCE window.
- **Phase 1 — insert `IRenderBackend`, extract `GLBackend`.** Pure refactor, no
  visual change. Route `Renderer`/`SceneGraph`/`State`/`Camera`/`Light` + core
  `Primitive::Render()` through the interface. Enabling move for raylib.
- **Phase 2 — ~~`RaylibBackend`~~ SKIPPED.** JUCE `OpenGLContext` + `GLBackend`
  already render real libfluxus. The `IRenderBackend` seam stays for a possible
  future backend swap, but no raylib impl is planned.
- **Phase 3 — scripting.** Add `S7ScriptHost` or `LuaScriptHost` + JUCE code editor
  + console. Restore live-coding.
- **Phase 4 — fidelity / breadth.** More primitives (sphere/torus/ribbon/particle),
  mouse-orbit camera, wire the fluxus command API; later a lit übershader.

---

## 9. Build notes

- Reuse the existing project's CMake + JUCE FetchContent pattern.
- `libfluxus` has non-GL deps to satisfy: FreeType (TypePrimitive), libpng
  (PNGLoader), ODE (Physics), GLEW. Gate optional subsystems off for Phase 0.
- Original build is SConstruct (`SConstruct`, 17KB) — not reused; author a fresh
  CMake target that compiles `libfluxus/src/*` minus the skip-v1 files.
- **No JACK** — do not link `fluxa`/`fluxus-audio`. Audio via JUCE `AudioDeviceManager`.
- Scripting spikes live in `spikes/` (own CMake, isolated from the JUCE build):
  `cmake -S spikes -B spikes/build -G Ninja && cmake --build spikes/build`, then
  `./spikes/build/spike_s7` and `./spikes/build/spike_lua`.

---

## Appendix A — GL-porting playbook (raw GL → `IRenderBackend`)

Mechanical recipe for Phase 1 (route engine's raw GL through the backend seam)
and Phase 2 (raylib impl). Goal: automate the boring 1:1 renames, spend the cheap
model on light-judgment reshapes, reserve the capable model for semantics.

### A.1 Buckets (classify every distinct `gl*`/`glu*` token first)

Inventory step — one list, ~50 symbols, is the port checklist:
```sh
grep -rhoE 'gl[uA-Z][A-Za-z0-9_]+' libfluxus/src --include='*.cpp' --include='*.h' \
  | sort | uniq -c | sort -rn > gl-tokens.txt
```
(Use GNU grep, or on macOS `/usr/bin/grep -E 'gl[uA-Z][A-Za-z0-9_]+'` without `\b`.)

| Bucket | What | Tooling | Who |
|---|---|---|---|
| **1 — 1:1 rename** | matrix stack, immediate verbs | ast-grep rule, deterministic | no LLM ($0) |
| **2 — rename + arg reshape** | pointer→value, 2-call collapse | ast-grep w/ captures + fixups | cheap agent (Haiku), 1 file each |
| **3 — semantic, no 1:1** | lighting/material/texgen/select/NURBS/FBO | manual | capable model |

### A.2 Map table (buckets 1 & 2)

```
# bucket 1 — token swap
glPushMatrix()            -> backend->pushMatrix()
glPopMatrix()             -> backend->popMatrix()
glMultMatrixf($M)         -> backend->multMatrix($M)      # $M already dMatrix::arr(); pass the dMatrix
glLoadIdentity()          -> backend->loadIdentity()
glNormal3f($X,$Y,$Z)      -> backend->normal({$X,$Y,$Z})
glColor4f($R,$G,$B,$A)    -> backend->colour({$R,$G,$B,$A})
glVertex3f($X,$Y,$Z)      -> backend->vertex({$X,$Y,$Z})
glBegin($MODE)            -> backend->begin($MODE)         # map GL_TRIANGLES->PrimType::Triangles etc
glEnd()                   -> backend->end()
glViewport($X,$Y,$W,$H)   -> backend->viewport($X,$Y,$W,$H)
glLineWidth($W)           -> backend->setLineWidth($W)

# bucket 2 — reshape args (deref / wrap / collapse)
glVertex3fv($P)           -> backend->vertex(dVector($P[0],$P[1],$P[2]))   # or *reinterpret dVector
glNormal3fv($P)           -> backend->normal(dVector($P[0],$P[1],$P[2]))
glColor4fv($P)            -> backend->colour(dColour($P[0],$P[1],$P[2],$P[3]))
glVertexPointer(..) + glDrawArrays($MODE,0,$N)  -> backend->drawArrays($MODE, view, nullptr, $N)

# bucket 3 — DO NOT auto-touch (leave, list for manual pass)
glEnable/glDisable(GL_LIGHTING|GL_LIGHT*|GL_TEXTURE_GEN*|GL_FOG|GL_COLOR_MATERIAL)
glLightfv / glLightf / glMaterialfv / glMaterialf
glTexGeni / glTexGenfv
glSelectBuffer / glRenderMode(GL_SELECT) / glInitNames / glPushName / glLoadName
gluNurbsSurface / gluBeginSurface / gluNewNurbsRenderer / gluBuild2DMipmaps
glGenFramebuffersEXT / glBindFramebufferEXT / glReadPixels / glAccum
glGetFloatv(GL_MODELVIEW_MATRIX|GL_PROJECTION_MATRIX)   # replace w/ CPU-tracked dMatrix
```

### A.3 ast-grep rules (bucket 1 — deterministic, run these first)

`gl-port.yml` (one rule per mapping; run `sg scan -U -r gl-port.yml libfluxus/src`):
```yaml
id: glPushMatrix
language: cpp
rule: { pattern: "glPushMatrix()" }
fix: "backend->pushMatrix()"
---
id: glVertex3f
language: cpp
rule: { pattern: "glVertex3f($X, $Y, $Z)" }
fix: "backend->vertex({$X, $Y, $Z})"
---
id: glNormal3fv
language: cpp
rule: { pattern: "glNormal3fv($P)" }
fix: "backend->normal(dVector($P[0], $P[1], $P[2]))"
```
Why ast-grep not sed: it parses C++ structure — never edits comments, strings,
`#if 0` blocks, or partial-token collisions (`glColor4fv` vs `glColorMaterial`).

### A.4 Cheap-agent dispatch (bucket 2 — Haiku, one file each, parallel)

Via the Agent tool with `model: "haiku"`, OR a Workflow `pipeline(files, stage)`
with `model: 'haiku', effort: 'low'`. Prompt template:
```
You are porting ONE file: <FILE>. Apply this mechanical GL→backend rename.
MAP TABLE: <paste A.2 buckets 1 & 2>.
RULES:
 - Rename per the map table only. Do not invent mappings.
 - backend is available as `backend->` (an IRenderBackend*). Signatures: <paste A.1 of §4a>.
 - DO NOT touch any bucket-3 line (glEnable(GL_LIGHTING), glLight*, glMaterial*,
   glTexGen*, glSelect*, gluNurbs*, FBO/EXT, glGetFloatv). Leave them verbatim.
 - Do not reformat untouched code. Do not edit comments or strings.
OUTPUT: apply edits, then list every GL line you LEFT untouched (file:line + text).
That list is the manual-review worklist.
```
The "untouched GL lines" each agent returns **auto-accumulates the bucket-3 worklist**.

### A.5 Guardrails / workflow

1. Inventory + classify tokens (A.1) → freeze the map table.
2. Bucket 1: `sg scan -U` (deterministic). Commit alone.
3. Bucket 2: Haiku agents, one file each. Commit per batch.
4. Bucket 3: capable model, per-subsystem. Commit per subsystem.
5. **Compile + `git diff` review after every batch.** Separate commits =
   one bad rule is one `git revert`.
6. Backend swap (GL↔raylib) is done ONCE in the impl — primitives are never
   re-sed'd per backend. That is the payoff of the seam.
