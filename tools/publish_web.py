#!/usr/bin/env python3
"""Build the hosted web bundle, sign its manifest and publish both to cgdn1.swithy.com.

The bundle goes to ~/sites/cgdn1/v/<version>/ on the Air (Caddy serves it, tunnel
"air" publishes it, see the homelab repository), then manifest.json is replaced to
name it. Old versions stay where they are.

manifest.json is {"payload": base64(P), "sig": base64(DER ECDSA P-256 over P)}, where
P is the JSON the device checks and keeps:
  {"seq": <unix time>, "version": "...", "base": "https://cgdn1.swithy.com/v/<version>/",
   "js": "assets/index-xxxx.js", "css": ["assets/style-xxxx.css"],
   "integrity": {"assets/index-xxxx.js": "sha384-...", ...}}
The device verifies the signature with firmware/components/net/web_manifest_pub.pem, accepts
only a seq above the one it holds, and gives the browser the hashes. The private key
is ~/.config/cgdn1/web_signing_key.pem, outside the checkout so nothing the build runs
from web/ finds it there by path; without it nothing can be published.

The version is web-<short git hash>. A working tree with changes under web/ is refused
unless --allow-dirty, which adds a -dirty suffix and a timestamp: every signed manifest
stays valid for devices below its seq, so test builds are not signed by default.

Usage: python3 -I tools/publish_web.py [--host sasha@100.94.155.123] [--dry-run] [--allow-dirty]
"""

import argparse
import base64
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
KEY = os.path.expanduser("~/.config/cgdn1/web_signing_key.pem")
PUB = os.path.join(ROOT, "firmware", "components", "net", "web_manifest_pub.pem")
SITE = "https://cgdn1.swithy.com"
# Cloudflare's Browser Integrity Check answers 403 (error 1010) to urllib's own
# User-Agent; a browser's passes, and only browsers load the bundle.
UA = "Mozilla/5.0 (Macintosh) publish_web.py"
# The device's limits (web_manifest.c): what does not fit is refused there.
MAX_CSS = 2
MAX_PAYLOAD = 1536


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def version():
    git = ["git", "-C", ROOT]
    sha = subprocess.check_output(git + ["rev-parse", "--short", "HEAD"], text=True).strip()
    dirty = subprocess.run(git + ["diff", "--quiet", "HEAD", "--", "web"]).returncode != 0
    return f"web-{sha}" + (f"-dirty-{int(time.time())}" if dirty else "")


def sri_of(data):
    return "sha384-" + base64.b64encode(hashlib.sha384(data).digest()).decode()


def openssl_sign(payload):
    with tempfile.NamedTemporaryFile() as f:
        f.write(payload)
        f.flush()
        return subprocess.check_output(["openssl", "dgst", "-sha256", "-sign", KEY, f.name])


def openssl_verify(payload, sig):
    with tempfile.NamedTemporaryFile() as p, tempfile.NamedTemporaryFile() as s:
        p.write(payload)
        p.flush()
        s.write(sig)
        s.flush()
        cmd = ["openssl", "dgst", "-sha256", "-verify", PUB, "-signature", s.name, p.name]
        r = subprocess.run(cmd, capture_output=True, text=True)
        return r.returncode == 0


def fetch(url):
    req = urllib.request.Request(url, headers={"Origin": "http://cgdn1.local", "User-Agent": UA})
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read(), r.headers.get("Access-Control-Allow-Origin")


def main():
    fmt = argparse.RawDescriptionHelpFormatter
    p = argparse.ArgumentParser(description=__doc__, formatter_class=fmt)
    p.add_argument("--host", default="sasha@100.94.155.123")
    p.add_argument("--dry-run", action="store_true", help="build and sign, upload nothing")
    p.add_argument("--allow-dirty", action="store_true", help="publish a build of uncommitted web/")
    args = p.parse_args()
    if not os.path.exists(KEY):
        print(f"no signing key at {KEY}", file=sys.stderr)
        return 1

    ver = version()
    if "-dirty-" in ver and not args.allow_dirty:
        print("web/ has uncommitted changes; commit them or pass --allow-dirty", file=sys.stderr)
        return 1
    # A seq at or below the live one is ignored by every device: refuse it here
    # rather than print "published" for a build nobody will load.
    try:
        live, _ = fetch(f"{SITE}/manifest.json?t={int(time.time())}")
        live_seq = json.loads(base64.b64decode(json.loads(live)["payload"]))["seq"]
    except Exception as e:  # first publish, or the site is down: nothing to compare
        print(f"no live manifest to compare with ({e})")
        live_seq = 0
    if int(time.time()) <= live_seq:
        print(f"this clock is behind the live manifest (seq {live_seq})", file=sys.stderr)
        return 1
    env = dict(os.environ, HOSTED_VERSION=ver)
    run(["npm", "run", "build", "-w", "web", "--", "--mode", "hosted"], cwd=ROOT, env=env)

    out = os.path.join(WEB, "dist-hosted")
    vite = json.load(open(os.path.join(out, ".vite", "manifest.json")))
    entry = vite["index.html"]
    # With cssCodeSplit off the stylesheet is its own manifest entry, not the
    # entry's css list. A second script would load without a hash: refuse it.
    css = entry.get("css", []) + ([vite["style.css"]["file"]] if "style.css" in vite else [])
    scripts = [v["file"] for v in vite.values() if v["file"].endswith(".js")]
    if scripts != [entry["file"]] or len(css) > MAX_CSS:
        print(
            f"the build must be one script and at most {MAX_CSS} stylesheets: {scripts} {css}",
            file=sys.stderr,
        )
        return 1
    files = [entry["file"]] + css
    inner = {
        "seq": int(time.time()),
        "version": ver,
        "base": f"{SITE}/v/{ver}/",
        "js": entry["file"],
        "css": css,
        "integrity": {f: sri_of(open(os.path.join(out, f), "rb").read()) for f in files},
    }
    payload = json.dumps(inner, separators=(",", ":")).encode()
    if len(payload) > MAX_PAYLOAD:
        print(f"payload {len(payload)} bytes, the device takes {MAX_PAYLOAD}", file=sys.stderr)
        return 1
    sig = openssl_sign(payload)
    if not openssl_verify(payload, sig):
        print("the fresh signature does not verify with web_manifest_pub.pem", file=sys.stderr)
        return 1
    envelope = {
        "payload": base64.b64encode(payload).decode(),
        "sig": base64.b64encode(sig).decode(),
    }
    text = json.dumps(envelope)
    print(json.dumps(inner, indent=2))
    if args.dry_run:
        return 0

    # The build first, then the manifest that points at it: a device never reads a
    # manifest naming files that are not there yet.
    run(
        [
            "rsync",
            "-a",
            "--exclude",
            ".vite",
            "--exclude",
            "index.html",
            out + "/",
            f"{args.host}:sites/cgdn1/v/{ver}/",
        ]
    )
    tmp = os.path.join(out, "manifest.json")
    with open(tmp, "w") as f:
        f.write(text + "\n")
    run(["rsync", "-a", tmp, f"{args.host}:sites/cgdn1/manifest.json"])
    # The site root explains itself; the interface is opened from the device.
    root_page = os.path.join(ROOT, "site", "index.html")
    run(["rsync", "-a", root_page, f"{args.host}:sites/cgdn1/index.html"])

    # Read it back through Cloudflare as the device and the browser will.
    body, _ = fetch(f"{SITE}/manifest.json?t={int(time.time())}")
    got = json.loads(body)
    got_payload = base64.b64decode(got["payload"])
    if not openssl_verify(got_payload, base64.b64decode(got["sig"])):
        print("the published manifest does not verify", file=sys.stderr)
        return 1
    got_inner = json.loads(got_payload)
    if got_inner["version"] != ver:
        said = got_inner["version"]
        print(f"manifest through the tunnel says {said}, expected {ver}", file=sys.stderr)
        return 1
    for f, want in inner["integrity"].items():
        data, cors = fetch(inner["base"] + f)
        ok = sri_of(data) == want and cors == "*"
        print(f"{'ok ' if ok else 'BAD'} {f} ({len(data)} bytes, cors {cors})")
        if not ok:
            return 1
    print(f"published {ver} (seq {inner['seq']}, signature verified)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
