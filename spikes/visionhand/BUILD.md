# Vision hand-pose spike (path 2)

Native macOS hand tracking with **Apple Vision** (`VNDetectHumanHandPoseRequest`),
GPU/Neural-Engine accelerated — no mediapipe, no Python, no JUCE. Camera → 21 hand
landmarks → OSC → fluxus. Drives `examples/hand-reactive.scm` unchanged (same OSC
schema as `tools/mediapipe_tracker.py`).

## Build
```sh
clang++ -fobjc-arc -O2 -std=c++17 spikes/visionhand/main.mm -o spikes/build/vision_hand_probe \
  -framework Foundation -framework AVFoundation -framework Vision \
  -framework CoreMedia -framework CoreVideo -framework CoreGraphics
```

## Run
```sh
./spikes/build/vision_hand_probe --mirror         # camera -> udp 127.0.0.1:8000
# flags: --mirror  --secs N (auto-stop)  --host H  --port P
```

Then, with the app running:
```sh
./cli/fluxus load examples/hand-reactive.scm
```
Show your hand → the fluxus window draws the live skeleton, colour shifts
red↔green as you pinch.

## Camera permission (required)
The probe prints `0 fps, 0 with-hand` when the camera is blocked. Grant it:
System Settings → Privacy & Security → Camera → enable your terminal app
(Terminal/iTerm/Ghostty), then fully quit + reopen that terminal and re-run.
A non-bundled CLI tool inherits the **terminal's** camera TCC grant.

## Notes
- Vision picks GPU/ANE automatically (this is the GPU path mediapipe's macOS pip
  wheels can't provide — their Metal delegate crashes).
- Vision hand pose is **2D** (no depth) → landmark z is sent as 0 (flat skeleton).
  MediaPipe gives z; if depth matters, keep the mediapipe CPU tracker for z.
- Vision joints are remapped into MediaPipe landmark order in `mpJointOrder()` so
  the demo's bone topology lines up.
- Next step (productionise): move this into an in-app `app/VisionHost.mm`
  (`IVisionHost`, AudioHost pattern) pushing landmarks straight into
  `FluxusCommands` — no OSC hop, bind `(hand-count)` / `(hand i joint)`.
