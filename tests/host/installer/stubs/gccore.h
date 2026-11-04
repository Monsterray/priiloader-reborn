/* stand-in for libogc's gccore.h: only what src/Installer uses (ISFS, ES, SHA, TMD types).
 * The implementations are in tests/host/installer/sim_nand.cpp, a simulated NAND. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <gctypes.h>

#define ATTRIBUTE_ALIGN(v)
#define STACK_ALIGN(type, name, cnt, alignment) type name##_stack[cnt]; type* name = name##_stack

#define ISFS_MAXPATH 64
#define ISFS_OPEN_READ 1
#define ISFS_OPEN_WRITE 2
#define ISFS_OPEN_RW (ISFS_OPEN_READ | ISFS_OPEN_WRITE)

typedef struct
{
	u32 file_length;
	u32 file_pos;
} fstats;

s32 ISFS_Open(const char* filepath, u8 mode);
s32 ISFS_Close(s32 fd);
s32 ISFS_Read(s32 fd, void* buffer, u32 length);
s32 ISFS_Write(s32 fd, const void* buffer, u32 length);
s32 ISFS_GetFileStats(s32 fd, fstats* status);
s32 ISFS_Delete(const char* filepath);
s32 ISFS_Rename(const char* filepathOld, const char* filepathNew);
s32 ISFS_CreateFile(const char* filepath, u8 attributes, u8 ownerperm, u8 groupperm, u8 otherperm);
s32 ISFS_GetAttr(const char* filepath, u32* ownerID, u16* groupID, u8* attributes, u8* ownerperm, u8* groupperm, u8* otherperm);

s32 SHA_Calculate(const void* data, u32 dataLen, void* hashOut);
static inline void DCFlushRange(void* ptr, u32 len)
{
	(void)ptr;
	(void)len;
}

#define MAX_SIGNED_TMD_SIZE 0x49E4
#define SIGNATURE_PAYLOAD(blob) ((void*)(((u8*)(blob)) + 0x140))

typedef u8 signed_blob;

#pragma pack(push, 1)
typedef struct
{
	u32 cid;
	u16 index;
	u16 type;
	u64 size;
	u8 hash[20];
} tmd_content;

typedef struct
{
	u8 header[0x9c];
	u32 access_rights;
	u16 title_version;
	u16 num_contents;
	u16 boot_index;
	u16 padding;
	tmd_content contents[];
} tmd;
#pragma pack(pop)

s32 ES_GetStoredTMDSize(u64 titleID, u32* size);
s32 ES_GetStoredTMD(u64 titleID, signed_blob* tmdBlob, u32 size);
s32 ES_GetTitleContentsCount(u64 titleID, u32* count);
