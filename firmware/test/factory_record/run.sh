#!/bin/bash
# Host check of the factory record parser (factory_record_parse.c) on real flash:
# the record in a stock dump, and the same flash after this firmware's NVS took it
# over. Neither file is in the repository; they are a unit's own backups.
# Usage: firmware/test/factory_record/run.sh <stock-full-dump.bin> <nvs_cfg-dump.bin>
#   stock-full-dump.bin  the whole 4 MB flash read from a unit on the stock firmware
#   nvs_cfg-dump.bin     0x30000 bytes read at 0x3D0000 from a unit on this firmware
set -euo pipefail
if (($# != 2)); then
  sed -n '5,7p' "$0"
  exit 2
fi
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 -I"$FW/components/system" \
  "$HERE/harness.c" "$FW/components/system/factory_record_parse.c" -o "$BUILD/harness"
"$BUILD/harness" "$1" "$2"
