"""The modules the store test installs: the clock built by build.sh, taken apart and put
back with altered plugin.json files. Reads clock.wasm, v1.json and sdk1.h in <work> and
writes every variant there. Usage: variants.py <plugins> <work>"""

import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from wasm_section import without_section  # noqa: E402

plugins, work = sys.argv[1], sys.argv[2]
os.chdir(work)
code = open("clock.wasm", "rb").read()


def write_leb(n):
    out = bytearray()
    while True:
        byte, n = n & 0x7F, n >> 7
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


# The module without its plugin.json section.
out = without_section(code)
open("bare.wasm", "wb").write(out)
flipped = bytearray(code)
flipped[len(flipped) // 2] ^= 1
open("clock_flipped.wasm", "wb").write(flipped)

meta = json.load(open(f"{plugins}/standard/clock/plugin.json"))
header = f"{plugins}/sdk/plugin_wasm.h"
open("sdk3.h", "w").write(
    re.sub(r"(?m)^#define WASM_SDK_VERSION \d+$", "#define WASM_SDK_VERSION 3", open(header).read())
)


def embed(name, obj, hdr=header, base="bare.wasm"):
    open(name, "wb").write(open(base, "rb").read())
    json.dump(obj, open("variant.json", "w"), ensure_ascii=False)
    subprocess.check_call(
        [
            "python3",
            "-I",
            f"{plugins}/sdk/embed_manifest.py",
            "--no-check",
            name,
            "variant.json",
            hdr,
        ]
    )


def raw(name, text):
    body = write_leb(len(b"cgdn1.plugin")) + b"cgdn1.plugin" + text.encode()
    open(name, "wb").write(out + bytes([0]) + write_leb(len(body)) + body)


params = meta["params"]
select = params[0]
embed("v101.wasm", dict(meta, version="1.0.1"))
embed("badid.wasm", dict(meta, id="Clock!"))
embed("sysid.wasm", dict(meta, id="sys"))
embed("longid.wasm", dict(meta, id="a_sixteen_char_x"))
embed("fivehosts.wasm", dict(meta, hosts=["a.ua", "b.ua", "c.ua", "d.ua", "e.ua"]))
embed("badhost.wasm", dict(meta, hosts=["Bank.gov.ua/x"]))
embed("noname.wasm", {k: v for k, v in meta.items() if k != "name"})
embed("plainname.wasm", dict(meta, name="Clock"))
embed("noen.wasm", dict(meta, name={"uk": "Годинник"}))
embed("badlang.wasm", dict(meta, name={"en": "Clock", "ukr": "Годинник"}))
embed("longname.wasm", dict(meta, name={"en": "Clock", "uk": "Г" * 24}))
embed("badcat.wasm", dict(meta, category="Час"))
embed("author.wasm", dict(meta, author="noxend"))
embed("sdk3.wasm", meta, hdr="sdk3.h")
embed("bank.wasm", dict(meta, id="clock_bank", hosts=["bank.gov.ua"]))
embed("stringopts.wasm", dict(meta, params=[dict(select, options=["24h", "12h"])]))
embed("baddef.wasm", dict(meta, params=[dict(select, **{"def": "24 години"})]))
embed("twinopts.wasm", dict(meta, params=[dict(select, options=[select["options"][0]] * 2)]))
embed("twinparams.wasm", dict(meta, params=[select, select]))
embed("manyparams.wasm", dict(meta, params=[dict(params[2], key=f"k{n}") for n in range(17)]))
embed(
    "textdef.wasm",
    dict(meta, params=[{"key": "note", "label": {"en": "Note"}, "type": "text", "def": ""}]),
)
embed(
    "badnumber.wasm",
    dict(meta, params=[{"key": "n", "label": {"en": "N"}, "type": "number", "def": "1e3"}]),
)
deep = []
for _ in range(40):
    deep = [deep]
embed("deep.wasm", dict(meta, params=deep))
# 31 levels: valid JSON for the reader, and one recursion level each for the key check.
deep = []
for _ in range(30):
    deep = [deep]
embed("deep31.wasm", dict(meta, params=deep))
# Every field within its own limit, the whole over 8 KB.
options = [{"key": f"o{k}", "label": {"en": "z" * 60}} for k in range(10)]
embed(
    "huge.wasm",
    dict(
        meta,
        params=[
            {
                "key": f"k{n}",
                "label": {"en": "z" * 60},
                "type": "select",
                "options": options,
                "def": "o0",
            }
            for n in range(16)
        ],
    ),
)
# Two host lists in one plugin.json: a page reading the last "hosts" would show none, a
# reader taking the first would allow evil.example.
listing = json.dumps(
    {k: v for k, v in dict(meta, sdk=2).items() if k != "hosts"}, ensure_ascii=False
)[1:-1]
raw("twice.wasm", '{"hosts":["evil.example"],' + listing + ',"hosts":[]}')
raw("case.wasm", '{"HOSTS":["evil.example"],' + listing + ',"hosts":[]}')
raw("raw_ok.wasm", "{" + listing + ',"hosts":[]}')
raw("notjson.wasm", "{" + listing)
# The plugin.json of 1.0.0-rc7, as that release's build.sh embedded it.
embed("v1.wasm", json.load(open("v1.json")), hdr="sdk1.h")
for n in range(64):
    embed(f"copy{n}.wasm", dict(meta, id=f"clock{n}"))
