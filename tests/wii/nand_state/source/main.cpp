/*
 * nand_state: what the System Menu's NAND files look like now, read-only. Run before and after each installer step
 * (install, update, remove) and compare the outputs.
 *
 *   - the installed TMD (through ES): title version, boot index, each content's id, size and SHA-1
 *   - /title/00000001/00000002/content: every file, its size and SHA-1 (title.tmd, the boot content, the System
 *     Menu copy 1xxxxxxx.app, title_or.tmd)
 *   - /title/00000001/00000002/data: every file and its size; loader.ini in hex
 *   - /tmp: should be empty
 *
 * Every line starts with "NANDSTATE:". Nothing is written to NAND: every ISFS_Open is ISFS_OPEN_READ. IOS is patched
 * in memory only (as the installer does), and HBC reloads its own IOS when this returns. Reloading IOS drops the
 * network, so the report also goes to sd:/priiloader-tests/nand_state_<label>.txt (argv[1], default "now"), which
 * run.py fetches.
 */
#include <gccore.h>
#include <ogc/sha.h>
#include <fat.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <string>
#include "IOS.hpp"
#include "hbc_netlog.h"

#define SM_TITLE 0x0000000100000002ULL
#define CONTENT_DIR "/title/00000001/00000002/content"
#define DATA_DIR "/title/00000001/00000002/data"

static void* xfb = NULL;
static GXRModeObj* rmode = NULL;
static std::string report;

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	printf("%s", line);
	report += line;
}

static void video_init(void)
{
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
}

static void hex(char* out, const u8* p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		sprintf(out + i * 2, "%02x", p[i]);
	out[n * 2] = '\0';
}

//reads a whole NAND file read-only. returns its size (buffer in *data, 32-aligned) or a negative ISFS error.
static s32 read_file(const char* path, u8** data)
{
	*data = NULL;
	s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
	if (fd < 0)
		return fd;
	static fstats st ATTRIBUTE_ALIGN(32);
	s32 r = ISFS_GetFileStats(fd, &st);
	if (r < 0)
	{
		ISFS_Close(fd);
		return r;
	}
	u32 size = st.file_length;
	u8* buf = (u8*)memalign(32, (size + 63) & ~31u);
	if (buf == NULL)
	{
		ISFS_Close(fd);
		return -1000;
	}
	r = size ? ISFS_Read(fd, buf, size) : 0;
	ISFS_Close(fd);
	if (r != (s32)size)
	{
		free(buf);
		return r < 0 ? r : -1001;
	}
	*data = buf;
	return (s32)size;
}

static void list_dir(const char* dir, bool hashes)
{
	u32 count = 0;
	s32 r = ISFS_ReadDir(dir, NULL, &count);
	if (r < 0)
	{
		say("NANDSTATE: %s: ReadDir error %d\n", dir, (int)r);
		return;
	}
	say("NANDSTATE: %s: %u entries\n", dir, (unsigned)count);
	if (count == 0)
		return;
	char* names = (char*)memalign(32, (count * 13 + 31) & ~31u);
	r = ISFS_ReadDir(dir, names, &count);
	if (r < 0)
	{
		say("NANDSTATE: %s: ReadDir error %d\n", dir, (int)r);
		free(names);
		return;
	}
	char* name = names;
	for (u32 i = 0; i < count; i++)
	{
		char path[96];
		snprintf(path, sizeof(path), "%s/%s", dir, name);
		u8* data = NULL;
		s32 size = read_file(path, &data);
		if (size < 0)
			say("NANDSTATE:   %-12s (folder, or read error %d)\n", name, (int)size);
		else if (hashes)
		{
			u8 sha[20] ATTRIBUTE_ALIGN(32);
			char h[41];
			memset(sha, 0, sizeof(sha));
			if (size > 0)
				SHA_Calculate(data, size, sha);
			hex(h, sha, 20);
			say("NANDSTATE:   %-12s %8d bytes sha1 %s\n", name, (int)size, h);
		}
		else
			say("NANDSTATE:   %-12s %8d bytes\n", name, (int)size);
		free(data);
		name += strlen(name) + 1;
	}
	free(names);
}

int main(int argc, char** argv)
{
	const char* label = (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') ? argv[1] : "now";
	video_init();
	hbc_netlog_init();
	say("NANDSTATE: start (%s), IOS %d rev %d\n", label, IOS_GetVersion(), IOS_GetRevision());

	//the installer's setup, for reading only: a fresh IOS, AHBPROT off, IOS patched in memory, the System Menu's uid
	ReloadIOS(IOS_GetVersion(), 0);
	bool ahb = DisableAHBProt() && ReadRegister32(0x0d800064) == 0xFFFFFFFF;
	s8 patched = ahb ? PatchIOS({ SetUidPatcher, NandAccessPatcher }) : -100;
	s32 uid = ahb ? ES_SetUID(SM_TITLE) : -100;
	say("NANDSTATE: AHBPROT off %d, IOS patches %d, ES_SetUID %d\n", (int)ahb, (int)patched, (int)uid);
	ISFS_Initialize();
	SHA_Init();

	//the installed TMD, as ES sees it
	u32 tmdSize = 0;
	s32 r = ES_GetStoredTMDSize(SM_TITLE, &tmdSize);
	if (r < 0 || tmdSize == 0 || tmdSize > MAX_SIGNED_TMD_SIZE)
		say("NANDSTATE: ES_GetStoredTMDSize error %d (size %u)\n", (int)r, (unsigned)tmdSize);
	else
	{
		signed_blob* stmd = (signed_blob*)memalign(32, (tmdSize + 31) & ~31u);
		r = ES_GetStoredTMD(SM_TITLE, stmd, tmdSize);
		if (r < 0)
			say("NANDSTATE: ES_GetStoredTMD error %d\n", (int)r);
		else
		{
			tmd* t = (tmd*)SIGNATURE_PAYLOAD(stmd);
			say("NANDSTATE: TMD: title version %u, boot index %u, %u contents, %u bytes\n", (unsigned)t->title_version,
				(unsigned)t->boot_index, (unsigned)t->num_contents, (unsigned)tmdSize);
			for (u32 i = 0; i < t->num_contents; i++)
			{
				tmd_content* c = &t->contents[i];
				char h[41];
				hex(h, c->hash, 20);
				say("NANDSTATE:   content index %u id %08x type %04x size %llu sha1 %s%s\n", (unsigned)c->index,
					(unsigned)c->cid, (unsigned)c->type, (unsigned long long)c->size, h,
					c->index == t->boot_index ? "  <- boot" : "");
			}
		}
		free(stmd);
	}

	list_dir(CONTENT_DIR, true);
	list_dir(DATA_DIR, false);

	u8* ini = NULL;
	s32 size = read_file(DATA_DIR "/loader.ini", &ini);
	if (size < 0)
		say("NANDSTATE: loader.ini: error %d\n", (int)size);
	else
	{
		char h[2 * 64 + 1];
		hex(h, ini, size > 64 ? 64 : size);
		say("NANDSTATE: loader.ini %d bytes: %s (autoboot %u, ReturnTo %u)\n", (int)size, h, size > 0 ? ini[0] : 0,
			size > 8 ? ini[8] : 0);
		free(ini);
	}
	list_dir("/tmp", false);
	say("NANDSTATE: done\n");

	if (fatInitDefault())
	{
		mkdir("sd:/priiloader-tests", 0777);
		char path[96];
		snprintf(path, sizeof(path), "sd:/priiloader-tests/nand_state_%s.txt", label);
		FILE* f = fopen(path, "wb");
		if (f != NULL)
		{
			fwrite(report.data(), 1, report.size(), f);
			fclose(f);
			printf("saved %s\n", path);
		}
		fatUnmount("sd:");
	}
	hbc_netlog_close();
	return 0;
}
