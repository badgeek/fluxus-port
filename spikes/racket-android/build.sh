#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build the Racket-on-Android spike for arm64 and push it to a device/emulator.
# Expects a cross-built Racket CS install (see README.md for the recipe) whose
# prefix is passed as $RACKET_ANDROID (default /tmp/racket-android/install-android).
#
#   sh build.sh          # compile + push + run on the attached device
#   sh build.sh build    # compile only
set -e

NDK=${NDK:-$HOME/Library/Android/sdk/ndk/27.1.12297006}
HOSTTAG=${HOSTTAG:-darwin-x86_64}
BIN=$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin
API=${API:-26}
CXX="$BIN/aarch64-linux-android$API-clang++"

PREFIX=${RACKET_ANDROID:-/tmp/racket-android/install-android}
OUT=${OUT:-/tmp/racket-android/spike-out}
DEV=/data/local/tmp/fluxspike
HERE=$(dirname "$0")

[ -f "$PREFIX/lib/libracketcs.a" ] || {
  echo "no $PREFIX/lib/libracketcs.a — cross-build Racket first (see README.md)"; exit 1; }

mkdir -p "$OUT"

# The .so: embedded Racket + the two probe symbols. -Wl,--export-dynamic so the
# exported-only probe is reachable by dlsym, matching how the real app links.
"$CXX" -O2 -fPIC -shared -o "$OUT/libspike.so" "$HERE/spike.cpp" \
    -I"$PREFIX/include/racket" \
    "$PREFIX/lib/libracketcs.a" -lm -lz -ldl -Wl,--export-dynamic

# Two drivers — see runner.cpp for why both.
"$CXX" -O2 -o "$OUT/runner_dlopen" "$HERE/runner.cpp" -ldl -Wl,--export-dynamic
"$CXX" -O2 -o "$OUT/runner_local"  "$HERE/runner.cpp" -DSPIKE_LOCAL -ldl -Wl,--export-dynamic
"$CXX" -O2 -o "$OUT/runner_linked" "$HERE/runner.cpp" -DSPIKE_LINKED \
    -L"$OUT" -lspike -Wl,--export-dynamic -Wl,-rpath,"$DEV"

echo "built: $OUT"
[ "$1" = "build" ] && exit 0

# --- push -------------------------------------------------------------------
# The runtime needs the boot files, the collects tree and its compiled roots.
adb shell mkdir -p $DEV
# libc++_shared: a real APK bundles it in lib/<abi>/, so ship it the same way
# rather than hiding the dependency with -static-libstdc++.
adb push "$NDK/toolchains/llvm/prebuilt/$HOSTTAG/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" \
         "$OUT/libspike.so" "$OUT/runner_dlopen" "$OUT/runner_local" "$OUT/runner_linked" $DEV/ > /dev/null
adb shell mkdir -p $DEV/racket/lib/racket $DEV/racket/share $DEV/racket/etc
adb push "$PREFIX/lib/racket" $DEV/racket/lib/ > /dev/null
adb push "$PREFIX/share/racket" $DEV/racket/share/ > /dev/null
[ -d "$PREFIX/etc/racket" ] && adb push "$PREFIX/etc/racket" $DEV/racket/etc/ > /dev/null
adb shell chmod 755 $DEV/runner_dlopen $DEV/runner_local $DEV/runner_linked

# --- run --------------------------------------------------------------------
for r in runner_dlopen runner_local runner_linked; do
  echo "=================== $r ==================="
  adb shell "cd $DEV && LD_LIBRARY_PATH=$DEV ./$r $DEV/racket" || echo "($r exited non-zero)"
done
