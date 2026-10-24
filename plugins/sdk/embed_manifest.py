#!/usr/bin/env python3
"""Checks a plugin's plugin.json and adds it to its module as the custom section
"cgdn1.plugin".

The device takes a module only with this section: it is where the id, the texts in each
language, the settings and the hosts the module may reach come from. The SDK version the
module was built against is added from plugin_wasm.h. The runtime ignores custom
sections. build.sh runs this after every build, so a plugin.json the device would refuse
stops the build here, with what to fix.

Usage: embed_manifest.py [--no-check] <module.wasm> <plugin.json> <plugin_wasm.h>
--no-check embeds a plugin.json as it is: the device's own tests make bad ones on purpose.
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wasm_section import encode_section  # noqa: E402

# The device's rules (components/wasm_host/manifest.c), text lengths in bytes of UTF-8 per
# language.
NAME_MAX = 47
DESCRIPTION_MAX = 255
LABEL_MAX = 63
TEXT_VALUE_MAX = 63
PARAM_KEY_MAX = 15
OPTION_KEY_MAX = 23
PARAMS_MAX = 16
OPTIONS_MAX = 64
HOSTS_MAX = 4
HOST_MAX = 63
VERSION_MAX = 15
SECTION_MAX = 8192
DEPTH_MAX = 8
CATEGORIES = (
    "climate",
    "time",
    "weather",
    "security",
    "finance",
    "transport",
    "news",
    "home",
    "fun",
    "tools",
    "examples",
    "other",
)
FIELDS = ("id", "name", "description", "category", "version", "hosts", "params")
PARAM_TYPES = ("select", "text", "number", "toggle")


def sdk_version(header):
    with open(header) as f:
        m = re.search(r"^#define WASM_SDK_VERSION (\d+)$", f.read(), re.MULTILINE)
    if not m:
        raise SystemExit(f"no WASM_SDK_VERSION in {header}")
    return int(m.group(1))


def size(text):
    return len(text.encode())


def text_problems(where, value, cap):
    """A text in each language: {"en": "...", "uk": "..."}, English required."""
    example = f'{where} must be {{"en": "...", "uk": "..."}}: English required, Ukrainian optional'
    if not isinstance(value, dict) or "en" not in value:
        return [example]
    out = []
    for lang, text in value.items():
        if not re.fullmatch(r"[a-z]{2}", lang):
            out.append(f'{where}: "{lang}" is not a two-letter language code such as "en"')
        elif not isinstance(text, str) or not text:
            out.append(f"{where}.{lang} must be a non-empty string")
        elif size(text) > cap:
            out.append(f"{where}.{lang} is {size(text)} bytes; the device takes {cap}")
    return out


def option_problems(where, options, default):
    if not isinstance(options, list) or not 1 <= len(options) <= OPTIONS_MAX:
        return [f'{where}: a select needs "options", 1 to {OPTIONS_MAX} of {{"key", "label"}}']
    out = []
    keys = []
    for k, o in enumerate(options):
        at = f"{where}.options[{k}]"
        if not isinstance(o, dict) or set(o) - {"key", "label"}:
            out.append(f'{at} must be {{"key": "...", "label": {{"en": "..."}}}}')
            continue
        key = o.get("key")
        if not isinstance(key, str) or not re.fullmatch(r"[a-z0-9_]{1,%d}" % OPTION_KEY_MAX, key):
            out.append(f'{at}: "key" must be 1 to {OPTION_KEY_MAX} of a-z, 0-9 and _')
        elif key in keys:
            out.append(f'{at}: the key "{key}" is used twice')
        keys.append(key)
        out += text_problems(f"{at}.label", o.get("label"), LABEL_MAX)
    if default not in keys:
        out.append(f'{where}: "def" must be the key of one of its options')
    return out


def param_problems(params):
    """What is wrong with the settings list, as sentences."""
    if not isinstance(params, list):
        return ['"params" must be a list (an empty one, [], for no settings)']
    out = []
    if len(params) > PARAMS_MAX:
        out.append(f"{len(params)} settings; the device takes {PARAMS_MAX}")
    keys = []
    for i, p in enumerate(params):
        where = f"params[{i}]"
        if not isinstance(p, dict):
            out.append(f"{where} must be an object with key, label, type and def")
            continue
        extra = set(p) - {"key", "label", "type", "def", "options"}
        if extra:
            out.append(f"{where}: unknown fields {', '.join(sorted(extra))}")
        key = p.get("key")
        if not isinstance(key, str) or not re.fullmatch(r"[a-z0-9_]{1,%d}" % PARAM_KEY_MAX, key):
            out.append(f'{where}: "key" must be 1 to {PARAM_KEY_MAX} of a-z, 0-9 and _')
        elif key in keys:
            out.append(f'{where}: the key "{key}" is used twice')
        keys.append(key)
        out += text_problems(f"{where}.label", p.get("label"), LABEL_MAX)
        kind = p.get("type")
        default = p.get("def")
        if kind not in PARAM_TYPES:
            out.append(f'{where}: "type" must be one of {", ".join(PARAM_TYPES)}')
        if not isinstance(default, str) or size(default) > TEXT_VALUE_MAX:
            out.append(f'{where}: "def" must be a string of at most {TEXT_VALUE_MAX} bytes')
        if kind == "select":
            out += option_problems(where, p.get("options"), default)
        elif "options" in p:
            out.append(f'{where}: only a select has "options"')
        if kind == "toggle" and default not in ("0", "1"):
            out.append(f'{where}: a toggle\'s "def" is "0" or "1"')
        if kind == "number" and not (
            isinstance(default, str) and re.fullmatch(r"-?[0-9]{1,9}", default)
        ):
            out.append(f'{where}: a number\'s "def" is an integer of at most 9 digits, as a string')
    return out


def problems_of(meta):
    """Everything the device would refuse in this plugin.json, as sentences."""
    out = []
    extra = set(meta) - set(FIELDS)
    if extra:
        out.append(f"unknown fields: {', '.join(sorted(extra))}")
    for field in FIELDS:
        if field not in meta:
            out.append(f'"{field}" is missing')
    plugin_id = meta.get("id")
    if (
        not isinstance(plugin_id, str)
        or not re.fullmatch(r"[a-z0-9_]{1,15}", plugin_id)
        or plugin_id == "sys"
    ):
        out.append('"id" must be 1 to 15 of a-z, 0-9 and _, and not "sys"')
    out += text_problems('"name"', meta.get("name"), NAME_MAX)
    out += text_problems('"description"', meta.get("description"), DESCRIPTION_MAX)
    if meta.get("category") not in CATEGORIES:
        out.append(f'"category" must be one of {", ".join(CATEGORIES)}')
    version = meta.get("version")
    if not isinstance(version, str) or not version or size(version) > VERSION_MAX:
        out.append(f'"version" must be a string of 1 to {VERSION_MAX} bytes, such as "1.0.0"')
    hosts = meta.get("hosts")
    if not isinstance(hosts, list):
        out.append('"hosts" must be a list (an empty one, [], for no network)')
    else:
        if len(hosts) > HOSTS_MAX:
            out.append(f"{len(hosts)} hosts; the device takes {HOSTS_MAX}")
        for h in hosts:
            if not isinstance(h, str) or not re.fullmatch(r"[a-z0-9.-]{1,%d}" % HOST_MAX, h):
                out.append(
                    f'host {h!r}: a lowercase host name alone, like "bank.gov.ua", '
                    "with no https://, path or port"
                )
    out += param_problems(meta.get("params"))
    return out


def refuse_constant(name):
    raise ValueError(f"{name} is not a JSON number")


class Pairs(list):
    """A JSON object as its key and value pairs, in order, repeats kept."""


def as_dicts(value):
    """The parsed JSON with each object as a dict."""
    if isinstance(value, Pairs):
        return {k: as_dicts(v) for k, v in value}
    if isinstance(value, list):
        return [as_dicts(v) for v in value]
    return value


def json_problems(value, where="plugin.json", depth=1):
    """What the device's JSON reader refuses: a key repeated in an object, nesting past
    its limit."""
    out = []
    if depth > DEPTH_MAX:
        return [f"the JSON nests deeper than {DEPTH_MAX} levels"]
    if isinstance(value, Pairs):
        seen = set()
        for key, v in value:
            if key in seen:
                out.append(f'{where}: the key "{key}" appears twice')
            seen.add(key)
            out += json_problems(v, f"{where}.{key}", depth + 1)
    elif isinstance(value, list):
        for k, v in enumerate(value):
            out += json_problems(v, f"{where}[{k}]", depth + 1)
    return out


def main():
    args = sys.argv[1:]
    check = args[:1] != ["--no-check"]
    if not check:
        args = args[1:]
    if len(args) != 3:
        raise SystemExit(__doc__)
    module, manifest, header = args
    try:
        with open(manifest, encoding="utf-8") as f:
            meta = json.load(f, object_pairs_hook=Pairs, parse_constant=refuse_constant)
    except ValueError as e:
        raise SystemExit(f"{manifest} is not valid JSON: {e}")
    if not isinstance(meta, Pairs):
        raise SystemExit(f"{manifest} must be a JSON object")
    problems = json_problems(meta) if check else []
    meta = as_dicts(meta)
    problems += problems_of(meta) if check else []
    if problems:
        print(f"{manifest}:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        raise SystemExit(1)
    meta["sdk"] = sdk_version(header)
    payload = json.dumps(meta, separators=(",", ":"), ensure_ascii=False).encode()
    if check and len(payload) > SECTION_MAX:
        raise SystemExit(
            f"{manifest} is {len(payload)} bytes as JSON; the device takes {SECTION_MAX}"
        )
    with open(module, "ab") as f:
        f.write(encode_section(payload))


if __name__ == "__main__":
    main()
