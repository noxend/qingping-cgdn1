#!/bin/bash
# Host test of the wrong-password limit: firmware/components/system/creds.c and
# creds_rules.c, the real files, over IDF's mbedtls (built once, test/lib.sh),
# a fake clock and this test's stub headers.
# Usage: firmware/test/creds_limit/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
# shellcheck source=../lib.sh
source "$HERE/../lib.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

build_mbedtls

FLAGS=(-O1 -Wall -Wextra -Wno-unused-parameter
  -I"$HERE/stub" -I"$FW/components/system" -I"$C/mbedtls/mbedtls/include")
# creds.c's PBKDF2 goes through the harness, which looks at the lock around it.
cc "${FLAGS[@]}" -Dcreds_hash_matches=traced_hash_matches -c "$FW/components/system/creds.c" \
  -o "$WORK/creds.o"
cc "${FLAGS[@]}" "$HERE/harness.c" "$WORK/creds.o" "$FW/components/system/creds_rules.c" \
  "$MBEDTLS_CACHE"/*.o -o "$WORK/harness"
"$WORK/harness"
