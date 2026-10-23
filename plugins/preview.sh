#!/bin/bash
# Builds a plugin and runs it on this computer as the device would:
#   ./preview.sh <folder> [setting=value ...]
# The frames it draws go to <folder>/preview/*.png: when it is shown, after its network
# answer, after each of its timers. A plugin the device would refuse, or one that
# overruns its limits, says so here with the same reason. Needs cmake and a C compiler
# besides build.sh's wasi-sdk; the first run also downloads WAMR (the version the
# firmware uses) unless the firmware has been built in this checkout.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)

if [[ $# -lt 1 || ! -f "$1/plugin.c" ]]; then
  echo "usage: $0 <plugin folder> [setting=value ...]" >&2
  exit 2
fi
for tool in cmake cc python3 curl; do
  if ! command -v "$tool" >/dev/null; then
    echo "$tool not found. On macOS: xcode-select --install; brew install cmake." >&2
    echo "On Debian or Ubuntu: sudo apt install cmake build-essential python3 curl." >&2
    exit 1
  fi
done
DIR=$(cd "$1" && pwd)
shift
MODULE="$DIR/$(basename "$DIR").wasm"
"$HERE/build.sh" "$DIR" "$MODULE"

BUILD="$HERE/preview/build"
mkdir -p "$BUILD"
if [[ ! -x "$BUILD/preview" ]]; then
  echo "building the preview (once, about a minute)..."
fi
if ! { cmake -S "$HERE/preview" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release &&
  cmake --build "$BUILD" -j 8; } >"$BUILD/log" 2>&1; then
  cat "$BUILD/log" >&2
  echo "the preview did not build: see above" >&2
  exit 1
fi

FRAMES="$DIR/preview"
mkdir -p "$FRAMES"
rm -f "$FRAMES"/[123]-*.png
status=0
(cd "$FRAMES" && "$BUILD/preview" "$MODULE" . "$@") || status=$?

# The frames as PNG, four times the panel's 160 x 128 so they read on a screen.
python3 -I - "$FRAMES" <<'PY'
import glob, os, struct, sys, zlib

def png(path, width, height, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    raw = b"".join(b"\0" + row for row in rows)
    head = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(raw))
                + chunk(b"IEND", b""))

SCALE = 4
for pgm in sorted(glob.glob(os.path.join(sys.argv[1], "*.pgm"))):
    data = open(pgm, "rb").read()
    header, pixels = data.split(b"255\n", 1)
    width, height = map(int, header.split()[1:3])
    rows = []
    for y in range(height):
        row = bytes(b for b in pixels[y * width:(y + 1) * width] for _ in range(SCALE))
        rows += [row] * SCALE
    png(pgm[:-4] + ".png", width * SCALE, height * SCALE, rows)
    os.remove(pgm)
    print(pgm[:-4] + ".png")
PY
exit "$status"
