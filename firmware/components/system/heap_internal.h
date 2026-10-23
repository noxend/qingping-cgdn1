// The heap plain malloc draws from. With PSRAM in CAPS_ALLOC mode, MALLOC_CAP_8BIT
// takes PSRAM in too, which plain malloc never gets: a free-memory figure or a
// check before a malloc asks for HEAP_INTERNAL.
#pragma once

#include "esp_heap_caps.h"

#define HEAP_INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define HEAP_PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

// A large or passing buffer (a TLS session, a network body, a JSON tree, a preview
// frame) goes to PSRAM when the board has it, internal RAM otherwise: the internal
// heap stays for Wi-Fi, lwIP and the stacks. Freed with free() or heap_caps_free().
static inline void *heap_large_malloc(size_t size) {
  return heap_caps_malloc_prefer(size, 2, HEAP_PSRAM, HEAP_INTERNAL);
}

static inline void *heap_large_calloc(size_t n, size_t size) {
  return heap_caps_calloc_prefer(n, size, 2, HEAP_PSRAM, HEAP_INTERNAL);
}

// Points mbedTLS (CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC) and cJSON at heap_large_*. Once, at
// the start of app_main, before anything opens a TLS session or builds JSON.
void heap_large_install(void);
