# Android subset — what works, what is deferred

Working document for the Android port. The target is **Milestone A**: the engine
drawing basic geometry — plain shapes, transforms, colours, solid/wire hints — on
a device, driven by a **C++ harness with no script host at all** — nothing can
draw until the renderer exists, so binding a language first would be paying for
it before there is anything to say in it.

**After Milestone A the script host is Racket.** s7 is set aside. That is also
the lower-risk choice on evidence: Racket CS is already proven to run on Android
(`spikes/racket-android`), while s7 has never been built for it.

Everything outside the target is listed here with the reason, so a failure on
Android is a *known* one rather than a mystery.

Sources for every claim: `spikes/gles-audit/` (compile audit, re-runnable with
`sh audit.sh`) and `spikes/gles-cube/` (proven rendering on device). Error counts
below are from the audit and change as work lands — re-run it rather than
trusting the numbers here.

## Ready or nearly ready

| Area | State |
|---|---|
| Maths, pdata, containers | `dada`, `Noise`, `SimplexNoise`, `PData*`, `Allocator`, `Geometry`, `Tree` compile clean — they touch no GL at all |
| `build-cube` / poly solid | **Proven on device** (`spikes/gles-cube`): solid, wireframe and hidden-line, no GL errors |
| `build-ribbon` | Draws through `IRenderBackend` as of the seam work; 19 audit errors left, all state enums, no draw calls |
| `build-particles` | Same; 4 errors left (`GL_LIGHTING`, `GL_POINT_SMOOTH`) |
| Transforms, `with-state`, colour, material, blend, cull | **`State.cpp` compiles clean.** `State::Apply` was already seam-based for colour/material; the last two raw enums became `setNormaliseNormals` / `setProgramPointSize` |
| Scene traversal | **`SceneGraph.cpp` down to 2 errors** — matrix queries go through `getModelView`/`getProjection`, picking through `pushPickName`/`popPickName` (no-ops on GLES), the axes draw through `RPrim::Lines` |
| The `flux_*` command layer | **7 of 8 TUs compile clean for Android**, including `RacketScriptHost.cpp`. Only `FluxusCommandsGpu.cpp` fails (17), and only on EXT spellings that have core GLES 3 equivalents |

## Needed for Milestone A, not done

Ordered by how much they block a basic sketch. Counts are from the audit and
move as work lands — total was 529 at the first measurement, **425** now.

1. **`Renderer.cpp`** (59) — the last big one, and now the only thing between a
   script and a picture on Android. Its GL calls are NOT evenly spread: the
   render path proper is tiny (`Render` 4, `PostRender` 4, `RenderLights` 1),
   while 42 sit in `PreRender` (global frame setup) and the rest in features
   already deferred below — `RenderStencilShadows` 24, `DrawText` 10,
   `Select`/`SelectAll` 8. So most of this file does not need porting, it needs
   *excluding*. `ApplyState` has zero GL calls.
2. **`PolyPrimitive` wire and points passes** (51). The solid pass already goes
   through the seam; wire/points still call raw GL with client arrays.
3. **`Light.cpp`** (27) — fixed-function lights become shader uniforms.
4. **`Camera.cpp`** (6) — still sets projection through `glFrustum`/`glOrtho`.
5. **Wireframe on triangulated meshes.** Per-face edges are correct for QUADS;
   on a TRILIST every triangulation diagonal would show. Needs edge dedup or a
   barycentric-coordinate shader. This is a **design decision**, not a port step,
   because it changes how the look is produced.

## Deferred — not supported on Android, with the reason

Not "hard", but genuinely absent from GLES or requiring a rewrite. A sketch using
these will not render correctly on Android.

| Feature | Why |
|---|---|
| `build-nurbs-sphere` / `build-nurbs-plane` | GLU does not exist on Android. `NURBSPrimitive` is 54 audit errors, all GLU tessellator and `GL_MAP2` evaluators. Drop or replace. |
| Texturing — `texture`, `load-texture`, `hint-sphere-map` | `TexturePainter` (20) uses the fixed-function texture environment, `glTexGen` sphere mapping and `gluBuild2DMipmaps`. GLES has none of it; a fragment shader decides the combine. |
| `hint-wire-stippled` | No `glLineStipple`. Needs dashing in a shader. |
| Fog | Fixed-function fog is gone; shader work. |
| Spot lights, attenuation, multiple lights | `Light.cpp` — becomes shader uniforms, not GL state. |
| `select` / picking | No selection buffer (`glSelectBuffer`/`glRenderMode(GL_SELECT)`). Rewrite as colour-pick or CPU ray — `Geometry.cpp` already has ray/triangle intersection AND compiles clean. |
| Accumulation-buffer effects | `glAccum` does not exist. |
| Stereo rendering | `GL_STEREO` does not exist. |
| Shadow volumes | `ShadowVolumeGen` (23) is immediate mode plus stencil work. |
| `build-blobby`, `build-voxels`, `build-type`/text prims | Still immediate mode; portable in principle, just not done. |
| `build-pixels` | `PixelPrimitive` uses `glPolygonMode` and readback. |
| The GLEditor apps | `PolyGlyph` uses display lists (`glCallList`); FreeType text meshes. The JUCE-editor app is the Android path. |
| NTSC filter, video/camera texture, hand tracking | App-level, already switchable — build with `FLUXUS_ENABLE_{NTSC,VIDEO,HAND}=OFF`. |
| s7 script host | Dropped as an Android target. Racket is the language. |
| Scripting during Milestone A | Deferred, not cancelled: Racket comes after the renderer. It is proven to run on Android already (`spikes/racket-android`: native `tarm64le`, FFI resolves, ~77 ms boot), so the remaining work there is trimming its ~143 MB runtime and compiling `app/FluxusCommands*.cpp` for Android, which has never been attempted. |

## Not a casualty, contrary to expectation

**Geometry shaders work.** `GL_GEOMETRY_SHADER` is core in GLES 3.2 — confirmed in
the NDK's own `GLES3/gl32.h`. `GLSLShader.cpp` needs 3 enums changed (the `*_EXT`
spelling; GLES configures the stage with in-shader layout qualifiers instead of
`glProgramParameteriEXT`). This is the opposite of the Raspberry Pi, whose v3d
has no geometry stage at all.

## Ground rules for the work

- Every change to shared engine code is verified with `sh test/golden.sh` —
  macOS output must stay **bit-identical** (`maxdelta 0`).
- When a golden case does not yet cover the path being changed, add the case and
  record its baseline from the code **before** the change. Recording afterwards
  just enshrines the new behaviour as truth.
- `sh spikes/gles-audit/audit.sh` is the progress meter. It reads its file list
  from `cmake/libfluxus_min.cmake`, so it cannot drift from the real build.
