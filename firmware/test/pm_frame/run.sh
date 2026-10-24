#!/bin/bash
# Host check of the particulate frame (pm_frame.c): the frames it accepts
# and the values the stock firmware shows for them.
# Usage: firmware/test/pm_frame/run.sh [pm_frame.c]   (default: the one in the tree)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
SRC=${1:-$FW/components/sensors/pm_frame.c}
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 -I"$FW/components/sensors" "$HERE/harness.c" "$SRC" -lm \
  -o "$BUILD/harness"
"$BUILD/harness"
