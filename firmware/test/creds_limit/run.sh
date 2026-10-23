#!/bin/bash
# Host test of the wrong-password limit: firmware/components/system/creds.c and
# creds_rules.c, the real files, over IDF's mbedtls (the build cache of test/creds),
# a fake clock and this test's stub headers.
# Usage: firmware/test/creds_limit/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
IDF=${IDF_PATH:-$HOME/Library/Caches/esphome/idf/frameworks/5.5.5}
C=$IDF/components
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CACHE=${TMPDIR:-/tmp}/cgdn1-host-mbedtls

if [[ ! -f $CACHE/done ]]; then
  mkdir -p "$CACHE"
  for f in "$C"/mbedtls/mbedtls/library/*.c; do
    cc -c -O1 -w -I"$C/mbedtls/mbedtls/include" -I"$C/mbedtls/mbedtls/library" "$f" -o "$CACHE/$(basename "$f" .c).o"
  done
  touch "$CACHE/done"
fi

FLAGS=(-O1 -Wall -Wextra -Wno-unused-parameter
  -I"$HERE/stub" -I"$FW/components/system" -I"$C/mbedtls/mbedtls/include")
# creds.c's PBKDF2 goes through the harness, which looks at the lock around it.
cc "${FLAGS[@]}" -Dcreds_hash_matches=traced_hash_matches -c "$FW/components/system/creds.c" \
  -o "$WORK/creds.o"
cc "${FLAGS[@]}" "$HERE/harness.c" "$WORK/creds.o" "$FW/components/system/creds_rules.c" \
  "$CACHE"/*.o -o "$WORK/harness"
"$WORK/harness"
