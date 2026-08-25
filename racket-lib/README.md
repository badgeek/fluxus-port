# racket-lib — fluxus `.ss` compatibility layer

Proves that **real, unmodified fluxus `.ss` library files load and run** on modern
Racket (CS) with our engine — the payoff of the Racket backend track.

## What's here

- **`fluxus-engine.ss`** — the module the fluxus `.ss` library requires. Provides
  the engine primitives (`build-cube`, `translate`, `vx`/`vy`/`vz`, matrix
  queries, …). In the CLI proof they are stubs; in the app they become FFI
  wrappers over the `flux_*` C functions.
- **`fluxus-audio/osc/midi/openal.ss`** — empty stub modules (fluxus-modules.ss
  requires them).
- **`fluxus-modules.ss`** — the real fluxus aggregator (re-exports the engine +
  the stub modules), used verbatim.
- **`building-blocks.ss`** — MINIMAL stub (vector/matrix maths only). The real one
  is 918 lines of macros/generators; stubbed so the leaf libraries load. Real one
  can be dropped in once its full primitive surface is bound.
- **`maths.ss`, `shapes.ss`, `randomness.ss`, `tasks.ss`, `time.ss`** — the REAL,
  unmodified fluxus source files (copied from `vendor/.../modules/scheme`).

## Proof (racket CLI)

```sh
R=$(brew --prefix minimal-racket)/bin/racket   # or the versioned Cellar path
$R -e '(require (file "maths.ss"))' \
   -e '(displayln (vmix (vector 1 0 0) (vector 0 0 1) 0.5))'
#   => #(0.5 0 0.5)                             REAL fluxus maths.ss, running

$R -e '(require (file "shapes.ss"))' \
   -e '(for-each displayln (build-circle-points 6 0.5))'
#   => six #(x y 0) points on a radius-0.5 circle    REAL fluxus shapes.ss
```

Both load clean and compute correctly. `maths.ss` (223 lines) and `shapes.ss`
run unmodified.

## Significance

Modern Racket parses and runs the fluxus `.ss` module system as-is. The only work
to make fluxus's library run **in the app** is binding the engine primitives the
`.ss` files call — via the same `get-ffi-obj` → `flux_*` mechanism already proven
in `RacketScriptHost`. i.e. the path from "our commands" to "the real fluxus
library" is now clear and demonstrated, not hypothetical.

## WIRED INTO THE APP ✅

`fluxus-engine.ss` now FFI-wires the real commands to the `flux_*` C engine (each
`get-ffi-obj` has a failure-thunk, so this file still loads standalone on the CLI
as stubs). `RacketScriptHost` loads this library at init
(`(require (file ".../fluxus-modules.ss") (file ".../maths.ss") (file ".../shapes.ss"))`),
so user scripts get the FFI commands **and** the real `.ss` library functions.

**Proven in `FluxusRacketApp`:** its starter calls `(build-circle-points 10 2.5)`
— the genuine `shapes.ss` function — and renders 10 cubes in a circle, each built
via FFI → libfluxus → GL, spinning by `(time)`. i.e. real fluxus `.ss` code drives
the engine in a live JUCE window.

## REAL building-blocks.ss unlocked ✅

The genuine 918-line `building-blocks.ss` now loads and runs on our engine —
`with-primitive`, `pdata-map!`, `pdata-index-map!`, `pdata-fold`, `vx`/`vy`/`vz`,
etc. Proven in `FluxusRacketApp`: a torus deformed live by the real
`(with-primitive … (pdata-index-map! (lambda (i p) …) "p"))`.

**Minimal edits to make it load on modern Racket** (its module `provide`s trip the
stricter re-import rules the old PLT Scheme didn't have):
- `building-blocks.ss` `provide`: dropped the engine re-exports it doesn't define
  (`vadd vsub mmul madd msub mdiv`, `shader-set!`) — those come from the engine
  directly, so re-exporting them double-imports. Kept its own `vmul`/`vdiv`.
- `fluxus-engine.ss`: `(except-out … vx vy vz vw with-state with-primitive)` +
  don't stub `pdata-map!`/`pdata-index-map!` (building-blocks defines them).

That's it — no changes to the *logic* of any fluxus file, only which names each
module exports. The full building-blocks command surface is now available to
scripts (RacketScriptHost requires it).

## Next

- Load more of the vendored `.ss` library (poly-tools, camera, randomness) the
  same way — each needs only its `provide` reconciled + any prims it calls bound.
