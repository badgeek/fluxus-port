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
compile "$GLUE/android_stubs.cpp"   app
compile "$CUBE/GLESBackend.cpp"     app
compile "$HERE/android_control.cpp" app
compile "$HERE/jni_racket.cpp"      app

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

# Trim the collects tree. TRIM=0 ships it whole — the first thing to try when a
# module goes missing at runtime, since that tells you in one build whether the
# trim is to blame.
#
# The trim is SMALL, and that is the finding, not a shortcut. Two cuts that
# looked far bigger were both measured and both rejected:
#
#   the .rkt sources (8.6 MB), where compiled/<name>_rkt.zo sits right beside
#   them — the shape `raco pkg install --binary` ships. Not here: the embedded
#   boot opens the source itself, .zo present or not, and the device dies with
#     open-input-file: cannot open module file
#       module path: racket/base
#       path: .../share/racket/collects/racket/base.rkt
#
#   the .dep files (2.6 MB), the compilation manager's dependency records.
#   Nothing recompiles collects on the device, so they look like dead weight —
#   but without them the manager cannot prove the tree is up to date, and the
#   fluxus-lib compile pass on first launch goes from 1.1 s to 17.6 s. Saving
#   2.6 MB by adding 16 seconds is not a trade worth making.
#
# What is left is whole collections nothing loads. They are only ever loaded on
# demand, so removing one is invisible until something requires it — which is
# what the smoke test at the end of this script is for.
if [ "${TRIM:-1}" = "1" ]; then
  C="$STAGE/share/racket/collects"
  before=$(du -sk "$C" | cut -f1)

  # Disjoint from what `racket spikes/android-racket-apk/collections-used.rkt`
  # reports: network, database, serialisation, and the machinery for BUILDING
  # Racket programs. A sketch that (require)s one of these will not find it.
  #
  # Guessing does not work here, twice over. `pkg` and `planet` look every bit
  # as removable and are not — compiler/cm reaches pkg/path. And `xml` is
  # reached only by collada-import.ss, a file nothing requires but the compile
  # pass still compiles, so it is invisible to both a grep and a require of the
  # library entry point. Re-run collections-used.rkt after changing what the
  # host or racket-lib requires.
  for c in db openssl net json data launcher dynext raco acks \
           readline scribble tests; do
    rm -rf "$C/$c"
  done

  after=$(du -sk "$C" | cut -f1)
  echo "collects trimmed: ${before} KB -> ${after} KB"
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

# Clear the log BEFORE launching, or the smoke test below happily matches the
# "Racket ready" from the previous run and reports a broken build as good.
adb logcat -c
adb shell am start -n $PKG/.MainActivity

# Live coding from the host. The app's control port is loopback-only on the
# device; this is what reaches it. Same protocol as the desktop app, so the
# repo's own CLI drives the phone:
#
#   cli/fluxus load  examples/foo.scm
#   cli/fluxus watch examples/foo.scm     # reload on every save
#   cli/fluxus get                        # what is running now
CTLPORT=${FLUXUS_CONTROL_PORT:-8020}
adb forward tcp:$CTLPORT tcp:$CTLPORT >/dev/null && \
  echo "control port forwarded: 127.0.0.1:$CTLPORT — try 'cli/fluxus get'"

# Smoke test. Worth the wait because the collects trim above can only fail at
# RUNTIME, when something requires a collection that is no longer there — and it
# fails as a Racket error in logcat, not as a build error.
echo "--- waiting for the runtime ---"
i=0
while [ $i -lt 40 ]; do
  if adb logcat -d -s fluxus 2>/dev/null | grep -q "Racket ready"; then
    adb logcat -d -s fluxus | grep -E "boot |Racket ready" | tail -6
    exit 0
  fi
  i=$((i + 1)); sleep 1
done
echo "TIMEOUT: no 'Racket ready' in 40 s — check 'adb logcat -s fluxus', and try TRIM=0"
exit 1
