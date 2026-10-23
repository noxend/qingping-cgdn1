// Host stand-in: one heap. heap_caps_free counts, so a test can see code given back.
#pragma once
#include <stdlib.h>
extern int host_heap_caps_frees;
static inline void heap_caps_free(void *p) {
  if (p) {
    host_heap_caps_frees++;
  }
  free(p);
}
#define MALLOC_CAP_SPIRAM 0
static inline void *heap_caps_calloc(size_t n, size_t size, int caps) {
  (void) caps;
  return calloc(n, size);
}
