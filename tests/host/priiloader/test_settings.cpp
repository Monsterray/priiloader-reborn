/*
 * test_settings.cpp -- host tests of LoadSettings in src/priiloader/source/settings.cpp (loader.ini on NAND),
 * against the simulated NAND and its guarded heap.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdio.h>
#include <string.h>
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
	sim_put(INI, &s, sizeof(Settings));
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
	CHECK(file != NULL && len == sizeof(Settings), "file is %zu bytes", len);
	CHECK(memcmp(file, settings, sizeof(Settings)) == 0, "file differs from the settings in memory");
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
	CHECK(sim_get(INI, &len) != NULL && len == sizeof(Settings), "file is %zu bytes", len);
	clean();
}

static void test_long_file_reset(void)
{
	std::vector<uint8_t> big(sizeof(Settings) + 10, 0x11);
	sim_put(INI, &big[0], big.size());
	CHECK(LoadSettings() == LOADSETTINGS_INI_CREATED, "not INI_CREATED");
	check_defaults();
	CHECK_OK();
	size_t len = 0;
	CHECK(sim_get(INI, &len) != NULL && len == sizeof(Settings), "file is %zu bytes", len);
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
	CHECK(len == sizeof(Settings) && memcmp(file, &good, sizeof(Settings)) == 0, "the file was changed");
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
	CHECK(file != NULL && len == sizeof(Settings), "file size");
	Settings onNand;
	memcpy(&onNand, file, sizeof(Settings));
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
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, setup);
}
