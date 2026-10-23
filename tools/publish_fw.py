#!/usr/bin/env python3
"""Build the firmware, sign its release manifest and publish both to cgdn1.swithy.com.

The image goes to ~/sites/cgdn1/fw/<version>/cgdn1.bin on the Air (Caddy serves it, tunnel
"air" publishes it, see the homelab repository), then firmware.json is replaced to name it.
Old images stay where they are.

firmware.json is {"payload": base64(P), "sig": base64(DER ECDSA P-256 over P)}, the envelope
of manifest.json (publish_web.py), signed with the same key, where P is
  {"kind": "firmware", "seq": <unix time>, "version": "0.27.6",
   "url": "https://cgdn1.swithy.com/fw/0.27.6/cgdn1.bin", "sha256": "<64 hex>",
   "size": <bytes>, "notes": "..."}
The device (firmware/components/net/fw_manifest.c) offers it as an update when the version is
above its own; fw_update.c then holds the downloaded image to this size, hash and version
before esp_ota_end checks the image's own signature.

The image is built here from a clean tree with DEV_BUILD off, and its version is FW_VERSION
(components/api/version.h). Refused: uncommitted changes under firmware/ or plugins/ (the image
embeds the standard plugins built from plugins/standard), a DEV_BUILD build, one with the test
endpoints of cgdn1-dev, a version not above the live one. --dry-run builds and signs, uploads
nothing; with --allow-dirty it also takes a dirty tree, which a real publish never does.

Usage: python3 -I tools/publish_fw.py --notes "..." [--host sasha@100.94.155.123]
                                         [--dry-run [--allow-dirty]] [--out firmware.json]
"""

import argparse
import base64
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")
IMAGE = os.path.join(FW, "build", "cgdn1.bin")
VERSION_H = os.path.join(FW, "components", "api", "version.h")
KEY = os.path.expanduser("~/.config/cgdn1/web_signing_key.pem")
PUB = os.path.join(FW, "components", "net", "web_manifest_pub.pem")
SITE = "https://cgdn1.swithy.com"
# Cloudflare's Browser Integrity Check answers 403 (error 1010) to urllib's own
# User-Agent; a browser's passes.
UA = "Mozilla/5.0 (Macintosh) publish_fw.py"
# The device's limits (fw_manifest.c, signed_envelope.c): what does not fit is refused there.
MAX_PAYLOAD = 1536
MAX_NOTES = 240
SLOT = 0x1E0000
# esp_app_desc_t sits at the start of the first segment: magic, then version and project.
DESC_MAGIC = bytes.fromhex("3254cdab")


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def fw_version():
    with open(VERSION_H) as f:
        text = f.read()
    return re.search(r'^#define FW_VERSION "([0-9.]+)"', text, re.MULTILINE).group(1)


def version_key(v):
    return [int(x) for x in v.split(".")]


def notes_problem(notes):
    if len(notes.encode()) > MAX_NOTES:
        return f"notes are {len(notes.encode())} bytes, the device takes {MAX_NOTES}"
    if any(ord(c) < 0x20 or c in '"\\\x7f' for c in notes):
        return "notes may not hold quotes, backslashes or control characters"
    return None


def image_desc(data):
    i = data.find(DESC_MAGIC)
    if i < 0:
        return None, None

    def field(off):
        return data[i + off : i + off + 32].split(b"\0")[0].decode()

    return field(16), field(48)


def payload_for(data, version, notes, seq):
    inner = {
        "kind": "firmware",
        "seq": seq,
        "version": version,
        "url": f"{SITE}/fw/{version}/cgdn1.bin",
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "notes": notes,
    }
    return inner, json.dumps(inner, separators=(",", ":"), ensure_ascii=False).encode()


def openssl_sign(payload, key=KEY):
    with tempfile.NamedTemporaryFile() as f:
        f.write(payload)
        f.flush()
        return subprocess.check_output(["openssl", "dgst", "-sha256", "-sign", key, f.name])


def openssl_verify(payload, sig):
    with tempfile.NamedTemporaryFile() as p, tempfile.NamedTemporaryFile() as s:
        p.write(payload)
        p.flush()
        s.write(sig)
        s.flush()
        cmd = ["openssl", "dgst", "-sha256", "-verify", PUB, "-signature", s.name, p.name]
        return subprocess.run(cmd, capture_output=True, check=False).returncode == 0


def envelope(payload, key=KEY):
    sig = openssl_sign(payload, key)
    return {
        "payload": base64.b64encode(payload).decode(),
        "sig": base64.b64encode(sig).decode(),
    }


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def live_manifest():
    """The live payload, None when the site has none yet (404); any other failure raises,
    since without it neither the version nor the clock can be checked."""
    try:
        body = fetch(f"{SITE}/firmware.json?t={int(time.time())}")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            print("no firmware.json on the site yet: first publish")
            return None
        raise
    return json.loads(base64.b64decode(json.loads(body)["payload"]))


def tree_dirty():
    git = ["git", "-C", ROOT, "status", "--porcelain", "--", "firmware", "plugins"]
    return subprocess.check_output(git, text=True).strip() != ""


def build():
    """None when the image is a release build, else why it is not."""
    # An empty EXTRA_COMPONENT_DIRS drops a test component a cached configure kept.
    run(["idf.py", "-C", FW, "-DDEV_BUILD=0", "-DEXTRA_COMPONENT_DIRS=", "build"])
    with open(os.path.join(FW, "build", "CMakeCache.txt")) as f:
        cache = f.read()
    m = re.search(r"^DEV_BUILD:[A-Z]+=(.*)$", cache, re.MULTILINE)
    if m is not None and m.group(1) not in ("0", "OFF", ""):
        return "the build directory is configured with DEV_BUILD on"
    with open(os.path.join(FW, "build", "project_description.json")) as f:
        components = json.load(f)["build_components"]
    if "test_hooks" in components:
        return "the build has the test_hooks component (cgdn1-dev)"
    return None


def main():
    fmt = argparse.RawDescriptionHelpFormatter
    p = argparse.ArgumentParser(description=__doc__, formatter_class=fmt)
    p.add_argument("--notes", required=True, help="what changed, shown on the device's page")
    p.add_argument("--host", default="sasha@100.94.155.123")
    p.add_argument("--dry-run", action="store_true", help="build and sign, upload nothing")
    p.add_argument("--allow-dirty", action="store_true", help="with --dry-run: a dirty tree")
    p.add_argument("--out", help="also write the signed firmware.json here")
    args = p.parse_args()
    if args.allow_dirty and not args.dry_run:
        print("--allow-dirty only goes with --dry-run", file=sys.stderr)
        return 1
    if not os.path.exists(KEY):
        print(f"no signing key at {KEY}", file=sys.stderr)
        return 1
    problem = notes_problem(args.notes)
    if problem:
        print(problem, file=sys.stderr)
        return 1
    if tree_dirty() and not args.allow_dirty:
        print("firmware/ or plugins/ has uncommitted changes; commit them first", file=sys.stderr)
        return 1

    ver = fw_version()
    live = live_manifest()
    seq = int(time.time())
    if live and version_key(ver) <= version_key(live["version"]):
        print(f"FW_VERSION {ver} is not above the live {live['version']}", file=sys.stderr)
        return 1
    if live and seq <= live["seq"]:
        print(f"this clock is behind the live manifest (seq {live['seq']})", file=sys.stderr)
        return 1

    problem = build()
    if problem:
        print(problem, file=sys.stderr)
        return 1
    with open(IMAGE, "rb") as f:
        data = f.read()
    got_ver, project = image_desc(data)
    if (got_ver, project) != (ver, "cgdn1"):
        print(f"the image says {project} {got_ver}, expected cgdn1 {ver}", file=sys.stderr)
        return 1
    if len(data) > SLOT:
        print(f"image is {len(data)} bytes, the slot holds {SLOT}", file=sys.stderr)
        return 1

    inner, payload = payload_for(data, ver, args.notes, seq)
    if len(payload) > MAX_PAYLOAD:
        print(f"payload {len(payload)} bytes, the device takes {MAX_PAYLOAD}", file=sys.stderr)
        return 1
    env = envelope(payload)
    if not openssl_verify(payload, base64.b64decode(env["sig"])):
        print("the fresh signature does not verify with web_manifest_pub.pem", file=sys.stderr)
        return 1
    text = json.dumps(env)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text + "\n")
    print(json.dumps(inner, indent=2, ensure_ascii=False))
    if args.dry_run:
        return 0

    # The image first, then the manifest that names it: a device never reads a
    # manifest whose image is not there yet.
    run(["ssh", args.host, f"mkdir -p sites/cgdn1/fw/{ver}"])
    run(["rsync", "-a", IMAGE, f"{args.host}:sites/cgdn1/fw/{ver}/cgdn1.bin"])
    with tempfile.NamedTemporaryFile("w", suffix=".json") as tmp:
        tmp.write(text + "\n")
        tmp.flush()
        run(["rsync", "-a", "--chmod=F644", tmp.name, f"{args.host}:sites/cgdn1/firmware.json"])

    # Read both back through Cloudflare.
    got = json.loads(fetch(f"{SITE}/firmware.json?t={int(time.time())}"))
    got_payload = base64.b64decode(got["payload"])
    if not openssl_verify(got_payload, base64.b64decode(got["sig"])):
        print("the published manifest does not verify", file=sys.stderr)
        return 1
    if json.loads(got_payload)["version"] != ver:
        print("firmware.json through the tunnel names another version", file=sys.stderr)
        return 1
    image = fetch(inner["url"])
    if hashlib.sha256(image).hexdigest() != inner["sha256"]:
        print(f"the image through the tunnel differs ({len(image)} bytes)", file=sys.stderr)
        return 1
    print(f"published {ver} (seq {seq}, {len(data)} bytes, signature verified)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
