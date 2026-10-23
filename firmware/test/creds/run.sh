#!/bin/bash
# Host test of firmware/components/system/creds_rules.c: the real file compiled on the Mac
# against ESP-IDF's own mbedtls (built once, test/lib.sh). The oracle is
# Python: hashlib's PBKDF2-HMAC-SHA256 for the stored key, and the rules the setup page
# and the AP password must follow.
# Usage: firmware/test/creds/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
# shellcheck source=../lib.sh
source "$HERE/../lib.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

build_mbedtls

cc -O1 -Wall -I"$FW/components/system" -I"$C/mbedtls/mbedtls/include" \
  "$HERE/harness.c" "$FW/components/system/creds_rules.c" "$MBEDTLS_CACHE"/*.o -o "$WORK/harness"

python3 -I -B "$HERE/check.py" "$WORK/harness"
