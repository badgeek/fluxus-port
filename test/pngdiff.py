#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compare two PNGs pixel by pixel. Prints one line; exits non-zero if they differ.

    python3 pngdiff.py expected.png actual.png [--tolerance N] [--diff out.png]

Standard library only, deliberately. The `magick` on this machine is built
WITHOUT a PNG delegate — `compare` cannot read these files at all, and `magick`
silently writes raw bytes through while exiting 0. Sixty lines of zlib is more
dependable than rediscovering that.

Default tolerance is 0: the guard exists to prove a refactor changed NOTHING on
macOS, and both images come off the same GPU on the same machine, so anything
above zero is a real difference worth looking at.
"""
import struct
import sys
import zlib


def read_png(path):
    """-> (width, height, rgba bytes). 8-bit, non-interlaced, colour type 2/6."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, [], None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            w, h, depth, colour, comp, filt, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace != 0 or colour not in (2, 6):
                raise ValueError(f"{path}: unsupported PNG "
                                 f"(depth={depth} colour={colour} interlace={interlace})")
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
        pos += 12 + length
    if w is None:
        raise ValueError(f"{path}: no IHDR")

    chans = 4 if colour == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    stride = w * chans
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ftype = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        # PNG per-scanline filters; see RFC 2083 section 6.
        if ftype == 1:      # Sub
            for i in range(chans, stride):
                line[i] = (line[i] + line[i - chans]) & 0xFF
        elif ftype == 2:    # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:    # Average
            for i in range(stride):
                a = line[i - chans] if i >= chans else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:    # Paeth
            for i in range(stride):
                a = line[i - chans] if i >= chans else 0
                b = prev[i]
                c = prev[i - chans] if i >= chans else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif ftype != 0:
            raise ValueError(f"{path}: bad filter type {ftype}")
        out[y * stride:(y + 1) * stride] = line
        prev = line

    if chans == 4:
        return w, h, bytes(out)
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        rgba[i * 4:i * 4 + 3] = out[i * 3:i * 3 + 3]
        rgba[i * 4 + 3] = 255
    return w, h, bytes(rgba)


def write_png(rgba, w, h, path):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
                + chunk(b"IEND", b""))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 2
    tol = 0
    diff_path = None
    for i, a in enumerate(sys.argv):
        if a == "--tolerance" and i + 1 < len(sys.argv):
            tol = int(sys.argv[i + 1])
        if a == "--diff" and i + 1 < len(sys.argv):
            diff_path = sys.argv[i + 1]

    try:
        w1, h1, a = read_png(args[0])
        w2, h2, b = read_png(args[1])
    except (OSError, ValueError) as e:
        print(f"ERROR {e}")
        return 2
    if (w1, h1) != (w2, h2):
        print(f"FAIL size {w1}x{h1} vs {w2}x{h2}")
        return 1

    worst = 0
    ndiff = 0
    diff = bytearray(len(a)) if diff_path else None
    for i in range(0, len(a), 4):
        d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
        if d > worst:
            worst = d
        if d > tol:
            ndiff += 1
            if diff is not None:
                diff[i] = 255; diff[i + 1] = 0; diff[i + 2] = 0; diff[i + 3] = 255
        elif diff is not None:
            g = (a[i] + a[i + 1] + a[i + 2]) // 6
            diff[i] = diff[i + 1] = diff[i + 2] = g; diff[i + 3] = 255

    if diff is not None:
        write_png(bytes(diff), w1, h1, diff_path)

    pct = 100.0 * ndiff / (w1 * h1)
    if ndiff:
        print(f"FAIL  maxdelta {worst}  pixels {ndiff} ({pct:.3f}%)")
        return 1
    print(f"OK    maxdelta {worst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
