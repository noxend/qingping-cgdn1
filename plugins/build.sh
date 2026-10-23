#!/bin/bash
# Builds a plugin: ./build.sh <folder> [out.wasm]
# The folder holds plugin.c and plugin.json; the module is written to
# <folder>/<folder name>.wasm unless an output is given. The compiler is wasi-sdk
# release 34, found in $WASI_SDK or ~/.local/wasi-sdk-34.0-*; ./get-wasi-sdk.sh puts it
# there. Only its clang and its libc headers are used: the module links sdk/libc_mini.c
# instead of a libc, and every call it leaves undefined must be an SDK function
# (sdk/imports.txt), or the link fails and names it.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)

if [[ $# -lt 1 || ! -f "$1/plugin.c" ]]; then
  echo "usage: $0 <plugin folder with plugin.c and plugin.json> [out.wasm]" >&2
  exit 2
fi
DIR=$(cd "$1" && pwd)
OUT=${2:-$DIR/$(basename "$DIR").wasm}

if [[ -z "${WASI_SDK:-}" ]]; then
  for candidate in "$HOME"/.local/wasi-sdk-34.0-*; do
    if [[ -x "$candidate/bin/clang" ]]; then
      WASI_SDK=$candidate
    fi
  done
fi
if [[ -z "${WASI_SDK:-}" || ! -x "$WASI_SDK/bin/clang" ]]; then
  echo "wasi-sdk 34 not found: run $HERE/get-wasi-sdk.sh, or set WASI_SDK" >&2
  exit 1
fi
if [[ ! -f "$DIR/plugin.json" ]]; then
  echo "$DIR/plugin.json is missing: the device takes a module only with it" >&2
  exit 1
fi

# MVP WebAssembly: the device's interpreter takes no bulk memory and no reference
# types. Freestanding, so clang never turns libc_mini's loops back into calls.
# Linear memory is whole 64 KB pages that never grow, 8 KB of it the module's stack: one
# page, or as many as wasm-ld says the module's data needs.
link() {
  local bytes=$(($1 * 65536))
  "$WASI_SDK/bin/clang" --target=wasm32-wasip1 --sysroot="$WASI_SDK/share/wasi-sysroot" \
    -mcpu=mvp -Os -nostdlib -ffreestanding -std=gnu23 -Wall -Werror \
    -I"$HERE/sdk" -I"$DIR" "$DIR/plugin.c" "$HERE/sdk/libc_mini.c" \
    -Wl,--no-entry -Wl,--allow-undefined-file="$HERE/sdk/imports.txt" \
    -Wl,--initial-memory=$bytes -Wl,--max-memory=$bytes -Wl,-z,stack-size=8192 \
    -Wl,--strip-all -o "$OUT"
}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
rm -f "$OUT"
pages=1
status=0
link $pages 2>"$LOG" || status=$?
needed=$(sed -n 's/.*initial memory too small, \([0-9]*\) bytes needed.*/\1/p' "$LOG")
if ((status)) && [[ -n "$needed" ]]; then
  pages=$(((needed + 65535) / 65536))
  status=0
  link $pages 2>"$LOG" || status=$?
fi
if ((status)); then
  cat "$LOG" >&2
  if grep -q "undefined symbol" "$LOG"; then
    echo "An \"undefined symbol\" is a function the plugin calls that is neither in" >&2
    echo "sdk/plugin_wasm.h nor in sdk/libc_mini.c: the device has no other code to run." >&2
  fi
  exit 1
fi
# The plugin's description travels in the module: the device reads it at the upload.
python3 -I "$HERE/sdk/embed_manifest.py" "$OUT" "$DIR/plugin.json" "$HERE/sdk/plugin_wasm.h"
echo "$OUT $(wc -c < "$OUT") bytes, $pages x 64 KB of memory"
