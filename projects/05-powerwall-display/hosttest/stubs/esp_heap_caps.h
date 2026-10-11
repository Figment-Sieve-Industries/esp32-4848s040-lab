#pragma once
// Host stand-in for ESP-IDF's heap_caps API (mem_util.h): plain malloc.
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4

inline void *heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
inline void *heap_caps_calloc(size_t c, size_t n, unsigned) { return calloc(c, n); }
inline void *heap_caps_realloc(void *p, size_t n, unsigned) { return realloc(p, n); }
inline void *heap_caps_aligned_alloc(size_t, size_t n, unsigned) { return malloc(n); }
inline void heap_caps_free(void *p) { free(p); }
