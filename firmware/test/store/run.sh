#!/bin/bash
# Host test of the plugin store and the manifest check (components/wasm_host/wasm_store.c,
# manifest.c, json_scan.c): the real files on the Mac against ESP-IDF's mbedtls, over an
# in-memory NVS with NVS's own page accounting (fake_nvs.c). The inputs come from the real
# tools: plugins/build.sh builds the clock, and the variants are that module with its
# plugin.json section taken off and put back by sdk/embed_manifest.py from altered copies
# of the real plugin.json, or written raw where JSON's own rules are the test. The record
# an older firmware kept is written by that firmware's own wasm_store.c (1.0.0-rc7,
# 80ccca1, from git) into the same NVS, with the clock's plugin.json of that release.
# Needs wasi-sdk (plugins/get-wasi-sdk.sh) and git.
# Usage: firmware/test/store/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
PLUGINS=$(cd "$FW/../plugins" && pwd)
IDF=${IDF_PATH:-$HOME/Library/Caches/esphome/idf/frameworks/5.5.5}
C=$IDF/components
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CACHE=${TMPDIR:-/tmp}/cgdn1-host-mbedtls
OLD=80ccca1

# IDF's mbedtls with its default config, built once per machine (shared with web_manifest).
if [[ ! -f $CACHE/done ]]; then
  mkdir -p "$CACHE"
  for f in "$C"/mbedtls/mbedtls/library/*.c; do
    obj="$CACHE/$(basename "$f" .c).o"
    cc -c -O1 -w -I"$C/mbedtls/mbedtls/include" -I"$C/mbedtls/mbedtls/library" "$f" -o "$obj"
  done
  touch "$CACHE/done"
fi

"$PLUGINS/build.sh" "$PLUGINS/standard/clock" "$WORK/clock.wasm" >/dev/null
mkdir -p "$WORK/old"
for f in wasm_host/wasm_store.c wasm_host/wasm_store.h wasm_host/wasm_binary.h; do
  git -C "$FW" show "$OLD:firmware/components/$f" >"$WORK/old/$(basename "$f")"
done
git -C "$FW" show "$OLD:firmware/plugins-wasm/examples/clock/plugin.json" >"$WORK/v1.json"
git -C "$FW" show "$OLD:firmware/plugins-wasm/sdk/plugin_wasm.h" >"$WORK/sdk1.h"
cd "$WORK"
python3 -I -B "$HERE/variants.py" "$PLUGINS" "$WORK"
cc -O1 -w -std=gnu11 -I"$HERE/stub" -I"$FW/components/system" -I"$FW/components/wasm_host" \
  -I"$FW/components/wasm_host/include" -I"$C/mbedtls/mbedtls/include" \
  "$HERE/harness.c" "$HERE/fake_nvs.c" "$FW/components/wasm_host/wasm_store.c" \
  "$FW/components/wasm_host/manifest.c" "$FW/components/wasm_host/json_scan.c" \
  "$FW/components/wasm_host/wasm_binary.c" "$CACHE"/*.o -c
# The older store, its public names prefixed so it links beside this one.
renames=()
for name in store_err_code store_id_ok store_init store_parse store_check store_module_matches \
  store_save store_remove store_read_manifest store_read store_list store_space \
  store_install_bytes; do
  renames+=("-D$name=old_$name")
done
cc -O1 -w -std=gnu11 -DCJSON_NESTING_LIMIT=8 "${renames[@]}" -I"$WORK/old" -I"$HERE/stub" \
  -I"$FW/components/system" -I"$C/mbedtls/mbedtls/include" -I"$C/json/cJSON" \
  -c "$WORK/old/wasm_store.c" -o old_store.o
cc -O1 -w -std=gnu11 "${renames[@]}" -I"$WORK/old" -I"$HERE/stub" -I"$FW/components/system" \
  -c "$HERE/old_writer.c" -o old_writer.o
cc -c -O1 -w -I"$C/json/cJSON" "$C/json/cJSON/cJSON.c" -o cJSON.o
cc harness.o fake_nvs.o wasm_store.o manifest.o json_scan.o wasm_binary.o old_store.o \
  old_writer.o cJSON.o "$CACHE"/*.o -o harness
./harness
