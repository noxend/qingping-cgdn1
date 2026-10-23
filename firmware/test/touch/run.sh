#!/bin/bash
# Host check of the touch reader's pacing (touch_bus.c) on a strip that stops
# answering: the reads back off, the bus is reset now and then, the log stays quiet,
# and the reader yields between every two reads.
# Usage: firmware/test/touch/run.sh [touch_bus.c]   (default: the one in the tree)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
SRC=${1:-$FW/components/drivers/touch_bus.c}
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 -I"$FW/components/drivers" "$HERE/harness.c" "$SRC" -o "$BUILD/harness"
"$BUILD/harness"
