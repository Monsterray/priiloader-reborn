/*
 * stand-in for libogc's gccore.h: the types, STACK_ALIGN and the ISFS calls priiloader's
 * parsers use. ISFS runs against the simulated NAND of sim.cpp.
 */
#ifndef STUB_GCCORE_H
#define STUB_GCCORE_H
#include "gctypes.h"
#include <string.h>

typedef struct _fstats {
	u32 file_length;
	u32 file_pos;
} fstats;

#define ISFS_OPEN_READ	1
#define ISFS_OPEN_WRITE	2
#define ISFS_OPEN_RW	3

/* the same as libogc's, but the pointer goes through uintptr_t: u32 is 32 bits wide on the PC only */
#define STACK_ALIGN(type, name, cnt, alignment) \
	u8 _al__##name[((sizeof(type)*(cnt)) + (alignment) + (((sizeof(type)*(cnt))%(alignment)) > 0 ? ((alignment) - ((sizeof(type)*(cnt))%(alignment))) : 0))]; \
	type *name = (type*)(((uintptr_t)(_al__##name)) + (((uintptr_t)(_al__##name))%(alignment) > 0 ? ((alignment) - (((uintptr_t)(_al__##name))%(alignment))) : 0))

#ifdef __cplusplus
extern "C" {
#endif
s32 ISFS_Open(const char* filepath, u8 mode);
s32 ISFS_Close(s32 fd);
s32 ISFS_Read(s32 fd, void* buffer, u32 length);
s32 ISFS_Write(s32 fd, const void* buffer, u32 length);
s32 ISFS_Seek(s32 fd, s32 where, s32 whence);
s32 ISFS_GetFileStats(s32 fd, fstats* status);
s32 ISFS_CreateFile(const char* filepath, u8 attributes, u8 ownerperm, u8 groupperm, u8 otherperm);
s32 ISFS_Delete(const char* filepath);
void DCFlushRange(void* startaddress, u32 len);
void ICInvalidateRange(void* startaddress, u32 len);
#ifdef __cplusplus
}
#endif
#endif
