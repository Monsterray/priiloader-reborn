/*
 * sim.cpp -- the simulated NAND (ISFS), MEM2 heap and small libogc stand-ins of the priiloader
 * host tests. See sim.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "gccore.h"
#include "gecko.h"
#include "unistd.h"
#include "sim.h"
#include "mem2_manager.h"
#include "../hostcheck.h"

extern "C" {
int sim_fail_stats;
const char* sim_fail_stats_path;
int sim_short_reads;
int sim_alloc_fail_at;
int sim_live_allocs;
int sim_open_fds;
}

u8 error;

namespace {

struct SimFile {
	std::vector<uint8_t> data;
};

struct SimHandle {
	std::string path;
	uint32_t pos;
	bool open;
};

std::map<std::string, SimFile> g_files;
std::vector<SimHandle> g_handles; /* fd n is g_handles[n - 1] */

const size_t GUARD = 64;

struct Block {
	void* raw;
	size_t size;
};
std::map<void*, Block> g_blocks;

}

extern "C" void sim_reset(void)
{
	g_files.clear();
	g_handles.clear();
	for (auto& b : g_blocks)
		free(b.second.raw);
	g_blocks.clear();
	sim_fail_stats = 0;
	sim_fail_stats_path = NULL;
	sim_short_reads = 0;
	sim_alloc_fail_at = 0;
	sim_live_allocs = 0;
	sim_open_fds = 0;
	error = 0;
}

extern "C" void sim_put(const char* path, const void* data, size_t len)
{
	SimFile& f = g_files[path];
	f.data.assign((const uint8_t*)data, (const uint8_t*)data + len);
}

extern "C" const uint8_t* sim_get(const char* path, size_t* len)
{
	auto it = g_files.find(path);
	if (it == g_files.end())
		return NULL;
	if (len)
		*len = it->second.data.size();
	return it->second.data.data();
}

/* ---- ISFS ---- */

static SimHandle* handle_of(s32 fd, const char* what)
{
	if (fd < 1 || (size_t)fd > g_handles.size() || !g_handles[fd - 1].open)
		hc_hang("%s on a file that is not open (fd %d)", what, (int)fd);
	return &g_handles[fd - 1];
}

extern "C" s32 ISFS_Open(const char* filepath, u8 mode)
{
	(void)mode;
	if (g_files.find(filepath) == g_files.end())
		return -106;
	g_handles.push_back({filepath, 0, true});
	sim_open_fds++;
	return (s32)g_handles.size();
}

extern "C" s32 ISFS_Close(s32 fd)
{
	SimHandle* h = handle_of(fd, "ISFS_Close");
	h->open = false;
	sim_open_fds--;
	return 0;
}

extern "C" s32 ISFS_Read(s32 fd, void* buffer, u32 length)
{
	SimHandle* h = handle_of(fd, "ISFS_Read");
	if (((uintptr_t)buffer) % 32 != 0)
		hc_hang("ISFS_Read into an unaligned buffer");
	if (length == 0)
		return 0;
	const std::vector<uint8_t>& d = g_files[h->path].data;
	uint32_t avail = h->pos >= d.size() ? 0 : (uint32_t)d.size() - h->pos;
	uint32_t n = length < avail ? length : avail;
	if (sim_short_reads && n > 1 && (!sim_fail_stats_path || h->path.find(sim_fail_stats_path) != std::string::npos))
		n--;
	if (n)
		memcpy(buffer, d.data() + h->pos, n);
	h->pos += n;
	return (s32)n;
}

extern "C" s32 ISFS_Write(s32 fd, const void* buffer, u32 length)
{
	SimHandle* h = handle_of(fd, "ISFS_Write");
	std::vector<uint8_t>& d = g_files[h->path].data;
	if (d.size() < h->pos + length)
		d.resize(h->pos + length);
	memcpy(d.data() + h->pos, buffer, length);
	h->pos += length;
	return (s32)length;
}

extern "C" s32 ISFS_Seek(s32 fd, s32 where, s32 whence)
{
	SimHandle* h = handle_of(fd, "ISFS_Seek");
	int64_t base = 0;
	if (whence == 1)
		base = h->pos;
	else if (whence == 2)
		base = (int64_t)g_files[h->path].data.size();
	int64_t pos = base + where;
	if (pos < 0 || pos > (int64_t)g_files[h->path].data.size())
		return -102;
	h->pos = (uint32_t)pos;
	return (s32)pos;
}

extern "C" s32 ISFS_GetFileStats(s32 fd, fstats* status)
{
	SimHandle* h = handle_of(fd, "ISFS_GetFileStats");
	if (sim_fail_stats && (!sim_fail_stats_path || h->path.find(sim_fail_stats_path) != std::string::npos))
		return -1;
	status->file_length = (u32)g_files[h->path].data.size();
	status->file_pos = h->pos;
	return 0;
}

extern "C" s32 ISFS_CreateFile(const char* filepath, u8 attributes, u8 ownerperm, u8 groupperm, u8 otherperm)
{
	(void)attributes; (void)ownerperm; (void)groupperm; (void)otherperm;
	g_files[filepath].data.clear();
	return 0;
}

extern "C" s32 ISFS_Delete(const char* filepath)
{
	return g_files.erase(filepath) ? 0 : -106;
}

extern "C" void DCFlushRange(void* startaddress, u32 len) { (void)startaddress; (void)len; }
extern "C" void ICInvalidateRange(void* startaddress, u32 len) { (void)startaddress; (void)len; }

extern "C" unsigned int sleep(unsigned int seconds) { return seconds * 0; }
extern "C" int usleep(unsigned int usec) { return usec * 0; }

extern "C" void gprintf(const char* str, ...)
{
	va_list ap;
	va_start(ap, str);
	if (getenv("SIM_LOG"))
		vprintf(str, ap);
	va_end(ap);
}

/* ---- MEM2 heap with a guard behind every block ---- */

extern "C" void* mem2_memalign(u8 align, u32 size, const int area)
{
	(void)area;
	if (sim_alloc_fail_at > 0 && --sim_alloc_fail_at == 0)
		return NULL;
	size_t a = align ? align : 1;
	void* raw = malloc(size + GUARD + a);
	if (!raw)
		return NULL;
	uintptr_t p = ((uintptr_t)raw + a - 1) / a * a;
	void* user = (void*)p;
	memset(user, 0xCD, size);
	memset((uint8_t*)user + size, 0xA5, GUARD);
	g_blocks[user] = {raw, size};
	sim_live_allocs++;
	return user;
}

extern "C" void* mem2_malloc(u32 size, const int area)
{
	return mem2_memalign(32, size, area);
}

extern "C" void mem2_free(void* ptr, const int area)
{
	(void)area;
	if (ptr == NULL)
		return;
	auto it = g_blocks.find(ptr);
	if (it == g_blocks.end())
		hc_hang("free of a pointer that is not a live block (double free?)");
	const uint8_t* g = (const uint8_t*)ptr + it->second.size;
	for (size_t i = 0; i < GUARD; i++)
		if (g[i] != 0xA5)
			hc_hang("heap overrun: byte %u behind a %u byte block was written", (unsigned)i, (unsigned)it->second.size);
	free(it->second.raw);
	g_blocks.erase(it);
	sim_live_allocs--;
}
