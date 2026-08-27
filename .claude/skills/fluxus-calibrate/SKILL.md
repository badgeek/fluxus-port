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
rm -f /tmp/fluxus-cap.png
pkill -f "MacOS/FluxusRacketApp"; sleep 1
FLUXUS_SCRIPT="$PWD/examples/<sketch>.scm" \
  build/FluxusRacketApp_artefacts/Release/FluxusRacketApp.app/Contents/MacOS/FluxusRacketApp \
  >/tmp/f.log 2>&1 &
# wait for the file the sketch writes (engine + first eval take a few seconds)
for i in $(seq 1 20); do sleep 1; [ -f /tmp/fluxus-cap.png ] && break; done
```

Then **Read `/tmp/fluxus-cap.png`** and compare against the intended layout. Adjust
the sketch, change the screenshot filename (or `rm` the old one), relaunch, Read
again. The app takes a few seconds to load — a too-early grab is black, which is
why the sketch guards on `(time)` and the shell waits for the file to appear.

`scripts/shoot.sh [APP] [OUT] [WAIT]` is a fallback that focuses the app bundle
and uses macOS `screencapture` (captures window chrome; needs the app frontmost).
Prefer the in-engine `(screenshot …)` — it captures exact pixels at the render
resolution with no chrome.

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

## Notes
- `build-text` pen advance is `CW = 0.44` per char; a line's world width is
  `CW * length * scale`. Centre text by translating x by `-0.5 * width`.
- Only the JUCE-editor apps resize from code (`set-window-size` is applied by the
  component's message-thread timer). `(screenshot …)` works in every app.
