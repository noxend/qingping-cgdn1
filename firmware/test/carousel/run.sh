#!/bin/bash
# Host check of the carousel's frame tick against its UI loop (the race that froze
# the panel mid-slide, a lost frame event, the normal case): the real carousel.c
# compiled on the Mac with harness.c and stubs for the timers, the panel and the LED.
# Usage: firmware/test/carousel/run.sh [carousel.c]   (default: the one in the tree)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
SRC=${1:-$FW/components/ui/carousel.c}
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 \
  -I"$HERE/stub" -I"$FW/components/drivers" -I"$FW/components/ui" \
  "$HERE/harness.c" "$SRC" -o "$BUILD/harness"
"$BUILD/harness"
