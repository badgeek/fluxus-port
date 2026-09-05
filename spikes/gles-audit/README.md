# Android GLES audit — stage 1

Compiles every `libfluxus_min` translation unit against Android's **GLES 3.2**
headers and tallies what breaks, per file. Nothing links or runs: the question
is "which symbols does this engine want that GLES does not have", and that is a
compile-time answer.

```sh
sh audit.sh                     # table + summary
sh audit.sh PolyPrimitive.cpp   # full errors for one TU
```

`shim/OpenGL.h` redirects the vendored sources onto GLES without editing any of
them — force-included with `-include` (a quoted `#include "OpenGL.h"` resolves
next to the including file, so an `-I` shim can never win) and claiming the same
`__OPENGL_H__` guard so the real header becomes a no-op.

Re-run it after any seam work — the error count is an objective progress meter.

**Progress: 529 errors / 14 clean TUs (first run) → 492 / 26 clean.** Two header
fixes did that, both behaviour-identical on macOS (`test/golden.sh`: all cases
`maxdelta 0`): `GL_MODULATE` no longer spelled with a desktop-GL enum in
`TexturePainter.h`, and `NURBSPrimitive.h` demoted to a forward declaration in
`ShadowVolumeGen.h` so GLU's `GLUnurbsObj` stops leaking into every TU that
merely includes `SceneGraph.h`. The numbers below are the original measurement.

## Result (2026-09-05, NDK 27.1.12297006, GLES 3.2 headers)

**48 TUs, 529 errors** — but the raw count overstates the work. Sorted by what
each file actually needs:

| Group | TUs | What it means |
|---|---|---|
| Compiles clean | 14 | `dada`, `Noise`, `SimplexNoise`, `Allocator`, `PData*`, `Geometry`, `Tree`, `SearchPaths`, `Trace`, `ShaderCache`, `RenderBackend` — the whole math/container/scene-tree half of the engine is already portable |
| Only `GL_MODULATE` missing | 13 | One constant, from a default in `TexturePainter.h:39`. Effectively clean — a one-line fix, not 13 problems |
| Real fixed-function bodies | ~20 | The actual job |

So **27 of 48 TUs are already portable or nearly so**, and the work concentrates
in about twenty files.

### Where the work is

| File | Errors | Nature |
|---|---|---|
| `Renderer.cpp` | 61 | fog, lighting, matrix stack, accum buffer, **selection buffer** |
| `NURBSPrimitive.cpp` | 54 | GLU tessellator + `GL_MAP2` evaluators — no GLU on Android, delete |
| `PolyPrimitive.cpp` | 53 | client arrays, `glPolygonMode`, texgen, stipple |
| `RibbonPrimitive.cpp` | 48 | immediate mode + texgen + stipple |
| `Primitive.cpp` | 45 | immediate mode, colour material |
| `ParticlePrimitive.cpp` | 37 | client arrays + immediate mode |
| `BlobbyPrimitive.cpp` | 33 | immediate mode, `glPolygonMode`, texgen |
| `Light.cpp` | 28 | fixed-function lighting → shader uniforms |
| `GLBackend.cpp` | 24 | the backend being replaced anyway |
| `ShadowVolumeGen.cpp` | 23 | immediate mode |
| `SceneGraph.cpp`, `TexturePainter.cpp` | 20 each | picking names / texenv + mipmap generation |
| `VoxelPrimitive.cpp` | 15 | immediate mode |
| `Camera.cpp` 8, `DepthSorter.cpp` 7, `State.cpp` 6, `ImmediateMode.cpp` 4, `GLSLShader.cpp` 3, `DDSLoader.cpp` 2, `DebugGL.cpp` 1 | | small, mostly matrix stack / one enum |

### Three findings that change the plan

1. **`glPolygonMode` really is absent** — confirmed by grepping the NDK's own
   `GLES3/gl32.h` (0 hits). Wireframe and hidden-line, the look most of our
   sketches depend on, must be rebuilt as index-drawn lines or a barycentric
   shader. This is a design decision, not a port detail.

2. **Geometry shaders SURVIVE.** `GL_GEOMETRY_SHADER` *is* in `gl32.h` (GLES 3.2
   has them in core). So `shader-source-geom`, `grass-gpu.scm` and
   `noise-grid-3d.scm` could live on Android — the opposite of the Raspberry Pi,
   where v3d has no geometry stage at all. `GLSLShader.cpp` needs only 3 enums
   changed (the `*_EXT` spelling; GLES sets them via in-shader layout qualifiers
   rather than `glProgramParameteriEXT`).

3. **Picking dies.** `glSelectBuffer` / `glInitNames` / `glPushName` /
   `glRenderMode(GL_SELECT)` do not exist in GLES, so fluxus `(select)` needs a
   colour-pick or CPU-ray rewrite. `Geometry.cpp` (ray/triangle intersection)
   already compiles clean, so the CPU path is available.

Smaller casualties: line stipple (dashed lines), sphere-map texgen, fog, and the
accumulation buffer — all fixed-function-only.

## Stage 2

Cut to the minimal subset (`PolyPrimitive` + `SceneGraph` + `State` + `dada` +
`PData`), write a real `GLESBackend` against `IRenderBackend`, and render one
primitive — solid and wireframe — into an EGL pbuffer, `glReadPixels` to a PNG,
`adb pull`. Note the seam is currently only **13 `Backend()->` call sites across
3 files** (`PolyPrimitive`, `SceneGraph`, `State`), so finishing the seam is the
bulk of the work, not writing the backend.
