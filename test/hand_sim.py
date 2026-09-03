#!/usr/bin/env python3
"""Synthetic hand -> OSC, so the hand demo is verifiable without a camera/MediaPipe.

Sends the SAME schema as tools/mediapipe_tracker.py (/hands, /hand/0 = 63 floats,
/pinch/0) but from a hand-shaped set of 21 landmarks that sways + curls over time.
Use it to drive examples/hand-reactive.scm and confirm the skeleton renders/reacts.

    python3 test/hand_sim.py [--secs 6] [--port 8000]
Pure stdlib.
"""
import argparse, socket, struct, math, time, sys

# base open-hand pose, MediaPipe 21-landmark order, normalised image coords (y down)
BASE = [
    (0.50, 0.92),                                   # 0 wrist
    (0.42, 0.86),(0.36,0.79),(0.32,0.73),(0.29,0.67),   # 1-4 thumb
    (0.46, 0.62),(0.45,0.50),(0.44,0.42),(0.44,0.35),   # 5-8 index
    (0.50, 0.60),(0.50,0.46),(0.50,0.37),(0.50,0.30),   # 9-12 middle
    (0.54, 0.62),(0.55,0.49),(0.56,0.41),(0.56,0.34),   # 13-16 ring
    (0.58, 0.65),(0.60,0.55),(0.61,0.48),(0.62,0.42),   # 17-20 pinky
]

def _pad(b): return b + b"\0" * ((4 - len(b) % 4) % 4)
def osc_encode(addr, *args):
    a = _pad(addr.encode() + b"\0")
    t = _pad(("," + "f" * len(args)).encode() + b"\0")
    return a + t + b"".join(struct.pack(">f", float(x)) for x in args)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--secs", type=float, default=6.0)
    a = ap.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dst = (a.host, a.port)
    print(f"synthetic hand -> udp://{a.host}:{a.port} for {a.secs}s")
    t0 = time.time()
    while time.time() - t0 < a.secs:
        t = time.time() - t0
        sway = 0.06 * math.sin(t * 1.5)             # whole-hand x sway
        curl = 0.5 + 0.5 * math.sin(t * 2.0)        # 0..1 index-finger curl
        flat = []
        for i, (x, y) in enumerate(BASE):
            xx = x + sway
            yy = y
            if i in (6, 7, 8):                      # curl the index fingertip in
                yy = y + curl * 0.12
            zz = 0.03 * math.sin(t * 1.2 + i)       # gentle depth wobble
            flat += [xx, yy, zz]
        sock.sendto(osc_encode("/hands", 1.0), dst)
        sock.sendto(osc_encode("/hand/0", *flat), dst)
        # pinch = thumb tip (4) .. index tip (8)
        p4, p8 = (BASE[4][0]+sway, BASE[4][1]), (BASE[8][0]+sway, BASE[8][1]+curl*0.12)
        sock.sendto(osc_encode("/pinch/0", math.dist(p4, p8)), dst)
        time.sleep(0.033)                           # ~30 fps
    print("done")
    return 0

if __name__ == "__main__":
    sys.exit(main())
