#!/bin/bash
# Host check of the panel's compose step (orientation, burn-in orbit, level table)
# and of the night rules (window, level table from a percentage, profile bounds):
# the real oled_compose.c and night_rules.c compiled on the Mac with harness.c.
# Writes the composed frames as PNG to <out> for a look: the drawn frame, the
# frame moved by the orbit, dimmed to 30 %, and off.
# Usage: firmware/test/oled/run.sh [out-dir]   (default: a temp dir, printed)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
OUT=${1:-$(mktemp -d)}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 \
  -I"$FW/components/drivers" -I"$FW/components/ui" \
  "$HERE/harness.c" "$FW/components/drivers/oled_compose.c" "$FW/components/ui/night_rules.c" \
  -o "$BUILD/harness"

cd "$OUT"
status=0
"$BUILD/harness" || status=$?

# PGM to PNG with the standard library only, so the frames open anywhere.
python3 -I - "$OUT" <<'PY'
import glob, os, struct, sys, zlib

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
PY
echo "frames in $OUT"
exit "$status"
