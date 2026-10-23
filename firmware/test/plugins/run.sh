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
python3 -I -B "$HERE/calm_answer.py" "$HERE/data/alerts.json" "$DATA/alerts_calm.json"

cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j 8 >/dev/null

cd "$OUT"
status=0
"$BUILD/harness" "$MODS" "$DATA" >"$BUILD/got.txt" || status=$?

python3 -I -B "$HERE/compare.py" "$HERE/golden" "$OUT" "$BUILD/got.txt" || status=1
# The setup page's QR code must read back as the hotspot it stands for, on the frame
# as drawn and shrunk to 60 % (under 2 px a module). Decoded by zxing-cpp, the reader
# behind ZXing, which Android's camera apps descend from.
uv run --quiet --no-project --with zxing-cpp==3.1.1 --with pillow==13.0.0 \
  python -I -B "$HERE/qr.py" "$OUT" || status=1
echo "frames in $OUT"
exit "$status"
