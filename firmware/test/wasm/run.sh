#!/bin/bash
# Host check of the WASM plugin path: builds the modules (plugins/build.sh needs
# wasi-sdk, the .wat ones need wabt), WAMR for the Mac and the harness (cmake: ESP-IDF's
# will do), then runs it. The rate plugin's frame goes to <out>/*.png. The network
# plugin's checks fetch the National Bank's real answers: needs the network. The alert
# map is measured on the answer kept in test/plugins/data.
# Usage: firmware/test/wasm/run.sh [out-dir]   (default: a temp dir, printed)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
PLUGINS=$(cd "$FW/../plugins" && pwd)
OUT=${1:-$(mktemp -d)}
mkdir -p "$OUT"
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

MODS="$BUILD/modules"
mkdir -p "$MODS"
for m in clock temperature co2 humidity alerts; do
  "$PLUGINS/build.sh" "$PLUGINS/standard/$m" "$MODS/$m.wasm" >/dev/null
done
"$PLUGINS/build.sh" "$PLUGINS/examples/nbu" "$MODS/nbu.wasm" >/dev/null
for m in hang evil slow fmt netevil netbad blitbad jsonbad; do
  "$PLUGINS/build.sh" "$HERE/modules/$m" "$MODS/$m.wasm" >/dev/null
done
for m in start postinst badtype drawret badled import growable twopages table; do
  wat2wasm "$HERE/modules/wat/$m.wat" -o "$MODS/$m.wasm"
done
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j 8 >/dev/null

# The National Bank's real answers for the rate plugin, and the rate rounded to
# kopecks (half up) from the same answer.
NBU="$BUILD/nbu"
mkdir -p "$NBU"
api="https://bank.gov.ua/NBUStatService/v1/statdirectory/exchange"
curl -fsS -A "Mozilla/5.0 cgdn1 test" "$api?valcode=USD&json" -o "$NBU/usd.json"
curl -fsS -A "Mozilla/5.0 cgdn1 test" "$api?valcode=EUR&json" -o "$NBU/eur.json"
python3 -I -B "$HERE/expected_rate.py" "$NBU"

cd "$OUT"
status=0
"$BUILD/harness" "$MODS" "$NBU" "$FW/test/plugins/data" || status=$?
for pgm in "$OUT"/*.pgm; do
  sips -s format png "$pgm" --out "${pgm%.pgm}.png" >/dev/null && rm "$pgm"
done
echo "frames in $OUT"
exit "$status"
