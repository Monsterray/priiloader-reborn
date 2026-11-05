/*
 * test_settings.cpp -- host tests of LoadSettings in src/priiloader/source/settings.cpp (loader.ini on NAND),
 * against the simulated NAND and its guarded heap.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "gccore.h"
#include "settings.h"
#include "error.h"
#include "mem2_manager.h"
#include "sim.h"

extern u8 error;

static const char* INI = "/title/00000001/00000002/data/loader.ini";

static void setup(void)
{
	sim_reset();
	error = ERROR_NONE;
	sim_live_allocs = 0;
	settings = NULL;
}

static void put_settings(const Settings& s)
{
	sim_put(INI, &s, SETTINGS_FILE_SIZE);
}

static Settings valid_settings(void)
{
	Settings s;
	memset(&s, 0, sizeof(s));
	s.version = VERSION_MERGED;
	s.RCVersion = VERSION_RC;
	s.autoboot = AUTOBOOT_HBC;
	s.ReturnTo = RETURNTO_PRIILOADER;
	s.UseSystemMenuIOS = 0;
	s.BlackBackground = 0;
	s.PreferredMountPoint = MOUNT_USB;
	return s;
}

static void clean(void)
{
	CHECK(sim_open_fds == 0, "%d files left open", sim_open_fds);
	mem_free(settings);
	CHECK(sim_live_allocs == 0, "%d heap blocks leaked", sim_live_allocs);
}

/* the settings of a new install */
static void check_defaults(void)
{
	CHECK(settings != NULL, "settings is NULL");
	CHECK(settings->autoboot == AUTOBOOT_SYS, "autoboot is %d", settings->autoboot);
	CHECK(settings->UseSystemMenuIOS == 1, "UseSystemMenuIOS %d", settings->UseSystemMenuIOS);
	CHECK(settings->BlackBackground == 1, "BlackBackground %d", settings->BlackBackground);
	CHECK(settings->version == VERSION_MERGED && settings->RCVersion == VERSION_RC, "versions");
	CHECK(settings->ReturnTo == RETURNTO_SYSMENU && settings->PreferredMountPoint == MOUNT_AUTO, "other fields not zero");
}

static void test_missing_file_created(void)
{
	CHECK(LoadSettings() == LOADSETTINGS_INI_CREATED, "not INI_CREATED");
	check_defaults();
	CHECK_OK();
	size_t len = 0;
	const uint8_t* file = sim_get(INI, &len);
	CHECK(file != NULL && len == SETTINGS_FILE_SIZE, "file is %zu bytes", len);
	CHECK(memcmp(file, settings, SETTINGS_FILE_SIZE) == 0, "file differs from the settings in memory");
	clean();
}

static void test_good_file(void)
{
	put_settings(valid_settings());
	CHECK(LoadSettings() == LOADSETTINGS_OK, "not OK");
	CHECK(settings->autoboot == AUTOBOOT_HBC && settings->ReturnTo == RETURNTO_PRIILOADER, "values not read");
	CHECK(settings->UseSystemMenuIOS == 0 && settings->PreferredMountPoint == MOUNT_USB, "values not read");
	CHECK(error == ERROR_NONE, "error %d", error);
	clean();
}

static void test_short_file_reset(void)
{
	sim_put(INI, "short", 5);
	CHECK(LoadSettings() == LOADSETTINGS_INI_CREATED, "not INI_CREATED");
	check_defaults();
	CHECK_OK();
	size_t len = 0;
	CHECK(sim_get(INI, &len) != NULL && len == SETTINGS_FILE_SIZE, "file is %zu bytes", len);
	clean();
}

static void test_long_file_reset(void)
{
	std::vector<uint8_t> big(SETTINGS_FILE_SIZE + 10, 0x11);
	sim_put(INI, &big[0], big.size());
	CHECK(LoadSettings() == LOADSETTINGS_INI_CREATED, "not INI_CREATED");
	check_defaults();
	CHECK_OK();
	size_t len = 0;
	CHECK(sim_get(INI, &len) != NULL && len == SETTINGS_FILE_SIZE, "file is %zu bytes", len);
	clean();
}

static void test_stats_failure_leaves_file(void)
{
	Settings good = valid_settings();
	put_settings(good);
	sim_fail_stats = 1;
	CHECK(LoadSettings() == LOADSETTINGS_FAIL, "not FAIL");
	check_defaults();
	CHECK_OK();
	CHECK(error == ERROR_SETTING_READ, "error %d", error);
	size_t len = 0;
	const uint8_t* file = sim_get(INI, &len);
	CHECK(file != NULL, "the file was deleted");
	CHECK(len == SETTINGS_FILE_SIZE && memcmp(file, &good, SETTINGS_FILE_SIZE) == 0, "the file was changed");
	clean();
}

static void test_short_read_fails_with_defaults(void)
{
	put_settings(valid_settings());
	sim_short_reads = 1;
	CHECK(LoadSettings() == LOADSETTINGS_FAIL, "a short read was accepted");
	CHECK(error == ERROR_SETTING_READ, "error %d", error);
	CHECK(settings->autoboot == AUTOBOOT_SYS, "autoboot is %d, not the default", settings->autoboot);
	check_defaults();
	CHECK_OK();
	clean();
}

static void test_old_version_healed(void)
{
	Settings old = valid_settings();
	old.version = VERSION_MERGED - 1;
	old.RCVersion = VERSION_RC + 1;
	put_settings(old);
	CHECK(LoadSettings() == LOADSETTINGS_OK, "not OK");
	CHECK(settings->version == VERSION_MERGED && settings->RCVersion == VERSION_RC, "versions not updated in memory");
	CHECK(settings->autoboot == AUTOBOOT_HBC, "the other settings were lost");
	size_t len = 0;
	const uint8_t* file = sim_get(INI, &len);
	CHECK(file != NULL && len == SETTINGS_FILE_SIZE, "file size");
	Settings onNand;
	memcpy(&onNand, file, SETTINGS_FILE_SIZE);
	CHECK(onNand.version == VERSION_MERGED && onNand.RCVersion == VERSION_RC, "versions not updated on NAND");
	CHECK(onNand.autoboot == AUTOBOOT_HBC, "autoboot changed on NAND");
	clean();
}

static void test_version_zero_healed(void)
{
	Settings old = valid_settings();
	old.version = 0;
	put_settings(old);
	CHECK(LoadSettings() == LOADSETTINGS_OK, "not OK");
	CHECK(settings->version == VERSION_MERGED, "version not set");
	clean();
}

static void test_alloc_failure(void)
{
	sim_alloc_fail_at = 1;
	CHECK(LoadSettings() == LOADSETTINGS_FAIL, "not FAIL");
	CHECK(settings == NULL, "settings set");
	clean();
}

/* a 32 byte file of the first layout: the old settings, 0xEE where the padding is */
static void put_v1(u32 version, u32 rcversion)
{
	uint8_t f[SETTINGS_FILE_SIZE_V1];
	memset(f, 0xEE, sizeof(f));
	Settings s = valid_settings();
	s.version = version;
	s.RCVersion = rcversion;
	s.StopDisc = 1;
	s.LidSlotOnError = 1;
	s.IgnoreShutDownMode = 1;
	s.ShutdownTo = SHUTDOWNTO_AUTOBOOT;
	s.SystemMenuIOS = 58;
	s.DumpGeckoText = 1;
	s.PasscheckPriiloader = 1;
	s.PasscheckMenu = 1;
	s.ShowRCUpdates = 1;
	memcpy(f, &s, SETTINGS_V1_USED_SIZE);
	sim_put(INI, f, sizeof(f));
}

static void check_v1_values(void)
{
	CHECK(settings->autoboot == AUTOBOOT_HBC && settings->ReturnTo == RETURNTO_PRIILOADER, "autoboot/ReturnTo lost");
	CHECK(settings->ShutdownTo == SHUTDOWNTO_AUTOBOOT && settings->StopDisc == 1 && settings->LidSlotOnError == 1, "lost");
	CHECK(settings->IgnoreShutDownMode == 1 && settings->SystemMenuIOS == 58 && settings->UseSystemMenuIOS == 0, "lost");
	CHECK(settings->BlackBackground == 0 && settings->DumpGeckoText == 1, "lost");
	CHECK(settings->PasscheckPriiloader == 1 && settings->PasscheckMenu == 1 && settings->ShowRCUpdates == 1, "lost");
	CHECK(settings->PreferredMountPoint == MOUNT_USB, "PreferredMountPoint lost");
	CHECK(settings->version == VERSION_MERGED && settings->RCVersion == VERSION_RC, "versions");
}

static void check_migrated_file(void)
{
	size_t len = 0;
	const uint8_t* file = sim_get(INI, &len);
	CHECK(file != NULL && len == SETTINGS_FILE_SIZE, "file is %zu bytes, not %zu", len, (size_t)SETTINGS_FILE_SIZE);
	if (file == NULL || len != SETTINGS_FILE_SIZE)
		return;
	CHECK(memcmp(file, settings, SETTINGS_FILE_SIZE) == 0, "the file differs from the settings in memory");
	CHECK(file[offsetof(Settings, AutobootWait)] == AUTOBOOTWAIT_DEFAULT, "AutobootWait on NAND");
	CHECK(file[offsetof(Settings, UpdateSource)] == UPDATESOURCE_DEFAULT, "UpdateSource on NAND");
	for (size_t i = 0; i < sizeof(settings->LegacyPadding); i++)
		CHECK(file[offsetof(Settings, LegacyPadding) + i] == 0, "padding byte %zu is 0x%02X", i, file[offsetof(Settings, LegacyPadding) + i]);
}

static void test_migrate_v1(void)
{
	put_v1(VERSION_MERGED, VERSION_RC);
	CHECK(LoadSettings() == LOADSETTINGS_OK, "not OK");
	check_v1_values();
	CHECK(settings->AutobootWait == AUTOBOOTWAIT_DEFAULT && settings->UpdateSource == UPDATESOURCE_DEFAULT, "new fields not default");
	for (size_t i = 0; i < sizeof(settings->LegacyPadding); i++)
		CHECK(settings->LegacyPadding[i] == 0, "padding not 0 in memory");
	CHECK(error == ERROR_NONE, "error %d", error);
	check_migrated_file();
	/* a second load takes the new file as it is */
	settings->AutobootWait = AUTOBOOTWAIT_250MS;
	settings->UpdateSource = UPDATESOURCE_DACOTACO;
	CHECK(SaveSettings() == 1, "SaveSettings failed");
	mem_free(settings);
	settings = NULL;
	CHECK(LoadSettings() == LOADSETTINGS_OK, "second load not OK");
	check_v1_values();
	CHECK(settings->AutobootWait == AUTOBOOTWAIT_250MS && settings->UpdateSource == UPDATESOURCE_DACOTACO, "new fields lost");
	size_t len = 0;
	CHECK(sim_get(INI, &len) != NULL && len == SETTINGS_FILE_SIZE, "file size %zu", len);
	clean();
}

static void test_migrate_v1_old_version(void)
{
	put_v1(VERSION_MERGED - 1, VERSION_RC + 1);
	CHECK(LoadSettings() == LOADSETTINGS_OK, "not OK");
	check_v1_values();
	check_migrated_file();
	size_t len = 0;
	const uint8_t* file = sim_get(INI, &len);
	Settings onNand;
	memset(&onNand, 0, sizeof(onNand));
	if (file != NULL)
		memcpy(&onNand, file, len < sizeof(onNand) ? len : sizeof(onNand));
	CHECK(onNand.version == VERSION_MERGED && onNand.RCVersion == VERSION_RC, "versions not updated on NAND");
	clean();
}

static void test_current_round_trip(void)
{
	CHECK(LoadSettings() == LOADSETTINGS_INI_CREATED, "not INI_CREATED");
	CHECK(settings->AutobootWait == AUTOBOOTWAIT_500MS && settings->UpdateSource == UPDATESOURCE_GITHUB, "defaults");
	settings->autoboot = AUTOBOOT_FILE;
	settings->AutobootWait = AUTOBOOTWAIT_100MS;
	settings->UpdateSource = UPDATESOURCE_DACOTACO;
	CHECK(SaveSettings() == 1, "SaveSettings failed");
	size_t len = 0;
	const uint8_t* file = sim_get(INI, &len);
	CHECK(file != NULL && len == SETTINGS_FILE_SIZE, "file is %zu bytes", len);
	CHECK(file != NULL && memcmp(file, settings, SETTINGS_FILE_SIZE) == 0, "file differs");
	mem_free(settings);
	settings = NULL;
	CHECK(LoadSettings() == LOADSETTINGS_OK, "not OK");
	CHECK(settings->autoboot == AUTOBOOT_FILE && settings->AutobootWait == AUTOBOOTWAIT_100MS && settings->UpdateSource == UPDATESOURCE_DACOTACO, "values lost");
	clean();
}

static void test_unknown_sizes_reset(void)
{
	const size_t sizes[] = { 0, 1, 27, 28, 31, 33, 35, 36, 64, 100 };
	for (size_t n = 0; n < sizeof(sizes) / sizeof(sizes[0]); n++)
	{
		setup();
		std::vector<uint8_t> f(sizes[n] + 1, 0x5A);
		sim_put(INI, &f[0], sizes[n]);
		CHECK(LoadSettings() == LOADSETTINGS_INI_CREATED, "size %zu: not INI_CREATED", sizes[n]);
		check_defaults();
		CHECK(settings->AutobootWait == AUTOBOOTWAIT_500MS && settings->UpdateSource == UPDATESOURCE_GITHUB, "size %zu: new fields", sizes[n]);
		size_t len = 0;
		CHECK(sim_get(INI, &len) != NULL && len == SETTINGS_FILE_SIZE, "size %zu: file is %zu bytes", sizes[n], len);
		clean();
	}
}

static void test_v1_short_read_fails(void)
{
	put_v1(VERSION_MERGED, VERSION_RC);
	sim_short_reads = 1;
	CHECK(LoadSettings() == LOADSETTINGS_FAIL, "a short read was accepted");
	CHECK(error == ERROR_SETTING_READ, "error %d", error);
	check_defaults();
	size_t len = 0;
	const uint8_t* file = sim_get(INI, &len);
	CHECK(file != NULL && len == SETTINGS_FILE_SIZE_V1, "the file was changed: %zu bytes", len);
	CHECK(file != NULL && file[SETTINGS_FILE_SIZE_V1 - 1] == 0xEE, "the file was changed");
	clean();
}

static void test_autoboot_wait_ms(void)
{
	CHECK(LoadSettings() == LOADSETTINGS_INI_CREATED, "not created");
	const struct { u8 choice; u32 ms; } table[] = {
		{ AUTOBOOTWAIT_0MS, 0 }, { AUTOBOOTWAIT_100MS, 100 }, { AUTOBOOTWAIT_250MS, 250 }, { AUTOBOOTWAIT_500MS, 500 },
		{ AUTOBOOTWAIT_COUNT, 500 }, { 5, 500 }, { 255, 500 },
	};
	for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
	{
		settings->AutobootWait = table[i].choice;
		CHECK(GetAutobootWaitMs() == table[i].ms, "choice %d gives %u ms", table[i].choice, GetAutobootWaitMs());
		CHECK(SGetSetting(SETTING_AUTOBOOTWAIT) == table[i].choice, "SGetSetting AUTOBOOTWAIT");
	}
	settings->UpdateSource = UPDATESOURCE_DACOTACO;
	CHECK(SGetSetting(SETTING_UPDATESOURCE) == UPDATESOURCE_DACOTACO, "SGetSetting UPDATESOURCE");
	settings->UpdateSource = UPDATESOURCE_GITHUB;
	CHECK(SGetSetting(SETTING_UPDATESOURCE) == UPDATESOURCE_GITHUB, "SGetSetting UPDATESOURCE");
	clean();
	settings = NULL;
	CHECK(GetAutobootWaitMs() == 500, "no settings: not the default");
}

/* docs/settings.ini lists every field, with the offset and size it has in the struct */
struct DocField { std::string name; long offset; long size; };

static std::string trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r");
	if (a == std::string::npos)
		return "";
	return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

static void test_doc_lists_every_field(void)
{
	const char* root = getenv("HC_REPO_ROOT");
	CHECK(root != NULL, "HC_REPO_ROOT is not set");
	if (root == NULL)
		return;
	std::string path = std::string(root) + "/docs/settings.ini";
	FILE* f = fopen(path.c_str(), "rb");
	CHECK(f != NULL, "cannot open %s", path.c_str());
	if (f == NULL)
		return;
	std::string text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	fclose(f);

	std::vector<DocField> docs;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos)
			eol = text.size();
		std::string line = trim(text.substr(pos, eol - pos));
		pos = eol + 1;
		if (line.empty() || line[0] == ';')
			continue;
		if (line[0] == '[' && line[line.size() - 1] == ']')
		{
			DocField d = { line.substr(1, line.size() - 2), -1, -1 };
			docs.push_back(d);
			continue;
		}
		CHECK(!docs.empty(), "a line before the first section: %s", line.c_str());
		if (docs.empty())
			continue;
		size_t eq = line.find('=');
		std::string key = trim(line.substr(0, eq));
		std::string val = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
		if (key == "offset")
			docs.back().offset = atol(val.c_str());
		else if (key == "size")
			docs.back().size = atol(val.c_str());
	}

#define FIELD(x) { #x, (long)offsetof(Settings, x), (long)sizeof(((Settings*)0)->x) }
	const DocField fields[] = {
		FIELD(autoboot), FIELD(version), FIELD(ReturnTo), FIELD(ShutdownTo), FIELD(StopDisc), FIELD(LidSlotOnError),
		FIELD(IgnoreShutDownMode), FIELD(RCVersion), FIELD(SystemMenuIOS), FIELD(UseSystemMenuIOS), FIELD(BlackBackground),
		FIELD(DumpGeckoText), FIELD(PasscheckPriiloader), FIELD(PasscheckMenu), FIELD(ShowRCUpdates),
		FIELD(PreferredMountPoint), FIELD(LegacyPadding), FIELD(AutobootWait), FIELD(UpdateSource),
	};
	const size_t count = sizeof(fields) / sizeof(fields[0]);
	long used = 0;
	long gaps = 0;
	for (size_t i = 0; i < count; i++)
	{
		int found = 0;
		for (size_t j = 0; j < docs.size(); j++)
		{
			if (docs[j].name != fields[i].name)
				continue;
			found++;
			CHECK(docs[j].offset == fields[i].offset, "%s: offset %ld in the doc, %ld in the struct", fields[i].name.c_str(), docs[j].offset, fields[i].offset);
			CHECK(docs[j].size == fields[i].size, "%s: size %ld in the doc, %ld in the struct", fields[i].name.c_str(), docs[j].size, fields[i].size);
		}
		CHECK(found == 1, "%s: %d sections in the doc", fields[i].name.c_str(), found);
		used += fields[i].size;
		if (i > 0)
			gaps += fields[i].offset - (fields[i - 1].offset + fields[i - 1].size);
	}
	for (size_t j = 0; j < docs.size(); j++)
	{
		bool known = false;
		for (size_t i = 0; i < count; i++)
			known = known || docs[j].name == fields[i].name;
		CHECK(known, "section [%s] is not a field of Settings", docs[j].name.c_str());
	}
	CHECK(docs.size() == count, "%zu sections, %zu fields", docs.size(), count);
	/* the fields, the compiler's alignment gaps (the 3 bytes after each u8 before a u32) and the padding fill the file exactly */
	CHECK(used + gaps == (long)SETTINGS_FILE_SIZE, "sizes %ld + gaps %ld is not %zu", used, gaps, (size_t)SETTINGS_FILE_SIZE);
}

static const struct hc_test tests[] = {
	{ "missing_file_created", test_missing_file_created, false },
	{ "good_file", test_good_file, false },
	{ "short_file_reset", test_short_file_reset, false },
	{ "long_file_reset", test_long_file_reset, false },
	{ "stats_failure_leaves_file", test_stats_failure_leaves_file, false },
	{ "short_read_fails_with_defaults", test_short_read_fails_with_defaults, false },
	{ "old_version_healed", test_old_version_healed, false },
	{ "version_zero_healed", test_version_zero_healed, false },
	{ "alloc_failure", test_alloc_failure, false },
	{ "migrate_v1", test_migrate_v1, false },
	{ "migrate_v1_old_version", test_migrate_v1_old_version, false },
	{ "current_round_trip", test_current_round_trip, false },
	{ "unknown_sizes_reset", test_unknown_sizes_reset, false },
	{ "v1_short_read_fails", test_v1_short_read_fails, false },
	{ "autoboot_wait_ms", test_autoboot_wait_ms, false },
	{ "doc_lists_every_field", test_doc_lists_every_field, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, setup);
}
