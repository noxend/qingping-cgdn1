#!/bin/bash
# Host check of the standard plugins and the device menu. build.sh builds the five
# modules from plugins/standard; the harness runs every case of cases.h through the
# device's plugin table and bindings on WAMR, and draws the menu pages. Each plugin
# frame must equal the golden one pixel for pixel, and its LED colour and alert stats
# the golden ones (cases.h says where they come from). A frame changed on purpose is
# looked at in <out> and copied over its golden one. The setup page's QR code must
# decode to the hotspot it stands for. The frames go to <out>/*.png either way.
# Needs wasi-sdk, cmake (ESP-IDF's will do) and uv; WAMR as test/host_wamr.cmake finds it.
# Usage: firmware/test/plugins/run.sh [out-dir]   (default: a temp dir, printed)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
PLUGINS=$(cd "$FW/../plugins" && pwd)
OUT=${1:-$(mktemp -d)}
mkdir -p "$OUT"
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

MODS="$BUILD/modules"
DATA="$BUILD/data"
mkdir -p "$MODS" "$DATA"
for p in clock temperature co2 humidity alerts; do
  "$PLUGINS/build.sh" "$PLUGINS/standard/$p" "$MODS/$p.wasm" >/dev/null
done
# The alerts API's answer as it was fetched, the same answer cut short, and the same
# answer with no alert in it.
cp "$HERE/data/alerts.json" "$DATA/"
head -c 10000 "$HERE/data/alerts.json" >"$DATA/alerts_cut.json"
python3 -I -c 'import json, sys
answer = json.load(open(sys.argv[1], encoding="utf-8"))
answer["raions"], answer["oblasts"] = [], []
json.dump(answer, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False)' \
  "$HERE/data/alerts.json" "$DATA/alerts_calm.json"

cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j 8 >/dev/null

cd "$OUT"
status=0
"$BUILD/harness" "$MODS" "$DATA" >"$BUILD/got.txt" || status=$?

python3 -I - "$HERE/golden" "$OUT" "$BUILD/got.txt" <<'PY' || status=1
import glob, json, os, struct, sys, zlib

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
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        if kind == b"IHDR":
            width = struct.unpack(">I", data[at + 8:at + 12])[0]
        if kind == b"IDAT":
            idat += data[at + 8:at + 8 + n]
        at += 12 + n
    raw = zlib.decompress(idat)
    return b"".join(raw[k + 1:k + 1 + width] for k in range(0, len(raw), width + 1))


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
    rows = [pixels[y * width:(y + 1) * width] for y in range(height)]
    png(pgm[:-4] + ".png", width, height, rows)
    os.remove(pgm)
if seen != len(want):
    print(f"{seen} plugin cases ran, golden/ has {len(want)}")
    failed += 1
print(f"{failed} plugin cases differ")
sys.exit(1 if failed else 0)
PY
# The setup page's QR code must read back as the hotspot it stands for, on the frame
# as drawn and shrunk to 60 % (under 2 px a module). Decoded by zxing-cpp, the reader
# behind ZXing, which Android's camera apps descend from.
uv run --quiet --no-project --with zxing-cpp==3.1.1 --with pillow==13.0.0 \
  python -I - "$OUT" <<'PY' || status=1
import os, sys
import zxingcpp
from PIL import Image

out = sys.argv[1]
hotspot = "WIFI:T:WPA;S:CGDN1-setup-C08D;P:mwx7k2pq9tre;;"
# SSID a;b:c\d and password p,q"r12345, escaped as the format asks.
escaped = r'WIFI:T:WPA;S:a\;b\:c\\d;P:p\,q\"r12345;;'
cases = {
    "menu_wifi": hotspot,
    "menu_wifi_qr": hotspot,
    "menu_wifi_online": hotspot,
    "menu_setup_boot": hotspot,
    "menu_wifi_escaped": escaped,
}
failed = 0
for name, want in cases.items():
    frame = Image.open(os.path.join(out, name + ".png"))
    small = frame.resize((frame.width * 3 // 5, frame.height * 3 // 5), Image.BILINEAR)
    for label, image in (("drawn", frame), ("60 %", small)):
        got = [r.text for r in zxingcpp.read_barcodes(image, formats=zxingcpp.BarcodeFormat.QRCode)]
        ok = got == [want]
        print(f"{name:18} qr {label:5} {'ok' if ok else 'FAIL: read ' + repr(got)}")
        failed += not ok
sys.exit(1 if failed else 0)
PY
echo "frames in $OUT"
exit "$status"
