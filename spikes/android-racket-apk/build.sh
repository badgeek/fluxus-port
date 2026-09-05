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
# The runtime (~91 MB) is pushed into the app's files directory with
# `adb shell run-as`, which needs the debuggable flag in the manifest. That is a
# development shortcut: a shipping build would carry it as APK assets and extract
# it on first launch. Doing it this way keeps the APK small while iterating.
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
         RacketScriptHost; do
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
javac --release 17 -classpath "$PLATFORM" -d "$OUT/classes" \
      $(find "$HERE/java" -name "*.java") 2>&1 | grep -v "^Note:" || true
"$BT/d8" --lib "$PLATFORM" --output "$OUT/dex" $(find "$OUT/classes" -name "*.class")

echo "--- apk ---"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM" \
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

if [ "$1" != "nopush" ]; then
  echo "--- pushing the Racket runtime (~91 MB, once) ---"
  # Staged through /data/local/tmp because adb push cannot write into an app's
  # private directory directly; run-as then copies it in as the app's uid.
  # gracket and the starter are skipped — 53 MB of executables nothing here uses.
  STAGE=/data/local/tmp/racketstage
  adb shell rm -rf $STAGE && adb shell mkdir -p $STAGE/lib/racket $STAGE/share $STAGE/etc $STAGE/fluxus-lib
  for f in "$RACKET"/lib/racket/*.boot "$RACKET"/lib/racket/system.rktd; do
    [ -f "$f" ] && adb push "$f" $STAGE/lib/racket/ > /dev/null
  done
  # Merge the out-of-tree compiled mirror INTO the collects tree before pushing.
  # The install keeps its .zo under lib/racket/compiled/<ABSOLUTE PATH OF THE
  # INSTALL>/share/racket/collects/..., which is keyed to where it was BUILT and
  # does not survive being moved to a device. Pushing it as-is left the app
  # verifying collects from source on every launch — 22 s in the compile pass,
  # against 0.7 s once merged. cmake/bundle_racket.cmake:75 does exactly this for
  # the macOS .app bundle; same problem, same fix.
  MERGED=/tmp/fluxus-android-collects
  rm -rf $MERGED && mkdir -p $MERGED
  cp -R "$RACKET/share/racket/collects" $MERGED/
  MIRROR=$(find "$RACKET/lib/racket/compiled" -type d -path "*/share/racket/collects" | head -1)
  if [ -n "$MIRROR" ]; then
    (cd "$MIRROR" && tar cf - .) | (cd $MERGED/collects && tar xf -)
    echo "merged compiled mirror from $MIRROR"
  else
    echo "WARNING: no compiled mirror found — collects will compile on device"
  fi
  adb shell mkdir -p $STAGE/share/racket
  adb push "$MERGED/collects" $STAGE/share/racket/ > /dev/null
  [ -d "$RACKET/etc/racket" ] && adb push "$RACKET/etc/racket" $STAGE/etc/ > /dev/null
  # Sources only — the .zo in racket-lib/compiled are macOS-built (tarm64osx) and
  # Chez refuses them here with "incompatible fasl-object machine-type".
  for f in "$ROOT"/racket-lib/*.ss; do adb push "$f" $STAGE/fluxus-lib/ > /dev/null; done

  # mkdir first: getFilesDir() creates it lazily, and the app has not run yet.
  adb shell "run-as $PKG sh -c 'mkdir -p files; rm -rf files/lib files/share files/etc files/fluxus-lib; cp -r $STAGE/lib files/lib; cp -r $STAGE/share files/share; cp -r $STAGE/etc files/etc; cp -r $STAGE/fluxus-lib files/fluxus-lib; ls files'"
  adb shell rm -rf $STAGE
fi

adb shell am start -n $PKG/.MainActivity
