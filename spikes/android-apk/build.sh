#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build, install and launch an APK that draws the fluxus engine on an Android
# screen — WITHOUT Gradle.
#
#   sh build.sh          # build + install + launch
#   sh build.sh build    # build only
#
# Gradle is skipped on purpose. The SDK already ships every tool an APK actually
# needs (aapt2, d8, zipalign, apksigner), and going straight to them means no
# wrapper download, no Gradle/JDK version matrix, and a build whose every step is
# visible here. openFrameworks uses Gradle with externalNativeBuild -> CMake,
# which is the right shape for a real app; this is the minimum that proves the
# engine renders on screen.
set -e

SDK=${ANDROID_SDK:-$HOME/Library/Android/sdk}
NDK=${NDK:-$SDK/ndk/27.1.12297006}
HOSTTAG=${HOSTTAG:-darwin-x86_64}
API=${API:-26}
BT=$(ls -d "$SDK"/build-tools/* | tail -1)
PLATFORM="$SDK/platforms/android-35/android.jar"
CXX="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin/aarch64-linux-android$API-clang++"

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/vendor/fluxus/libfluxus/src"
CUBE="$ROOT/spikes/gles-cube"
OUT=${OUT:-/tmp/fluxus-apk}
PKG=cc.fluxus.spike

rm -rf "$OUT"; mkdir -p "$OUT/obj" "$OUT/lib/arm64-v8a" "$OUT/classes" "$OUT/dex"

# --- native ----------------------------------------------------------------
INC="-I$SRC -I$CUBE"
COMMON="-O2 -std=c++17 -w -fPIC $INC -DGLSL -DFLUXUS_MINIMAL_NO_PNG \
        -DFLUXUS_MAJOR_VERSION=0 -DFLUXUS_MINOR_VERSION=18"

echo "--- engine ---"
OBJS=""
for f in dada.cpp Allocator.cpp PData.cpp PDataContainer.cpp PDataArithmetic.cpp \
         PDataOperator.cpp Primitive.cpp PolyPrimitive.cpp State.cpp \
         GraphicsUtils.cpp TexturePainter.cpp SearchPaths.cpp Trace.cpp \
         RenderBackend.cpp GLSLShader.cpp ShaderCache.cpp Evaluator.cpp \
         PolyEvaluator.cpp PNGLoader.cpp DDSLoader.cpp Geometry.cpp \
         GLBackend.cpp RibbonPrimitive.cpp ParticlePrimitive.cpp; do
  o="$OUT/obj/${f%.cpp}.o"
  "$CXX" $COMMON -c "$SRC/$f" -o "$o"
  OBJS="$OBJS $o"
done

echo "--- spike ---"
"$CXX" $COMMON -c "$CUBE/GLESBackend.cpp" -o "$OUT/obj/GLESBackend.o"
"$CXX" $COMMON -c "$HERE/jni_main.cpp"    -o "$OUT/obj/jni_main.o"

"$CXX" -shared -o "$OUT/lib/arm64-v8a/libfluxusspike.so" \
    $OBJS "$OUT/obj/GLESBackend.o" "$OUT/obj/jni_main.o" \
    -lEGL -lGLESv3 -lm -llog
cp "$NDK/toolchains/llvm/prebuilt/$HOSTTAG/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" \
   "$OUT/lib/arm64-v8a/"

# --- java -> dex -----------------------------------------------------------
# --release 17: this machine's javac is far newer than d8 accepts.
echo "--- java ---"
javac --release 17 -classpath "$PLATFORM" -d "$OUT/classes" \
      $(find "$HERE/java" -name "*.java") 2>&1 | grep -v "^Note:" || true
"$BT/d8" --lib "$PLATFORM" --output "$OUT/dex" $(find "$OUT/classes" -name "*.class")

# --- package ---------------------------------------------------------------
echo "--- apk ---"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM" \
    --manifest "$HERE/AndroidManifest.xml" --min-sdk-version $API --target-sdk-version 35
cd "$OUT"
cp dex/classes.dex .
zip -q base.apk classes.dex
zip -q base.apk lib/arm64-v8a/libfluxusspike.so lib/arm64-v8a/libc++_shared.so
"$BT/zipalign" -f 4 base.apk aligned.apk

# Debug keystore: apksigner refuses to sign without one. It lives OUTSIDE $OUT,
# which this script wipes on every run — regenerating it produced a new signature
# each build and Android then refused to update the installed package
# (INSTALL_FAILED_UPDATE_INCOMPATIBLE).
KS=${KEYSTORE:-/tmp/fluxus-spike-debug.keystore}
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android \
        -alias spike -keyalg RSA -keysize 2048 -validity 3650 \
        -dname "CN=fluxus spike" 2>/dev/null
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
        --out "$OUT/fluxus.apk" "$OUT/aligned.apk"
echo "built: $OUT/fluxus.apk"
[ "$1" = "build" ] && exit 0

# --- install + launch ------------------------------------------------------
# -r alone fails if a differently-signed build is already there.
adb install -r "$OUT/fluxus.apk" 2>/dev/null || {
  adb uninstall $PKG >/dev/null 2>&1
  adb install "$OUT/fluxus.apk"
}
adb shell am start -n $PKG/.MainActivity
