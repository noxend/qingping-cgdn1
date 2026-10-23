"""The harness's frames, LED colours and alert stats against golden/, then every frame to
PNG. Usage: compare.py <golden> <out> <harness output>"""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../../plugins/preview"))
from frames import convert, read_pgm, read_png  # noqa: E402

golden, out, got_path = sys.argv[1:]


want = {}
for line in open(os.path.join(golden, "led.txt"), encoding="utf-8"):
    name, led, *stats = line.rstrip("\n").split(" ", 2)
    want[name] = (led, json.loads(stats[0]) if stats else None)

failed = 0
seen = 0
for line in open(got_path, encoding="utf-8"):
    if not line.strip():
        continue
    name, led, *stats = line.rstrip("\n").split(" ", 2)
    seen += 1
    problems = []
    width, height, pixels = read_pgm(os.path.join(out, name + ".pgm"))
    expected = read_png(os.path.join(golden, name + ".png"))
    if pixels != expected:
        diff = sum(a != b for a, b in zip(pixels, expected))
        problems.append(f"{diff} pixels differ from golden/{name}.png")
    if led != want[name][0]:
        problems.append(f"LED {led}, golden {want[name][0]}")
    if want[name][1] is not None:
        mine = json.loads(stats[0]) if stats else {}
        # The native plugin had no "err" (the module has no esp_err_t) and named the region
        # by its label, the module by its key.
        for key in ("fetch_ok", "fetch_fail", "http", "alerts", "draws", "level"):
            if mine.get(key) != want[name][1][key]:
                problems.append(f"stats {key} {mine.get(key)}, golden {want[name][1][key]}")
    print(f"{name:18} {'ok' if not problems else 'FAIL: ' + '; '.join(problems)}")
    failed += bool(problems)

convert(out)
if seen != len(want):
    print(f"{seen} plugin cases ran, golden/ has {len(want)}")
    failed += 1
print(f"{failed} plugin cases differ")
sys.exit(1 if failed else 0)
