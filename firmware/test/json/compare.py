"""The JSON scanner against Python's json module, on the samples in <work>/samples and
their mutations. Usage: compare.py <work> <mutations per sample>"""

import glob
import json
import os
import random
import subprocess
import sys

work, per_sample = sys.argv[1], int(sys.argv[2])
DEPTH_MAX = 32  # JSON_DEPTH_MAX


class Refused(Exception):
    pass


def refuse(_):
    raise Refused()


def number(text):
    return ("n", text)


# Python's reading in the harness's notation, or "bad". An object comes back tagged,
# so it is never taken for an array.
def tree(text):
    try:
        doc = json.loads(
            text.decode("utf-8"),
            object_pairs_hook=lambda p: ("obj", p),
            parse_int=number,
            parse_float=number,
            parse_constant=refuse,
        )
    except (UnicodeDecodeError, ValueError, Refused, RecursionError):
        return "bad"
    out = []

    def string(s):
        if any(0xD800 <= ord(c) <= 0xDFFF for c in s):
            raise Refused()
        return s.encode("utf-8").hex()

    def walk(v, depth):
        if isinstance(v, tuple) and v[0] == "obj":
            if depth >= DEPTH_MAX:
                raise Refused()
            out.append("{")
            for k, x in v[1]:
                out.append("k" + ("" if "\0" in k else string(k)) + ":")
                walk(x, depth + 1)
                out.append(",")
            out.append("}")
        elif isinstance(v, list):
            if depth >= DEPTH_MAX:
                raise Refused()
            out.append("[")
            for x in v:
                walk(x, depth + 1)
                out.append(",")
            out.append("]")
        elif isinstance(v, tuple):
            out.append("n" + v[1])
        elif isinstance(v, str):
            out.append("s" + ("!" if "\0" in v else string(v)))
        elif v is True:
            out.append("t")
        elif v is False:
            out.append("f")
        else:
            out.append("z")

    try:
        walk(doc, 0)
    except Refused:
        return "bad"
    return "".join(out)


SPECIAL = [
    b"{",
    b"}",
    b"[",
    b"]",
    b'"',
    b",",
    b":",
    b"\\",
    b"0",
    b"1",
    b"-",
    b".",
    b"e",
    b"E",
    b"+",
    b"u",
    b"t",
    b"f",
    b"n",
    b" ",
    b"\n",
    b"\x00",
    b"\x1f",
    b"\x7f",
    b"\xc3",
    b"\xa9",
    b"\xed\xa0\x80",
    b"\xf4\x90\x80\x80",
    b"\xe0\x80\xaf",
    b"\\u0000",
    b"\\ud83d\\ude00",
    b"\\ud83d",
    b"\\ude00",
    b"\\u00e9",
    b"\\uD7FF",
    b"true",
    b"false",
    b"null",
    b"01",
    b"-0",
    b"1e5",
    b"1.",
    b".5",
    b"[" * 40,
    b"]" * 40,
    b"\\/",
    b"\\x",
]


def mutate(rng, data):
    data = bytearray(data)
    for _ in range(rng.randint(1, 3)):
        op = rng.randrange(5)
        at = rng.randrange(len(data) + 1)
        if op == 0 and data:
            data[min(at, len(data) - 1)] = rng.randrange(256)
        elif op == 1:
            data[at:at] = rng.choice(SPECIAL)
        elif op == 2:
            del data[at : at + rng.randint(1, 8)]
        elif op == 3:
            del data[at:]
        else:
            end = min(len(data), at + rng.randint(1, 40))
            data[at:at] = data[at:end]
    return bytes(data)


rng = random.Random(20261022)
cases = []
for path in sorted(glob.glob(os.path.join(work, "samples", "*.json"))):
    text = open(path, "rb").read()
    cases.append(text)
    cases.extend(mutate(rng, text) for _ in range(per_sample))
# Hand-made edges JSON's own grammar decides.
cases += [
    b"",
    b" ",
    b"0",
    b"-",
    b"[]",
    b"{}",
    b"[1,]",
    b'{"a":1,}',
    b'{"a" 1}',
    b"[1 2]",
    b'"\\ud83d\\ude00"',
    b'"\\ud83d"',
    b'"\\ude00x"',
    b"[" * 32 + b"]" * 32,
    b"[" * 33 + b"]" * 33,
    b'{"a":' * 32 + b"1" + b"}" * 32,
    b'"a\x7fb"',
    b'"\t"',
    b"1 ",
    b" 1",
    b"1 2",
    b"01",
    b"[-01]",
    b"1.e5",
    b"1e",
    b"-",
    b"[.5]",
    b"tru",
    b"nul",
    b'{"a":1,"a":2}',
    b'"\\u0000"',
]

files = []
for i, text in enumerate(cases):
    path = os.path.join(work, f"case{i}.bin")
    open(path, "wb").write(text)
    files.append(path)
got = []
for start in range(0, len(files), 500):
    chunk = files[start : start + 500]
    out = subprocess.run(
        [os.path.join(work, "harness")] + chunk, capture_output=True, check=True
    ).stdout.decode()
    got += out.split("\n")[: len(chunk)]

failed = 0
valid = 0
for text, line in zip(cases, got):
    want = tree(text)
    valid += want != "bad"
    if line != want:
        failed += 1
        if failed <= 5:
            print("DIFFERENT for", text[:200])
            print("  scanner:", line[:300])
            print("  python: ", want[:300])
print(f"{len(cases)} texts, {valid} valid, {failed} different")
sys.exit(1 if failed else 0)
