"""The manifests the web_manifest test checks, made from real.json in <work>: altered,
signed by other.pem, and signed by <key> with a foreign base, a quote in the version
and an older seq. Usage: variants.py <key> <work>"""

import base64
import json
import os
import subprocess
import sys
import tempfile

key, work = sys.argv[1], sys.argv[2]
os.chdir(work)
real = json.load(open("real.json"))
payload = base64.b64decode(real["payload"])
inner = json.loads(payload)


def sign(p, k):
    with tempfile.NamedTemporaryFile() as f:
        f.write(p)
        f.flush()
        return subprocess.check_output(["openssl", "dgst", "-sha256", "-sign", k, f.name])


def write(name, p, sig):
    json.dump(
        {"payload": base64.b64encode(p).decode(), "sig": base64.b64encode(sig).decode()},
        open(name, "w"),
    )


def signed(name, obj):
    p = json.dumps(obj, separators=(",", ":")).encode()
    write(name, p, sign(p, key))


write(
    "tampered.json",
    payload.replace(b"cgdn1.swithy.com", b"cgdn1.swithy.con", 1),
    base64.b64decode(real["sig"]),
)
write("otherkey.json", payload, sign(payload, "other.pem"))
signed("badshape.json", dict(inner, base="https://evil.example/v/x/"))
signed("quote.json", dict(inner, version='x"><script>'))
signed("older.json", dict(inner, seq=inner["seq"] - 1000, version="older"))
