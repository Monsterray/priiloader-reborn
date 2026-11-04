/*
 * Host tests for the Installer's NAND sequence: src/Installer/source/installer.cpp and nand.cpp,
 * unchanged, against the simulated NAND of sim_nand.h (fault injection at any ISFS call).
 * Built and run by tests/test_host_installer.py.
 *
 * The state checks ask the questions that matter for a Wii: does the TMD still match the boot
 * content ("bootable"), and if not, does a backup of the System Menu survive?
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <gccore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "../../../src/Installer/include/installer.h"
#include "../../../src/Installer/include/nand.h"
#include "sim_nand.h"

// the fake Priiloader app the Installer writes (see stubs/priiloader_app.h)
extern const uint8_t priiloader_app[3000] = { 'P', 'R', 'I', 'I', 1, 2, 3 };
static const size_t PRIILOADER_SIZE = 3000;

static const u64 SM_TITLE = 0x0000000100000002ll;
static const char* CONTENT_DIR = "/title/00000001/00000002/content/";
#define ORIG_APP "/title/00000001/00000002/content/0000001a.app"
#define COPY_APP "/title/00000001/00000002/content/1000001a.app"
#define VWII_APP "/title/00000001/00000002/content/2000001a.app"
#define TMD_PATH "/title/00000001/00000002/content/title.tmd"
#define TMD_BACKUP "/title/00000001/00000002/content/title_or.tmd"
#define NANDLOADER_APP "/title/00000001/00000200/content/00000055.app"

typedef std::vector<uint8_t> Bytes;

static Bytes pattern(size_t size, uint8_t mul, uint8_t add)
{
	Bytes b(size);
	for (size_t i = 0; i < size; i++)
		b[i] = (uint8_t)(i * mul + add);
	return b;
}

static Bytes vanilla_app() { return pattern(5000, 7, 1); }
static Bytes old_priiloader() { return pattern(PRIILOADER_SIZE, 3, 9); }
static Bytes nandloader_app() { return pattern(2000, 5, 3); }
static Bytes new_priiloader() { return Bytes(priiloader_app, priiloader_app + PRIILOADER_SIZE); }

static void hash_of(const Bytes& data, uint8_t out[20])
{
	SHA_Calculate(data.data(), (u32)data.size(), out);
}

static Bytes make_tmd(u16 version, u32 numContents, u16 bootIndex, const Bytes& bootApp, u32 access)
{
	Bytes b(0x140 + sizeof(tmd) + numContents * sizeof(tmd_content), 0);
	tmd* t = (tmd*)SIGNATURE_PAYLOAD(b.data());
	t->access_rights = access;
	t->title_version = version;
	t->num_contents = (u16)numContents;
	t->boot_index = bootIndex;
	for (u32 i = 0; i < numContents; i++)
	{
		t->contents[i].index = (u16)i;
		t->contents[i].cid = i == bootIndex ? 0x1a : 0x30 + i;
		t->contents[i].size = 1000;
	}
	hash_of(bootApp, t->contents[bootIndex].hash);
	return b;
}

static tmd* tmd_of(Bytes& b) { return (tmd*)SIGNATURE_PAYLOAD(b.data()); }

static void world_vanilla(bool vwii)
{
	g_nand.reset();
	g_nand.put(ORIG_APP, vanilla_app());
	// the real vWii System Menu boots its last content (9 contents: index 8); a non-vWii one boots index 1
	g_nand.put(TMD_PATH, make_tmd(vwii ? 608 : 480, 9, vwii ? 8 : 1, vanilla_app(), 0));
	g_nand.put("/ticket/00000001/00000002.tik", pattern(676, 11, 5));
	if (vwii)
	{
		g_nand.nandloader_tmd = make_tmd(1, 1, 0, nandloader_app(), 3);
		tmd_of(g_nand.nandloader_tmd)->contents[0].cid = 0x55;
		g_nand.put(NANDLOADER_APP, nandloader_app());
	}
}

// a non-vWii System Menu with an older Priiloader installed: the copy, the TMD backup, a patched TMD
static void world_updatable()
{
	world_vanilla(false);
	g_nand.put(COPY_APP, vanilla_app());
	g_nand.put(TMD_BACKUP, *g_nand.get(TMD_PATH));
	g_nand.put(ORIG_APP, old_priiloader());
	g_nand.put(TMD_PATH, make_tmd(480, 9, 1, old_priiloader(), 3));
}

struct Result
{
	bool threw = false;
	std::string message;
};

// what main.cpp does for install/update, remove and its error handling
static Result run_flow(InstallerAction request, bool vwii)
{
	Result r;
	try
	{
		InitializeInstaller(SM_TITLE, vwii);
		if (request == InstallerAction::Remove)
		{
			if (!PriiloaderInstalled())
				throw "Priiloader not found";
			RemovePriiloader();
			PatchTMD(request);
			DeletePriiloaderFiles(request);
		}
		else
		{
			InstallerAction action = PriiloaderInstalled() ? InstallerAction::Update : InstallerAction::Install;
			CopyTicket();
			WritePriiloader(action);
			if (PatchTMD(action) < 0)
				throw "patching TMD error";
			DeletePriiloaderFiles(action);
		}
	}
	catch (const std::string& ex)
	{
		r.threw = true;
		r.message = ex;
	}
	catch (char const* ex)
	{
		r.threw = true;
		r.message = ex;
	}
	return r;
}

// the TMD matches the boot content (and the vWii Priiloader content) on NAND
static bool bootable(std::string* why)
{
	const Bytes* tmdFile = g_nand.get(TMD_PATH);
	if (tmdFile == NULL)
	{
		*why = "the TMD is missing";
		return false;
	}
	Bytes copy = *tmdFile;
	if (copy.size() < 0x140 + sizeof(tmd))
	{
		*why = "the TMD is truncated";
		return false;
	}
	tmd* t = tmd_of(copy);
	if (copy.size() < 0x140 + sizeof(tmd) + t->num_contents * sizeof(tmd_content) || t->boot_index >= t->num_contents)
	{
		*why = "the TMD is damaged";
		return false;
	}

	char path[ISFS_MAXPATH];
	for (u32 i = 0; i < t->num_contents; i++)
	{
		snprintf(path, sizeof(path), "%s%08x.app", CONTENT_DIR, t->contents[i].cid);
		const Bytes* app = g_nand.get(path);
		if (app == NULL)
		{
			if (i == t->boot_index)
			{
				*why = std::string("the boot content ") + path + " is missing";
				return false;
			}
			continue;
		}
		uint8_t h[20];
		hash_of(*app, h);
		if (memcmp(h, t->contents[i].hash, 20) != 0)
		{
			*why = std::string("the hash of ") + path + " is not the TMD's";
			return false;
		}
	}
	return true;
}

// a state the Wii survives: bootable, or a System Menu copy plus a way back for the TMD
static bool recoverable(std::string* why)
{
	if (bootable(why))
		return true;
	std::string bootWhy = *why;
	const Bytes* copy = g_nand.get(COPY_APP);
	if (copy == NULL || *copy != vanilla_app())
	{
		*why = bootWhy + ", and no System Menu copy survives";
		return false;
	}
	const Bytes* backup = g_nand.get(TMD_BACKUP);
	if (backup == NULL && !g_nand.exists(TMD_PATH))
	{
		*why = bootWhy + ", and the TMD has no backup";
		return false;
	}
	return true;
}

static void check_fds(void)
{
	CHECK(g_nand.bad_closes == 0, "%d ISFS_Close calls on a descriptor that is not open", g_nand.bad_closes);
	CHECK(g_nand.bad_paths == 0, "%d ISFS calls with a path IOS refuses (component over 12 characters or path over 63)", g_nand.bad_paths);
	CHECK(g_nand.open_fds() == 0, "%d descriptors left open", g_nand.open_fds());
	CHECK(g_nand.guard_violations == 0, "%d buffers overflowed", g_nand.guard_violations);
}

/* ---- nand.cpp ---- */

static const NandPermissions PERMS = { 0, 0, 0, 3, 3, 3 };

static void test_nand_write_ok(void)
{
	g_nand.reset();
	Bytes data = pattern(1234, 3, 1);
	s32 ret = NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(ret >= 0, "NandWrite failed: %d", ret);
	CHECK(g_nand.get("/title/dest.bin") && *g_nand.get("/title/dest.bin") == data, "the content differs");
	CHECK(!g_nand.exists("/tmp/dest.bin"), "temporary files left behind");
	check_fds();
}

static void test_nand_write_replaces_and_closes_once(void)
{
	g_nand.reset();
	g_nand.put("/title/dest.bin", pattern(10, 1, 1));
	Bytes data = pattern(1234, 3, 1);
	s32 ret = NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(ret >= 0, "NandWrite failed: %d", ret);
	CHECK(*g_nand.get("/title/dest.bin") == data, "the content differs");
	check_fds();
}

static void test_nand_write_size_mismatch_is_an_error(void)
{
	g_nand.reset();
	g_nand.silent_short_write = true; // the NAND reports a full write but stores one byte less
	Bytes data = pattern(1234, 3, 1);
	s32 ret = NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(ret < 0, "a size mismatch returned %d (success)", ret);
	CHECK(!g_nand.exists("/title/dest.bin"), "the destination was written");
	check_fds();
}

static void test_nand_write_short_write_is_an_error(void)
{
	g_nand.reset();
	g_nand.short_write = true;
	Bytes data = pattern(1234, 3, 1);
	s32 ret = NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(ret < 0, "a short write returned %d (success)", ret);
	CHECK(!g_nand.exists("/title/dest.bin"), "the destination was written");
	check_fds();
}

// every rename into the destination fails: the delete + rename fallback loses the old file, so the new one must stay in /tmp
static void test_nand_write_failed_rename_keeps_a_good_copy(void)
{
	g_nand.reset();
	Bytes old = pattern(77, 1, 1);
	g_nand.put("/title/dest.bin", old);
	g_nand.fail_rename_dest = "/title/dest.bin";
	Bytes data = pattern(1234, 3, 1);
	s32 ret = NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(ret < 0, "a failed rename returned %d", ret);
	bool kept = (g_nand.get("/title/dest.bin") && *g_nand.get("/title/dest.bin") == old) || (g_nand.get("/tmp/dest.bin") && *g_nand.get("/tmp/dest.bin") == data);
	CHECK(kept, "neither the old nor the new file is left");
	check_fds();
}

// the rename onto the old file is refused (this IOS does not replace): delete + rename must finish the job
static void test_nand_write_rename_refused_falls_back(void)
{
	g_nand.reset();
	g_nand.rename_no_replace = true;
	g_nand.put("/title/dest.bin", pattern(77, 1, 1));
	Bytes data = pattern(1234, 3, 1);
	s32 ret = NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(ret >= 0, "NandWrite failed: %d", ret);
	CHECK(g_nand.get("/title/dest.bin") && *g_nand.get("/title/dest.bin") == data, "the content differs");
	CHECK(!g_nand.exists("/tmp/dest.bin"), "temporary file left behind");
	check_fds();
}

// an IOS that replaces on rename: when the rename fails, the old file was never touched
static void test_nand_write_replaces_in_one_rename(void)
{
	g_nand.reset();
	g_nand.rename_no_replace = false;
	g_nand.put("/title/dest.bin", pattern(77, 1, 1));
	Bytes data = pattern(1234, 3, 1);
	NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(g_nand.get("/title/dest.bin") && *g_nand.get("/title/dest.bin") == data, "the content differs");
	int total = g_nand.calls;

	g_nand.reset();
	g_nand.rename_no_replace = false;
	g_nand.put("/title/dest.bin", pattern(77, 1, 1));
	g_nand.fail_at = total - 1; // the rename into place (the last call is the delete of the temp file)
	g_nand.sticky = true;
	s32 ret = NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
	CHECK(ret < 0, "the failed rename returned %d", ret);
	CHECK(g_nand.get("/title/dest.bin") && *g_nand.get("/title/dest.bin") == pattern(77, 1, 1), "the old file was not left alone");
	check_fds();
}

// the simulated IOS: a rename keeps the file name, and replaces unless told not to
static void test_sim_rename_rules(void)
{
	g_nand.reset();
	g_nand.put("/tmp/a.bin", pattern(5, 1, 1));
	g_nand.put("/title/a.bin", pattern(6, 1, 1));
	CHECK(ISFS_Rename("/tmp/a.bin", "/title/b.bin") == -101, "a rename to another file name was accepted");
	CHECK(g_nand.exists("/tmp/a.bin"), "the file moved anyway");
	g_nand.bad_paths = 0;
	g_nand.rename_no_replace = true;
	CHECK(ISFS_Rename("/tmp/a.bin", "/title/a.bin") == -105, "no_replace: the rename onto a file worked");
	g_nand.rename_no_replace = false;
	CHECK(ISFS_Rename("/tmp/a.bin", "/title/a.bin") == 0, "the rename onto a file failed");
	CHECK(!g_nand.exists("/tmp/a.bin") && g_nand.get("/title/a.bin")->size() == 5, "the file was not replaced");
}

static void test_nand_copy_short_read_is_an_error(void)
{
	g_nand.reset();
	g_nand.put("/title/src.bin", pattern(500, 3, 1));
	g_nand.short_read = true;
	s32 ret = NandCopy("/title/src.bin", "/title/dest.bin", PERMS);
	CHECK(ret < 0, "a short read returned %d (success)", ret);
	CHECK(!g_nand.exists("/title/dest.bin"), "a truncated copy was written");
	check_fds();
}

static void test_nand_copy_ok(void)
{
	g_nand.reset();
	Bytes data = pattern(500, 3, 1);
	g_nand.put("/title/src.bin", data);
	s32 ret = NandCopy("/title/src.bin", "/title/dest.bin", PERMS);
	CHECK(ret >= 0, "NandCopy failed: %d", ret);
	CHECK(g_nand.get("/title/dest.bin") && *g_nand.get("/title/dest.bin") == data, "the copy differs");
	check_fds();
}

// a fault at every ISFS call of NandWrite and NandCopy: descriptors stay balanced, success means the data is there
static void test_nand_fault_sweep_descriptors(void)
{
	Bytes data = pattern(1234, 3, 1);
	for (int op = 0; op < 2; op++)
	{
		g_nand.reset();
		g_nand.put("/title/src.bin", data);
		g_nand.put("/title/dest.bin", pattern(10, 1, 1));
		if (op == 0)
			NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS);
		else
			NandCopy("/title/src.bin", "/title/dest.bin", PERMS);
		int total = g_nand.calls;
		for (int n = 1; n <= total; n++)
		{
			g_nand.reset();
			g_nand.put("/title/src.bin", data);
			g_nand.put("/title/dest.bin", pattern(10, 1, 1));
			g_nand.fail_at = n;
			s32 ret = op == 0 ? NandWrite("/title/dest.bin", data.data(), (u32)data.size(), PERMS) : NandCopy("/title/src.bin", "/title/dest.bin", PERMS);
			CHECK(g_nand.bad_closes == 0, "op %d, fault at call %d: %d closes of a descriptor that is not open", op, n, g_nand.bad_closes);
			CHECK(g_nand.open_fds() == 0, "op %d, fault at call %d: %d descriptors left open", op, n, g_nand.open_fds());
			const Bytes* dest = g_nand.get("/title/dest.bin");
			if (dest == NULL)
			{
				// delete + rename fallback (IOS without replace on rename): the new file waits in /tmp
				const Bytes* temp = g_nand.get("/tmp/dest.bin");
				CHECK(ret < 0 && temp != NULL && *temp == data, "op %d, fault at call %d: the destination is gone and no good copy is kept", op, n);
				continue;
			}
			if (ret >= 0)
				CHECK(*dest == data, "op %d, fault at call %d: reported success, but the data is not there", op, n);
			else
				CHECK(*dest == data || *dest == pattern(10, 1, 1), "op %d, fault at call %d: the destination is neither old nor new", op, n);
		}
	}
}

/* ---- installer.cpp ---- */

static void test_install_ok(void)
{
	world_vanilla(false);
	Result r = run_flow(InstallerAction::Install, false);
	CHECK(!r.threw, "install failed: %s", r.message.c_str());
	std::string why;
	CHECK(bootable(&why), "not bootable: %s", why.c_str());
	CHECK(*g_nand.get(ORIG_APP) == new_priiloader(), "Priiloader is not the boot content");
	CHECK(g_nand.get(COPY_APP) && *g_nand.get(COPY_APP) == vanilla_app(), "no System Menu copy");
	CHECK(g_nand.exists(TMD_BACKUP), "no TMD backup");
	check_fds();
}

static void test_install_vwii_ok(void)
{
	world_vanilla(true);
	Result r = run_flow(InstallerAction::Install, true);
	CHECK(!r.threw, "install failed: %s", r.message.c_str());
	std::string why;
	CHECK(bootable(&why), "not bootable: %s", why.c_str());
	CHECK(*g_nand.get(ORIG_APP) == nandloader_app(), "BC-NAND is not the boot content");
	CHECK(g_nand.get(VWII_APP) && *g_nand.get(VWII_APP) == new_priiloader(), "no Priiloader content");
	check_fds();
}

static void test_update_ok(void)
{
	world_updatable();
	Result r = run_flow(InstallerAction::Update, false);
	CHECK(!r.threw, "update failed: %s", r.message.c_str());
	std::string why;
	CHECK(bootable(&why), "not bootable: %s", why.c_str());
	CHECK(*g_nand.get(ORIG_APP) == new_priiloader(), "the update was not written");
	check_fds();
}

static void test_remove_ok(void)
{
	world_updatable();
	Result r = run_flow(InstallerAction::Remove, false);
	CHECK(!r.threw, "remove failed: %s", r.message.c_str());
	std::string why;
	CHECK(bootable(&why), "not bootable: %s", why.c_str());
	CHECK(*g_nand.get(ORIG_APP) == vanilla_app(), "the System Menu was not restored");
	check_fds();
}

// bug 1: a failed Priiloader write on install: the System Menu copy is deleted although the restore failed
static void test_install_write_fails_keeps_copy(void)
{
	world_vanilla(false);
	g_nand.fail_rename_dest = ORIG_APP; // no write to the boot content works any more
	Result r = run_flow(InstallerAction::Install, false);
	CHECK(r.threw, "install reported success");
	std::string why;
	CHECK(recoverable(&why), "%s", why.c_str());
	CHECK(g_nand.exists(COPY_APP), "the System Menu copy was deleted although the restore failed");
	check_fds();
}

// bug 1, update: the copy is the only real System Menu, and the restore must not touch a working Priiloader
static void test_update_write_fails_keeps_copy(void)
{
	world_updatable();
	g_nand.fail_rename_dest = ORIG_APP;
	Result r = run_flow(InstallerAction::Update, false);
	CHECK(r.threw, "update reported success");
	std::string why;
	CHECK(recoverable(&why), "%s", why.c_str());
	CHECK(g_nand.exists(COPY_APP), "the System Menu copy was deleted on an update");
	check_fds();
}

static void test_update_vwii_write_fails_keeps_copy(void)
{
	world_vanilla(true);
	Result r = run_flow(InstallerAction::Install, true);
	CHECK(!r.threw, "install failed: %s", r.message.c_str());
	g_nand.put(VWII_APP, old_priiloader());
	tmd* t = tmd_of(g_nand.files[TMD_PATH].data);
	hash_of(old_priiloader(), t->contents[1].hash);
	g_nand.fail_rename_dest = ORIG_APP;
	r = run_flow(InstallerAction::Update, true);
	CHECK(r.threw, "update reported success");
	std::string why;
	CHECK(recoverable(&why), "%s", why.c_str());
	CHECK(g_nand.exists(COPY_APP), "the System Menu copy was deleted on an update");
	check_fds();
}

static void test_install_vwii_write_fails_keeps_copy(void)
{
	world_vanilla(true);
	g_nand.fail_rename_dest = ORIG_APP; // replacing the System Menu with BC-NAND fails
	Result r = run_flow(InstallerAction::Install, true);
	CHECK(r.threw, "install reported success");
	std::string why;
	CHECK(recoverable(&why), "%s", why.c_str());
	CHECK(g_nand.exists(COPY_APP), "the System Menu copy was deleted although the restore failed");
	check_fds();
}

// bug 2: PatchTMD swallows a failed TMD restore and deletes the backup
static void test_patchtmd_failed_restore_keeps_backup(void)
{
	world_vanilla(false);
	g_nand.put(COPY_APP, vanilla_app());
	g_nand.put(ORIG_APP, new_priiloader()); // WritePriiloader is done: the TMD must follow
	InitializeInstaller(SM_TITLE, false);
	g_nand.fail_rename_dest = TMD_PATH; // the patched TMD cannot be written, nor the backup put back
	bool threw = false;
	try
	{
		PatchTMD(InstallerAction::Install);
	}
	catch (const std::string&)
	{
		threw = true;
	}
	catch (char const*)
	{
		threw = true;
	}
	CHECK(threw, "PatchTMD reported success");
	CHECK(g_nand.exists(TMD_BACKUP), "the TMD backup was deleted although the restore failed");
	std::string why;
	CHECK(recoverable(&why), "%s", why.c_str());
	check_fds();
}

// bug 2: the backup TMD is read into a MAX_SIGNED_TMD_SIZE buffer without a bound
static void test_patchtmd_oversized_backup(void)
{
	world_vanilla(false);
	g_nand.put(COPY_APP, vanilla_app());
	g_nand.put(ORIG_APP, new_priiloader());
	g_nand.put(TMD_BACKUP, pattern(MAX_SIGNED_TMD_SIZE + 16, 1, 1));
	InitializeInstaller(SM_TITLE, false);
	bool threw = false;
	try
	{
		PatchTMD(InstallerAction::Install);
	}
	catch (const std::string&)
	{
		threw = true;
	}
	catch (char const*)
	{
		threw = true;
	}
	CHECK(g_nand.guard_violations == 0, "the backup TMD overflowed its buffer");
	CHECK(threw, "an unusable backup TMD was accepted");
	CHECK(g_nand.exists(TMD_BACKUP), "the backup TMD was deleted");
	check_fds();
}

// bug 2: the vWii nandloader check used the System Menu's file id, so it could never fire
static void test_vwii_nandloader_without_boot_content(void)
{
	world_vanilla(true);
	tmd_of(g_nand.nandloader_tmd)->boot_index = 5; // no content has this index
	Result r = run_flow(InstallerAction::Install, true);
	CHECK(r.threw, "a nandloader TMD without a boot content was accepted");
	check_fds();
}

// PriiloaderInstalled() says "not installed" when the open merely failed: an update becomes an install
static void test_installed_check_fails_loudly(void)
{
	world_updatable();
	InitializeInstaller(SM_TITLE, false);
	g_nand.fail_at = g_nand.calls + 1; // the open of the System Menu copy
	bool threw = false;
	bool installed = false;
	try
	{
		installed = PriiloaderInstalled();
	}
	catch (const std::string&)
	{
		threw = true;
	}
	catch (char const*)
	{
		threw = true;
	}
	CHECK(threw || installed, "an ISFS error was read as 'Priiloader is not installed'");
}

static void sweep(const char* name, void (*world)(void), InstallerAction request, bool vwii, bool sticky)
{
	world();
	Result clean = run_flow(request, vwii);
	CHECK(!clean.threw, "%s: the clean run failed: %s", name, clean.message.c_str());
	std::string why;
	CHECK(bootable(&why), "%s: the clean run is not bootable: %s", name, why.c_str());
	int total = g_nand.calls;
	CHECK(total > 20, "%s: only %d ISFS calls?", name, total);
	for (int n = 1; n <= total; n++)
	{
		world();
		g_nand.fail_at = n;
		g_nand.sticky = sticky;
		Result r = run_flow(request, vwii);
		g_nand.fail_at = 0;
		if (sticky)
			CHECK(recoverable(&why), "%s: %s from call %d on (%s): %s", name, "NAND broken", n, r.message.c_str(), why.c_str());
		else
			CHECK(bootable(&why), "%s: one fault at call %d (%s): %s", name, n, r.message.c_str(), why.c_str());
		CHECK(g_nand.bad_paths == 0, "%s: fault at call %d: %d calls with a path IOS refuses", name, n, g_nand.bad_paths);
		CHECK(g_nand.bad_closes == 0, "%s: fault at call %d: %d closes of a descriptor that is not open", name, n, g_nand.bad_closes);
		CHECK(g_nand.open_fds() == 0, "%s: fault at call %d: %d descriptors left open", name, n, g_nand.open_fds());
		CHECK(g_nand.guard_violations == 0, "%s: fault at call %d: a buffer overflowed", name, n);
	}
}

static void world_vanilla_plain(void) { world_vanilla(false); }
static void world_vanilla_vwii(void) { world_vanilla(true); }
static void world_updated_vwii(void)
{
	world_vanilla(true);
	run_flow(InstallerAction::Install, true);
	g_nand.put(VWII_APP, old_priiloader());
	tmd* t = tmd_of(g_nand.files[TMD_PATH].data);
	hash_of(old_priiloader(), t->contents[1].hash);
	g_nand.calls = 0;
	g_nand.bad_closes = 0;
}

// the boot content is gone after a failed restore: on vWii the TMD wants BC-NAND there, not Priiloader
static void test_remove_vwii_failed_restore_puts_back_bcnand(void)
{
	world_updated_vwii();
	g_nand.fail_rename_dest = ORIG_APP;
	g_nand.fail_rename_dest_times = 2; // the restore's rename and its fallback; the put-back works
	Result r = run_flow(InstallerAction::Remove, true);
	CHECK(r.threw, "remove reported success");
	std::string why;
	CHECK(g_nand.get(ORIG_APP) && *g_nand.get(ORIG_APP) == nandloader_app(), "the boot content is not BC-NAND");
	CHECK(bootable(&why), "not bootable: %s", why.c_str());
	CHECK(g_nand.exists(COPY_APP), "the System Menu copy was deleted");
	check_fds();
}

// Remove: a failed TMD restore leaves the TMD wanting Priiloader's hash: the System Menu copy is the way back
static void remove_failed_tmd_restore(bool vwii)
{
	if (vwii)
		world_updated_vwii();
	else
		world_updatable();
	g_nand.fail_rename_dest = TMD_PATH;
	Result r = run_flow(InstallerAction::Remove, vwii);
	CHECK(r.threw, "remove reported success although the TMD was not restored");
	if (!vwii)
		CHECK(g_nand.get(COPY_APP) && *g_nand.get(COPY_APP) == vanilla_app(), "the System Menu copy was deleted before the TMD was restored");
	else
		CHECK(g_nand.exists(VWII_APP), "the Priiloader content was deleted before the TMD was restored");
	check_fds();
}
static void test_remove_failed_tmd_restore_keeps_copy(void) { remove_failed_tmd_restore(false); }
static void test_remove_vwii_failed_tmd_restore_keeps_copy(void) { remove_failed_tmd_restore(true); }

static void test_sweep_install_single_fault(void) { sweep("install", world_vanilla_plain, InstallerAction::Install, false, false); }
static void test_sweep_install_broken_nand(void) { sweep("install", world_vanilla_plain, InstallerAction::Install, false, true); }
static void test_sweep_update_single_fault(void) { sweep("update", world_updatable, InstallerAction::Update, false, false); }
static void test_sweep_update_broken_nand(void) { sweep("update", world_updatable, InstallerAction::Update, false, true); }
static void test_sweep_vwii_install_single_fault(void) { sweep("vwii install", world_vanilla_vwii, InstallerAction::Install, true, false); }
static void test_sweep_vwii_install_broken_nand(void) { sweep("vwii install", world_vanilla_vwii, InstallerAction::Install, true, true); }
static void test_sweep_vwii_update_single_fault(void) { sweep("vwii update", world_updated_vwii, InstallerAction::Update, true, false); }
static void test_sweep_vwii_update_broken_nand(void) { sweep("vwii update", world_updated_vwii, InstallerAction::Update, true, true); }

static const struct hc_test tests[] = {
	{ "nand_write_ok", test_nand_write_ok, false },
	{ "nand_write_replaces_and_closes_once", test_nand_write_replaces_and_closes_once, false },
	{ "nand_write_size_mismatch_is_an_error", test_nand_write_size_mismatch_is_an_error, false },
	{ "nand_write_short_write_is_an_error", test_nand_write_short_write_is_an_error, false },
	{ "nand_write_failed_rename_keeps_a_good_copy", test_nand_write_failed_rename_keeps_a_good_copy, false },
	{ "nand_write_rename_refused_falls_back", test_nand_write_rename_refused_falls_back, false },
	{ "nand_write_replaces_in_one_rename", test_nand_write_replaces_in_one_rename, false },
	{ "sim_rename_rules", test_sim_rename_rules, false },
	{ "nand_copy_ok", test_nand_copy_ok, false },
	{ "nand_copy_short_read_is_an_error", test_nand_copy_short_read_is_an_error, false },
	{ "nand_fault_sweep_descriptors", test_nand_fault_sweep_descriptors, false },
	{ "install_ok", test_install_ok, false },
	{ "install_vwii_ok", test_install_vwii_ok, false },
	{ "update_ok", test_update_ok, false },
	{ "remove_ok", test_remove_ok, false },
	{ "install_write_fails_keeps_copy", test_install_write_fails_keeps_copy, false },
	{ "install_vwii_write_fails_keeps_copy", test_install_vwii_write_fails_keeps_copy, false },
	{ "update_write_fails_keeps_copy", test_update_write_fails_keeps_copy, false },
	{ "update_vwii_write_fails_keeps_copy", test_update_vwii_write_fails_keeps_copy, false },
	{ "patchtmd_failed_restore_keeps_backup", test_patchtmd_failed_restore_keeps_backup, false },
	{ "patchtmd_oversized_backup", test_patchtmd_oversized_backup, false },
	{ "vwii_nandloader_without_boot_content", test_vwii_nandloader_without_boot_content, false },
	{ "remove_vwii_failed_restore_puts_back_bcnand", test_remove_vwii_failed_restore_puts_back_bcnand, false },
	{ "remove_failed_tmd_restore_keeps_copy", test_remove_failed_tmd_restore_keeps_copy, false },
	{ "remove_vwii_failed_tmd_restore_keeps_copy", test_remove_vwii_failed_tmd_restore_keeps_copy, false },
	{ "installed_check_fails_loudly", test_installed_check_fails_loudly, false },
	{ "sweep_install_single_fault", test_sweep_install_single_fault, false },
	{ "sweep_install_broken_nand", test_sweep_install_broken_nand, false },
	{ "sweep_update_single_fault", test_sweep_update_single_fault, false },
	{ "sweep_update_broken_nand", test_sweep_update_broken_nand, false },
	{ "sweep_vwii_install_single_fault", test_sweep_vwii_install_single_fault, false },
	{ "sweep_vwii_install_broken_nand", test_sweep_vwii_install_broken_nand, false },
	{ "sweep_vwii_update_single_fault", test_sweep_vwii_update_single_fault, false },
	{ "sweep_vwii_update_broken_nand", test_sweep_vwii_update_broken_nand, false },
};

int main(int argc, char** argv)
{
	g_rename_no_replace_default = getenv("HC_RENAME_NO_REPLACE") != NULL; // the second run: IOS refuses to rename onto an existing file
	g_verbose = getenv("HC_VERBOSE") != NULL; // shows what the Installer prints
	return hc_main(tests, HC_COUNT(tests), argc, argv, NULL);
}
