#!/bin/bash
# Host check of the temperature calibration (temp_offset.c): the offsets the API
# accepts and the corrected reading.
# Usage: firmware/test/temp_offset/run.sh [temp_offset.c]   (default: the one in the tree)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
SRC=${1:-$FW/components/sensors/temp_offset.c}
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 -I"$FW/components/sensors/include" "$HERE/harness.c" "$SRC" -lm \
  -o "$BUILD/harness"
"$BUILD/harness"
