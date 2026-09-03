#!/usr/bin/env python3
"""OSC end-to-end test for the fluxus->JUCE port.

Assumes an app is running with examples/osc-test.scm loaded, which:
  - listens for OSC on udp/8000
  - echoes the latest /in args back to /out on 127.0.0.1:9001

This harness (pure stdlib, no deps) sends /in and asserts the /out echo, proving
the OSC receive AND send paths work. Exit 0 = all pass, 1 = failure.
"""
import socket, struct, time, sys

IN_PORT, OUT_PORT = 8000, 9001

# ---- minimal OSC codec (float32 / int32 args) -------------------------------
def _pad(b): return b + b"\0" * ((4 - len(b) % 4) % 4)

def osc_encode(addr, *args):
    a = _pad(addr.encode() + b"\0")
    types = "," + "".join("f" for _ in args)
    t = _pad(types.encode() + b"\0")
    body = b"".join(struct.pack(">f", float(x)) for x in args)
    return a + t + body

def _rd_str(data, i):
    j = data.index(b"\0", i)
    s = data[i:j].decode("utf-8", "replace")
    i = j + 1
    i += (4 - i % 4) % 4
    return s, i

def osc_decode(data):
    addr, i = _rd_str(data, 0)
    types, i = _rd_str(data, i)
    args = []
    for tt in types[1:]:
        if tt == "f":
            (v,) = struct.unpack(">f", data[i:i+4]); i += 4; args.append(v)
        elif tt == "i":
            (v,) = struct.unpack(">i", data[i:i+4]); i += 4; args.append(v)
    return addr, args

# ---- sockets ----------------------------------------------------------------
recv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
recv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
recv.bind(("127.0.0.1", OUT_PORT))
recv.settimeout(0.25)
send = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def send_in(*vals):
    send.sendto(osc_encode("/in", *vals), ("127.0.0.1", IN_PORT))

def wait_out(pred, timeout=4.0):
    """Keep resending is caller's job; drain /out until pred(args) or timeout."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            data, _ = recv.recvfrom(2048)
        except socket.timeout:
            continue
        addr, args = osc_decode(data)
        if addr == "/out" and len(args) >= 2 and pred(args):
            return args
    return None

def approx(a, b, tol=0.02): return abs(a - b) < tol

def run_case(name, sendvals, pred):
    # send a few times (UDP best-effort), then wait for the matching echo
    for _ in range(3):
        send_in(*sendvals); time.sleep(0.05)
    got = wait_out(pred)
    ok = got is not None
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}: sent {sendvals} -> got {got}")
    return ok

def main():
    print("OSC e2e: /in -> (fluxus echo) -> /out")
    # sanity: is anything echoing at all? (sketch echoes every frame, baseline 0,0)
    base = wait_out(lambda a: True, timeout=3.0)
    if base is None:
        print("  [FAIL] no /out received — app not running / sketch not loaded / port busy")
        return 1
    print(f"  [ ok ] receiving /out echoes (baseline {base})")

    results = [
        run_case("single arg 42.5",  (42.5,),      lambda a: approx(a[0], 42.5)),
        run_case("two args 1.0 7.0", (1.0, 7.0),   lambda a: approx(a[0], 1.0) and approx(a[1], 7.0)),
        run_case("update 3.25",      (3.25,),      lambda a: approx(a[0], 3.25)),
        run_case("negative -5.5",    (-5.5, 0.0),  lambda a: approx(a[0], -5.5)),
    ]
    ok = all(results)
    print(f"RESULT: {'ALL PASS' if ok else 'FAILURE'} ({sum(results)}/{len(results)})")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
