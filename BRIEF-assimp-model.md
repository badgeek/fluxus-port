# Task: load 3D models (fbx / gltf / glb / dae / ply / stl / 3ds) via assimp

> **STATUS: implemented — tiers 0-3 all landed.** `app/FluxusCommandsModel.cpp`,
> `racket-lib/model.ss`, `spikes/model-load/main.cpp` (`model_test`),
> `examples/model-load.scm`, `examples/model-anim.scm`. Geometry, materials +
> external/embedded textures, and skeletal animation all verified; skinning matches
> assimp's own reference formula to 0.0000 on fox/astroBoy/druid. Release bundling
> of the assimp dylib is the one deliberate omission (see §0). Kept as the record of
> why it is built this way.

Repo: `/Users/manticore/work/bauhouse/juce-test/fluxus-port` (a git repo; commit when done).
Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` then
`cmake --build build --target FluxusApp FluxusGLApp FluxusRacketApp FluxusGLRacketApp`.

## Goal
Scripts can load a real 3D model and get fluxus primitives back — geometry first,
then materials/textures, then skeletal animation. Reference for the *logic* is
openFrameworks' `ofxAssimpModelLoader`, but **do not port the class** (see below).

## Read this first — the analysis is already done

A feasibility pass established the following. Do not redo it; build on it.

**Don't port the oF addon.** `addons/ofxAssimpModelLoader/src/ofxAssimpModelLoader.cpp`
(1287 lines) is welded to `ofVbo` / `ofTexture` / `ofImage` / `ofMaterial` / `ofLight` /
`ofFilePath` / glm. Porting it drags in oF's graphics layer, which duplicates
libfluxus. Port only the assimp-side logic:
- `initImportProperties` flag set (`ofxAssimpModelLoader.cpp:124`) — copy verbatim,
  **minus `aiProcess_ConvertToLeftHanded`** (fluxus is right-handed GL).
- `aiMeshToOfMesh` (`ofxAssimpUtils.h:39`, ~40 lines) — retarget to pdata.
- `updateMeshes` node recursion (`:586`) and `updateBones` (`:607`) — matrix maths only.
- `ofxAssimpAnimation.cpp` (238 lines) — keyframe interpolation, portable as-is.
Clone with `git clone --depth 1 https://github.com/openframeworks/openFrameworks.git`
if the tree is gone; test assets live under `examples/3d/*/bin/data/`.

**libfluxus already provides most of the pipeline:**

| need | where |
|---|---|
| file→primitive dispatch + geometry cache + `Clone()` | `vendor/fluxus/libfluxus/src/PrimitiveIO.cpp:40,77` (wired as `(load-primitive)`, `app/FluxusCommandsCore.cpp:603`) |
| indexed mesh idiom to copy | `OBJPrimitiveIO.cpp:99` — `Resize()` / `SetDataRaw("p"/"n"/"t")` / `GetIndex()=` / `SetIndexMode(true)` |
| indexed rendering (incl. VBO path) | `PolyPrimitive.cpp:276-287,303`, `GLBackend.cpp:55-80` |
| index list from script | `flux_poly_set_index` (`app/FluxusCommands.h:395`) |
| skinning | `SkinningPrimFunc.cpp:32`, exposed via `flux_pfunc_make("skinning")` (`app/FluxusCommandsPdata.cpp:301`) |
| skeleton nodes | `flux_build_locator` / `flux_parent` (`FluxusCommands.h:48,84`); `SceneGraph::GetNodes` is **pre-order DFS** (`SceneGraph.cpp:281`) |
| materials | `flux_colour` / `flux_specular` / `flux_ambient` / `flux_emissive` / `flux_shinyness` / `flux_opacity` |
| textures | `flux_load_texture` (`app/TextureLoader.cpp:55`) → GL id; `flux_texture` → `State.Textures[0]` (`FluxusCommandsCore.cpp:406`); 8 units via `flux_multitexture` |
| bulk pdata | `flux_pdata_write_all` / `flux_pdata_add` / `flux_pdata_copy` (`FluxusCommands.h:127-130`) |
| working script-level skinning example | `vendor/fluxus/examples/skinning.scm` |

**Measured facts (spikes, already run):**
- assimp 6.0.5 via `brew install assimp` imports securityCamera.fbx / druid.gltf /
  Fox_05.fbx / astroBoy_walk.dae / lofi-bunny.ply cleanly; import 5-6 ms, prim build
  0.1 ms.
- Indexed prims are **4.1x fewer verts** than an unindexed TRILIST on the same models.
- Bone→channel mapping is exact: on Fox (33 bones / 35 skeleton nodes) and astroBoy
  (35 bones / 65 nodes) **every bone matched a node** and weights sum to 1.000 on
  every vertex.
- `SkinningPrimFunc` **works in this port** — headless test, 648-vert cylinder,
  hand-authored `w0`/`w1`, 60° bend → 564/648 verts moved. But
  `genskinweights` produces a rigid, flat-coloured mesh in the app; treat weight
  *generation* as suspect, and always take weights from the file instead.
- Skinning inner loop, dense (what `SkinningPrimFunc` does) vs sparse (what oF does):
  0.151 vs 0.014 ms/frame at 65 nodes × 2073 verts; **4.9 vs 0.57 ms at 50k verts**.

## Deliverables

### 0. Build wiring (assimp)
- Find assimp with `find_package(assimp CONFIG)` (brew ships
  `/opt/homebrew/opt/assimp/lib/cmake/assimp-6.0`). If not found, **skip the model
  targets gracefully** the way Racket targets are skipped — the other apps must still
  build on a machine without assimp.
- Dev builds link the brew dylib. Bundling (`-DFLUXUS_BUNDLE_RACKET=ON` path,
  `cmake/bundle_racket.cmake`) is OUT OF SCOPE for this task — but leave a comment
  noting brew ships **dylib only**, so a release `.app` will later need a vendored
  static assimp (`BUILD_SHARED_LIBS=OFF`, importers trimmed) or a dylib copy + rpath
  fix + `codesign --force --deep --sign -` re-sign.
- New TU goes in **`fluxus_render`**, NOT `fluxus_core` — `fluxus_core` is what
  `pdata_bench` links (`CMakeLists.txt:118,427`) and must not grow an assimp
  dependency. Both libs already carry the `-Wl,-force_load` INTERFACE that the
  Racket `get-ffi-obj` (dlsym) binding requires (`CMakeLists.txt:135,151`).

### 1. Tier 1 — static geometry (do this first, ship it)
New `app/FluxusCommandsModel.cpp` (+ declarations in `app/FluxusCommands.h`).
JUCE-free, like every other command TU (CLAUDE.md gotcha 2). C API:

```c
int    flux_load_model(const char* path, int flags);  // handle (>=0), caches the aiScene
int    flux_model_root(int h);                        // locator; every mesh prim parents to it
int    flux_model_mesh_count(int h);
int    flux_model_prim(int h, int i);                 // prim id for mesh i
const char* flux_model_mesh_name(int h, int i);
void   flux_model_free(int h);
```
- One **indexed** `PolyPrimitive(TRILIST)` per `aiMesh`, built exactly the
  `OBJPrimitiveIO::MakePrimitive` way. Fill `p`, `n`, `t` (UV channel 0), and `c` when
  the mesh has vertex colours.
- Walk the node tree and bake each node's world transform into the verts.
  **Composition order:** assimp does `world = parentWorld * local` in its
  column-vector convention; transposed into fluxus's row-vector `dMatrix` that is the
  standard product `local·parentWorld`, and since the engine's `operator*` is REVERSED
  the code must read `world = parentXf * aiToD(node)` — parent first. Verified against
  assimp's own matrix product across astroBoy's rig; the other order is exact only for
  a flat hierarchy (a two-node fbx looks right either way) and is off by up to 42 units
  on a real skeleton.
- Parent all mesh prims to one locator so a script can move the whole model.
- Cache the imported scene by path (mirror `PrimitiveIO`'s geometry cache) — immediate
  mode re-runs the whole buffer every frame and would otherwise re-import per frame.
- Bind in BOTH hosts: `app/S7ScriptHost.cpp` (`f_*` + `def(...)`) and
  `racket-lib/fluxus-engine.ss` (`get-ffi-obj` + failure thunk).
- Scheme sugar in a new `racket-lib/model.ss`: `(load-model path)` → list of prim ids,
  `(model-root m)`, `(with-model m proc)`. Add it to `RacketScriptHost::requireLibForm()`
  **and** to the Makefile `LIBSS` list, then `make precompile` (stale `.zo` shadows
  source — CLAUDE.md).

**Optional companion (small, high value):** an `AssimpPrimitiveIO` registered in
`PrimitiveIO::GetFromExtension` (`PrimitiveIO.cpp:77`) for the same extensions, so
plain `(load-primitive "x.fbx")` works for single-mesh files with zero new API.

### 2. Tier 2 — materials + textures
- Per mesh: diffuse colour → `flux_colour`; specular / ambient / emissive / shininess →
  the existing setters; two-sided → `flux_hint_cull_ccw` / backface state.
- Diffuse texture path from `aiMaterial::GetTexture(aiTextureType_DIFFUSE, …)`,
  resolved **relative to the model file's directory**, then `flux_load_texture` →
  `flux_texture`. Handle Blender's leading `//` (see `ofxAssimpModelLoader.cpp:378`).
- **Embedded textures** (`texPath` starts with `*`, e.g. druid.gltf and Fox_05.fbx both
  have one): add ONE new entry point next to `flux_load_texture` in
  `app/TextureLoader.cpp` —
  `unsigned flux_load_texture_mem(const void* bytes, int len, const char* cacheKey)`,
  using `juce::ImageFileFormat::loadFrom(const void*, size_t)` + the existing
  `uploadImage()`. ~15 lines. Uncompressed embedded textures (`mHeight != 0`) can be
  skipped in this tier — log and move on.
- Expect a **UV-origin calibration pass**: `flux_load_texture` uploads with
  `flipY=true`, glTF/FBX conventions differ. If the texture is upside down, flip `t`
  in the importer, not in the loader (the loader is shared with `(load-texture)`).

### 3. Tier 3 — skeletal animation (only after 1+2 are solid)
Two sub-steps; do the sampler before the skinning.

**3a. Animation sampler.** Port `ofxAssimpAnimation`'s key interpolation (position /
rotation slerp / scale, with the `mTicksPerSecond` fallback) and expose:
```c
int    flux_model_anim_count(int h);
double flux_model_anim_duration(int h, int a);   // seconds
void   flux_model_set_anim_time(int h, int a, double t);
```
Test it standing alone by animating an unskinned node hierarchy (astroBoy's rig moves
even with skinning off).

**3b. Skinning via the existing pfunc.** Zero engine changes:
- Build TWO locator trees per skinned mesh — the live skeleton and an identical
  bindpose copy — from the aiNode hierarchy, in the same order you traverse it.
- **`w<n>` index is the pre-order DFS position, not the bone index**
  (`SceneGraph::GetNodes`, `SkinningPrimFunc.cpp:100`). One float channel per skeleton
  NODE, zero-filled for nodes with no influence (65 channels for 35 bones on astroBoy).
- Fill them from `aiBone::mWeights` (`mVertexId` → weight), then
  `flux_pdata_copy("p","pref")` and `("n","nref")`.
- Per frame: `flux_model_set_anim_time` writes bone locator transforms, then
  `pfunc-run 'skinning'` with `skeleton-root` / `bindpose-root` / `skin-normals`.
- **Do NOT use `genskinweights`** — see the analysis above.
- Memory note: 65 channels × 2073 verts ≈ 540 KB per mesh. Acceptable; log it.
- If a heavy model is slow, the fix is a sparse C-side skinner (per-bone vertex lists,
  ~9x faster measured), not micro-tuning the pfunc. **Measure before optimizing.**

## Performance notes (do not "optimize" blindly)
- **VBO is already ON and already covers these prims** (`FLUXUS_ENABLE_VBO`,
  `PolyPrimitive.cpp:195`, indexed draws included). Nothing to do. It is NOT the lever —
  the bottleneck on Apple's Metal-emulated GL is per-draw STATE DISPATCH (CLAUDE.md).
- The real levers, in order: indexed geometry (4.1x fewer verts), **fewer draws**
  (`build-merged` per texture/material group — one State per merged prim, so never
  merge across different textures), and retained mode + the scene cache.
- **Skinned prims fight the VBO cache**: skinning bumps the pdata version every frame,
  so `UpdateVBO` re-uploads 4 full buffers per frame with a `GL_STATIC_DRAW` hint
  (`PolyPrimitive.cpp:86-98`) — ~340 KB/frame for a 6.5k-vert model. Leave it alone in
  Tier 1/2; when Tier 3 lands, MEASURE, then either bypass the VBO for skinned prims or
  switch them to `GL_DYNAMIC_DRAW` + `glBufferSubData`.

## Gotchas that will bite (all confirmed)
- `addPrim` STOMPS state set on a freshly-`new`ed primitive — set engine-owned prim
  state AFTER `addPrim` (CLAUDE.md GPU section).
- `hint-solid` defaults ON; `with-state` does not restore hints (CLAUDE.md gotchas 6/7).
- Any out-of-tree spike linking `libfluxus_min.a` MUST be compiled with
  `-DFLUXUS_ENABLE_VBO` or `PolyPrimitive`'s layout mismatches the archive and the heap
  corrupts (`malloc: Corruption of free object … msizes disagree`).
- The script engines are GL-thread-bound; the importer runs from a script call so it is
  already on the right thread — but do not move it to a background thread without
  routing the result through the mutex-protected command state.

## Verify
- All four apps build clean (0 errors; keep vendored warnings suppressed via SYSTEM
  includes).
- `pdata_bench` still builds and passes — proof `fluxus_core` did not grow the dep.
- Racket CLI still loads the library:
  `RK=/opt/homebrew/Cellar/minimal-racket/9.3/bin/racket; $RK -e '(require (file "racket-lib/model.ss"))' -e '(displayln (quote ok))'`
- A headless correctness spike in `spikes/` (model → prim counts, vert/index counts,
  bbox, and for Tier 3 the weight-sum check) — mirror `spikes/pdata-bench/main.cpp`'s
  stub-the-JUCE-hooks pattern so it needs no GL.
- Visual check with `cli/fluxus load` + `shot` on an example sketch
  (`examples/model-load.scm`). Two traps when doing this: **the window must be
  frontmost** or macOS throttles the render loop and `(time)` nearly freezes (motion
  looks broken when it isn't), and never use `cli/fluxus eval` for the screenshot — it
  REPLACES the buffer (CLAUDE.md gotcha 5).

## Commit
`git add -A && git commit`, message ending:
`Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`

Report back: files added, commands bound, which tiers landed, measured numbers
(import ms, prim/vert counts, frame cost if Tier 3), and anything skipped + why.
