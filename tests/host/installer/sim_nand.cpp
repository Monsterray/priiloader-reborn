/* the simulated NAND, ES and SHA of sim_nand.h, behind the libogc names the Installer calls */
#include <gccore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_nand.h"

// some C libraries pull in <malloc.h> (our stub) from <stdlib.h>: hc_free() must call the real free()
#undef free

SimNand g_nand;
bool g_verbose = false;
bool g_rename_no_replace_default = false;

static const s32 ERR_INJECTED = -1;
static const s32 ERR_EXISTS = -105;
static const s32 ERR_NOT_FOUND = -106;
static const s32 ERR_BAD_FD = -4;
static const s32 ERR_INVALID = -101; // ISFS_EINVAL: a path IOS refuses

// IOS limits: a path is at most 63 characters (64 with the NUL), each component at most 12 (8.3 style)
static bool path_valid(const char* path);
static bool path_ok(const char* path)
{
	bool ok = path_valid(path);
	if (!ok)
		g_nand.bad_paths++;
	return ok;
}

static bool path_valid(const char* path)
{
	if (path == NULL || path[0] != '/')
		return false;
	size_t len = strlen(path);
	if (len >= 64)
		return false;
	size_t run = 0;
	for (size_t i = 1; i <= len; i++)
	{
		if (path[i] == '/' || path[i] == 0)
		{
			if (run > 12)
				return false;
			run = 0;
		}
		else
			run++;
	}
	return true;
}

void SimNand::reset()
{
	files.clear();
	fds.clear();
	calls = 0;
	fail_at = 0;
	sticky = false;
	fail_rename_dest.clear();
	fail_rename_dest_times = -1;
	rename_no_replace = g_rename_no_replace_default;
	short_read = short_write = silent_short_write = false;
	bad_closes = 0;
	bad_paths = 0;
	guard_violations = 0;
	nandloader_tmd.clear();
}

int SimNand::open_fds() const
{
	int n = 0;
	for (size_t i = 0; i < fds.size(); i++)
		n += fds[i].open ? 1 : 0;
	return n;
}

bool SimNand::exists(const std::string& path) const
{
	return files.find(path) != files.end();
}

const std::vector<uint8_t>* SimNand::get(const std::string& path) const
{
	std::map<std::string, SimFile>::const_iterator it = files.find(path);
	return it == files.end() ? NULL : &it->second.data;
}

void SimNand::put(const std::string& path, const std::vector<uint8_t>& data)
{
	files[path].data = data;
}

int hc_quiet_printf(const char* fmt, ...)
{
	if (!g_verbose)
		return 0;
	va_list ap;
	va_start(ap, fmt);
	int ret = vprintf(fmt, ap);
	va_end(ap);
	return ret;
}

// counts the call, and says whether it must fail
static bool inject(void)
{
	g_nand.calls++;
	if (g_nand.fail_at == 0)
		return false;
	return g_nand.calls == g_nand.fail_at || (g_nand.sticky && g_nand.calls > g_nand.fail_at);
}

static SimFd* fd_get(s32 fd)
{
	if (fd < 0 || (size_t)fd >= g_nand.fds.size() || !g_nand.fds[fd].open)
		return NULL;
	return &g_nand.fds[fd];
}

s32 ISFS_Open(const char* filepath, u8 mode)
{
	(void)mode;
	if (!path_ok(filepath))
		return ERR_INVALID;
	if (inject())
		return ERR_INJECTED;
	if (!g_nand.exists(filepath))
		return ERR_NOT_FOUND;

	// like IOS, hand out the lowest free descriptor, starting at 0
	size_t i = 0;
	while (i < g_nand.fds.size() && g_nand.fds[i].open)
		i++;
	if (i == g_nand.fds.size())
		g_nand.fds.push_back(SimFd());
	g_nand.fds[i].open = true;
	g_nand.fds[i].path = filepath;
	g_nand.fds[i].pos = 0;
	return (s32)i;
}

s32 ISFS_Close(s32 fd)
{
	SimFd* f = fd_get(fd);
	if (f == NULL)
	{
		g_nand.bad_closes++;
		return ERR_BAD_FD;
	}
	f->open = false;
	return inject() ? ERR_INJECTED : 0;
}

s32 ISFS_Read(s32 fd, void* buffer, u32 length)
{
	if (inject())
		return ERR_INJECTED;
	SimFd* f = fd_get(fd);
	if (f == NULL)
		return ERR_BAD_FD;
	std::map<std::string, SimFile>::iterator it = g_nand.files.find(f->path);
	if (it == g_nand.files.end())
		return ERR_NOT_FOUND;

	std::vector<uint8_t>& data = it->second.data;
	size_t want = length;
	if (g_nand.short_read && want > 0)
		want--;
	size_t avail = f->pos < data.size() ? data.size() - f->pos : 0;
	size_t n = want < avail ? want : avail;
	memcpy(buffer, data.data() + f->pos, n);
	f->pos += n;
	return (s32)n;
}

s32 ISFS_Write(s32 fd, const void* buffer, u32 length)
{
	if (inject())
		return ERR_INJECTED;
	SimFd* f = fd_get(fd);
	if (f == NULL)
		return ERR_BAD_FD;
	std::map<std::string, SimFile>::iterator it = g_nand.files.find(f->path);
	if (it == g_nand.files.end())
		return ERR_NOT_FOUND;

	size_t stored = length;
	if ((g_nand.short_write || g_nand.silent_short_write) && stored > 0)
		stored--;
	std::vector<uint8_t>& data = it->second.data;
	if (data.size() < f->pos + stored)
		data.resize(f->pos + stored);
	memcpy(data.data() + f->pos, buffer, stored);
	f->pos += stored;
	return g_nand.silent_short_write ? (s32)length : (s32)stored;
}

s32 ISFS_GetFileStats(s32 fd, fstats* status)
{
	if (inject())
		return ERR_INJECTED;
	SimFd* f = fd_get(fd);
	if (f == NULL)
		return ERR_BAD_FD;
	status->file_length = (u32)g_nand.files[f->path].data.size();
	status->file_pos = (u32)f->pos;
	return 0;
}

s32 ISFS_Delete(const char* filepath)
{
	if (!path_ok(filepath))
		return ERR_INVALID;
	if (inject())
		return ERR_INJECTED;
	return g_nand.files.erase(filepath) ? 0 : ERR_NOT_FOUND;
}

s32 ISFS_Rename(const char* filepathOld, const char* filepathNew)
{
	if (!path_ok(filepathOld) || !path_ok(filepathNew))
		return ERR_INVALID;
	if (inject())
		return ERR_INJECTED;
	if (!g_nand.fail_rename_dest.empty() && g_nand.fail_rename_dest == filepathNew && g_nand.fail_rename_dest_times != 0)
	{
		if (g_nand.fail_rename_dest_times > 0)
			g_nand.fail_rename_dest_times--;
		return ERR_INJECTED;
	}
	std::map<std::string, SimFile>::iterator it = g_nand.files.find(filepathOld);
	if (it == g_nand.files.end())
		return ERR_NOT_FOUND;
	// IOS renames a file to another directory only, never to another file name
	const char* oldName = strrchr(filepathOld, '/');
	const char* newName = strrchr(filepathNew, '/');
	if (strcmp(oldName, newName) != 0)
	{
		g_nand.bad_paths++;
		return ERR_INVALID;
	}
	if (g_nand.exists(filepathNew) && g_nand.rename_no_replace)
		return ERR_EXISTS;
	SimFile moved = it->second;
	if (strcmp(filepathOld, filepathNew) != 0)
	{
		g_nand.files.erase(it);
		g_nand.files[filepathNew] = moved;
	}
	return 0;
}

s32 ISFS_CreateFile(const char* filepath, u8 attributes, u8 ownerperm, u8 groupperm, u8 otherperm)
{
	if (!path_ok(filepath))
		return ERR_INVALID;
	if (inject())
		return ERR_INJECTED;
	if (g_nand.exists(filepath))
		return ERR_EXISTS;
	SimFile file;
	file.attributes = attributes;
	file.ownerperm = ownerperm;
	file.groupperm = groupperm;
	file.otherperm = otherperm;
	g_nand.files[filepath] = file;
	return 0;
}

s32 ISFS_GetAttr(const char* filepath, u32* ownerID, u16* groupID, u8* attributes, u8* ownerperm, u8* groupperm, u8* otherperm)
{
	if (!path_ok(filepath))
		return ERR_INVALID;
	if (inject())
		return ERR_INJECTED;
	std::map<std::string, SimFile>::iterator it = g_nand.files.find(filepath);
	if (it == g_nand.files.end())
		return ERR_NOT_FOUND;
	*ownerID = 0x1000;
	*groupID = 0x1;
	*attributes = it->second.attributes;
	*ownerperm = it->second.ownerperm;
	*groupperm = it->second.groupperm;
	*otherperm = it->second.otherperm;
	return 0;
}

// not SHA-1, but 160 bits of a deterministic hash: all the Installer needs is "same data, same hash"
s32 SHA_Calculate(const void* data, u32 dataLen, void* hashOut)
{
	const uint8_t* p = (const uint8_t*)data;
	uint32_t out[5];
	for (int k = 0; k < 5; k++)
	{
		uint32_t h = 2166136261u ^ (uint32_t)(k * 0x9E3779B1u);
		for (u32 i = 0; i < dataLen; i++)
			h = (h ^ p[i]) * 16777619u;
		out[k] = h ^ dataLen;
	}
	memcpy(hashOut, out, sizeof(out));
	return 0;
}

static const u64 NANDLOADER_TITLE = 0x0000000100000200ll;
static const char* SM_TMD_PATH = "/title/00000001/00000002/content/title.tmd";

s32 ES_GetStoredTMDSize(u64 titleID, u32* size)
{
	const std::vector<uint8_t>* tmdData = titleID == NANDLOADER_TITLE ? &g_nand.nandloader_tmd : g_nand.get(SM_TMD_PATH);
	if (tmdData == NULL || tmdData->empty())
		return -106;
	*size = (u32)tmdData->size();
	return 0;
}

s32 ES_GetStoredTMD(u64 titleID, signed_blob* tmdBlob, u32 size)
{
	const std::vector<uint8_t>* tmdData = titleID == NANDLOADER_TITLE ? &g_nand.nandloader_tmd : g_nand.get(SM_TMD_PATH);
	if (tmdData == NULL || tmdData->size() != size)
		return -106;
	memcpy(tmdBlob, tmdData->data(), size);
	return 0;
}

s32 ES_GetTitleContentsCount(u64 titleID, u32* count)
{
	if (titleID != NANDLOADER_TITLE || g_nand.nandloader_tmd.empty())
		return -106;
	*count = 1;
	return 0;
}

// memalign() with 32 guard bytes behind each block; hc_free() checks them
struct Block
{
	void* user;
	size_t size;
};
static std::vector<Block> g_blocks;

void* hc_memalign(size_t alignment, size_t size)
{
	(void)alignment;
	uint8_t* raw = (uint8_t*)malloc(size + 32);
	if (raw == NULL)
		return NULL;
	memset(raw + size, 0xA5, 32);
	Block b = { raw, size };
	g_blocks.push_back(b);
	return raw;
}

void hc_free(void* ptr)
{
	for (size_t i = 0; i < g_blocks.size(); i++)
	{
		if (g_blocks[i].user != ptr)
			continue;
		const uint8_t* guard = (const uint8_t*)ptr + g_blocks[i].size;
		for (int k = 0; k < 32; k++)
		{
			if (guard[k] != 0xA5)
			{
				g_nand.guard_violations++;
				break;
			}
		}
		g_blocks.erase(g_blocks.begin() + i);
		break;
	}
	free(ptr);
}
