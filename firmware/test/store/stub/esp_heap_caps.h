// Host stand-in: one heap, so PSRAM is malloc.
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
static inline void *heap_caps_malloc(size_t size, int caps) {
  (void) caps;
  return malloc(size);
}
static inline void heap_caps_free(void *p) {
  free(p);
}
