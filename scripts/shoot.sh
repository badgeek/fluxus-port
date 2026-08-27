#!/usr/bin/env bash
# shoot.sh — bring a Fluxus app to the front and screenshot it, for visual
# self-calibration of a sketch's layout.
#
#   scripts/shoot.sh [APP] [OUT] [WAIT]
#     APP   app name or .app bundle path (default FluxusRacketApp)
#     OUT   output png                    (default /tmp/fluxus-shot.png)
#     WAIT  seconds to wait after focus    (default 3; the engine + first eval
#           take a moment, so a too-early grab is black)
#
# Focus strategy: `open` the .app bundle reliably raises it WITHOUT needing the
# Accessibility grant. If APP is a bare name we look for the built bundle. If
# Accessibility IS granted we crop to the window rect; otherwise we grab the main
# display (the app is frontmost + centred, so it's clearly visible).

set -u
APP="${1:-FluxusRacketApp}"
OUT="${2:-/tmp/fluxus-shot.png}"
WAIT="${3:-3}"
cd "$(dirname "$0")/.." || exit 2

# resolve a bundle path to `open`
name="$APP"; bundle="$APP"
if [[ "$APP" != *.app ]]; then
  name="$APP"
  bundle="build/${APP}_artefacts/Release/${APP}.app"
else
  name="$(basename "$APP" .app)"
fi
[[ -d "$bundle" ]] && open "$bundle" >/dev/null 2>&1

sleep "$WAIT"

# try an accessibility crop (System Events); fall back to the main display
bounds=$(osascript <<EOF 2>/dev/null
tell application "System Events" to tell process "$name"
  set p to position of window 1
  set s to size of window 1
  return (item 1 of p) & "," & (item 2 of p) & "," & (item 1 of s) & "," & (item 2 of s)
end tell
EOF
)
if [[ "$bounds" =~ ^-?[0-9]+,-?[0-9]+,[0-9]+,[0-9]+$ ]]; then
  IFS=',' read -r x y w h <<< "$bounds"
  screencapture -x -R"${x},${y},${w},${h}" "$OUT"
  echo "$OUT  window=${w}x${h}"
else
  screencapture -x -D 1 "$OUT"
  echo "$OUT  (main display; grant Accessibility to crop to the window)"
fi
