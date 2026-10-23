#include "wasm_binary.h"

#include <string.h>

#define SECTION_CUSTOM 0

static const uint8_t HEADER[8] = {0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00};

bool wasm_leb_read(const uint8_t **p, const uint8_t *end, uint32_t *out) {
  uint32_t v = 0;
  for (int shift = 0; shift < 35 && *p < end; shift += 7) {
    uint8_t b = *(*p)++;
    v |= (uint32_t) (b & 0x7f) << shift;
    if (!(b & 0x80)) {
      *out = v;
      return true;
    }
  }
  return false;
}

bool wasm_sections(const uint8_t *code, uint32_t len, wasm_section_fn fn, void *ctx) {
  if (len < sizeof(HEADER) || memcmp(code, HEADER, sizeof(HEADER)) != 0) {
    return false;
  }
  const uint8_t *p = code + sizeof(HEADER);
  const uint8_t *end = code + len;
  while (p < end) {
    uint8_t id = *p++;
    uint32_t size;
    if (!wasm_leb_read(&p, end, &size) || size > (uint32_t) (end - p)) {
      return false;
    }
    if (!fn(id, p, size, ctx)) {
      return true;
    }
    p += size;
  }
  return true;
}

typedef struct {
  const char *name;
  const uint8_t *found;
  uint32_t size;
} custom_find_t;

static bool match_custom(uint8_t id, const uint8_t *body, uint32_t size, void *ctx) {
  custom_find_t *f = ctx;
  if (id != SECTION_CUSTOM) {
    return true;
  }
  const uint8_t *p = body;
  const uint8_t *end = body + size;
  uint32_t name_len;
  if (!wasm_leb_read(&p, end, &name_len) || name_len > (uint32_t) (end - p)) {
    return true;  // a custom section with a broken name is someone else's problem
  }
  if (name_len != strlen(f->name) || memcmp(p, f->name, name_len) != 0) {
    return true;
  }
  p += name_len;
  f->found = p;
  f->size = (uint32_t) (end - p);
  return false;
}

const uint8_t *wasm_custom_section(const uint8_t *code, uint32_t len, const char *name,
                                   uint32_t *size) {
  custom_find_t f = {.name = name};
  if (!wasm_sections(code, len, match_custom, &f) || !f.found) {
    return NULL;
  }
  *size = f.size;
  return f.found;
}
