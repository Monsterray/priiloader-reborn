/*
 * test_mem2.c -- host tests of src/priiloader/source/mem2_manager.c (compiled unchanged, as C, for a 32 bit
 * target like the Wii: the file casts pointers to u32), against a bump allocator standing in for libogc's
 * heap and a real block standing in for MEM2.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdlib.h>
#include <string.h>

#include "mem2_manager.h"
#include <ogc/lwp_heap.h>

#define ARENA_SIZE 0x200000

static char* arena_base;
static char* arena_hi;

void* SYS_GetArena2Lo(void) { return arena_base; }
void* SYS_GetArena2Hi(void) { return arena_hi; }
void SYS_SetArena2Hi(void* newhi) { arena_hi = (char*)newhi; }

u32 __lwp_heap_init(heap_cntrl* heap, void* area, u32 size, u32 pg_size)
{
	heap->start = (heap_block*)area;
	heap->final = (heap_block*)((char*)area + size - sizeof(heap_block));
	heap->pg_size = pg_size;
	/* the user data of a block starts 8 bytes in; 32 aligned user data needs the block at 24 mod 32 */
	heap->cursor = (char*)(((uintptr_t)area + 31) / 32 * 32) + 24;
	return size;
}

void* __lwp_heap_allocate(heap_cntrl* heap, u32 size)
{
	char* block = (char*)heap->cursor;
	u32 rounded = (size + 31) / 32 * 32;
	if (size == 0 || (char*)heap->final < block + 8 + (uint64_t)rounded + 32)
		return NULL;
	heap_block* b = (heap_block*)block;
	b->front_flag = (rounded + 8) | HEAP_BLOCK_USED;
	b->back_flag = 0;
	heap->cursor = block + rounded + 32;
	return block + 8;
}

bool __lwp_heap_free(heap_cntrl* heap, void* ptr)
{
	(void)heap;
	heap_block* b = (heap_block*)((char*)ptr - 8);
	b->front_flag &= ~HEAP_BLOCK_USED;
	return true;
}

static void setup(void)
{
	free(arena_base);
	arena_base = (char*)malloc(ARENA_SIZE);
	memset(arena_base, 0, ARENA_SIZE);
	arena_hi = arena_base + ARENA_SIZE;
	/* every test starts with no areas set up, then adds its own; RemoveMem2Area undoes it */
	RemoveMem2Area(OTHER_AREA);
}

static bool add_area(void)
{
	return AddMem2Area(0x100000, OTHER_AREA);
}

static void test_pointer_size(void)
{
	CHECK(sizeof(void*) == 4, "built for a %d bit target: mem2_manager.c needs 32", (int)sizeof(void*) * 8);
}

static void test_roundtrip(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	char* p = (char*)mem2_malloc(100, OTHER_AREA);
	CHECK(p != NULL, "malloc failed");
	CHECK(((uintptr_t)p & 31) == 0, "not 32 aligned: %p", (void*)p);
	memset(p, 0x42, 100);
	char* q = (char*)mem2_malloc(64, OTHER_AREA);
	CHECK(q != NULL && q != p, "second block");
	CHECK(p[99] == 0x42, "blocks overlap");
	mem2_free(p, OTHER_AREA);
	mem2_free(q, OTHER_AREA);
	CHECK(RemoveMem2Area(OTHER_AREA), "RemoveMem2Area failed");
	CHECK(SYS_GetArena2Hi() == arena_base + ARENA_SIZE, "the arena top was not given back");
}

static void test_calloc_zeroes(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	unsigned char* p = (unsigned char*)mem2_malloc(256, OTHER_AREA);
	CHECK(p != NULL, "malloc failed");
	memset(p, 0xEE, 256);
	mem2_free(p, OTHER_AREA);
	unsigned char* c = (unsigned char*)mem2_calloc(16, 16, OTHER_AREA);
	CHECK(c != NULL, "calloc failed");
	for (int i = 0; i < 256; i++)
		CHECK(c[i] == 0, "calloc byte %d is %02X", i, c[i]);
}

static void test_calloc_overflow(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	/* 0x10000 * 0x10001 = 0x100010000: wrapped to 32 bits it is 0x10000, a 64 KB block that is far too small */
	void* p = mem2_calloc(0x10000, 0x10001, OTHER_AREA);
	CHECK(p == NULL, "calloc(0x10000, 0x10001) returned %p", p);
	p = mem2_calloc(0xFFFFFFFFu, 2, OTHER_AREA);
	CHECK(p == NULL, "calloc(0xFFFFFFFF, 2) returned %p", p);
}

static void test_calloc_zero_size(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	/* size 0 must not divide by zero; the allocation of 0 bytes may fail or not */
	void* p = mem2_calloc(5, 0, OTHER_AREA);
	(void)p;
	p = mem2_calloc(0, 5, OTHER_AREA);
	(void)p;
}

static void test_strndup_short_string(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	/* "abc" then 0xA5 bytes: only the string may be copied, not what follows its NUL */
	unsigned char src[16];
	memset(src, 0xA5, sizeof(src));
	memcpy(src, "abc", 4);
	unsigned char* dst = (unsigned char*)mem2_strndup((const char*)src, 100, OTHER_AREA);
	CHECK(dst != NULL, "strndup failed");
	CHECK(strcmp((char*)dst, "abc") == 0, "copy is '%s'", (char*)dst);
	for (int i = 4; i < 12; i++)
		CHECK(dst[i] != 0xA5, "dst[%d] is 0xA5: the copy read past the string's NUL", i);
}

static void test_strndup_limit(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	char* dst = mem2_strndup("abcdef", 3, OTHER_AREA);
	CHECK(dst != NULL && strcmp(dst, "abc") == 0, "limit 3 gave '%s'", dst ? dst : "(null)");
	dst = mem2_strndup("abcdef", 0, OTHER_AREA);
	CHECK(dst != NULL && dst[0] == 0, "limit 0 is not an empty string");
	CHECK(mem2_strndup(NULL, 5, OTHER_AREA) == NULL, "NULL source");
}

static void test_strndup_unterminated(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	char src[4] = { 'w', 'x', 'y', 'z' };
	char* dst = mem2_strndup(src, 4, OTHER_AREA);
	CHECK(dst != NULL && strcmp(dst, "wxyz") == 0, "got '%s'", dst ? dst : "(null)");
}

static void test_memalign_over_32(void)
{
	CHECK(add_area(), "AddMem2Area failed");
	void* p = mem2_memalign(64, 128, OTHER_AREA);
	CHECK(p == NULL, "align 64 returned %p, a block the heap only aligns to 32", p);
	p = mem2_memalign(32, 128, OTHER_AREA);
	CHECK(p != NULL && ((uintptr_t)p & 31) == 0, "align 32 failed");
}

static void test_bad_area(void)
{
	CHECK(mem2_memalign(32, 16, -1) == NULL, "area -1");
	CHECK(mem2_memalign(32, 16, MAX_AREA) == NULL, "area MAX_AREA");
	CHECK(mem2_memalign(32, 16, OTHER_AREA) == NULL, "an area that was never added");
}

static const struct hc_test tests[] = {
	{ "pointer_size", test_pointer_size, false },
	{ "roundtrip", test_roundtrip, false },
	{ "calloc_zeroes", test_calloc_zeroes, false },
	{ "calloc_overflow", test_calloc_overflow, false },
	{ "calloc_zero_size", test_calloc_zero_size, false },
	{ "strndup_short_string", test_strndup_short_string, false },
	{ "strndup_limit", test_strndup_limit, false },
	{ "strndup_unterminated", test_strndup_unterminated, false },
	{ "memalign_over_32", test_memalign_over_32, false },
	{ "bad_area", test_bad_area, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, setup);
}
