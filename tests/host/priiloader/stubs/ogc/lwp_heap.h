/* stand-in for libogc's ogc/lwp_heap.h: the types mem2_manager.c uses. test_mem2.c implements the
 * __lwp_heap_* calls with a bump allocator. The user data starts 8 bytes into a block, on the free-list
 * links, and the u32 just before it holds the offset mem2_manager.c's __lwp_heap_usrblockat expects
 * (0: the block is exactly 8 bytes before the user pointer). */
#ifndef STUB_OGC_LWP_HEAP_H
#define STUB_OGC_LWP_HEAP_H
#include <gctypes.h>

#define HEAP_BLOCK_USED 1
#define HEAP_BLOCK_USED_OVERHEAD 8

typedef struct _heap_block {
	u32 front_flag;
	u32 back_flag;
	struct _heap_block* next;
	struct _heap_block* previous;
} heap_block;

typedef struct {
	heap_block* start;
	heap_block* final;
	u32 pg_size;
	void* cursor;
} heap_cntrl;

#ifdef __cplusplus
extern "C" {
#endif
u32 __lwp_heap_init(heap_cntrl* heap, void* area, u32 size, u32 pg_size);
void* __lwp_heap_allocate(heap_cntrl* heap, u32 size);
bool __lwp_heap_free(heap_cntrl* heap, void* ptr);
#ifdef __cplusplus
}
#endif
#endif
