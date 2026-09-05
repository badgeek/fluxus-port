# Android GLES cube — stage 2

Renders a **real libfluxus primitive** through a **real `IRenderBackend`
implementation on OpenGL ES**, offscreen on an Android device, and dumps the
framebuffer to PNG. No JUCE, no Activity, no APK — and **no edits under
`vendor/`**.

```sh
sh build.sh          # build + push + run + pull + convert
sh build.sh build    # build only
```

Needs a device/emulator on `adb`. The AVD used here is `fluxspike`
(`android-35 google_apis arm64-v8a`); boot it headless with
`emulator -avd fluxspike -no-window -no-audio -no-boot-anim -no-snapshot`.

## Result (2026-09-06) — it works

Four passes, four files, each answering a different question. The first three
are the original cube experiment; `scene` was added once `RibbonPrimitive` and
`ParticlePrimitive` moved behind `IRenderBackend`, to check the other primitive
types on the device. It builds a cube, a `MakeSphere` TRISTRIP, a ribbon and 200
particles, and — since there is still no `SceneGraph` or `Renderer` on GLES — the
harness plays their part, setting each primitive's transform on the backend and
calling `Render()` itself. `SetSceneInfo()` supplies the camera vectors the
ribbon and particles need to face.

| Pass | Hints | Outcome |
|---|---|---|
| `solid.png` | `HINT_SOLID` | correct cube: three faces, per-face shading from the engine's normals, right colour and perspective |
| `wire.png` | `HINT_WIRE` | **see-through wireframe, all 12 edges, in the state's wire colour — with no `glPolygonMode`** |
| `both.png` | `HINT_SOLID\|HINT_WIRE` | hidden-line: solid fill, edges on top, depth test correctly hiding the back ones |

No GL errors. The cube is built by the engine's own `MakeCube` into a
`PolyPrimitive(QUADS)` — 24 verts, the same geometry the desktop app draws.

**The wireframe question is the one that mattered**, and the answer is yes: GLES
has no `glPolygonMode`, so `GLESBackend` converts the topology into real line
geometry (for QUADS, the four true edges per face) and draws `GL_LINES`. The
look survives.

## How it avoids touching shared code

- `shim/OpenGL.h` is force-included (`-include`) ahead of everything and claims
  the vendored `__OPENGL_H__` guard, so `vendor/.../OpenGL.h` becomes a no-op
  and the engine compiles against GLES unmodified. It supplies the missing
  enums, no-ops the fixed-function calls, and — the interesting part —
  **captures** the client-array pointers, current colour and polygon mode.
- `glDrawArrays`/`glDrawElements` are macro-redirected into the spike. The
  engine's SOLID pass already goes through `Backend()->drawArrays`
  (`PolyPrimitive.cpp:277`), but its WIRE and POINTS passes call raw GL
  (`PolyPrimitive.cpp:290-313`); the redirect is what lets a GLES backend draw
  them anyway.
- `GLESBackend.cpp` is compiled WITHOUT the shim, so it sees real GLES. The
  declarations they share live in `LegacyCapture.h`.

`GLESBackend` is what a port would actually need, in miniature: its own matrix
stack, lighting as a shader, quads split to triangles, polygon-fan
triangulation, and wireframe as line indices.

## What this does NOT prove

- **Emulator, not silicon.** `GL_RENDERER` is ANGLE over SwiftShader (software
  Vulkan). The API surface is the same, so semantics carry over; driver quirks
  and performance do not. Re-run on a physical device before trusting either.
- **Only the seam path.** `PolyPrimitive` is tested. `Ribbon`, `Particle`,
  `Voxel`, `Blobby` and friends still draw with `glBegin`, which the shim
  no-ops — under this spike they render **nothing**. Moving them behind
  `IRenderBackend` is the bulk of the real work (see
  `spikes/gles-audit/README.md`).
- **Wireframe on triangulated meshes.** Emitting edges per face is correct for
  QUADS; do it naively on a TRILIST and every triangulation diagonal shows up as
  a visible line. A real port needs edge dedup or a barycentric-coordinate
  shader.
- No textures, one hard-coded headlight, no state sorting, no `SceneGraph` — the
  driver sets the matrices itself.

## Notes

- `rgba2png.py` exists because the `magick` on this machine has **no PNG
  delegate**: it exits 0 and writes the raw bytes through, so the "PNG" it
  produces is not one. The stdlib zlib writer is more dependable. (Worth
  remembering if the golden-image regression harness is built later — its
  `compare` cannot read PNG either.)
- `GLBackend.cpp` is compiled in only because `RenderBackend.cpp` constructs one
  as the process default; `SetBackend()` swaps in `GLESBackend` before anything
  draws.
