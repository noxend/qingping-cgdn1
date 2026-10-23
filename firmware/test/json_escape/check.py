"""json_escape against Python's json module: every byte alone, a few strings, and the
cut at every buffer size. Usage: check.py <harness>"""

import json
import subprocess
import sys

harness = sys.argv[1]
failed = 0


def escape(data, cap=2048):
    result = subprocess.run([harness, data.hex(), str(cap)], capture_output=True, check=True)
    return result.stdout


def check(name, ok):
    global failed
    if not ok:
        failed += 1
        print(f"BAD {name}")


def reads_back(out):
    """The string out stands for, or None when it is not valid inside a JSON string."""
    try:
        return json.loads(b'"' + out + b'"')
    except ValueError:
        return None


for b in range(1, 0x80):
    out = escape(bytes([b]))
    check(f"byte {b:#04x} reads back", reads_back(out) == chr(b))
    if b < 0x20:
        check(f"byte {b:#04x} as \\u00XX", out == f"\\u{b:04x}".encode())
for b in range(0x80, 0x100):
    check(f"byte {b:#04x} stays", escape(bytes([b])) == bytes([b]))

for text in ['a"b\\c', "\x01\x1f\n\t", 'Кафе "Сонях"\n', "plain"]:
    check(f"{text!r} reads back", reads_back(escape(text.encode())) == text)

ten = "\x01" * 10
for cap in range(0, 70):
    out = escape(ten.encode(), cap)
    whole = len(out) % 6 == 0 and len(out) <= max(cap - 1, 0)
    check(f"cap {cap}: {len(out)} bytes, whole escapes", whole and reads_back(out) in ten)

print("ALL PASS" if not failed else f"{failed} FAILED")
sys.exit(1 if failed else 0)
