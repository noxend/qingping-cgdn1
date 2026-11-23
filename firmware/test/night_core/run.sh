#!/bin/bash
# Host check of night.c: a night that turns the screen off still shows the core's own
# pages (the device menu, the setup page, the messages before a restart or power off).
# The real night.c and night_rules.c with stubs for the panel, the LED and the store.
# Usage: firmware/test/night_core/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
# shellcheck source=../lib.sh
. "$HERE/../lib.sh"
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

cc -O1 -Wall -std=gnu11 \
  -I"$HERE/stub" -I"$FW/components/ui" -I"$FW/components/ui/private" \
  -I"$FW/components/drivers" -I"$C/json/cJSON" \
  "$HERE/harness.c" "$FW/components/ui/night.c" "$FW/components/ui/night_rules.c" \
  "$C/json/cJSON/cJSON.c" -o "$BUILD/harness"
"$BUILD/harness"
