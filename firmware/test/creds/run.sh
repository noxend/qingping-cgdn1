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

python3 -I -B - "$WORK/harness" <<'PY'
import hashlib, os, subprocess, sys

h = sys.argv[1]
def run(*a):
    return subprocess.run([h, *a], capture_output=True, text=True, check=True).stdout.strip()

failed = 0
def check(name, got, want):
    global failed
    ok = got == want
    failed += not ok
    print(f"{'ok ' if ok else 'BAD'} {name}: {got!r}" + ("" if ok else f" want {want!r}"))

for pw in ["correct horse", "x7#Kq!9z", "a" * 63]:
    salt = os.urandom(16)
    want = hashlib.pbkdf2_hmac("sha256", pw.encode(), salt, 4096, 32).hex()
    check(f"key {len(pw)} chars", run("key", salt.hex(), pw), want)

salt = os.urandom(16).hex()
check("match same", run("match", salt, "x7#Kq!9z", "x7#Kq!9z"), "1")
check("match other", run("match", salt, "x7#Kq!9z", "x7#Kq!9Z"), "0")
check("match prefix", run("match", salt, "x7#Kq!9z", "x7#Kq!9"), "0")

cases = {"1234567": "0", "12345678": "1", "a" * 63: "1", "a" * 64: "0",
         "has space1": "0", "pässword1": "0", "tab\tpass1": "0", "x7#Kq!9z~": "1"}
for pw, want in cases.items():
    check(f"ok {pw!r}", run("ok", pw), want)

alphabet = "abcdefghjkmnpqrstuvwxyz23456789"
seen = set()
for _ in range(200):
    p = run("gen", os.urandom(32).hex())
    if p != "short":
        seen.update(p)
        if len(p) != 12 or any(c not in alphabet for c in p):
            check("gen shape", p, "12 chars of the alphabet")
check("gen covers the alphabet", "".join(sorted(seen)), "".join(sorted(alphabet)))
check("gen skips 31", run("gen", "1f" * 11 + "00" * 12), "a" * 12)
check("gen too few bytes", run("gen", "00" * 11), "short")
print("ALL PASS" if not failed else f"{failed} FAILED")
sys.exit(1 if failed else 0)
PY
