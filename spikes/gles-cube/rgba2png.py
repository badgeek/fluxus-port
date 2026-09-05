#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Raw RGBA -> PNG, using nothing but the standard library.

The obvious tool would be ImageMagick, but the `magick` on this machine is built
without a PNG delegate: it exits 0 and writes the raw bytes straight through, so
the "PNG" it produces is not one. Twenty lines of zlib is more dependable than
diagnosing that again.

    python3 rgba2png.py in.raw out.png [width] [height]
"""
import struct
import sys
import zlib


def write_png(rgba: bytes, w: int, h: int, path: str) -> None:
    # Each scanline is prefixed with filter byte 0 (None).
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 512
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 512
    with open(src, "rb") as f:
        rgba = f.read()
    want = w * h * 4
    if len(rgba) != want:
        print(f"{src}: expected {want} bytes for {w}x{h} RGBA, got {len(rgba)}")
        return 1
    write_png(rgba, w, h, dst)
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
