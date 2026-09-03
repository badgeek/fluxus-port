#!/usr/bin/env python3
"""MediaPipe Hands -> OSC bridge for the fluxus->JUCE port.

Streams hand landmarks to fluxus over OSC (path 1: out-of-process tracker, no C++
build changes). fluxus reads them with (osc-source 8000) + (osc "/hand/0" i).

OSC schema (all float32):
  /hands            <count>
  /hand/<h>         63 floats = 21 landmarks x (x, y, z), MediaPipe order/units
                    (x,y normalised 0..1 in image space; z relative depth)
  /pinch/<h>        thumb_tip(4)-index_tip(8) euclidean distance (normalised)

Uses the mediapipe 0.10.x `solutions.hands` API (CPU). NOTE: mediapipe's macOS
pip wheels have no working GPU/Metal delegate (the Tasks GPU path crashes in
DrishtiMetalHelper) — CPU is real-time for hands anyway. For GPU/Neural-Engine
tracking on macOS use the native Apple Vision host (path 2).

Deps:  uv venv --python 3.12 && uv pip install "mediapipe==0.10.14" "numpy<2" opencv-python
Run:   python3 tools/mediapipe_tracker.py [--cam 0] [--mirror] [--secs N] [--headless]
"""
import argparse, socket, struct, math, sys, time

def _pad(b): return b + b"\0" * ((4 - len(b) % 4) % 4)
def osc_encode(addr, *args):
    a = _pad(addr.encode() + b"\0")
    t = _pad(("," + "f" * len(args)).encode() + b"\0")
    return a + t + b"".join(struct.pack(">f", float(x)) for x in args)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--cam", type=int, default=0)
    ap.add_argument("--max-hands", type=int, default=2)
    ap.add_argument("--complexity", type=int, default=1, help="model_complexity 0|1")
    ap.add_argument("--mirror", action="store_true", help="flip x (selfie view)")
    ap.add_argument("--secs", type=float, default=0.0, help="auto-stop after N seconds (0=forever)")
    ap.add_argument("--headless", action="store_true", help="no preview window")
    a = ap.parse_args()

    try:
        import cv2
        import mediapipe as mp
    except ImportError as e:
        print(f"missing dep: {e}\n  uv pip install 'mediapipe==0.10.14' 'numpy<2' opencv-python", file=sys.stderr)
        return 1

    hands = mp.solutions.hands.Hands(
        max_num_hands=a.max_hands,
        model_complexity=a.complexity,
        min_detection_confidence=0.6,
        min_tracking_confidence=0.5)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dst = (a.host, a.port)
    cap = cv2.VideoCapture(a.cam)
    if not cap.isOpened():
        print(f"cannot open camera {a.cam} (grant camera permission to the terminal app)", file=sys.stderr)
        return 1
    print(f"tracking cam {a.cam} -> osc udp://{a.host}:{a.port}"
          f"{'  (headless)' if a.headless else '  (q to quit)'}")

    t0 = time.time(); frames = 0; seen = 0
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                continue
            if a.mirror:
                frame = cv2.flip(frame, 1)
            res = hands.process(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
            hs = res.multi_hand_landmarks or []
            if hs: seen += 1
            sock.sendto(osc_encode("/hands", float(len(hs))), dst)
            for h, lm in enumerate(hs):
                flat = []
                for p in lm.landmark:
                    flat += [p.x, p.y, p.z]
                sock.sendto(osc_encode(f"/hand/{h}", *flat), dst)
                t, i = lm.landmark[4], lm.landmark[8]
                pinch = math.dist((t.x, t.y, t.z), (i.x, i.y, i.z))
                sock.sendto(osc_encode(f"/pinch/{h}", pinch), dst)
            frames += 1
            if not a.headless:
                for lm in hs:
                    for p in lm.landmark:
                        cv2.circle(frame, (int(p.x*frame.shape[1]), int(p.y*frame.shape[0])), 4, (0,255,0), -1)
                cv2.imshow("mediapipe_tracker (q=quit)", frame)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break
            if a.secs > 0 and time.time() - t0 >= a.secs:
                break
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        if not a.headless:
            cv2.destroyAllWindows()
    dt = time.time() - t0
    print(f"stopped: {frames} frames, {seen} with a hand, {dt:.1f}s ({frames/max(dt,1e-6):.1f} fps)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
