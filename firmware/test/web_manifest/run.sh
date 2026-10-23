#!/bin/bash
# Host test of firmware/components/net/web_manifest.c: the real file, compiled on the Mac against
# ESP-IDF's own mbedtls and cJSON with stub headers for the rest. Inputs are derived
# from the manifest published at cgdn1.swithy.com: the real one, the same payload
# altered, signed by another key, signed with a foreign base, with a quote in the
# version, and an older seq. Needs the network, openssl, and the signing key
# (~/.config/cgdn1/web_signing_key.pem) for the signed variants.
# Usage: firmware/test/web_manifest/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
IDF=${IDF_PATH:-$HOME/Library/Caches/esphome/idf/frameworks/5.5.5}
C=$IDF/components
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CACHE=${TMPDIR:-/tmp}/cgdn1-host-mbedtls

# IDF's mbedtls with its default config, built once per machine.
if [[ ! -f $CACHE/done ]]; then
  mkdir -p "$CACHE"
  for f in "$C"/mbedtls/mbedtls/library/*.c; do
    cc -c -O1 -w -I"$C/mbedtls/mbedtls/include" -I"$C/mbedtls/mbedtls/library" "$f" -o "$CACHE/$(basename "$f" .c).o"
  done
  touch "$CACHE/done"
fi

cp -R "$HERE/stub" "$WORK/stub"
cp "$FW"/components/net/{web_manifest,signed_envelope}.{c,h} "$WORK/stub/"
cp "$FW/components/net/web_manifest_pub.pem" "$WORK/pub.pem"
cp "$HERE/harness.c" "$WORK/"
cd "$WORK"
curl -fsS -A "Mozilla/5.0 web_manifest test" "https://cgdn1.swithy.com/manifest.json?t=$(date +%s)" -o real.json
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out other.pem 2>/dev/null
python3 -I -B "$HERE/variants.py" "$HOME/.config/cgdn1/web_signing_key.pem" \
  "$WORK"
cc -O1 -w -Istub -I"$C/mbedtls/mbedtls/include" -I"$C/json/cJSON" harness.c stub/signed_envelope.c "$C/json/cJSON/cJSON.c" "$CACHE"/*.o -o harness
./harness
