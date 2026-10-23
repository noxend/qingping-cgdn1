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
python3 -I - "$HOME/.config/cgdn1/web_signing_key.pem" <<'PY'
import base64, json, subprocess, sys, tempfile
key = sys.argv[1]
real = json.load(open("real.json"))
payload = base64.b64decode(real["payload"])
inner = json.loads(payload)
def sign(p, k):
    with tempfile.NamedTemporaryFile() as f:
        f.write(p)
        f.flush()
        return subprocess.check_output(["openssl", "dgst", "-sha256", "-sign", k, f.name])
def write(name, p, sig):
    json.dump({"payload": base64.b64encode(p).decode(), "sig": base64.b64encode(sig).decode()}, open(name, "w"))
def signed(name, obj):
    p = json.dumps(obj, separators=(",", ":")).encode()
    write(name, p, sign(p, key))
write("tampered.json", payload.replace(b"cgdn1.swithy.com", b"cgdn1.swithy.con", 1), base64.b64decode(real["sig"]))
write("otherkey.json", payload, sign(payload, "other.pem"))
signed("badshape.json", dict(inner, base="https://evil.example/v/x/"))
signed("quote.json", dict(inner, version='x"><script>'))
signed("older.json", dict(inner, seq=inner["seq"] - 1000, version="older"))
PY
cc -O1 -w -Istub -I"$C/mbedtls/mbedtls/include" -I"$C/json/cJSON" harness.c stub/signed_envelope.c "$C/json/cJSON/cJSON.c" "$CACHE"/*.o -o harness
./harness
