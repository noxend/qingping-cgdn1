#!/bin/bash
# Host test of sign-in: components/http/{auth,session,util,pass_queue}.c and components/api/login.c
# and wifi_setup.c,
# the real files, compiled on the Mac against ESP-IDF's cJSON and mbedtls with this
# test's stub headers. The harness plays the browser: it keeps the cookie the device
# sets and sends it back, and reads the status lines and headers that come out.
# Usage: firmware/test/session/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
IDF=${IDF_PATH:-$HOME/Library/Caches/esphome/idf/frameworks/5.5.5}
MBEDTLS=$IDF/components/mbedtls/mbedtls
CJSON=$IDF/components/json/cJSON
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cc -O1 -Wall -Wextra -Wno-unused-parameter \
  -I"$HERE/stub" -I"$FW/components/http" -I"$FW/components/system" \
  -I"$FW/components/api/private" -I"$CJSON" -I"$MBEDTLS/include" -I"$MBEDTLS/library" \
  "$HERE/harness.c" \
  "$FW/components/http/auth.c" "$FW/components/http/session.c" "$FW/components/http/util.c" \
  "$FW/components/http/pass_queue.c" \
  "$FW/components/api/login.c" "$FW/components/api/wifi_setup.c" \
  "$CJSON/cJSON.c" "$MBEDTLS/library/sha256.c" "$MBEDTLS/library/platform_util.c" \
  -o "$WORK/harness"
"$WORK/harness"
