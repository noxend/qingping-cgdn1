#!/bin/bash
# Host test of firmware/components/system/creds_rules.c: the real file compiled on the Mac
# against ESP-IDF's own mbedtls (the build cache of the fw_manifest test). The oracle is
# Python: hashlib's PBKDF2-HMAC-SHA256 for the stored key, and the rules the setup page
# and the AP password must follow.
# Usage: firmware/test/creds/run.sh
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

cc -O1 -Wall -I"$FW/components/system" -I"$C/mbedtls/mbedtls/include" \
  "$HERE/harness.c" "$FW/components/system/creds_rules.c" "$CACHE"/*.o -o "$WORK/harness"

python3 -I -B "$HERE/check.py" "$WORK/harness"
