#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build, install and launch an APK in which RACKET drives fluxus on the device
# screen. No Gradle — same hand-rolled aapt2/d8/apksigner pipeline as
# spikes/android-apk, plus the Racket runtime.
#
#   sh build.sh          # build + install + push runtime + launch
#   sh build.sh build    # build only
#   sh build.sh nopush   # build + install + launch, skip the runtime copy
#
# The Racket runtime rides INSIDE the APK as a single compressed asset and is
# extracted to the app's files directory on first launch. No adb, no run-as, no
# debuggable flag — this APK installs and runs on any arm64 device.
set -e

SDK=${ANDROID_SDK:-$HOME/Library/Android/sdk}
NDK=${NDK:-$SDK/ndk/27.1.12297006}
HOSTTAG=${HOSTTAG:-darwin-x86_64}
API=${API:-26}
BT=$(ls -d "$SDK"/build-tools/* | tail -1)
PLATFORM="$SDK/platforms/android-35/android.jar"
CXX="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin/aarch64-linux-android$API-clang++"
CC="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin/aarch64-linux-android$API-clang"

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/vendor/fluxus/libfluxus/src"
CUBE="$ROOT/spikes/gles-cube"
GLUE="$ROOT/spikes/racket-gles"
RACKET=${RACKET_ANDROID:-/tmp/racket-android/install-android}
OUT=${OUT:-/tmp/fluxus-racket-apk}
PKG=cc.fluxus.racket

[ -f "$RACKET/lib/libracketcs.a" ] || {
  echo "no $RACKET/lib/libracketcs.a — see spikes/racket-android/README.md"; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT/obj/engine" "$OUT/obj/vterm" "$OUT/obj/app" \
                        "$OUT/lib/arm64-v8a" "$OUT/classes" "$OUT/dex"

INC="-I$SRC -I$ROOT/app -I$CUBE -I$ROOT/vendor/libvterm/include -I$RACKET/include/racket"
# No RACKET_DIR baked in: the app calls RacketScriptHost::setRuntimeRoot with
# its files directory at startup. The runtime is laid out under it exactly like
# a macOS .app bundle — lib/racket, share/racket, etc/racket, fluxus-lib — so
# the same bundleRoot() lookup serves both platforms.
DEFS="-DGLSL -DFLUXUS_MINIMAL_NO_PNG -DFLUXUS_MAJOR_VERSION=0 -DFLUXUS_MINOR_VERSION=18"
COMMON="-O2 -std=c++17 -w -fPIC $INC $DEFS"

OBJS=""
compile() { o="$OUT/obj/$2/$(basename ${1%.*}).o"; "$CXX" $COMMON -c "$1" -o "$o"; OBJS="$OBJS $o"; }

echo "--- libfluxus ---"
for f in "$SRC"/*.cpp; do
  grep -q "/$(basename $f)" "$ROOT/cmake/libfluxus_min.cmake" || continue
  compile "$f" engine
done

echo "--- vterm ---"
# Separate object dir: macOS is case-insensitive, so State.cpp and state.c would
# otherwise write the same file.
for f in "$ROOT"/vendor/libvterm/src/*.c; do
  o="$OUT/obj/vterm/$(basename ${f%.c}).o"
  "$CC" -O2 -w -fPIC -std=c99 -I"$ROOT/vendor/libvterm/include" \
        -I"$ROOT/vendor/libvterm/src" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done

echo "--- flux_* + Racket host ---"
for f in FluxusCommandsCore FluxusCommandsPdata FluxusCommandsMaths \
         FluxusCommandsTerminal FluxusCommandsInput FluxusCommandsFx \
         FluxusCommandsGpu RacketScriptHost; do
  compile "$ROOT/app/$f.cpp" app
done
compile "$GLUE/android_stubs.cpp" app
compile "$CUBE/GLESBackend.cpp"   app
compile "$HERE/jni_racket.cpp"    app

"$CXX" -shared -o "$OUT/lib/arm64-v8a/libfluxusracket.so" $OBJS \
    "$RACKET/lib/libracketcs.a" -lEGL -lGLESv3 -lm -lz -ldl -llog \
    -Wl,--export-dynamic
cp "$NDK/toolchains/llvm/prebuilt/$HOSTTAG/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" \
   "$OUT/lib/arm64-v8a/"

echo "--- java ---"
# Stamp the build so the extraction marker changes when the runtime does.
BUILD_ID=$(date +%Y%m%d%H%M%S)
mkdir -p "$OUT/gen/cc/fluxus/racket"
sed "s|@BUILD_ID@|$BUILD_ID|" "$HERE/java/cc/fluxus/racket/BuildId.java" \
    > "$OUT/gen/cc/fluxus/racket/BuildId.java"
javac --release 17 -classpath "$PLATFORM" -d "$OUT/classes" \
      $(find "$HERE/java" -name "*.java" ! -name BuildId.java) \
      "$OUT/gen/cc/fluxus/racket/BuildId.java" 2>&1 | grep -v "^Note:" || true
"$BT/d8" --lib "$PLATFORM" --output "$OUT/dex" $(find "$OUT/classes" -name "*.class")

echo "--- runtime asset ---"
# Laid out exactly like a macOS .app bundle so one bundleRoot() serves both.
STAGE="$OUT/runtime"
mkdir -p "$STAGE/lib/racket" "$STAGE/share/racket" "$STAGE/etc" "$STAGE/fluxus-lib"
for f in "$RACKET"/lib/racket/*.boot "$RACKET"/lib/racket/system.rktd; do
  [ -f "$f" ] && cp "$f" "$STAGE/lib/racket/"
done
# Merge the out-of-tree compiled mirror INTO the collects tree. The install keeps
# its .zo under lib/racket/compiled/<ABSOLUTE PATH OF THE INSTALL>/..., keyed to
# where it was BUILT, so it does not survive relocation — shipped as-is the app
# re-verifies collects from source on every launch (22 s, measured).
# cmake/bundle_racket.cmake:75 does the same for the macOS bundle.
cp -R "$RACKET/share/racket/collects" "$STAGE/share/racket/"
MIRROR=$(find "$RACKET/lib/racket/compiled" -type d -path "*/share/racket/collects" | head -1)
if [ -n "$MIRROR" ]; then
  (cd "$MIRROR" && tar cf - .) | (cd "$STAGE/share/racket/collects" && tar xf -)
  echo "merged compiled mirror from $MIRROR"
else
  echo "WARNING: no compiled mirror — collects would compile on device"
fi
[ -d "$RACKET/etc/racket" ] && cp -R "$RACKET/etc/racket" "$STAGE/etc/"
# Sources only: the .zo in racket-lib/compiled are macOS-built (tarm64osx) and
# Chez refuses them here with "incompatible fasl-object machine-type".
cp "$ROOT"/racket-lib/*.ss "$STAGE/fluxus-lib/"

mkdir -p "$OUT/assets"
(cd "$STAGE" && zip -q -r -9 "$OUT/assets/runtime.zip" .)
echo "runtime.zip: $(du -h "$OUT/assets/runtime.zip" | cut -f1) (from $(du -sh "$STAGE" | cut -f1) on disk)"

echo "--- apk ---"
# -0 zip: the asset is already deflated; letting aapt2 compress it again costs
# build time and gains nothing.
"$BT/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM" -A "$OUT/assets" -0 zip \
    --manifest "$HERE/AndroidManifest.xml" --min-sdk-version $API --target-sdk-version 35
cd "$OUT"
cp dex/classes.dex .
zip -q base.apk classes.dex
zip -q base.apk lib/arm64-v8a/libfluxusracket.so lib/arm64-v8a/libc++_shared.so
"$BT/zipalign" -f 4 base.apk aligned.apk
KS=${KEYSTORE:-/tmp/fluxus-spike-debug.keystore}
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android \
        -alias spike -keyalg RSA -keysize 2048 -validity 3650 \
        -dname "CN=fluxus spike" 2>/dev/null
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
        --out "$OUT/fluxus-racket.apk" "$OUT/aligned.apk"
echo "built: $OUT/fluxus-racket.apk"
[ "$1" = "build" ] && exit 0

adb install -r "$OUT/fluxus-racket.apk" 2>/dev/null || {
  adb uninstall $PKG >/dev/null 2>&1; adb install "$OUT/fluxus-racket.apk"; }

adb shell am start -n $PKG/.MainActivity
