#!/bin/bash
# Downloads the compiler build.sh uses, wasi-sdk release 34, for this Mac or Linux
# machine into ~/.local/wasi-sdk-34.0-<arch>-<os>. Run once: ./get-wasi-sdk.sh
set -euo pipefail
case "$(uname -s)" in
  Darwin) os=macos ;;
  Linux) os=linux ;;
  *)
    echo "wasi-sdk 34 has builds for macOS and Linux only" >&2
    exit 1
    ;;
esac
case "$(uname -m)" in
  arm64 | aarch64) arch=arm64 ;;
  x86_64) arch=x86_64 ;;
  *)
    echo "no wasi-sdk 34 build for $(uname -m)" >&2
    exit 1
    ;;
esac
name=wasi-sdk-34.0-$arch-$os
if [[ -x "$HOME/.local/$name/bin/clang" ]]; then
  echo "already there: $HOME/.local/$name"
  exit 0
fi
mkdir -p "$HOME/.local"
url=https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/$name.tar.gz
echo "downloading $url"
# Unpacked beside its place and moved in whole: an interrupted download leaves nothing
# that build.sh would take for a compiler.
partial=$(mktemp -d "$HOME/.local/$name.partial.XXXXXX")
trap 'rm -rf "$partial"' EXIT
curl -fL "$url" | tar -xz -C "$partial"
rm -rf "${HOME:?}/.local/$name"
mv "$partial/$name" "$HOME/.local/$name"
echo "installed: $HOME/.local/$name"
