#!/usr/bin/env bash
# Load every examples/*.scm through the fluxus Racket library to catch unbound
# identifiers / binding breakage. On the CLI the flux_* FFI resolve to their
# failure-thunk stubs, so this validates the SCHEME surface (names + syntax), not
# rendering. Exit non-zero if any example fails to load.
#
# Usage: scripts/check-examples.sh   (run from the repo root)

set -u
RK="${RK:-/opt/homebrew/Cellar/minimal-racket/9.3/bin/racket}"
cd "$(dirname "$0")/.." || exit 2

LIB='(require (file "racket-lib/fluxus-modules.ss")
              (file "racket-lib/building-blocks.ss")
              (file "racket-lib/maths.ss")
              (file "racket-lib/randomness.ss")
              (file "racket-lib/poly-tools.ss")
              (file "racket-lib/shapes.ss"))'

fail=0
for f in examples/*.scm; do
  out=$("$RK" -e "$LIB" -e "(load \"$f\")" -e '(void)' 2>&1)
  if [ $? -eq 0 ]; then
    printf 'OK   %s\n' "$f"
  else
    printf 'FAIL %s\n' "$f"
    printf '%s\n' "$out" | sed 's/^/     /'
    fail=1
  fi
done

# also verify the full combined library require still loads clean
combo=$("$RK" -e "$LIB
        (require (file \"racket-lib/input.ss\") (file \"racket-lib/camera.ss\")
                 (file \"racket-lib/mouse.ss\") (file \"racket-lib/help.ss\")
                 (file \"racket-lib/pixels-tools.ss\") (file \"racket-lib/voxels-tools.ss\")
                 (file \"racket-lib/planetarium.ss\") (file \"racket-lib/collada-import.ss\"))" \
        -e '(void)' 2>&1)
if [ $? -eq 0 ]; then printf 'OK   (full library require)\n'; else printf 'FAIL (full library require)\n%s\n' "$combo"; fail=1; fi

exit $fail
