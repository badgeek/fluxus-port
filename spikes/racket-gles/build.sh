#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build the Racket -> fluxus -> GLES spike for Android arm64 and run it on the
# attached device/emulator.
#
#   sh build.sh          # build + push + run + pull + convert
#   sh build.sh build    # build only
#
# Needs the PIC cross-built Racket from spikes/racket-android/README.md.
set -e

NDK=${NDK:-$HOME/Library/Android/sdk/ndk/27.1.12297006}
HOSTTAG=${HOSTTAG:-darwin-x86_64}
API=${API:-26}
CXX="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin/aarch64-linux-android$API-clang++"
CC="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin/aarch64-linux-android$API-clang"

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/vendor/fluxus/libfluxus/src"
CUBE="$ROOT/spikes/gles-cube"
RACKET=${RACKET_ANDROID:-/tmp/racket-android/install-android}
OUT=${OUT:-/tmp/racket-gles}
DEV=/data/local/tmp/racketgles
W=512; H=512

[ -f "$RACKET/lib/libracketcs.a" ] || {
  echo "no $RACKET/lib/libracketcs.a — see spikes/racket-android/README.md"; exit 1; }

# Separate object dirs: macOS is case-insensitive, so the engine's State.cpp
# and vterm's state.c would otherwise write the same State.o / state.o file.
mkdir -p "$OUT/engine" "$OUT/vterm" "$OUT/app"

# On-device layout, baked in the way the real app bakes RACKET_DIR.
DEFS="-DGLSL -DFLUXUS_MINIMAL_NO_PNG -DFLUXUS_MAJOR_VERSION=0 -DFLUXUS_MINOR_VERSION=18 \
  -DRACKET_DIR=\"$DEV/racket\" -DRACKET_LIB_DIR=\"$DEV/fluxus-lib\""
INC="-I$SRC -I$ROOT/app -I$CUBE -I$ROOT/vendor/libvterm/include -I$RACKET/include/racket"
COMMON="-O2 -std=c++17 -w $INC $DEFS"

OBJS=""
compile() { o="$OUT/$2/$(basename ${1%.*}).o"; "$CXX" $COMMON -c "$1" -o "$o"; OBJS="$OBJS $o"; }

# The whole engine, NURBS included: GLCompatES stubs GLU so the class still
# links (ShadowVolumeGen dynamic_casts to it).
echo "--- libfluxus ---"
for f in "$SRC"/*.cpp; do
  case "$(basename $f)" in
  esac
  # only the TUs the real libfluxus_min target builds
  grep -q "/$(basename $f)" "$ROOT/cmake/libfluxus_min.cmake" || continue
  compile "$f" engine
done

echo "--- vterm (build-terminal) ---"
for f in "$ROOT"/vendor/libvterm/src/*.c; do
  o="$OUT/vterm/$(basename ${f%.c}).o"
  "$CC" -O2 -w -std=c99 -I"$ROOT/vendor/libvterm/include" -I"$ROOT/vendor/libvterm/src" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done

echo "--- flux_* command layer + Racket host ---"
for f in FluxusCommandsCore FluxusCommandsPdata FluxusCommandsMaths \
         FluxusCommandsTerminal FluxusCommandsInput FluxusCommandsFx \
         RacketScriptHost; do
  compile "$ROOT/app/$f.cpp" app
done
# FluxusCommandsGpu is left out: it still uses the EXT spellings of the FBO and
# float-texture calls, which have core GLES 3 equivalents but have not been
# renamed yet. The GPU particle commands fall back to their failure thunks.

echo "--- spike ---"
compile "$CUBE/GLESBackend.cpp" app
compile "$HERE/android_stubs.cpp" app
compile "$HERE/main.cpp" app

"$CXX" -o "$OUT/racketgles" $OBJS "$RACKET/lib/libracketcs.a" \
    -lEGL -lGLESv3 -lm -lz -ldl -llog -Wl,--export-dynamic
echo "built: $OUT/racketgles"
[ "$1" = "build" ] && exit 0

# --- push -------------------------------------------------------------------
adb shell mkdir -p $DEV/racket/lib $DEV/racket/share $DEV/racket/etc $DEV/fluxus-lib
adb push "$NDK/toolchains/llvm/prebuilt/$HOSTTAG/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" \
         "$OUT/racketgles" $DEV/ > /dev/null
adb push "$RACKET/lib/racket"   $DEV/racket/lib/   > /dev/null
adb push "$RACKET/share/racket" $DEV/racket/share/ > /dev/null
[ -d "$RACKET/etc/racket" ] && adb push "$RACKET/etc/racket" $DEV/racket/etc/ > /dev/null
# Sources only — NOT racket-lib/compiled/. Racket bytecode carries a machine
# type, so the .zo `make precompile` builds on macOS are 'tarm64osx and Chez
# refuses them here with "incompatible fasl-object machine-type". Android needs
# its own precompile, or none: this pushes .ss and lets the device compile.
for f in "$ROOT"/racket-lib/*.ss; do adb push "$f" $DEV/fluxus-lib/ > /dev/null; done
adb shell chmod 755 $DEV/racketgles

# --- run --------------------------------------------------------------------
adb shell "cd $DEV && LD_LIBRARY_PATH=$DEV ./racketgles $DEV"
for f in racket-scene sphere-centred; do
  adb pull $DEV/$f.raw "$OUT/$f.raw" > /dev/null 2>&1 || continue
  python3 "$CUBE/rgba2png.py" "$OUT/$f.raw" "$OUT/$f.png" $W $H
done
