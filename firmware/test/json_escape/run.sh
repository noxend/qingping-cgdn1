#!/bin/bash
# Host check of the JSON string escaper every HTTP answer uses
# (components/system/json_escape.c) against Python's json module.
# Usage: firmware/test/json_escape/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -Werror -std=gnu11 -I"$FW/components/system" \
  "$HERE/harness.c" "$FW/components/system/json_escape.c" -o "$BUILD/harness"
python3 -I -B "$HERE/check.py" "$BUILD/harness"
