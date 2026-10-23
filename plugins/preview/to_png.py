"""Every PGM frame in <dir> as PNG, four times the panel's 160 x 128 so they read on a
screen. Usage: to_png.py <dir>"""

import glob
import os
import struct
import sys
import zlib


def png(path, width, height, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    raw = b"".join(b"\0" + row for row in rows)
    head = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", head)
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b"")
        )


SCALE = 4
for pgm in sorted(glob.glob(os.path.join(sys.argv[1], "*.pgm"))):
    data = open(pgm, "rb").read()
    header, pixels = data.split(b"255\n", 1)
    width, height = map(int, header.split()[1:3])
    rows = []
    for y in range(height):
        row = bytes(b for b in pixels[y * width : (y + 1) * width] for _ in range(SCALE))
        rows += [row] * SCALE
    png(pgm[:-4] + ".png", width * SCALE, height * SCALE, rows)
    os.remove(pgm)
    print(pgm[:-4] + ".png")
