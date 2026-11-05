/* stand-in for libogc's ogc/system.h: the types and the MEM2 arena calls mem2_manager.c uses.
 * The arena lives in test_mem2.c, backed by a real block. */
#ifndef STUB_OGC_SYSTEM_H
#define STUB_OGC_SYSTEM_H
#include <gctypes.h>
#ifdef __cplusplus
extern "C" {
#endif
void* SYS_GetArena2Lo(void);
void* SYS_GetArena2Hi(void);
void SYS_SetArena2Hi(void* newhi);
#ifdef __cplusplus
}
#endif
#endif
