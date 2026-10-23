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
python3 -I -B "$FW/../plugins/preview/frames.py" "$OUT"
echo "frames in $OUT"
exit "$status"
