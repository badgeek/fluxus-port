# Spike: embed real Racket (CS) in-process

Answers: *is embedding modern Racket into the app actually viable?* — as a possible
`IScriptHost` backend that could run fluxus's real `.ss` library.

## Result: YES, it embeds and runs

Built against **minimal-racket 9.3 [cs]** (`brew install minimal-racket`).
`main.c` links `libracketcs.a`, boots the runtime from the 3 boot files
(`petite.boot`, `scheme.boot`, `racket.boot`), requires `racket/base`, and evals
Scheme from C. Output:

```
booted libracketcs OK
raw Sfixnum(42) no-eval -> 42  [fixnum returned to C]
  hello from racket ~ 42          <- real Racket display + (* 6 7), in-process
...
```

Real Racket arithmetic, `display`, and library code (`racket/list`) execute
in-process. **Embedding is viable.**

## Spike 2 (`bind.c`): Racket-calls-C — the fluxus binding pattern — WORKS

C exposes fake "engine" primitives (`spike_cube`, `spike_colour`, `spike_add`);
Racket calls them via `ffi/unsafe`. Registered with `Sregister_symbol(name, ptr)`
+ exported for dlsym (`-Wl,-export_dynamic`); looked up with `get-ffi-obj`. Output:

```
  [C ENGINE] colour      (0.90, 0.50, 0.20)      <- Scheme drove C
  [C ENGINE] build-cube  at (0.0, 0.0, 0.0)
  [C ENGINE] build-cube  at (2.0, 0.0, 0.0)      <- from a Scheme (for ...) loop
  [C ENGINE] build-cube  at (4.0, 0.0, 0.0)
  [racket] (spike_add 40 2) returned: 42          <- C return value back in Scheme
```

Args marshal C→Scheme, returns Scheme←C — **both directions**. This is exactly
how `fluxus-engine` would expose the C++ renderer to Scheme. Gotcha found: Racket
`(* 0 2.0)` is exact `0`, which `_double` rejects — use `exact->inexact`.

**So a Racket backend is genuinely viable**: boot works, eval works, and the
Scheme-drives-C binding pattern works. The remaining work is real but mechanical —
register the ~hundreds of engine primitives (via a C shim + `get-ffi-obj`, or port
fluxus's binding files), and ship the ~50MB runtime.

## What we learned (the real costs)

- **Links + boots fine** on macOS arm64. Link flags: `libracketcs.a` +
  `-framework CoreFoundation -liconv -lm -lncurses`.
- **Ships a big runtime**: the 3 boot files total **~50MB** must travel with the
  app (or be embedded into the binary via `racket_embedded_load_*`).
- **CS embedding API differs from BC** (`racketcs.h`: `racket_boot`,
  `racket_eval`, `racket_namespace_require`). No `scheme_eval_string` — eval a
  string by building `(eval (read (open-input-string s)))` as a Chez datum.
- **Value marshalling back to C** needs care: `racket_eval` returns Racket-level
  values (not raw Chez fixnums). But fluxus's model is **Racket-calls-C**
  (register primitives / FFI), so C-reads-return matters less than exposing C
  functions to Racket — that path (Chez `foreign-procedure` / Racket FFI) is the
  next thing to prove for a real fluxus backend.
- **Modern Racket parses fluxus `.ss` modules** (tested: `maths.ss` loads until it
  needs the `fluxus-engine` C module). So the `.ss` library is largely
  compatible — the missing piece is exposing the engine primitives to Racket.

## Verdict for the port

Embedding real Racket is **feasible but heavy** (big runtime, CS-style bindings to
write). It becomes worth it only if *running existing fluxus `.ss` scripts
unchanged* is a goal. For a lightweight live-coding tool, s7 remains the better
default; a Racket backend can slot behind `IScriptHost` later if needed.

## Build

```sh
brew install minimal-racket
./build.sh
```
