#!/bin/bash
# Host check of the JSON scanner (components/wasm_host/json_scan.c) against Python's json
# module. The inputs are real texts: the alerts API's and the National Bank's answers
# (fetched, so the network is needed), and every plugin.json in plugins/; then
# thousands of mutations of them (bytes changed, cut, repeated, escapes and nesting put
# in). For each one the scanner and Python must agree on whether it is JSON and, when it
# is, on every key, string (byte for byte, decoded) and number.
# Usage: firmware/test/json/run.sh [mutations per sample, default 3000]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
PLUGINS=$(cd "$FW/../plugins" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cc -O1 -Wall -Werror -std=gnu11 -I"$FW/components/wasm_host" \
  "$HERE/harness.c" "$FW/components/wasm_host/json_scan.c" -o "$WORK/harness"

mkdir -p "$WORK/samples"
curl -fsS -A "Mozilla/5.0 cgdn1 test" https://neptun.in.ua/api/v1/alerts \
  -o "$WORK/samples/alerts.json"
curl -fsS -A "Mozilla/5.0 cgdn1 test" \
  "https://bank.gov.ua/NBUStatService/v1/statdirectory/exchange?valcode=USD&json" \
  -o "$WORK/samples/nbu.json"
k=0
while IFS= read -r f; do
  cp "$f" "$WORK/samples/plugin$((k++)).json"
done < <(find "$PLUGINS" -name plugin.json)

python3 -I -B "$HERE/compare.py" "$WORK" "${1:-3000}"
