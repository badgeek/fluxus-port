# HANDOVER — perf + correctness overhaul (Sep 4–5, 2026)

What changed, how it was proven, how to keep it healthy, and what's left.
Companion docs: `CLAUDE.md` (gotchas, build), `examples/DRAWING.md` (21 drawing
gotchas), `DESIGN.md` (architecture seams).

---

## 1. What was done (by commit)

### Performance — script-host seam (measured, not guessed)
| Commit | Change | Measured win |
|---|---|---|
| `8b62bbc` | pdata channel cache (C-side, per-grab) + bulk `flux_pdata_get3/set3` + compile-once immediate mode + GC-locked runner closures (`racket_apply`) | pdata C layer 4.3× (32→7.3 ns/call); immediate-mode sketch CPU ~2× (23%→12%) |
| `48f3930` | whole-channel `pdata-map!` (`read_all`/`write_all`, 2 FFI crossings per channel per map) | `pdata-map!` end-to-end 4.1× total (363→88 ms / 20 passes, 24k verts) |
| `c1518db` | `(build-merged ids)` — bake N static polys into ONE prim (render with `(hint-vertcols)`, destroy sources) | 400 cubes: draw cost ~6× down; the "fewer draws" lever from the perf notes made directly usable |

### Refactors (behavior-identical, verified by the same benches)
| Commit | Change |
|---|---|
| `c93c504` | pdata accessors deduped through one `withChannel` visitor |
| `51370b7` | `FluxusCommands.cpp` (2351 lines) split into 7 domain TUs + `FluxusCommandsInternal.h` |
| `f124c20` | `fluxus_core`/`fluxus_render` static libs — shared JUCE-free TUs compile once. **MUST stay `-force_load`** (see §3) |
| `f14b370` | `FluxusScene::renderFrame` extracted into per-phase helpers |
| `40fa1a8` | reprojection math routed through `dMatrix` (after its inverse was fixed) |

### Math correctness — 16 bugs, all measured-first, all guarded
| Commit | Bug |
|---|---|
| `c2a4728` | `dMatrix::inverse()` divided the adjugate by det(adjugate)=det³ AND skipped row 4 via `scale(s,s,s)` — both errors cancel at det==1, which hid it for ~20 years |
| `0808469` | `dQuat`: dot typo (`z*q.x`), renorm missing sqrt, slerp discarding its own orthonormal component |
| `3650b83` | `dQuat` from-matrix ctor returned the CONJUGATE; `toAxisAngle` returned radians vs degree input |
| `21bdf42` | `dMatrix::aim` rolled the frame 180° (up pointed down) |
| `979baed` | `get_scale`/`remove_scale` read columns instead of rows |
| `b646a00` | `extract_euler` rewritten (transposed + negated + no atan2) |
| `e67cace` | `roty` built rotation by −a |
| `9f6d7ef` | `dBoundingBox(min,max)` left `m_Empty` uninitialised (clang trapped on it) |
| `3d69e12` | Racket `mmul` was arity-2 — poly-tools extrude completely broken on the Racket host |
| `d7c3766` | Racket `(maim)` was an identity stub — extrusions built with identity aim |
| `9c65eba` | Racket `mmul` composed in STANDARD order while engine/upstream use the reversed convention — every upstream matrix chain composed backwards. Convention: **`(mmul A B)` applies B first (rightmost first)** |
| `220f4da` | `with-state` only saved transform+colour — `(parent id)`, hints, wire-colour, texture, shader etc. LEAKED. Now pushes the whole build state (upstream parity) |

Guards (run these, they exist because every one of them caught something):
```sh
./build/math_test                       # 94 checks: dMatrix/dQuat/Geometry vs ground truth
racket spikes/math-test/mmul-order.rkt  # composition-order convention guard
./build/pdata_bench 50                  # pdata layer correctness + perf (headless)
```

### New demo
`examples/gimbal-lock.scm` — euler vs quaternion under identical inputs; the
euler nose-trail collapses at pitch 90°, the quat trail keeps looping. Doubles
as an end-to-end quat/matrix/node sanity sketch.

---

## 2. Guidelines — the method that found 16 bugs (keep using it)

1. **Measure before fixing, numbers before reading.** Every bug here was
   diagnosed from its numeric signature (error ratio exactly det² → wrong
   determinant source; global pos = local + another node's → parenting, not
   matrices; `normal·dir` 0.0 vs 1.0 → composition order). Layout/convention
   questions (row/column, memcpy vs load) are NEVER settled by reading code —
   write a 10-line probe and print the discriminating number.
2. **Test-first on vendored code.** Write the oracle check into
   `spikes/math-test/` FIRST; if it passes, keep it as a guard; if it fails,
   fix with a `fluxus->JUCE port:` comment explaining the original bug.
3. **Prove "was-correct stays identical".** Before changing semantics, grep
   callers; show that previously-working call sites are bit-identical (e.g.
   det==1 paths) or dead. That's what made 16 fixes land with zero visual
   regressions.
4. **Verify visually, per change.** Screenshot inside the every-frame thunk of
   a /tmp COPY of a sketch, `cli/fluxus load` (never `eval` — CLAUDE.md gotcha
   5), Read the PNG. The constraint set that must keep rendering correct:
   `iso-city.scm`, `camera-node.scm`, `quaternion-demo.scm`, `gimbal-lock.scm`,
   the helix-extrude tube.
5. **Perf claims need paired measurement.** `ps` averaged across runs is noise
   (CLAUDE.md); prefer in-sketch timers, `pdata_bench`-style micro benches, or
   paired on/off in one app instance.
6. **Delegating to workers**: write a self-contained brief file with (a) the
   repo gotchas that WILL bite (stale `.zo`, screenshot once-per-path is
   app-lifetime, other sessions' dirty files), (b) measure-first method order,
   (c) an explicit verify list incl. screenshots, (d) commit conventions.
   Require the report to include the measured evidence. It worked 6 times in a
   row here (split, static libs, scene cleanup, dada audit, mmul order,
   with-state leak).

### Sharp edges to respect (new ones from this work)
- **`fluxus_core`/`fluxus_render` must stay whole-archive (`-force_load`)**:
  Racket resolves `flux_*` by dlsym at runtime — the linker can't see it and
  will strip "unused" TUs; bindings then silently become no-op stubs.
  Canary: `nm <app> | grep flux_vadd`.
- **`(mmul A B)` = engine order, rightmost first.** Guarded by
  `mmul-order.rkt`; documented at the `mmul` definition.
- **After editing `racket-lib/*.ss`: `make precompile`** or a stale `.zo`
  shadows your edit (top CLAUDE.md gotcha, bit twice this session).
- **`(screenshot path)` fires once per path PER APP LIFETIME** — restart the
  app (not just reload) to reuse a path.
- **`with-state` now restores everything.** Sketches that relied on hint/
  parent leaks would change; defensive pinning stays harmless.
- **`dMatrix::blend` is a cheap component-wise lerp by design** (camera lag) —
  don't "fix" it. Dead-code oddities intentionally left (with reasons) are
  listed in the dada-audit commit `591a115`'s message.

---

## 3. Remaining backlog (priority order)

1. **`BRIEF-midi-osc.md`** — MIDI/OSC input hosts via the `IAudioHost`
   pattern. Worker-ready brief already in repo root.
2. **`BRIEF-remaining-ss.md`** — port the remaining upstream `.ss` library
   files. More attractive now: extrude/maim/mmul actually work.
3. **Scheme-side math sweep** — `maths.ss`/`building-blocks.ss` pure-Scheme
   helpers (lerp, hermite, vector ops) never got the dada treatment. Low risk,
   small worker job, same method.
4. **PBO async `glReadPixels`** — only helps realtime Record Frames; skip
   unless live recording stutters.
5. **Push decision** — main is ~30 commits ahead of origin (this work + the
   NTSC/GPU/camera-node work from the parallel session). Nothing pushed.

## 4. Known state at handover
- Working tree: `CLAUDE.md` (gotcha-7 update, uncommitted on purpose — a
  parallel session owns that file's other edits), `Makefile`,
  `app/RacketScriptHost.cpp`, `examples/iso-city.scm`, `examples/iso-crt.scm`
  dirty from the parallel session; `examples/gimbal-lock.scm` +
  `tools/hand_landmarker.task` + BRIEF files untracked.
- Project memory (`~/.claude/projects/...fluxus-port/memory/`) has entries for
  the stale-`.zo` trap, s7 state persistence, and the mmul-order analysis.
