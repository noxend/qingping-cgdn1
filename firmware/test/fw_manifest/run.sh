#!/bin/bash
# Host test of firmware/components/net/fw_manifest.c: the real file, compiled on the Mac against
# ESP-IDF's own mbedtls and cJSON with the stub headers of the web_manifest test. The signed
# inputs come from tools/publish_fw.py's own functions (the publisher the device must accept),
# over the built image: a release above the running version, the same payload altered, signed
# by another key, a url off the firmware path, notes with a quote, the running version again
# and an older seq; the live web manifest stands for a payload signed for the other channel.
# Needs the network, openssl, the signing key (~/.config/cgdn1/web_signing_key.pem) and a
# built image: only its bytes count (the manifest carries their size and sha256), so the
# newest firmware/build*/cgdn1.bin of any build directory will do.
# Usage: firmware/test/fw_manifest/run.sh [image.bin]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
IDF=${IDF_PATH:-$HOME/Library/Caches/esphome/idf/frameworks/5.5.5}
C=$IDF/components
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CACHE=${TMPDIR:-/tmp}/cgdn1-host-mbedtls

IMAGE=${1:-}
if [[ -z "$IMAGE" ]]; then
  # The build folders are ours and their names plain, which is what ls -t needs.
  # shellcheck disable=SC2012
  IMAGE=$(ls -t "$FW"/build*/cgdn1.bin 2>/dev/null | head -1 || true)
fi
if [[ -z "$IMAGE" || ! -f "$IMAGE" ]]; then
  echo "no built image: build the firmware (idf.py build) or pass one as the argument" >&2
  exit 1
fi

# IDF's mbedtls with its default config, built once per machine (shared with web_manifest).
if [[ ! -f $CACHE/done ]]; then
  mkdir -p "$CACHE"
  for f in "$C"/mbedtls/mbedtls/library/*.c; do
    cc -c -O1 -w -I"$C/mbedtls/mbedtls/include" -I"$C/mbedtls/mbedtls/library" "$f" -o "$CACHE/$(basename "$f" .c).o"
  done
  touch "$CACHE/done"
fi

cp -R "$HERE/../web_manifest/stub" "$WORK/stub"
cp "$FW"/components/net/{fw_manifest,signed_envelope}.{c,h} "$FW/components/net/web_manifest.h" "$WORK/stub/"
cp "$FW/components/net/web_manifest_pub.pem" "$WORK/pub.pem"
cp "$HERE/harness.c" "$WORK/"
cd "$WORK"
curl -fsS -A "Mozilla/5.0 fw_manifest test" "https://cgdn1.swithy.com/manifest.json?t=$(date +%s)" -o webchannel.json
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out other.pem 2>/dev/null
python3 -I -B - "$FW/../tools" "$IMAGE" <<'PY'
import base64, json, sys, time
sys.path.insert(0, sys.argv[1])
import publish_fw as p
data = open(sys.argv[2], "rb").read()
seq = int(time.time())
def write(name, env):
    json.dump(env, open(name, "w"))
def release(name, version="0.27.6", notes="Нова версія: firmware update from the web", seq=seq, **over):
    inner, _ = p.payload_for(data, version, notes, seq)
    inner.update(over)
    payload = json.dumps(inner, separators=(",", ":"), ensure_ascii=False).encode()
    write(name, p.envelope(payload))
    return payload
real = release("real.json")
env = json.load(open("real.json"))
write("tampered.json", dict(env, payload=base64.b64encode(real.replace(b"0.27.6", b"0.27.7", 1)).decode()))
write("otherkey.json", p.envelope(real, key="other.pem"))
release("offpath.json", url="https://cgdn1.swithy.com/v/x/cgdn1.bin")
release("quote.json", notes='a "quoted" note')
release("same.json", version="0.27.5", seq=seq + 1)
release("older.json", version="0.27.9", seq=seq - 1000)
PY
cc -O1 -w -Istub -I"$C/mbedtls/mbedtls/include" -I"$C/json/cJSON" harness.c stub/signed_envelope.c "$C/json/cJSON/cJSON.c" "$CACHE"/*.o -o harness
./harness
