#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build the GLES cube spike for Android arm64, run it on the attached
# device/emulator, pull the framebuffer dumps and convert them to PNG.
#
#   sh build.sh          # build + push + run + pull + convert
#   sh build.sh build    # build only
#
# The engine TUs are compiled with shim/OpenGL.h force-included, which is what
# lets vendored libfluxus sources compile against GLES with NO edits under
# vendor/. GLESBackend.cpp is compiled WITHOUT it, so it sees the real GLES.
set -e

NDK=${NDK:-$HOME/Library/Android/sdk/ndk/27.1.12297006}
HOSTTAG=${HOSTTAG:-darwin-x86_64}
API=${API:-26}
CXX="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin/aarch64-linux-android$API-clang++"

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/vendor/fluxus/libfluxus/src"
OUT=${OUT:-/tmp/gles-cube}
DEV=/data/local/tmp/glescube
W=512; H=512

mkdir -p "$OUT"

COMMON="-O2 -std=c++17 -w -I$SRC -I$HERE \
  -DFLUXUS_MAJOR_VERSION=0 -DFLUXUS_MINOR_VERSION=18 \
  -DFLUXUS_MINIMAL_NO_PNG -DGLSL"

# Minimal engine subset for a cube. Deliberately small: every TU added here is
# one the real port would have to deal with, so the list doubles as a record of
# what a cube actually costs.
ENGINE="dada.cpp Allocator.cpp PData.cpp PDataContainer.cpp PDataArithmetic.cpp \
        PDataOperator.cpp Primitive.cpp PolyPrimitive.cpp State.cpp \
        GraphicsUtils.cpp TexturePainter.cpp SearchPaths.cpp Trace.cpp \
        RenderBackend.cpp GLSLShader.cpp ShaderCache.cpp \
        Evaluator.cpp PolyEvaluator.cpp PNGLoader.cpp DDSLoader.cpp \
        Geometry.cpp GLBackend.cpp \
        RibbonPrimitive.cpp ParticlePrimitive.cpp"
# NURBSPrimitive is deliberately absent: it is pure GLU, which Android lacks.
# GLBackend is here only because RenderBackend.cpp constructs one as the process
# default; SetBackend() swaps in GLESBackend before anything draws.

# No shim any more: the engine carries its own Android path now
# (OpenGL.h -> GLCompatES.h), so this builds exactly what a real port builds.
# The consequence is honest — paths that still use immediate mode or client
# arrays (PolyPrimitive's wire and points passes) warn and draw nothing, where
# the old spike shim faked them with a draw-capture hack.
echo "--- engine TUs (real Android path) ---"
OBJS=""
for f in $ENGINE; do
  o="$OUT/${f%.cpp}.o"
  "$CXX" $COMMON -c "$SRC/$f" -o "$o"
  OBJS="$OBJS $o"
done

echo "--- spike TUs ---"
# GLESBackend must NOT see the shim.
"$CXX" $COMMON -c "$HERE/GLESBackend.cpp" -o "$OUT/GLESBackend.o"
"$CXX" $COMMON -c "$HERE/main.cpp" -o "$OUT/main.o"

"$CXX" -o "$OUT/glescube" $OBJS "$OUT/GLESBackend.o" "$OUT/main.o" \
    -lEGL -lGLESv3 -lm -llog
echo "built: $OUT/glescube"
[ "$1" = "build" ] && exit 0

# --- run on device ----------------------------------------------------------
adb shell mkdir -p $DEV
adb push "$NDK/toolchains/llvm/prebuilt/$HOSTTAG/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" \
         "$OUT/glescube" $DEV/ > /dev/null
adb shell chmod 755 $DEV/glescube
adb shell "cd $DEV && LD_LIBRARY_PATH=$DEV ./glescube $DEV"

# --- pull + convert ---------------------------------------------------------
for p in solid wire both scene; do
  adb pull $DEV/$p.raw "$OUT/$p.raw" > /dev/null 2>&1 || continue
  # stdlib Python, not ImageMagick — see the note in rgba2png.py.
  python3 "$HERE/rgba2png.py" "$OUT/$p.raw" "$OUT/$p.png" $W $H
done
