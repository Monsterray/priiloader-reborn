/*
 * test_hacks.cpp -- host tests of src/priiloader/source/hacks.cpp, unchanged, against the simulated NAND.
 *
 * Every test ends with the same invariants: there is one state per loaded hack (callers index
 * states_hash with a hack's index), no file is left open, no heap block is left, and the heap guards
 * were not touched (the simulated heap stops the test on an overrun).
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "gccore.h"
#include "hacks.h"
#include "sim.h"

extern FILE* fat_file_handler;
extern s32 nand_file_handler;
extern u32 g_sysmenu_version;
extern std::string g_fat_path;

#define HACKS "/title/00000001/00000002/data/hackshas.ini"
#define STATES "/title/00000001/00000002/data/hacksh_s.ini"

static const char* TWO_HACKS =
	"[First hack]\n"
	"minversion=480\n"
	"maxversion=610\n"
	"patch=0x38000001,0x2c000000\n"
	"offset=0x81000000\n"
	"[Second hack]\n"
	"minversion=480\n"
	"maxversion=610\n"
	"patch=0x60000000\n"
	"offset=0x81000010\n";

static void put_str(const char* path, const std::string& s)
{
	sim_put(path, s.data(), s.size());
}

static void setup(void)
{
	sim_reset();
	system_hacks.clear();
	states_hash.clear();
	fat_file_handler = NULL;
	nand_file_handler = -1;
	g_sysmenu_version = 514;
}

static void invariants(void)
{
	CHECK(states_hash.size() == system_hacks.size(), "states_hash has %u entries for %u hacks", (unsigned)states_hash.size(), (unsigned)system_hacks.size());
	CHECK(sim_open_fds == 0, "%d files left open", sim_open_fds);
	CHECK(sim_live_allocs == 0, "%d heap blocks leaked", sim_live_allocs);
	CHECK(nand_file_handler == -1, "nand_file_handler left at %d", (int)nand_file_handler);
	CHECK(fat_file_handler == NULL, "fat_file_handler left open");
}

static void test_valid_states(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[2] = {1, 1};
	sim_put(STATES, st, 2);
	CHECK(LoadSystemHacks(StorageDevice::NAND) == 1, "load failed");
	CHECK(system_hacks.size() == 2, "%u hacks", (unsigned)system_hacks.size());
	CHECK(states_hash[0] == 1 && states_hash[1] == 1, "states were not kept");
	invariants();
}

static void test_states_for_other_version(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[2] = {1, 1};
	sim_put(STATES, st, 2);
	g_sysmenu_version = 700; /* outside minversion..maxversion */
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash.size() == 2 && states_hash[0] == 0 && states_hash[1] == 0, "hacks for another version stayed on");
	invariants();
}

static void test_states_missing(void)
{
	put_str(HACKS, TWO_HACKS);
	CHECK(LoadSystemHacks(StorageDevice::NAND) == 1, "load failed");
	CHECK(system_hacks.size() == 2, "%u hacks", (unsigned)system_hacks.size());
	size_t len = 0;
	const uint8_t* f = sim_get(STATES, &len);
	CHECK(f != NULL && len == 2, "the states file was not created (len %u)", (unsigned)len);
	invariants();
}

static void test_states_short(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[1] = {1};
	sim_put(STATES, st, 1);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash.size() == 2 && states_hash[0] == 0 && states_hash[1] == 0, "wrong states for a short file");
	invariants();
}

static void test_states_long(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[5] = {1, 1, 1, 1, 1};
	sim_put(STATES, st, 5);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash.size() == 2 && states_hash[0] == 0, "wrong states for a long file");
	invariants();
}

static void test_states_short_read(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[2] = {1, 1};
	sim_put(STATES, st, 2);
	sim_short_reads = 1;
	sim_fail_stats_path = "hacksh_s";
	LoadSystemHacks(StorageDevice::NAND);
	/* the early return used to leave states_hash empty while there were hacks, and the file open */
	CHECK(system_hacks.size() == 2, "%u hacks", (unsigned)system_hacks.size());
	invariants();
}

static void test_states_stats_fail(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[2] = {1, 1};
	sim_put(STATES, st, 2);
	sim_fail_stats = 1;
	sim_fail_stats_path = "hacksh_s";
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(system_hacks.size() == 2, "%u hacks", (unsigned)system_hacks.size());
	invariants();
}

static void test_hacks_stats_fail(void)
{
	put_str(HACKS, TWO_HACKS);
	sim_fail_stats = 1;
	LoadSystemHacks(StorageDevice::NAND);
	invariants();
}

/* the Nth allocation fails, for every N that is used while loading */
static void test_alloc_failure_sweep(void)
{
	for (int n = 1; n < 80; n++) {
		setup();
		put_str(HACKS, TWO_HACKS);
		const char st[2] = {1, 1};
		sim_put(STATES, st, 2);
		sim_alloc_fail_at = n;
		LoadSystemHacks(StorageDevice::NAND);
		if (hc_failure == NULL)
			invariants();
		if (hc_failure) {
			char m[512];
			snprintf(m, sizeof(m), "allocation %d fails: %s", n, hc_failure);
			snprintf(hc_message, sizeof(hc_message), "%s", m);
			hc_failure = hc_message;
			return;
		}
	}
}

static void test_empty_file(void)
{
	sim_put(HACKS, "", 0);
	CHECK(LoadSystemHacks(StorageDevice::NAND) == 0, "an empty file is not a success");
	CHECK(system_hacks.empty(), "hacks from an empty file");
	invariants();
}

static void test_missing_file(void)
{
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(system_hacks.empty(), "hacks from no file");
	invariants();
}

static std::string with_long_line(size_t n, bool nuls, bool newline_after)
{
	std::string s = "[Hack]\nminversion=480\nmaxversion=610\n";
	std::string line(n, 'A');
	if (nuls)
		for (size_t i = 7; i < n; i += 97)
			line[i] = 0;
	s += line;
	if (newline_after)
		s += "\n" + std::string(TWO_HACKS);
	return s;
}

static void test_long_lines_nand(void)
{
	/* around the 0x2000 byte limit of a line, with and without NULs, with and without a newline at the end */
	static const size_t sizes[] = {100, 8190, 8191, 8192, 8193, 8224, 9000, 20000, 70000};
	for (size_t n : sizes)
		for (int nuls = 0; nuls < 2; nuls++)
			for (int nl = 0; nl < 2; nl++) {
				setup();
				put_str(HACKS, with_long_line(n, nuls, nl));
				LoadSystemHacks(StorageDevice::NAND);
				if (hc_failure == NULL)
					invariants();
				if (hc_failure) {
					char m[512];
					snprintf(m, sizeof(m), "line of %u, nuls %d, newline %d: %s", (unsigned)n, nuls, nl, hc_failure);
					snprintf(hc_message, sizeof(hc_message), "%s", m);
					hc_failure = hc_message;
					return;
				}
			}
}

static void test_valid_hacks_after_normal_lines(void)
{
	/* no trailing newline on the last line */
	std::string s = TWO_HACKS;
	s.pop_back();
	put_str(HACKS, s);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(system_hacks.size() == 2, "%u hacks", (unsigned)system_hacks.size());
	CHECK(system_hacks[1].patches.size() == 1 && system_hacks[1].patches[0].offset == 0x81000010, "second hack misparsed");
	invariants();
}

static void load_fat(const std::string& content)
{
	char* name = tmpnam(NULL);
	CHECK(name != NULL, "no temporary name");
	g_fat_path = name;
	FILE* f = fopen(name, "wb");
	CHECK(f != NULL, "cannot write %s", name);
	fwrite(content.data(), 1, content.size(), f);
	fclose(f);
	LoadSystemHacks(StorageDevice::Auto);
	remove(name);
}

static void test_fat_valid(void)
{
	load_fat(TWO_HACKS);
	CHECK_OK();
	CHECK(system_hacks.size() == 2, "%u hacks", (unsigned)system_hacks.size());
	invariants();
}

static void test_fat_long_lines(void)
{
	static const size_t sizes[] = {8191, 8192, 8193, 20000};
	for (size_t n : sizes)
		for (int nuls = 0; nuls < 2; nuls++) {
			setup();
			load_fat(with_long_line(n, nuls, true));
			if (hc_failure == NULL)
				invariants();
			if (hc_failure) {
				char m[512];
				snprintf(m, sizeof(m), "fat line of %u, nuls %d: %s", (unsigned)n, nuls, hc_failure);
				snprintf(hc_message, sizeof(hc_message), "%s", m);
				hc_failure = hc_message;
				return;
			}
		}
}

static const struct hc_test tests[] = {
	{ "valid_states", test_valid_states, false },
	{ "states_for_other_version", test_states_for_other_version, false },
	{ "states_missing", test_states_missing, false },
	{ "states_short", test_states_short, false },
	{ "states_long", test_states_long, false },
	{ "states_short_read", test_states_short_read, false },
	{ "states_stats_fail", test_states_stats_fail, false },
	{ "hacks_stats_fail", test_hacks_stats_fail, false },
	{ "alloc_failure_sweep", test_alloc_failure_sweep, false },
	{ "empty_file", test_empty_file, false },
	{ "missing_file", test_missing_file, false },
	{ "long_lines_nand", test_long_lines_nand, false },
	{ "no_trailing_newline", test_valid_hacks_after_normal_lines, false },
	{ "fat_valid", test_fat_valid, false },
	{ "fat_long_lines", test_fat_long_lines, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, setup);
}
