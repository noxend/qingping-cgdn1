#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_8BIT 0
static inline void *heap_caps_malloc(size_t n, int caps) {
  (void) caps;
  return malloc(n);
}
static inline size_t heap_caps_get_free_size(int caps) {
  (void) caps;
  return 100000;
}
