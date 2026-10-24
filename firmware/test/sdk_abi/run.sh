#!/bin/bash
# Host check that the device reads a module the way the SDK builds it: the numbers and
# struct layouts of plugins/sdk/plugin_wasm.h against wasm_host's wasm_abi.h and
# MANIFEST_SDK. The two headers cannot share a file (their names clash), so each side
# is a program printing the same lines, and the two outputs must be equal.
# Usage: firmware/test/sdk_abi/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

# plugin_wasm.h's import attributes mean nothing outside wasm32 (gcc and clang both
# take -Wno-attributes; -Wno-unknown-attributes is clang's only).
cc -std=gnu11 -Wall -Werror -Wno-attributes -I"$FW/../plugins/sdk" \
  "$HERE/module_side.c" -o "$BUILD/module_side"
cc -std=gnu11 -Wall -Werror -I"$FW/components/wasm_host" -I"$FW/components/wasm_host/include" \
  "$HERE/device_side.c" -o "$BUILD/device_side"
"$BUILD/module_side" >"$BUILD/module.txt"
"$BUILD/device_side" >"$BUILD/device.txt"
if ! diff -u "$BUILD/module.txt" "$BUILD/device.txt"; then
  echo "plugin_wasm.h and the device disagree (- module, + device)"
  exit 1
fi
echo "ALL PASS: $(wc -l <"$BUILD/module.txt" | tr -d ' ') values agree"
