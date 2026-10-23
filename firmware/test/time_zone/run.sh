#!/bin/bash
# Host check of the time zones (time_zone.c): each POSIX string against the host's zone
# database, the stored record's checks, and the same keys in the harness, the web
# interface's list and the mock device.
# Usage: firmware/test/time_zone/run.sh [time_zone.c]   (default: the one in the tree)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
SRC=${1:-$FW/components/net/time_zone.c}
WEB=$FW/../web/src/lib/timeZones.js
MOCK=$FW/../web/mock_device.py
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 -I"$FW/components/net" "$HERE/harness.c" "$SRC" -o "$BUILD/check"
"$BUILD/check"

grep -oE '^    \{"[a-z_]+"' "$SRC" | tr -d ' {"' >"$BUILD/firmware"
grep -oE 'matches\("[a-z_]+"' "$HERE/harness.c" | sed -E 's/.*"(.*)"/\1/' >"$BUILD/harness"
grep -oE 'key: "[a-z_]+"' "$WEB" | sed -E 's/.*"(.*)"/\1/' >"$BUILD/web"
sed -n '/^TIME_ZONES = (/,/^)/p' "$MOCK" | grep -oE '"[a-z_]+"' | tr -d '"' >"$BUILD/mock"

failed=0
for list in harness web mock; do
  if diff "$BUILD/firmware" "$BUILD/$list"; then
    echo "ok   $list lists the same zones as time_zone.c"
  else
    echo "FAIL $list lists other zones than time_zone.c"
    failed=1
  fi
done
exit $failed
