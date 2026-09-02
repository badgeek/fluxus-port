---
name: fluxus-calibrate
description: >
  Visually self-calibrate a Fluxus sketch's on-screen layout. Use whenever a
  sketch needs precise positioning (text, terrain, guides) at a specific window
  size / aspect ratio — especially vertical Instagram (9:16) content — or when
  the request is to "screenshot the app and check it looks right", "make the
  text/terrain line up", "verify positioning", "resize the window from code", or
  to iterate on a drawing until it matches an intended frame. Has two modes:
  human-verdict (user watches the live window and answers one-line questions —
  the cheap default when the user is present) and self-Read (agent reads
  screenshots — fallback when the user is away or asks for self-verification).
---

# Fluxus visual self-calibration

The engine can resize its own window and grab its own framebuffer from Scheme, so
you can close the loop: **draw → screenshot from code → Read the PNG → adjust →
repeat**, with no external screen capture and no window-focus problems.

## MODE CHOICE — human-verdict first (cheap), self-Read second (expensive)

Each screenshot Read costs ~1.1–1.6k tokens and a typical calibration takes 5–15
rounds. When the user is present, their eyes are free — so **default to
human-verdict mode** and only fall back to Reading PNGs yourself when they are
away or explicitly ask you to self-verify.

### Human-verdict loop

1. Launch the app once with the sketch (section 3 below, but SKIP the screenshot
   plumbing — no `(screenshot …)` in the sketch, no `rm`/stat-poll, no Read).
   Keep the window visible on the user's screen. Launch with
   `FLUXUS_CONTROL_PORT=8020` so step 2's live reload works.
2. After each edit, reload in place — `cli/fluxus load <file>` (never `eval`) —
   so the change appears live in ~a second. No relaunch.
3. Ask the user for a verdict in ONE line, and make the question concrete so the
   answer is actionable: not "does it look right?" but
   "Check the live window: (a) title baseline on the top-third line? (b) terrain
   band clipped at either edge? (c) circle resting on the horizon?"
4. Translate the verdict into a numeric edit (positions are deterministic —
   calibrate by construction, section on lessons below), reload, ask again.
5. Repeat until the user says good. **Zero PNG Reads for the whole loop.**
   Optionally finish with a single confirming screenshot+Read if the result must
   be archived or the task demands machine verification.

Verdict prompts work best when the user can answer with a direction + rough
magnitude ("title ~10% too low", "clips on the left"). If an answer is vague
("looks off"), ask for ONE specific: which element, which direction.

When several parameter values are plausible, render them side by side in ONE
frame (offset each variant along x) and ask "left, middle, or right?" — one
verdict replaces a whole convergence loop.

### Self-Read loop (fallback — user away / explicit ask)

Use the original screenshot→Read cycle below, and keep it cheap: iterate at the
small window size (540x960), crop mentally to the element under adjustment, and
stop as soon as the layout constraint is met rather than polishing pixels you
were not asked about.

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
- **Ribbon strokes are CENTRELINE geometry and "w" is the HALF-width** (the
  stroke extends w to each side of the path). So two ribbon shapes tangent at
  their paths visually overlap by a full stroke; for an outline circle (radius r,
  width wc) resting ON a rule (width wr), the centre is `rule_y + r + wc + wr` —
  not `+ r`, and not `+ half` of each. Verified by human verdict on
  examples/high-risk.scm (sank at +0, still sank at +halves, sat at +full).

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
