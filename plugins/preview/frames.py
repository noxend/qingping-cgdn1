"""The frames the preview and the host tests draw, written as PGM, turned into PNG with
the standard library only so they open anywhere. As a script it converts every PGM in a
folder and removes it: frames.py <dir> [scale]"""

import glob
import os
import struct
import sys
import zlib


def read_pgm(path):
    """The width, height and pixels (one byte each, row by row) of a binary PGM."""
    with open(path, "rb") as f:
        f.readline()  # P5
        width, height = (int(v) for v in f.readline().split())
        f.readline()  # 255
        return width, height, f.read()


def write_png(path, width, height, pixels, scale=1):
    """8-bit grey PNG, each pixel drawn as a scale x scale square."""
    rows = []
    for y in range(height):
        row = pixels[y * width : (y + 1) * width]
        wide = bytes(level for level in row for _ in range(scale))
        rows += [b"\x00" + wide] * scale

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", width * scale, height * scale, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def read_png(path):
    """The pixels of a PNG write_png wrote at scale 1: 8-bit grey, no filters."""
    with open(path, "rb") as f:
        data = f.read()
    at, idat, width = 8, b"", 0
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at : at + 8])
        if kind == b"IHDR":
            width = struct.unpack(">I", data[at + 8 : at + 12])[0]
        if kind == b"IDAT":
            idat += data[at + 8 : at + 8 + n]
        at += 12 + n
    raw = zlib.decompress(idat)
    return b"".join(raw[k + 1 : k + 1 + width] for k in range(0, len(raw), width + 1))


def convert(folder, scale=1):
    """Every PGM in folder as a PNG beside it; the PGM is removed."""
    for pgm in sorted(glob.glob(os.path.join(folder, "*.pgm"))):
        width, height, pixels = read_pgm(pgm)
        write_png(pgm[:-4] + ".png", width, height, pixels, scale)
        os.remove(pgm)


if __name__ == "__main__":
    convert(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 1)
