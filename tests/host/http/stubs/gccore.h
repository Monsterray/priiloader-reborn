/* stand-in for libogc's gccore.h: the types and the cache calls HTTP_Parser makes */
#ifndef STUB_GCCORE_H
#define STUB_GCCORE_H
#include "gctypes.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
void DCFlushRange(void* startaddress, u32 len);
void DCInvalidateRange(void* startaddress, u32 len);
#ifdef __cplusplus
}
#endif
#endif
