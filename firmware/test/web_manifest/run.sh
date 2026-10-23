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
# shellcheck source=../lib.sh
source "$HERE/../lib.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

build_mbedtls

cp -R "$HERE/stub" "$WORK/stub"
cp "$FW"/components/net/{web_manifest,signed_envelope}.{c,h} "$WORK/stub/"
cp "$FW/components/net/web_manifest_pub.pem" "$WORK/pub.pem"
cp "$HERE/harness.c" "$WORK/"
cd "$WORK"
curl -fsS -A "Mozilla/5.0 web_manifest test" "https://cgdn1.swithy.com/manifest.json?t=$(date +%s)" -o real.json
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out other.pem 2>/dev/null
python3 -I -B "$HERE/variants.py" "$HOME/.config/cgdn1/web_signing_key.pem" \
  "$WORK"
cc -O1 -w -Istub -I"$C/mbedtls/mbedtls/include" -I"$C/json/cJSON" harness.c \
  stub/signed_envelope.c "$C/json/cJSON/cJSON.c" "$MBEDTLS_CACHE"/*.o -o harness
./harness
