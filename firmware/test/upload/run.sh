#!/bin/bash
# Host check of components/wasm_host/wasm_host.c: the first boot's standard plugins, the
# upload and install handover, deletion, what an older firmware left, and installing
# until the storage is full (harness.c says what each scenario checks). The modules are
# the clock under other ids: build.sh builds it, and sdk/embed_manifest.py puts back its
# real plugin.json with the id changed; "old" carries the clock's plugin.json of
# 1.0.0-rc7 (80ccca1, from git). The standard plugins are built from plugins/standard
# and embedded as the firmware embeds them. Each scenario is a boot on the NVS the last one
# left. Needs wasi-sdk, git, cmake (ESP-IDF's will do) and a built firmware (for WAMR).
# Usage: firmware/test/upload/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
PLUGINS=$(cd "$FW/../plugins" && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

MODS="$BUILD/modules"
mkdir -p "$MODS"
"$PLUGINS/build.sh" "$PLUGINS/standard/clock" "$MODS/clock.wasm" >/dev/null
git -C "$FW" show 80ccca1:firmware/plugins-wasm/examples/clock/plugin.json >"$MODS/v1.json"
git -C "$FW" show 80ccca1:firmware/plugins-wasm/sdk/plugin_wasm.h >"$MODS/sdk1.h"
python3 -I -B - "$PLUGINS" "$MODS" "$HERE/.." <<'PY'
import json, subprocess, sys
plugins, mods = sys.argv[1], sys.argv[2]
sys.path.insert(0, sys.argv[3])
from wasm_section import without_section
bare = without_section(open(f"{mods}/clock.wasm", "rb").read())

def embed(name, meta, header=f"{plugins}/sdk/plugin_wasm.h"):
    open(f"{mods}/{name}.wasm", "wb").write(bare)
    json.dump(meta, open(f"{mods}/{name}.json", "w"), ensure_ascii=False)
    subprocess.check_call(["python3", "-I", f"{plugins}/sdk/embed_manifest.py", "--no-check",
                           f"{mods}/{name}.wasm", f"{mods}/{name}.json", header])

meta = json.load(open(f"{plugins}/standard/clock/plugin.json"))
for plugin_id in [f"w{k}" for k in range(64)] + ["broken"]:
    embed(plugin_id, dict(meta, id=plugin_id))
embed("old", dict(json.load(open(f"{mods}/v1.json")), id="old"), f"{mods}/sdk1.h")
PY
# The standard plugins as the image carries them: built as wasm_host's CMakeLists builds
# them, under the symbols target_add_binary_data gives them.
mkdir -p "$BUILD/standard"
{
  for dir in "$PLUGINS"/standard/*/; do
    id=$(basename "$dir")
    f="$BUILD/standard/$id.wasm"
    "$PLUGINS/build.sh" "$dir" "$f" >/dev/null
    printf '.section __DATA,__const\n.globl _binary_%s_wasm_start\n_binary_%s_wasm_start:\n' "$id" "$id"
    printf '.incbin "%s"\n.globl _binary_%s_wasm_end\n_binary_%s_wasm_end:\n' "$f" "$id" "$id"
  done
} >"$BUILD/defaults.s"
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DDEFAULTS_ASM="$BUILD/defaults.s" >/dev/null
cmake --build "$BUILD" -j 8 >/dev/null

NVS="$BUILD/nvs.bin"
status=0
for scenario in first_boot second_boot older_firmware uploads unreadable_standard fresh until_full; do
  if [[ $scenario == fresh ]]; then
    rm -f "$NVS"
    continue
  fi
  echo "== $scenario"
  "$BUILD/harness" "$MODS" "$NVS" "$scenario" 2>/dev/null || status=1
done
exit "$status"
