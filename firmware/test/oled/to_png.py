"""Every PGM frame in <dir> to PNG with the standard library only, so the frames open
anywhere. Usage: to_png.py <dir>"""

import glob
import os
import struct
import sys
import zlib


def png(path, width, height, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


for pgm in sorted(glob.glob(os.path.join(sys.argv[1], "*.pgm"))):
    with open(pgm, "rb") as f:
        magic, size, _max = f.readline(), f.readline(), f.readline()
        width, height = (int(v) for v in size.split())
        data = f.read()
    rows = [data[y * width : (y + 1) * width] for y in range(height)]
    png(pgm[:-4] + ".png", width, height, rows)
    os.remove(pgm)
