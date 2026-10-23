// Host stand-in: one heap, plenty of it. heap_caps_free counts, as in test/wasm.
#pragma once
#include <stdlib.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#define host_block_size malloc_size
#else
#include <malloc.h>
#define host_block_size malloc_usable_size
#endif
extern int host_heap_caps_frees;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
static inline void heap_caps_free(void *p) {
  if (p) {
    host_heap_caps_frees++;
  }
  free(p);
}
static inline void *heap_caps_malloc(size_t size, int caps) {
  (void) caps;
  return malloc(size);
}
static inline void *heap_caps_calloc(size_t n, size_t size, int caps) {
  (void) caps;
  return calloc(n, size);
}
static inline void *heap_caps_realloc(void *p, size_t size, int caps) {
  (void) caps;
  return realloc(p, size);
}
// The *_prefer forms take a list of capabilities to try in order; one heap here.
static inline void *heap_caps_malloc_prefer(size_t size, size_t num, ...) {
  (void) num;
  return malloc(size);
}
static inline void *heap_caps_calloc_prefer(size_t n, size_t size, size_t num, ...) {
  (void) num;
  return calloc(n, size);
}
static inline size_t heap_caps_get_free_size(int caps) {
  (void) caps;
  return 4 * 1024 * 1024;
}
static inline size_t heap_caps_get_minimum_free_size(int caps) {
  return heap_caps_get_free_size(caps);
}
static inline size_t heap_caps_get_total_size(int caps) {
  return 2 * heap_caps_get_free_size(caps);
}
static inline size_t heap_caps_get_largest_free_block(int caps) {
  return heap_caps_get_free_size(caps);
}
// The block's size, as the runtime's realloc needs it to keep a grown memory's pages.
static inline size_t heap_caps_get_allocated_size(void *p) {
  return p ? host_block_size(p) : 0;
}
