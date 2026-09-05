# Racket CS on Android — spike

Headless proof that the Racket script host can run on Android arm64: no JUCE, no
GL, no APK. It answers the three questions that gate any Racket-on-Android plan,
and every one of them came back green.

Run it with `sh build.sh` (compiles, pushes to the attached device/emulator, runs).
`sh build.sh build` compiles only.

## Results (2026-09-05, emulator `android-35 google_apis arm64-v8a`, NDK 27.1.12297006)

**1. Racket CS cross-builds for Android arm64 — with NATIVE code, not `pb`.**
The build selects Chez machine type **`tarm64le`**, and `file` on the result says
`ELF 64-bit LSB pie executable, ARM aarch64 … interpreter /system/bin/linker64`.
The portable-bytecode fallback (`--enable-pb`) was never needed.

**2. `racket_boot` + runtime code generation work under Android.** No W^X /
`execmem` problem — the concern that looked most likely to be fatal simply isn't
one. Boot + `racket/base` + `ffi/unsafe` + an eval takes **~77 ms** on the
emulator (3 runs: 82/74/77 ms), which also confirms the installed `.zo` are being
used rather than collects being recompiled from source.

**3. The FFI reaches our C symbols — every way we tried.** This is the one I
expected to FAIL, because `racket-lib/fluxus-engine.ss:15` binds all ~300
commands with `(get-ffi-obj cname #f …)`, where `#f` means "the current process",
and on Android our code is never the main executable (that is `app_process`).
It resolves anyway:

| | `self (#f)` | `ffi-lib "libspike.so"` |
|---|---|---|
| `flux_probe_add` (exported only) | RESOLVED | RESOLVED |
| `flux_probe_reg` (also `Sregister_symbol`) | RESOLVED | RESOLVED |

and identically under all three loading shapes — `dlopen` with `RTLD_NOW|RTLD_GLOBAL`,
`dlopen` with **`RTLD_NOW` alone** (what `System.loadLibrary` actually does), and
link-time `DT_NEEDED`. So `Sregister_symbol` is not required to make the `#f` path
work, and `fluxus-engine.ss` would not need changing.

## The one real blocker found

**The default cross-build produces a NON-PIC `libracketcs.a`, which cannot go into
a `.so`** — and on Android every line of app code lives in a `.so`. Linking fails
with `relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against symbol …;
recompile with -fPIC`. Fix: pass `CFLAGS+="-fPIC" CPPFLAGS+="-fPIC"` to
`configure`. Note that re-running `configure` alone is not enough — the build
system does not notice the flag change, so the target objects must be removed
(`rm -f cs/c/*.o; rm -rf cs/c/ChezScheme/tarm64le cs/c/rktio/*.o cs/c/rktio/librktio.a`)
before `make`. Keeping `cs/c/local` skips rebuilding the host bootstrap.

## Cross-build recipe that worked

```sh
# host-prefixed wrappers, because Racket's configure looks for
# aarch64-linux-android-gcc etc. and NDK 27 ships api-versioned clang instead.
# They must be scripts, not symlinks: the NDK clang wrapper resolves its own
# directory and a symlink makes it look in the wrong place.
NDKBIN=$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin
for t in gcc:aarch64-linux-android26-clang cc:aarch64-linux-android26-clang \
         g++:aarch64-linux-android26-clang++ c++:aarch64-linux-android26-clang++ \
         ar:llvm-ar ranlib:llvm-ranlib strip:llvm-strip nm:llvm-nm \
         objdump:llvm-objdump readelf:llvm-readelf ld:ld.lld; do
  printf '#!/bin/sh\nexec "%s" "$@"\n' "$NDKBIN/${t#*:}" > shim/aarch64-linux-android-${t%%:*}
  chmod +x shim/aarch64-linux-android-${t%%:*}
done

PATH=$PWD/shim:$PATH racket-9.3/src/configure \
    --host=aarch64-linux-android --enable-csonly \
    --enable-racket=auto --prefix=$PREFIX \
    CFLAGS+="-fPIC" CPPFLAGS+="-fPIC"
PATH=$PWD/shim:$PATH make -j8 && make install
```

`--enable-racket=auto` is mandatory: pointing `--enable-racket` at the installed
host racket is rejected for a cross CS build with
`cross build needs --enable-scheme=SCHEME or --enable-racket=auto`. "auto" then
bootstraps a host Chez/Racket first, which is most of the build time.

## What this does NOT prove

- **Inside a real APK.** The runners are native executables under
  `/data/local/tmp`. `runner_local` mimics `System.loadLibrary`'s dlopen flags,
  but a genuine Activity + zygote process is still a step away.
- **Startup with the fluxus library loaded.** ~77 ms covers `racket/base` +
  `ffi/unsafe` only. The real host also requires the whole `racket-lib/*.ss` set;
  on macOS that is what takes ~4 s.
- **Anything graphical.** libfluxus is fixed-function desktop GL and Android is
  GLES-only — see the Android section of `ROADMAP.md`. Racket working changes
  nothing about that; a `GLESBackend` remains the actual blocker.

## Size

The pushed runtime is ~143 MB (boot files + collects + compiled roots), which
would need trimming for an APK.
