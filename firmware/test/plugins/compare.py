"""The harness's frames, LED colours and alert stats against golden/, then every frame to
PNG. Usage: compare.py <golden> <out> <harness output>"""

import glob
import json
import os
import struct
import sys
import zlib

golden, out, got_path = sys.argv[1:]


def png(path, width, height, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def png_pixels(path):
    """The pixels of a PNG this script wrote: 8-bit grey, no filters."""
    data = open(path, "rb").read()
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


def pgm_pixels(path):
    data = open(path, "rb").read()
    header, pixels = data.split(b"255\n", 1)
    width, height = (int(v) for v in header.split()[1:3])
    return width, height, pixels


want = {}
for line in open(os.path.join(golden, "led.txt"), encoding="utf-8"):
    name, led, *stats = line.rstrip("\n").split(" ", 2)
    want[name] = (led, json.loads(stats[0]) if stats else None)

failed = 0
seen = 0
for line in open(got_path, encoding="utf-8"):
    if not line.strip():
        continue
    name, led, *stats = line.rstrip("\n").split(" ", 2)
    seen += 1
    problems = []
    width, height, pixels = pgm_pixels(os.path.join(out, name + ".pgm"))
    expected = png_pixels(os.path.join(golden, name + ".png"))
    if pixels != expected:
        diff = sum(a != b for a, b in zip(pixels, expected))
        problems.append(f"{diff} pixels differ from golden/{name}.png")
    if led != want[name][0]:
        problems.append(f"LED {led}, golden {want[name][0]}")
    if want[name][1] is not None:
        mine = json.loads(stats[0]) if stats else {}
        # The native plugin had no "err" (the module has no esp_err_t) and named the region
        # by its label, the module by its key.
        for key in ("fetch_ok", "fetch_fail", "http", "alerts", "draws", "level"):
            if mine.get(key) != want[name][1][key]:
                problems.append(f"stats {key} {mine.get(key)}, golden {want[name][1][key]}")
    print(f"{name:18} {'ok' if not problems else 'FAIL: ' + '; '.join(problems)}")
    failed += bool(problems)

for pgm in sorted(glob.glob(os.path.join(out, "*.pgm"))):
    width, height, pixels = pgm_pixels(pgm)
    rows = [pixels[y * width : (y + 1) * width] for y in range(height)]
    png(pgm[:-4] + ".png", width, height, rows)
    os.remove(pgm)
if seen != len(want):
    print(f"{seen} plugin cases ran, golden/ has {len(want)}")
    failed += 1
print(f"{failed} plugin cases differ")
sys.exit(1 if failed else 0)
