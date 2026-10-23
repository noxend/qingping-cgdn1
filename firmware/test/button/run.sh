#!/bin/bash
# Host check of the power button's click, hold and long press decisions (button_press.c)
# against presses sampled the way button.c samples GPIO36, every 50 ms.
# Usage: firmware/test/button/run.sh [button_press.c]   (default: the one in the tree)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
SRC=${1:-$FW/components/drivers/button_press.c}
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 -I"$FW/components/drivers" "$HERE/harness.c" "$SRC" -o "$BUILD/harness"
"$BUILD/harness"
