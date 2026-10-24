# shellcheck shell=bash
# What several host tests share; a run.sh sources it.

# ESP-IDF's components: those of the IDF_PATH set, else ESPHome's cached 5.5.5.
IDF=${IDF_PATH:-$HOME/Library/Caches/esphome/idf/frameworks/5.5.5}
C=$IDF/components

# IDF's mbedtls with its default config, compiled once per machine for every test that
# links it: link "$MBEDTLS_CACHE"/*.o after build_mbedtls.
MBEDTLS_CACHE=${TMPDIR:-/tmp}/cgdn1-host-mbedtls

build_mbedtls() {
  if [[ -f $MBEDTLS_CACHE/done ]]; then
    return
  fi
  mkdir -p "$MBEDTLS_CACHE"
  local src lib=$C/mbedtls/mbedtls
  for src in "$lib"/library/*.c; do
    cc -c -O1 -w -I"$lib/include" -I"$lib/library" "$src" \
      -o "$MBEDTLS_CACHE/$(basename "$src" .c).o"
  done
  touch "$MBEDTLS_CACHE/done"
}
