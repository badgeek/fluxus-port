# Racket → fluxus → GLES on Android

The whole chain, headless, on a device: a real `.scm` string evaluated by the
real `RacketScriptHost`, calling the real `flux_*` command layer, building into
the real `SceneGraph`, drawn by `Renderer` through `GLESBackend` into an EGL
pbuffer. No JUCE, no Activity, no APK, and no engine code written for the
occasion.

```sh
sh build.sh          # build + push + run + pull + convert to PNG
sh build.sh build    # build only
```

Needs the **PIC** cross-built Racket from `spikes/racket-android/README.md`
(`CFLAGS+="-fPIC"`), and a device/emulator on `adb`.

## What it links

Everything the desktop app links, minus what Android cannot have:

- the whole of `libfluxus_min` — including `NURBSPrimitive`, because
  `ShadowVolumeGen` dynamic_casts to it and dropping the TU would leave a
  missing vtable rather than a clean absence. `GLCompatES.h` stubs GLU for it.
- `vendor/libvterm` (the terminal primitive's parser)
- the `flux_*` command layer: Core, Pdata, Maths, Terminal, Input, Fx
- `app/RacketScriptHost.cpp` and `libracketcs.a`
- `spikes/gles-cube/GLESBackend.cpp`

**Left out**, each for a stated reason:
- `app/FluxusCommandsGpu.cpp` — still uses the EXT spellings of the FBO and
  float-texture calls. GLES 3 has core equivalents; they have not been renamed
  yet, so the GPU particle commands fall back to their failure thunks.
- `android_stubs.cpp` supplies `pixelsErase`, `pixelsCount`, `flux_font_atlas`
  and `flux_glyph_cell` — the first two belong to the excluded GPU TU, the last
  two to `app/TextureLoader.cpp`, which is JUCE code (JUCE decodes the font
  image) and cannot build here. `build-text` and `build-terminal` therefore draw
  untextured; both warn once.

## Two findings worth keeping

**1. Racket bytecode carries a machine type.** Pushing `racket-lib/compiled/`
along with the sources kills the boot immediately:

```
fasl-read: incompatible fasl-object machine-type 'tarm64osx found in ...
```

The `.zo` that `make precompile` builds on macOS are `tarm64osx`; Chez on
Android is `tarm64le` and refuses them. So Android needs **its own** precompile
or none at all — this script pushes `.ss` only and lets the device compile. That
also means the startup-time fix documented in `CLAUDE.md`
(`current-compiled-file-roots`) buys nothing on Android until there is an
Android-side precompile step.

**2. The stub warnings are a live progress list.** Running the sketch prints
exactly which fixed-function paths the engine still takes:

```
[fluxus] GLES: 'glMatrixMode (fixed-function matrix stack)' ...
[fluxus] GLES: 'glFrustum' ...
[fluxus] GLES: 'glEnableClientState (client arrays)' ...
[fluxus] GLES: 'glLightfv (fixed-function lighting)' ...
```

Those four name the remaining work precisely: `Renderer::PreRender`'s camera and
projection setup, `Light.cpp`, and `PolyPrimitive`'s wire/points client-array
passes. When a warning stops appearing, that path is genuinely ported. The
projection is supplied to the backend directly here, standing in for the
`glFrustum` that does nothing.
