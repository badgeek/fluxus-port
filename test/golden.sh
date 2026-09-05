#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Golden-image regression guard for the renderer.
#
# Renders each sketch in test/golden/cases/ in the real app and compares the
# screenshot against a recorded baseline, pixel for pixel. Its whole purpose is
# to prove that a refactor changed NOTHING on macOS — which is what makes the
# GLES seam work (ROADMAP.md, "Android") safe to do in shared engine code.
#
#   sh test/golden.sh            # check against the baselines
#   sh test/golden.sh --update   # (re)record the baselines
#
# Baselines are per-machine: same GPU, same driver, exact match. Do not commit
# a baseline recorded elsewhere and expect it to pass here.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
CASES="$HERE/golden/cases"
EXPECT="$HERE/golden/expect"
OUT=${OUT:-/tmp/fluxus-golden}
APP=${APP:-$ROOT/build/FluxusApp_artefacts/Release/FluxusApp.app/Contents/MacOS/FluxusApp}
PORT=${FLUXUS_CONTROL_PORT:-8031}
CLI="$ROOT/cli/fluxus"
SETTLE=${SETTLE:-1.5}     # seconds to let the sketch load and a frame render

[ -x "$APP" ] || { echo "no app at $APP — build FluxusApp first"; exit 1; }
mkdir -p "$OUT" "$EXPECT"

update=0
[ "$1" = "--update" ] && update=1

# One app instance for every case: launching is the slow part, and a fresh
# process also guarantees a known camera/state for each run.
FLUXUS_CONTROL_PORT=$PORT "$APP" > "$OUT/app.log" 2>&1 &
APP_PID=$!
trap 'kill $APP_PID 2>/dev/null || true' EXIT

# Wait for the control port rather than sleeping a guessed amount.
for i in $(seq 1 60); do
  if FLUXUS_CONTROL_PORT=$PORT "$CLI" error >/dev/null 2>&1; then break; fi
  sleep 0.5
done

fail=0
for case in "$CASES"/*.scm; do
  name=$(basename "$case" .scm)
  FLUXUS_CONTROL_PORT=$PORT "$CLI" load "$case" >/dev/null
  sleep "$SETTLE"
  FLUXUS_CONTROL_PORT=$PORT "$CLI" shot "$OUT/$name.png" >/dev/null

  if [ "$update" = "1" ]; then
    cp "$OUT/$name.png" "$EXPECT/$name.png"
    printf '%-20s recorded\n' "$name"
    continue
  fi
  if [ ! -f "$EXPECT/$name.png" ]; then
    printf '%-20s NO BASELINE (run with --update)\n' "$name"
    fail=1
    continue
  fi
  printf '%-20s ' "$name"
  python3 "$HERE/pngdiff.py" "$EXPECT/$name.png" "$OUT/$name.png" \
          --diff "$OUT/$name.diff.png" || fail=1
done

echo
if [ "$update" = "1" ]; then
  echo "baselines written to $EXPECT"
elif [ "$fail" = "0" ]; then
  echo "all cases identical"
else
  echo "REGRESSION — see $OUT/*.diff.png (differing pixels in red)"
fi
exit $fail
