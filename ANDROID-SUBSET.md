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

## Known defects — TODO

Real bugs found and deliberately parked, not deferred features.

- **Sphere shows a dark sawtooth band on one limb.** View-dependent: present in
  two of three sampled frames, absent in the third, and sitting at exactly the
  ambient-only colour value (so the triangles are lit as if facing away, not
  missing — a flood fill finds no holes). Visible in the offscreen renders too,
  so it is not an APK or emulator artifact.
  Suspicion, unverified: `GLESBackend`'s TriStrip-to-triangles expansion
  mishandling the seam between rings, where `MakeSphere` stitches with
  degenerate vertices. The odd/even winding swap is the place to look first.
  Only affects TRISTRIP primitives; QUADS (cubes) are clean.

- **Wireframe on triangulated meshes is untested and probably wrong.** Per-face
  edges are correct for QUADS, but on a TRILIST or TRISTRIP every triangulation
  diagonal would be drawn as a visible line. Needs edge dedup or a
  barycentric-coordinate shader — a design decision, not a fix.

- **The light path has no test.** No golden case uses `(make-light …)`, so the
  seam mapping in `Light.cpp` is verified by construction and compilation only.

## The app around the engine (`spikes/android-racket-apk`)

**Where the sketch comes from.** Not a string in the binary any more. On first
launch the app seeds `<filesDir>/sketch.scm` and from then on that file is the
sketch: `android_control.cpp` watches its mtime and serves a control port on
127.0.0.1:8020 speaking the SAME line-JSON protocol as the desktop
`app/ControlServer.cpp`, so the repo's own CLI drives the device.

```sh
adb forward tcp:8020 tcp:8020
cli/fluxus load  examples/foo.scm
cli/fluxus watch examples/foo.scm     # reload on every save
```

`load`, `eval`, `get` and `error` are served. `save` and `screenshot` are not:
screenshot needs the PNG writer this build compiles out
(`FLUXUS_MINIMAL_NO_PNG`). The seed file is never rewritten on upgrade — after
the first launch the file is the user's.

The server thread never calls the script engine. It writes a mutex-protected
buffer that the GL thread drains in `nativeDraw`, the same rule the desktop
audio and video hosts follow. `android.permission.INTERNET` is in the manifest
because Android puts an app in the `inet` group only when it holds that
permission — without it `socket()` fails with `EACCES` even for a loopback
listener.

**Touch.** Drag orbits, pinch dollies, double tap resets. The gesture feeds
`flux_camera_drag` / `flux_camera_zoom` / `flux_camera_reset` — the same orbit
state the desktop mouse drives — and `flux_camera_finalize()` runs after the
eval, where `FluxusScene::renderFrame` runs it, because the sketch's own
`(clear)` replaces the camera.

This spike previously built its own view matrix and pushed it with
`backend.loadMatrix`. That never had any effect: `Renderer::PreRender` applies
`Camera`'s matrix afterwards, so the hand-rolled orbit was silently overwritten
and the view sat at the engine default the whole time. It is the same mistake as
`setResolution` / `SetClearFrame` / `SetSceneInfo` — **drive `Renderer`, do not
stand in for it** — and it stayed invisible because a fixed camera looks like a
working one.

Verified by measurement on the emulator: drag changes the view, and two resets
land on the same pixels. **Pinch is reasoning, not measurement** — `adb shell
input` has no multitouch, and raw `sendevent` did not reach the app even as
root, so the two-finger branch has never actually run.

**Runtime size — the trim is small, and that is the finding.** 88 MB extracted /
29,217,455 B APK, now **85 MB / 27,390,639 B**: 3 MB off the device, 1.8 MB off
the APK, 6%. `build.sh` removes whole collections nothing loads; `TRIM=0` ships
the tree whole, which is the first thing to try when a module goes missing at
runtime, and the build's smoke test waits for `Racket ready` in logcat because a
trim can only fail there.

Both cuts that would have mattered were tried and rejected on measurement:

- **The `.rkt` sources (8.6 MB)**, with `compiled/<name>_rkt.zo` sitting right
  beside them — the shape `raco pkg install --binary` ships. The embedded boot
  opens the source itself, `.zo` or not: `open-input-file: cannot open module
  file; module path: racket/base`. Dead before the first frame.
- **The `.dep` files (2.6 MB)**, the compilation manager's dependency records.
  Nothing recompiles collects on the device, so they look like pure waste — but
  without them the manager cannot prove the tree is up to date, and the
  fluxus-lib compile pass on first launch goes **1.1 s → 17.6 s**.

Which collections are safe is derived, not guessed:
`racket spikes/android-racket-apk/collections-used.rkt` wraps the load handler
and requires what the host requires. Guessing failed twice — `pkg` and `planet`
look removable but `compiler/cm` reaches `pkg/path`, and `xml` is reached only by
`collada-import.ss`, a file nothing requires but the compile pass still compiles,
so it is invisible both to a grep and to requiring the library entry point.

What remains is not app data: **50 MB is Chez's three `.boot` files** and ~35 MB
is collects Racket genuinely opens. Shrinking either needs a different Racket
build, not a different packaging step. The `.so` is 2.0 MB and strips to 1.7 MB —
not taken, because every `flux_*` command is resolved by `dlsym` at runtime and
300 kB is not worth reasoning about the dynamic symbol table (see the
whole-archive note in `CLAUDE.md`).

**Two traps this uncovered, both now fixed in the spike:**

- **Extraction used to merge.** Unzipping the runtime over the old tree only
  adds and overwrites, so a file the new runtime no longer ships stayed behind.
  A trimmed runtime therefore tested **green on an upgrade and died on a clean
  install** — the upgrade was still running the deleted files. `MainActivity`
  now wipes `lib/ share/ etc/ fluxus-lib/` before extracting (never
  `sketch.scm`, which is the user's). Any measurement of a runtime change made
  with `adb install -r` and no wipe is worthless.
- **Racket's output had nowhere to go.** Android gives an app no console, so a
  Racket error — the one line that says what is actually wrong — went to
  `/dev/null` and a failed boot looked like a hang. `pumpStdioToLog()` pipes
  stdout and stderr into logcat under the tag `fluxus-racket`, line-buffered
  (Racket's stdout is block-buffered and the embedded process exits without
  unwinding). Every diagnosis above came from that one change.

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
