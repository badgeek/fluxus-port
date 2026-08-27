---
name: fluxus-perf
description: >
  Diagnose and fix Fluxus performance — high CPU while a sketch runs, or slow app
  startup/load. Use when asked "why is the app/sketch using so much CPU", "it's
  slow", "high CPU", "laggy", "optimize this sketch", "the app takes forever to
  start / load", or to profile the render loop. Covers the measure-correctly
  method, the profiler recipe, and the two known big wins (retained + (clear) for
  per-frame cost; compiled-file-roots for startup).
---

# Fluxus performance

Two different problems. Diagnose which one first — they have different fixes.

## Always: measure steady-state, not startup

Right after launch the process pegs ~99% for **15–25s** while Racket loads — that
is NOT the sketch. Wait for full load, then measure:

```bash
P=$(pgrep -f 'MacOS/FluxusRacketApp' | head -1)
for i in 1 2 3 4; do ps -o %cpu= -p $P; sleep 2; done
```

An idle/empty sketch settles to ~10%. If a specific sketch stays high (e.g.
~90%), that's a per-frame cost → below.

## Per-frame CPU (sketch stays high after load)

Root cause is almost always that **immediate mode re-`read`s + re-compiles the
WHOLE script every frame** — every top-level `define`, `string-append`, list, and
the every-frame body — and the allocation churn feeds Racket's GC. Confirm with a
profile of the render thread:

```bash
sample $P 2 -mayDie 2>/dev/null > /tmp/samp.txt
grep -nE "OpenGL Renderer|renderFrame|RacketScriptHost::eval|Scall2|S_gc|Render\(|Nurbs|gluN|build_text" /tmp/samp.txt | head
```

The tell: the "OpenGL Renderer" thread sits in `renderFrame → RacketScriptHost::
eval → Scall2` (with a slice in `S_do_gc`), while actual GL drawing
(`Renderer::Render`, NURBS tessellation, `glDrawArrays`) is only a few percent.

**Fix — retained mode + real `(clear)`:**
```scheme
(retained)                              ; compile the buffer ONCE
(every-frame
  (clear) (background (vector 0 0 0))   ; wipe + repaint each frame (no re-parse)
  … rebuild the scene as usual …)
```
`(retained)` makes the host compile the program once and per frame run only the
every-frame thunk; `(clear)` is a genuine `Renderer::Clear`, so the thunk still
clears + rebuilds every frame (typing, rotation, etc.) — same visuals, no
per-frame re-parse. The deck went ~96% → ~20% this way. Verify the output is
unchanged (screenshot — watch for primitive accumulation/smearing, which means a
`(clear)` is missing). Do NOT use retained without a per-frame `(clear)` for a
rebuild-each-frame sketch, or geometry accumulates.

Secondary levers if still heavy: fewer per-frame primitives; a poly `build-sphere`
instead of `build-nurbs-sphere` (GLU re-tessellates the NURBS every frame);
smaller vertex counts; don't recompute constants (wrapping, layout) per frame.

## Slow startup / load

If the WINDOW takes ~25s to appear, it's the embedded Racket recompiling the
collects. This is already fixed in `RacketScriptHost::init` (it points
`current-compiled-file-roots` + `use-compiled-file-paths` at the install's
compiled-collects root, matching the `racket` CLI) — startup is ~4s. If it
regresses, check that block. Time the phases by temporarily adding
`fprintf(stderr, …)` around boot / `racket/base` / `ffi/unsafe` / the `.ss`
require in init; the `.ss` require should be <1s, not ~22s. `make precompile`
builds `racket-lib/compiled/*.zo` for the last ~1s.
