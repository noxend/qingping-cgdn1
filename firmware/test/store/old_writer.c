// The store of 1.0.0-rc7 (run.sh takes its wasm_store.c from git, its names prefixed
// with old_): keeps a module as that release's upload did, so the record it writes is
// the record a device updating from it holds.
#include <stdlib.h>
#include "wasm_store.h"

int old_install(const uint8_t *code, size_t len) {
  store_manifest_t *m = malloc(sizeof(*m));
  store_why_t why;
  store_err_t e = store_parse(code, len, m, &why);
  if (e == STORE_OK) {
    e = store_save(m, code);
  }
  free(m);
  return e;
}
