# Golden-image regression guard

Proves a renderer change altered **nothing** on macOS. That is the safety net for
the GLES seam work (`ROADMAP.md`, "Android"), which has to happen in shared
engine code under `vendor/` that all four desktop apps also use.

```sh
sh test/golden.sh            # check every case against its baseline
sh test/golden.sh --update   # (re)record baselines
```

Each case in `cases/` is loaded into a real `FluxusApp` over the control port,
screenshotted with `cli/fluxus shot`, and compared pixel for pixel against
`expect/`. Tolerance is **0**: same machine, same GPU, so a single differing
channel value is a real change and worth looking at. On failure a diff image
lands in `/tmp/fluxus-golden/<case>.diff.png` with the offending pixels in red.

Verified to be worth trusting: five cases come back `maxdelta 0` from a fresh app
instance, and `pngdiff.py` on two deliberately different images reports
`FAIL maxdelta 247` and exits 1 — it is deterministic *and* it actually catches
differences.

## Cases

Chosen to cover the paths the seam work will touch, not to look pretty:

| Case | Covers |
|---|---|
| `poly-solid` | `PolyPrimitive` SOLID — already goes through `Backend()->drawArrays`, so it is the baseline that must not move |
| `poly-wire` | the WIRE pass, which bypasses `IRenderBackend` entirely (`PolyPrimitive.cpp:290-313`) — most likely to shift, and the look most sketches depend on |
| `hidden-line` | solid + wire on one prim: ordering and depth interaction the single-pass cases miss |
| `ribbon-particles` | `RibbonPrimitive` + `ParticlePrimitive`, still drawing with `glBegin` — exactly what ladder step 2 rewrites |
| `vertcols-merged` | indexed draws, `HINT_VERTCOLS` and `build-merged` — the static-scene optimisation |

## Rules for adding a case

- **No `(time)`, no randomness.** A sketch that animates gives a different frame
  every run and the diff is meaningless. Drive from the vertex index or a fixed
  constant.
- Pin the size with `(set-window-size …)` so the grab resolution is stable.
- Don't touch the camera; the default orbit state is part of the baseline.

## Caveats

- **Baselines are per-machine.** Different GPU or driver, different pixels. Do
  not commit someone else's baseline and expect it to pass, and treat CI use as
  needing its own recorded set.
- `pngdiff.py` is standard library only, on purpose: the `magick` on this
  machine is built without a PNG delegate, so `compare` cannot read these files
  and `magick` silently passes raw bytes through while exiting 0.
