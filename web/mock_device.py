#!/usr/bin/env python3
"""A stand-in for the device's HTTP API, for working on the web page without the board.

It answers the calls the page makes in the firmware's formats (screen_http.c
config_get, status.c, wasm_host.c plugins_get) and keeps what the page posts. The
standard plugins are built from plugins/standard with plugins/build.sh at start, as the
firmware build builds them (wasi-sdk is needed), their manifests read from the modules. An
upload is checked with plugins/sdk/embed_manifest.py's rules and installed on the next
second, deletions and installs from the image the same way:

    python3 -I web/mock_device.py --port 8137
    DEVICE=http://localhost:8137 npm run dev -w web

The password is "test": sign in on the page, or send "Authorization: Bearer test".
--first-setup shows the page of a new device, --update offers 1.0.1 and --rolled-back
reports that 1.0.1 was rolled back. --dev shows the Developer page of a DEV_BUILD image
and --test-hooks the one of a test build, whose endpoints the mock does not answer. A
saved network is joined three seconds later; "wrong-pass" as its Wi-Fi password fails as
a wrong password. Frames are blank: previews show an empty panel.
Not a test oracle: the device's own checks are richer.
"""

import argparse
import json
import math
import os
import re
import secrets
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PLUGINS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "plugins")
sys.path.insert(0, os.path.join(PLUGINS, "sdk"))
sys.dont_write_bytecode = True  # no __pycache__ beside the SDK's script
from embed_manifest import HOSTS_MAX, Pairs, as_dicts, json_problems, problems_of  # noqa: E402
from wasm_section import custom_section  # noqa: E402

PASSWORD = "test"
SDK = 2
PARTITION = 196608
RESERVE = 3 * 126 * 32
PSRAM_TOTAL = 4 * 1024 * 1024
STANDARD = ["alerts", "clock", "humidity", "temperature", "co2"]


def refusal(code):
    """Why the device would refuse this upload, as its 400 body, or None."""
    raw = custom_section(code)
    if raw is None:
        return {"error": "section"}
    try:
        meta = json.loads(raw, object_pairs_hook=Pairs)
    except ValueError:
        return {"error": "json"}
    if not isinstance(meta, Pairs) or json_problems(meta):
        return {"error": "json"}
    meta = as_dicts(meta)
    sdk = meta.pop("sdk", None)
    if not isinstance(sdk, int) or sdk > SDK:
        return {"error": "sdk"}
    if sdk < SDK:
        return {"error": "old_sdk"}
    problems = problems_of(meta)
    if not problems:
        return None
    first = problems[0]
    if first.startswith("unknown fields"):
        return {"error": "fields"}
    if first.startswith("host ") or first.endswith(f" hosts; the device takes {HOSTS_MAX}"):
        return {"error": "field", "field": "hosts"}
    field = next((f for f in meta if f'"{f}"' in first or first.startswith(f)), "")
    return {"error": "field", "field": field or "params"}


def standard_modules():
    """The image's standard plugins: id -> (module bytes, manifest)."""
    out = {}
    with tempfile.TemporaryDirectory() as work:
        for pid in STANDARD:
            path = os.path.join(work, pid + ".wasm")
            src = os.path.join(PLUGINS, "standard", pid)
            build = [os.path.join(PLUGINS, "build.sh"), src, path]
            subprocess.run(build, check=True, stdout=subprocess.DEVNULL)
            code = open(path, "rb").read()
            out[pid] = (code, json.loads(custom_section(code)))
    return out


def magnus_ratio(measured, corrected):
    """How relative humidity scales when the temperature is corrected, as temp_offset.c does."""

    def magnus(t):
        return 17.62 * t / (243.12 + t)

    return math.exp(magnus(measured) - magnus(corrected))


class Device:
    """What the board would hold: the screens, the installed plugins, the install job."""

    def __init__(self, args):
        self.lock = threading.Lock()
        self.status = {
            "api": 2,
            "fw": "1.0.0",
            "slot": "ota_1",
            "wifi": "connected",
            "ssid": "home",
            "dev": args.dev or args.test_hooks,
            "test_hooks": args.test_hooks,
            "web": "",
            "ip": "192.168.10.172",
            "host": "cgdn1.local",
            "wifi_fail": "",
            "update": None,
            "ota": None,
            "checking": False,
            "auto_update": False,
        }
        self.cfg = not args.first_setup
        if args.first_setup:
            self.status.update(wifi="not connected", ssid="", ip="")
        if args.update:
            self.status["update"] = {"version": "1.0.1", "notes": "Faster plugin install."}
        if args.rolled_back:
            self.status["rolled_back"] = "1.0.1"
        self.standard = standard_modules()
        self.enabled = ["clock", "alerts", "co2"]
        self.active = "clock"
        self.temp_offset = 0.0
        # id -> {"manifest", "size", "values"}, in install order (the table's).
        self.installed = {
            pid: {"manifest": meta, "size": len(code), "values": {}}
            for pid, (code, meta) in self.standard.items()
        }
        self.job = {"n": 0, "id": "", "stage": "idle", "code": "", "name": ""}
        # Taken up by tick() once a second, as the UI loop does on the device.
        self.upload = None
        self.remove_q = []
        # A wrong password makes the device answer "busy" (429) for a second.
        self.wrong_until = 0.0
        self.sessions = set()
        threading.Thread(target=self.tick, daemon=True).start()

    def tick(self):
        while True:
            time.sleep(1.0)
            with self.lock:
                if self.remove_q:
                    self.installed.pop(self.remove_q.pop(), None)
                elif self.upload:
                    item, self.upload = self.upload, None
                    pid = item["manifest"]["id"]
                    values = self.installed.get(pid, {}).get("values", {})
                    self.installed[pid] = dict(item, values=values)
                    self.job.update(stage="done")

    def check(self, token):
        """The password as creds_config_check answers it: "ok", "wrong" or "busy"."""
        with self.lock:
            if token == PASSWORD:
                return "ok"
            if time.time() < self.wrong_until:
                return "busy"
            self.wrong_until = time.time() + 1.0
            return "wrong"

    def catalog(self):
        rows = [
            {
                "id": pid,
                "installed": True,
                "state": "ready",
                "active": pid == self.active,
                "faults": 0,
                "disabled": False,
                "error": "",
                "manifest": dict(e["manifest"], sdk=SDK),
                "values": e["values"],
            }
            for pid, e in self.installed.items()
        ]
        rows += [
            {"id": pid, "installed": False, "manifest": meta}
            for pid, (_, meta) in self.standard.items()
            if pid not in self.installed
        ]
        return rows

    def plugins(self, diag):
        modules = sum(e["size"] + 2048 for e in self.installed.values())
        free = PARTITION - 4096 - 20000 - modules - RESERVE
        psram_used = 80000 + sum(e["size"] + 81000 for e in self.installed.values())
        body = {
            "sdk": SDK,
            "runtime": True,
            "storage": {"modules": modules, "free": free, "partition": PARTITION},
            "psram": {"free": PSRAM_TOTAL - psram_used, "total": PSRAM_TOTAL},
            "installed": [
                {
                    "id": pid,
                    "name": e["manifest"]["name"]["en"],
                    "version": e["manifest"]["version"],
                    "hosts": e["manifest"]["hosts"],
                    "size": e["size"],
                    "pages": 1,
                    "state": "ready",
                    "error": "",
                }
                for pid, e in self.installed.items()
            ],
        }
        body["job"] = dict(self.job)
        if diag:
            body["heap"] = {"free": 94908, "min": 24616, "largest": 73728}
        return body

    def queue(self, manifest, size):
        """Hands an install to the tick: the status and the body of the answer."""
        with self.lock:
            if self.upload:
                return 409, {"error": "busy"}
            self.upload = {"manifest": manifest, "size": size}
            n = self.job["n"] + 1
            self.job = {"n": n, "id": manifest["id"], "stage": "waiting", "code": "", "name": ""}
        return 202, {"ok": True}

    def take_upload(self, code):
        """POST /plugins/upload as the device answers it."""
        why = refusal(code)
        if why:
            return 400, why
        return self.queue(json.loads(custom_section(code)), len(code))

    def take_install(self, pid):
        """POST /plugins/install {"id"}: a standard plugin from the image."""
        if pid not in self.standard:
            return 400, {"error": 'need {"id"} of a standard plugin'}
        code, meta = self.standard[pid]
        return self.queue(meta, len(code))


COOKIE = "cgdn1_session"
OPEN = ("/status", "/login", "/logout", "/scan", "/save")


class Handler(BaseHTTPRequestHandler):
    device = None

    def reply(self, status, body, kind="application/json", cookie=None):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        if cookie is not None:
            age = 2592000 if cookie else 0
            self.send_header(
                "Set-Cookie", f"{COOKIE}={cookie}; Path=/; Max-Age={age}; HttpOnly; SameSite=Strict"
            )
        self.end_headers()
        self.wfile.write(data)

    def session(self):
        for part in (self.headers.get("Cookie") or "").split(";"):
            name, _, value = part.strip().partition("=")
            if name == COOKIE:
                return value
        return ""

    def signed_in(self):
        """A session cookie or the password as Bearer, as auth.c's credential() takes them."""
        if self.session() in self.device.sessions:
            return True
        auth = self.headers.get("Authorization") or ""
        return auth.startswith("Bearer ") and self.device.check(auth[7:]) == "ok"

    def refused(self, path):
        """The answer auth_request gives, or None when the request may go on."""
        origin = self.headers.get("Origin")
        if origin and origin != "http://" + (self.headers.get("Host") or ""):
            return 403, {"error": "a page of another site cannot use this device"}
        if path in OPEN or self.signed_in():
            return None
        return 401, {"error": "sign in first"}

    def body_bytes(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n)

    def do_GET(self):
        d = self.device
        path = self.path.split("?")[0]
        refused = self.refused(path)
        if refused:
            return self.reply(*refused)
        if path == "/status" and not self.signed_in():
            keys = ("api", "fw", "ip", "host", "wifi_fail")
            public = dict({k: d.status[k] for k in keys}, cfg=d.cfg, signed_in=False)
            return self.reply(200, public)
        with d.lock:
            if path == "/status":
                return self.reply(200, dict(d.status, cfg=d.cfg, signed_in=True))
            if path == "/config":
                return self.reply(
                    200,
                    {
                        "catalog": d.catalog(),
                        "enabled": [i for i in d.enabled if i in d.installed],
                        "night": {"enabled": False},
                        "temp_offset": d.temp_offset,
                    },
                )
            if path == "/sensors":
                temp = round(23.4 + d.temp_offset, 1)
                hum = round(min(41.0 * magnus_ratio(23.4, 23.4 + d.temp_offset), 100.0), 1)
                return self.reply(
                    200,
                    {
                        "temp": {"valid": True, "value": temp, "age_ms": 900},
                        "hum": {"valid": True, "value": hum, "age_ms": 900},
                        "co2": {"valid": True, "value": 612.0, "age_ms": 1200},
                    },
                )
            if path in ("/alerts", "/plugin/stats"):
                region = (
                    d.installed.get("alerts", {}).get("values", {}).get("region", "kyiv_oblast")
                )
                stats = {
                    "fetch_ok": 1,
                    "fetch_fail": 0,
                    "http": 200,
                    "alerts": 12,
                    "draws": 1,
                    "region": region,
                    "level": "red",
                }
                return self.reply(200, stats)
            if path == "/screen":
                return self.reply(200, bytes(10240), "application/octet-stream")
        if path == "/plugins":
            with d.lock:
                return self.reply(200, d.plugins(True))
        return self.reply(404, {"error": "not found"})

    def form(self):
        return dict(urllib.parse.parse_qsl(self.body_bytes().decode()))

    def join_later(self, ssid, password):
        def join():
            time.sleep(3)
            with self.device.lock:
                if password == "wrong-pass":
                    self.device.status.update(wifi="not connected", ssid="", ip="")
                    self.device.status["wifi_fail"] = "password"
                else:
                    self.device.status.update(wifi="connected", ssid=ssid, ip="192.168.10.172")
                    self.device.status["wifi_fail"] = ""

        threading.Thread(target=join, daemon=True).start()

    def do_POST(self):
        d = self.device
        path = self.path.split("?")[0]
        refused = self.refused(path)
        if refused:
            return self.reply(*refused)
        if path == "/login":
            password = json.loads(self.body_bytes() or b"{}").get("password", "")
            result = d.check(password)
            if result != "ok":
                return self.reply(429 if result == "busy" else 401, {"error": result})
            token = secrets.token_hex(32)
            d.sessions.add(token)
            return self.reply(200, {"ok": True}, cookie=token)
        if path == "/logout":
            d.sessions.discard(self.session())
            return self.reply(200, {"ok": True}, cookie="")
        if path in ("/scan", "/save") and d.cfg and not self.signed_in():
            return self.reply(401, {"error": "sign in first"})
        if path == "/scan":
            return self.reply(200, {"nets": ["home", "Oh, hi Mark", "neighbours 5G"]})
        if path == "/save":
            f = self.form()
            if not d.cfg and not re.fullmatch(r"[\x21-\x7e]{8,63}", f.get("token", "")):
                return self.reply(400, {"error": "config password must be 8-63 characters"})
            with d.lock:
                d.cfg = True
                d.status.update(wifi="not connected", ssid="", ip="")
            self.join_later(f.get("ssid", ""), f.get("pass", ""))
            return self.reply(200, b"saved, rebooting\n", "text/plain")
        if path == "/ota":
            # The device writes the image while it reads it and then reboots; a few
            # seconds stand in for that. Nothing is kept.
            self.body_bytes()
            time.sleep(6)
            return self.reply(200, b"OK, rebooting.\n", "text/plain")
        if path == "/update/check":
            with d.lock:
                d.status["checking"] = True

            def done():
                time.sleep(3)
                with d.lock:
                    d.status["checking"] = False

            threading.Thread(target=done, daemon=True).start()
            return self.reply(200, {"checking": True})
        if path in ("/reboot", "/reset", "/update", "/update/auto"):
            self.body_bytes()
            if path == "/update":
                with d.lock:
                    d.status["ota"] = {
                        "state": "failed",
                        "version": "1.0.1",
                        "done": 0,
                        "total": 0,
                        "err": "image sha256 differs from the manifest",
                    }
            return self.reply(200, {"ok": True})
        if path == "/config":
            body = json.loads(self.body_bytes() or b"{}")
            with d.lock:
                if "enabled" in body:
                    # The device drops an id that is not installed.
                    d.enabled = [i for i in body["enabled"] if i in d.installed]
                if "active" in body:
                    d.active = body["active"]
                if "temp_offset" in body:
                    offset = body["temp_offset"]
                    tenths = round(offset * 10) if isinstance(offset, (int, float)) else None
                    if tenths is None or abs(tenths) > 30 or abs(offset * 10 - tenths) > 1e-6:
                        return self.reply(400, {"error": "bad temp_offset"})
                    d.temp_offset = tenths / 10
                if body.get("screen") in d.installed:
                    d.installed[body["screen"]]["values"].update(body.get("values", {}))
            return self.reply(200, b"saved\n", "text/plain")
        if path in ("/plugins/upload", "/plugins/remove", "/plugins/install"):
            want = "application/wasm" if path.endswith("upload") else "application/json"
            if self.headers.get("Content-Type") != want:
                return self.reply(400, {"error": f"need Content-Type: {want}"})
            data = self.body_bytes()
            if path.endswith("upload"):
                return self.reply(*d.take_upload(data))
            if path.endswith("install"):
                return self.reply(*d.take_install(json.loads(data or b"{}").get("id", "")))
            pid = json.loads(data or b"{}").get("id", "")
            with d.lock:
                d.remove_q.append(pid)
            return self.reply(202, {"ok": True})
        return self.reply(404, {"error": "not found"})

    def log_message(self, fmt, *args):
        pass


def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--port", type=int, default=8137)
    p.add_argument("--first-setup", action="store_true", help="no password set yet")
    p.add_argument("--update", action="store_true", help="offer 1.0.1")
    p.add_argument("--rolled-back", action="store_true", help="1.0.1 was rolled back")
    p.add_argument("--dev", action="store_true", help="a DEV_BUILD image: the Developer page")
    p.add_argument(
        "--test-hooks", action="store_true", help="a test build: the Developer page with its tools"
    )
    args = p.parse_args()
    Handler.device = Device(args)
    print(f"mock device on http://localhost:{args.port}")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
