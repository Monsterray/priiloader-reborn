/* stand-in for <malloc.h>: memalign() with guard bytes behind every block, so the tests catch a
 * buffer overflow. free() is routed through hc_free() to check them. Include this after the
 * standard C++ headers (the unit wrappers do), so the free() macro never reaches their inlines. */
#pragma once
#include <stdlib.h>

void* hc_memalign(size_t alignment, size_t size);
void hc_free(void* ptr);

#define memalign(alignment, size) hc_memalign(alignment, size)
#define free(ptr) hc_free(ptr)
