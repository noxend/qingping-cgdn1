"""The clock under 64 ids, a broken one and an old one, for the upload test. Reads
clock.wasm, v1.json and sdk1.h in <mods> and writes the modules there.
Usage: modules.py <plugins> <mods>"""

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../../plugins/sdk"))
from wasm_section import without_section  # noqa: E402

plugins, mods = sys.argv[1], sys.argv[2]
bare = without_section(open(f"{mods}/clock.wasm", "rb").read())


def embed(name, meta, header=f"{plugins}/sdk/plugin_wasm.h"):
    open(f"{mods}/{name}.wasm", "wb").write(bare)
    json.dump(meta, open(f"{mods}/{name}.json", "w"), ensure_ascii=False)
    subprocess.check_call(
        [
            "python3",
            "-I",
            f"{plugins}/sdk/embed_manifest.py",
            "--no-check",
            f"{mods}/{name}.wasm",
            f"{mods}/{name}.json",
            header,
        ]
    )


meta = json.load(open(f"{plugins}/standard/clock/plugin.json"))
for plugin_id in [f"w{k}" for k in range(64)] + ["broken"]:
    embed(plugin_id, dict(meta, id=plugin_id))
# A standard plugin as an earlier image carried it, and a user's own module under a
# standard id.
embed("clock_earlier", dict(meta, version="0.9.0"))
embed("pm10_own", dict(meta, id="pm10", version="9.9.9"))
embed("old", dict(json.load(open(f"{mods}/v1.json")), id="old"), f"{mods}/sdk1.h")
