#!/bin/sh
# Spike: embed real Racket (CS) in-process. Requires: brew install minimal-racket
set -e
RK="$(brew --prefix minimal-racket 2>/dev/null || echo /opt/homebrew/Cellar/minimal-racket/9.3)"
# resolve the versioned Cellar path if the opt symlink is used
[ -d "$RK/include/racket" ] || RK="$(ls -d /opt/homebrew/Cellar/minimal-racket/* | head -1)"

echo "Racket: $RK"
clang -I "$RK/include/racket" main.c "$RK/lib/libracketcs.a" \
  -framework CoreFoundation -liconv -lm -lncurses \
  -o racket_spike
echo "built racket_spike"
./racket_spike

echo; echo "=== spike 2: Racket-calls-C ==="
clang -I "$RK/include/racket" bind.c "$RK/lib/libracketcs.a" \
  -framework CoreFoundation -liconv -lm -lncurses \
  -Wl,-export_dynamic \
  -o racket_bind
./racket_bind
