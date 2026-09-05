#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Stage 1 of the Android GLES spike: compile every libfluxus_min translation unit
# against Android's GLES headers and tally what breaks, per file.
#
# Nothing is linked or run — `-fsyntax-only` is enough, because the question is
# "which symbols does this engine want that GLES does not have", and that is a
# compile-time answer. See shim/OpenGL.h for how the vendored sources are
# redirected onto GLES without editing them.
#
#   sh audit.sh            # table + summary
#   sh audit.sh <file.cpp> # full error output for one TU
set -e

NDK=${NDK:-$HOME/Library/Android/sdk/ndk/27.1.12297006}
HOSTTAG=${HOSTTAG:-darwin-x86_64}
CXX="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin/aarch64-linux-android26-clang++"

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/vendor/fluxus/libfluxus/src"
OUT=${OUT:-/tmp/gles-audit}

# Same defines the real libfluxus_min target uses, so we measure the code as it
# is actually built — not some other configuration of it.
# -include, not -I: the engine says #include "OpenGL.h", and a quoted include
# resolves next to the including file first, so an -I shim can never win. Forcing
# ours in first works because it claims the same __OPENGL_H__ guard, which turns
# the vendored header into a no-op. -ferror-limit=0 or clang stops counting at 20.
FLAGS="-fsyntax-only -std=c++17 -w -ferror-limit=0 -I$SRC \
  -DGL_SILENCE_DEPRECATION -DFLUXUS_MAJOR_VERSION=0 -DFLUXUS_MINOR_VERSION=18 \
  -DFLUXUS_MINIMAL_NO_PNG -DGLSL -DFLUXUS_ENABLE_VBO"

mkdir -p "$OUT"

# One file, verbose — for digging into a specific failure.
if [ -n "$1" ]; then
  exec "$CXX" $FLAGS -c "$SRC/$1"
fi

# The TU list comes from the real build fragment, so this can never drift from
# what libfluxus_min actually compiles.
SOURCES=$(sed -n 's|.*\${FLUXUS_MIN_SRC_DIR}/\([A-Za-z0-9_]*\.cpp\).*|\1|p' \
          "$ROOT/cmake/libfluxus_min.cmake")

printf '%-28s %8s  %s\n' FILE ERRORS "MISSING GL SYMBOLS"
printf '%s\n' "----------------------------------------------------------------------"

clean=0; broken=0
: > "$OUT/all-symbols.txt"
for f in $SOURCES; do
  "$CXX" $FLAGS -c "$SRC/$f" > "$OUT/$f.log" 2>&1 || true
  n=$(grep -c "error:" "$OUT/$f.log" || true)
  # every gl*/GL_* the compiler says does not exist, deduplicated
  syms=$(grep -o "use of undeclared identifier '[A-Za-z0-9_]*'" "$OUT/$f.log" \
         | sed "s/.*'\(.*\)'/\1/" | sort -u | tr '\n' ' ')
  [ -z "$syms" ] && syms=$(grep -o "no member named '[A-Za-z0-9_]*'" "$OUT/$f.log" \
         | sed "s/.*'\(.*\)'/\1/" | sort -u | tr '\n' ' ')
  echo "$syms" | tr ' ' '\n' >> "$OUT/all-symbols.txt"
  if [ "$n" = "0" ]; then
    clean=$((clean+1)); printf '%-28s %8s  %s\n' "$f" 0 "— compiles clean"
  else
    broken=$((broken+1)); printf '%-28s %8s  %s\n' "$f" "$n" "$syms"
  fi
done

echo
echo "clean: $clean   broken: $broken   (logs in $OUT)"
echo
echo "most-wanted missing symbols across the engine:"
grep -v '^$' "$OUT/all-symbols.txt" | sort | uniq -c | sort -rn | head -25
