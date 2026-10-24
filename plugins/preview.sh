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
python3 -I -B "$HERE/preview/frames.py" "$FRAMES" 4
for png in "$FRAMES"/[123]-*.png; do
  if [[ -f $png ]]; then
    echo "$png"
  fi
done
exit "$status"
