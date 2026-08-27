---
name: fluxus-calibrate
description: >
  Visually self-calibrate a Fluxus sketch's on-screen layout. Use whenever a
  sketch needs precise positioning (text, terrain, guides) at a specific window
  size / aspect ratio — especially vertical Instagram (9:16) content — or when
  the request is to "screenshot the app and check it looks right", "make the
  text/terrain line up", "verify positioning", "resize the window from code", or
  to iterate on a drawing until it matches an intended frame.
---

# Fluxus visual self-calibration

The engine can resize its own window and grab its own framebuffer from Scheme, so
you can close the loop: **draw → screenshot from code → Read the PNG → adjust →
repeat**, with no external screen capture and no window-focus problems.

## 1. Size the canvas from code

`(set-window-size w h)` resizes the GL content to exactly `w x h`. For vertical
Instagram video use `1080x1920` (or a smaller proportional `540x960` while
iterating). Put it once at the top of the sketch:

```scheme
(set-window-size 540 960)   ; 9:16 vertical
```

The render aspect follows the window automatically if the sketch calls
`(set-fov …)` each frame (front-facing text shows no distortion in perspective).

## 2. Screenshot from code (guarded, one-shot)

`(screenshot "path")` grabs the finished frame (after the post pass) and writes a
PNG. It is **captured once per path** — the engine dedups, so calling it every
frame is safe. Guard it so it fires after the sketch has loaded/settled:

```scheme
(when (> (time) 5.0) (screenshot "/tmp/fluxus-cap.png"))
```

To grab again, use a different filename (e.g. `/tmp/cap-2.png`).

## 3. The loop

```bash
CAP=/tmp/fluxus-cap.png
rm -f "$CAP"
pkill -f "MacOS/FluxusRacketApp"; sleep 1
FLUXUS_SCRIPT="$PWD/examples/<sketch>.scm" \
  build/FluxusRacketApp_artefacts/Release/FluxusRacketApp.app/Contents/MacOS/FluxusRacketApp \
  >/tmp/f.log 2>&1 &
# wait for the file to APPEAR (load + first eval can take 15-25s), then for its
# size to STOP GROWING (the PNG is written in-place; a 0-byte read is mid-write).
for i in $(seq 1 30); do sleep 1; [ -s "$CAP" ] && break; done
prev=0; for i in $(seq 1 10); do s=$(stat -f%z "$CAP" 2>/dev/null||echo 0); \
  [ "$s" = "$prev" ] && [ "$s" -gt 0 ] && break; prev=$s; sleep 1; done
```

Then **Read `$CAP`** and compare against the intended layout. Adjust the sketch,
change the screenshot filename (or `rm` the old one), relaunch, Read again.

`scripts/shoot.sh [APP] [OUT] [WAIT]` is a fallback that focuses the app bundle
and uses macOS `screencapture` (captures window chrome; needs the app frontmost).
Prefer the in-engine `(screenshot …)` — exact pixels, no chrome.

## Calibration lessons (learned in practice)

- **Capture is at RETINA scale.** A `(set-window-size 540 960)` window is grabbed
  at **1080x1920** (2x). So iterate at 540x960 (fast, small window) and the PNG is
  already exact Instagram-vertical export resolution — no separate hi-res pass.
- **Wait for a STABLE, non-zero file.** The grab writes the PNG in place; reading
  the instant it appears can catch a 0-byte truncation. Poll until size > 0 and
  unchanged (snippet above), or just `sleep` a couple seconds after it appears.
- **Load is slow (~15-25s).** The engine + Racket + first eval take a while; guard
  the shot with `(when (> (time) 5.0) …)` and let the shell wait for the file.
- **The grab is the GL scene ONLY** — the JUCE code overlay is a child painted over
  the GL, not part of the framebuffer, so `(screenshot …)` gives clean art with no
  editor text. (For live recording, hide the overlay via View -> Show Editor.)
- **Positioning is deterministic, so calibrate by construction.** E.g. to rest a
  circle of radius r on a horizon line at height y, set its centre to `y + r` —
  don't eyeball it. Screenshot only to confirm.

## Worked example

`examples/bauhaus.scm` — a black & white 9:16 Bauhaus layout (heavy rule, left-set
title, outline circle resting on a horizon, solid square, generative ridgeline
terrain band). Its mark helpers are reusable: `line`, `rect` (solid block),
`circle` (outline via a closed ribbon), `ltext` (left-anchored text), and a
`ridges` band. All positions are fractions of `halfh`/`halfw` off grid margins
`ml`/`mr`, so it re-frames at any window size.

## 4. Coordinate reference

Default camera sits at `(0,0,-10)` looking down +Z. In perspective the visible
region at the text plane `z=0` is:

```
halfh = CAM-DIST * tan(FOV/2 in radians)      ; CAM-DIST = 10
halfw = halfh * aspect                         ; aspect = screen_w / screen_h
; visible x in [-halfw, halfw], y in [-halfh, halfh]
```

So position everything in fractions of `halfh`/`halfw` and it re-frames correctly
at any window size. Get the live aspect with `(get-screen-size)` → `(vector w h)`.

`examples/calib.scm` is a ready calibration frame: it sizes to 9:16 and draws the
centre cross, rule-of-thirds, title-safe margin (90%) and labels. Load it to see
where the safe area and thirds fall, then match your sketch's text/terrain to
those guides.

## Aligning text (build-text is LEFT-anchored)

`build-text` builds glyphs from a left origin, so a bare `(build-text …)` is
left-aligned. A line's world width is `width = CW * length * scale`
(`CW = 0.44`, the pen advance per char). Anchor by computing that width — never a
fixed x, or short strings drift:

```scheme
(define (text-w str h) (* CW (string-length str) (/ h 0.9)))   ; h = cap height
(define (ltext str x  y h) …)            ; left  edge at x
(define (rtext str xr y h) (ltext str (- xr (text-w str h)) y h))  ; RIGHT edge at xr
;; centre: (ltext str (- cx (* 0.5 (text-w str h))) y h)
```

Common bug: a top-right caption placed with a fixed left x looks unaligned when
the text is short — right-align it to the margin with `rtext … mr …` instead.

## Notes
- Only the JUCE-editor apps resize from code (`set-window-size` is applied by the
  component's message-thread timer). `(screenshot …)` works in every app.
