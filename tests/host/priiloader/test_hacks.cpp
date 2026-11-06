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
/* the old states file, one byte per hack by position, and the one by name that replaced it */
#define STATES "/title/00000001/00000002/data/hacksh_s.ini"
#define NAMED_STATES "/title/00000001/00000002/data/hackstat.ini"

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
	CHECK(states_hash[0] == 0 && states_hash[1] == 0, "hacks on without a states file");
	/* nothing to save yet: the load writes no states file */
	CHECK(sim_get(STATES, NULL) == NULL && sim_get(NAMED_STATES, NULL) == NULL, "a states file was written");
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

/* ---- the parse of a file, as a digest, so the old line reader and the whole-file reader can be compared ----
 * GOLDEN below was made with the line-by-line reader (git 626413a, GetLine: 32 bytes per ISFS_Read). */
static uint64_t fnv(uint64_t h, const void* p, size_t n)
{
	const uint8_t* b = (const uint8_t*)p;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}

static uint64_t digest_hacks(void)
{
	uint64_t h = 1469598103934665603ULL;
	for (const system_hack& k : system_hacks) {
		h = fnv(h, k.desc.data(), k.desc.size()); h = fnv(h, "|", 1);
		h = fnv(h, k.masterID.data(), k.masterID.size()); h = fnv(h, "|", 1);
		h = fnv(h, k.requiredID.data(), k.requiredID.size()); h = fnv(h, "|", 1);
		h = fnv(h, &k.min_version, sizeof(k.min_version));
		h = fnv(h, &k.max_version, sizeof(k.max_version));
		for (const system_patch& pt : k.patches) {
			h = fnv(h, &pt.offset, sizeof(pt.offset));
			h = fnv(h, pt.hash.data(), pt.hash.size()); h = fnv(h, "|", 1);
			h = fnv(h, pt.patch.data(), pt.patch.size()); h = fnv(h, "#", 1);
		}
		h = fnv(h, "\n", 1);
	}
	return h;
}

struct golden_case { const char* name; std::string text; unsigned count; uint64_t digest; };

static std::string real_hacks_ini(bool crlf)
{
	const char* root = getenv("HC_REPO_ROOT");
	std::string path = std::string(root ? root : ".") + "/src/priiloader/hacks_hash.ini";
	std::string out;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return out;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.append(buf, n);
	fclose(f);
	if (crlf) {
		std::string c;
		for (char ch : out) { if (ch == '\n') c += '\r'; c += ch; }
		out = c;
	}
	return out;
}

static std::vector<golden_case> golden_cases(void)
{
	std::string two = TWO_HACKS;
	std::string crlf;
	for (char ch : two) { if (ch == '\n') crlf += '\r'; crlf += ch; }
	std::string nuls = two;
	nuls[nuls.find("minversion") + 3] = 0;
	std::vector<golden_case> v;
	v.push_back({"real_lf", real_hacks_ini(false), 0, 0});
	v.push_back({"real_crlf", real_hacks_ini(true), 0, 0});
	v.push_back({"two", two, 0, 0});
	v.push_back({"two_crlf", crlf, 0, 0});
	v.push_back({"comments_and_spaces", "# a comment\n[First hack]  \n  minversion=480 # c\nmaxversion=610\n\tpatch = 0x38000001, 0x2c000000\noffset=0x81000000\n", 0, 0});
	v.push_back({"blank_line_lf", "[First hack]\nminversion=480\n\nmaxversion=610\npatch=0x38000001\noffset=0x81000000\n[Second]\nminversion=480\nmaxversion=610\npatch=0x60000000\noffset=0x81000010\n", 0, 0});
	v.push_back({"blank_line_crlf", "[First hack]\r\nminversion=480\r\n\r\nmaxversion=610\r\npatch=0x38000001\r\noffset=0x81000000\r\n", 0, 0});
	v.push_back({"cr_only", "[First hack]\rminversion=480\rmaxversion=610\rpatch=0x38000001\roffset=0x81000000\r", 0, 0});
	v.push_back({"nul_in_line", nuls, 0, 0});
	v.push_back({"bad_line_inside", "[First hack]\nminversion=480\nmaxversion=610\nnonsense\npatch=0x38000001\noffset=0x81000000\n", 0, 0});
	v.push_back({"master_require", "[M]\nmaster=ID1\nminversion=480\nmaxversion=610\npatch=0x1111\noffset=0x81000000\n[User]\nrequire=ID1\nminversion=480\nmaxversion=610\npatch=0x2222\noffset=0x81000004\n", 0, 0});
	v.push_back({"block_edge_31", std::string("[A]\nminversion=480\nmaxversion=610\npatch=0x38000001\noffset=0x81000000\n#") + std::string(21, 'x') + "\n[B]\nminversion=480\nmaxversion=610\npatch=0x60000000\noffset=0x81000010\n", 0, 0});
	/* around the 0x2000 byte limit of a line */
	static const size_t sizes[] = {8190, 8191, 8192, 8193, 20000};
	static char names[5 * 4][32];
	int k = 0;
	for (size_t n : sizes)
		for (int nuls = 0; nuls < 2; nuls++)
			for (int nl = 0; nl < 2; nl++, k++) {
				snprintf(names[k], sizeof(names[k]), "long_%u_nul%d_nl%d", (unsigned)n, nuls, nl);
				v.push_back({names[k], with_long_line(n, nuls != 0, nl != 0), 0, 0});
			}
	return v;
}

static const struct { const char* name; unsigned count; uint64_t digest; } GOLDEN[] = {
	{ "real_lf", 75, 0x0c3cb70fa924c5c9ULL },
	{ "real_crlf", 75, 0x0c3cb70fa924c5c9ULL },
	{ "two", 2, 0xd0849c4e86c192aaULL },
	{ "two_crlf", 2, 0xd0849c4e86c192aaULL },
	{ "comments_and_spaces", 0, 0x14650fb0739d0383ULL },
	{ "blank_line_lf", 1, 0xd2afbe992e557112ULL },
	{ "blank_line_crlf", 0, 0x14650fb0739d0383ULL },
	{ "cr_only", 0, 0x14650fb0739d0383ULL },
	{ "nul_in_line", 1, 0x98d2e32d52a0485dULL },
	{ "bad_line_inside", 1, 0x09435114a9753bceULL },
	{ "master_require", 2, 0xaa7ee62d8eb5f857ULL },
	{ "block_edge_31", 2, 0x645ede4fe5a6edabULL },
	{ "long_8190_nul0_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8190_nul0_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_8190_nul1_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8190_nul1_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_8191_nul0_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8191_nul0_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_8191_nul1_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8191_nul1_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_8192_nul0_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8192_nul0_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_8192_nul1_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8192_nul1_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_8193_nul0_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8193_nul0_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_8193_nul1_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_8193_nul1_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_20000_nul0_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_20000_nul0_nl1", 0, 0x14650fb0739d0383ULL },
	{ "long_20000_nul1_nl0", 0, 0x14650fb0739d0383ULL },
	{ "long_20000_nul1_nl1", 0, 0x14650fb0739d0383ULL },
};

static void run_golden(bool nand, bool print)
{
	std::vector<golden_case> cases = golden_cases();
	for (size_t i = 0; i < cases.size(); i++) {
		setup();
		if (nand) {
			put_str(HACKS, cases[i].text);
			LoadSystemHacks(StorageDevice::NAND);
		} else {
			load_fat(cases[i].text);
			CHECK_OK();
		}
		uint64_t d = digest_hacks();
		if (print) {
			printf("\t{ \"%s\", %u, 0x%016llxULL },\n", cases[i].name, (unsigned)system_hacks.size(), (unsigned long long)d);
			continue;
		}
		CHECK(strcmp(GOLDEN[i].name, cases[i].name) == 0, "golden table out of step at %u", (unsigned)i);
		CHECK(system_hacks.size() == GOLDEN[i].count, "%s: %u hacks, the line reader found %u", cases[i].name, (unsigned)system_hacks.size(), GOLDEN[i].count);
		CHECK(d == GOLDEN[i].digest, "%s: the parse differs from the line reader's (digest %016llx, was %016llx)", cases[i].name, (unsigned long long)d, (unsigned long long)GOLDEN[i].digest);
		invariants();
		CHECK_OK();
	}
}

static void test_golden_nand(void)
{
	run_golden(true, getenv("HC_PRINT_GOLDEN") != NULL);
}

static void test_golden_fat(void)
{
	run_golden(false, false);
}

/* the real file must give hacks at all, or the comparison above proves nothing */
static void test_real_file_is_big(void)
{
	std::string t = real_hacks_ini(false);
	CHECK(t.size() > 20000, "hacks_hash.ini not found (HC_REPO_ROOT)");
	put_str(HACKS, t);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(system_hacks.size() > 50, "only %u hacks", (unsigned)system_hacks.size());
	invariants();
}

/* ISFS round trips to load the hacks of the real file: the line reader needed one read, one stats and one seek per line */
static void test_isfs_calls(void)
{
	std::string t = real_hacks_ini(false);
	put_str(HACKS, t);
	LoadSystemHacks(StorageDevice::NAND);
	printf("        ISFS calls to load %u hacks from %u bytes: %d\n", (unsigned)system_hacks.size(), (unsigned)t.size(), sim_isfs_calls);
	CHECK(sim_isfs_calls <= 12, "%d ISFS calls", sim_isfs_calls);
}

static void test_too_big_file(void)
{
	std::string t = TWO_HACKS;
	t += std::string(2 * 1024 * 1024, '#');
	put_str(HACKS, t);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(system_hacks.empty(), "hacks from a file over the limit");
	invariants();
}

static const char* SWAPPED_HACKS =
	"[Second hack]\n"
	"minversion=480\n"
	"maxversion=610\n"
	"patch=0x60000000\n"
	"offset=0x81000010\n"
	"[First hack]\n"
	"minversion=480\n"
	"maxversion=610\n"
	"patch=0x38000001,0x2c000000\n"
	"offset=0x81000000\n";

static std::string get_str(const char* path)
{
	size_t len = 0;
	const uint8_t* f = sim_get(path, &len);
	return f ? std::string(reinterpret_cast<const char*>(f), len) : std::string();
}

static void test_named_states(void)
{
	put_str(HACKS, TWO_HACKS);
	put_str(NAMED_STATES, "# a comment\r\n  Second hack  \r\n\r\nNo such hack\n");
	CHECK(LoadSystemHacks(StorageDevice::NAND) == 1, "load failed");
	CHECK(states_hash.size() == 2 && states_hash[0] == 0 && states_hash[1] == 1, "wrong states from names");
	invariants();
}

/* the reason for names: a hack list in another order keeps the hack that was chosen, where positions moved it */
static void test_named_states_reordered(void)
{
	put_str(HACKS, SWAPPED_HACKS);
	put_str(NAMED_STATES, "First hack\n");
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(system_hacks.size() == 2 && system_hacks[1].desc == "First hack", "unexpected hack order");
	CHECK(states_hash[0] == 0 && states_hash[1] == 1, "the reordered list turned on the wrong hack");
	invariants();
}

static void test_named_states_win(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[2] = {1, 1};
	sim_put(STATES, st, 2);
	put_str(NAMED_STATES, "First hack\n");
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash[0] == 1 && states_hash[1] == 0, "the old file was read while the named one exists");
	invariants();
}

static void test_named_states_empty(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[2] = {1, 1};
	sim_put(STATES, st, 2);
	sim_put(NAMED_STATES, "", 0);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash[0] == 0 && states_hash[1] == 0, "an empty named file must mean every hack is off");
	invariants();
}

static void test_named_states_other_version(void)
{
	put_str(HACKS, TWO_HACKS);
	put_str(NAMED_STATES, "First hack\nSecond hack\n");
	g_sysmenu_version = 700;
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash[0] == 0 && states_hash[1] == 0, "hacks for another version stayed on");
	invariants();
}

static void test_legacy_migrates(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[2] = {0, 5};
	sim_put(STATES, st, 2);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash[0] == 0 && states_hash[1] == 1, "the old states were not kept as 0/1");
	std::string named = get_str(NAMED_STATES);
	CHECK(named.find("Second hack\n") != std::string::npos && named.find("First hack") == std::string::npos, "migrated file: '%s'", named.c_str());
	CHECK(sim_get(STATES, NULL) == NULL, "the old states file was kept");
	invariants();
}

static void test_legacy_mismatch_kept(void)
{
	put_str(HACKS, TWO_HACKS);
	const char st[1] = {1};
	sim_put(STATES, st, 1);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash[0] == 0 && states_hash[1] == 0, "states from a file of the wrong size");
	CHECK(sim_get(NAMED_STATES, NULL) == NULL, "a named file was written from nothing");
	invariants();
}

static void test_save_round_trip(void)
{
	put_str(HACKS, TWO_HACKS);
	LoadSystemHacks(StorageDevice::NAND);
	states_hash[0] = 1;
	CHECK(SaveHackStates() == 0, "save failed");
	CHECK(get_str(NAMED_STATES).find("First hack\n") != std::string::npos, "saved: '%s'", get_str(NAMED_STATES).c_str());

	/* a new hack list with the same hacks in another order */
	put_str(HACKS, SWAPPED_HACKS);
	LoadSystemHacks(StorageDevice::NAND);
	CHECK(states_hash[0] == 0 && states_hash[1] == 1, "the saved hack did not follow its name");
	invariants();
}

static const struct hc_test tests[] = {
	{ "valid_states", test_valid_states, false },
	{ "states_for_other_version", test_states_for_other_version, false },
	{ "states_missing", test_states_missing, false },
	{ "states_short", test_states_short, false },
	{ "states_long", test_states_long, false },
	{ "states_short_read", test_states_short_read, false },
	{ "states_stats_fail", test_states_stats_fail, false },
	{ "named_states", test_named_states, false },
	{ "named_states_reordered", test_named_states_reordered, false },
	{ "named_states_win", test_named_states_win, false },
	{ "named_states_empty", test_named_states_empty, false },
	{ "named_states_other_version", test_named_states_other_version, false },
	{ "legacy_migrates", test_legacy_migrates, false },
	{ "legacy_mismatch_kept", test_legacy_mismatch_kept, false },
	{ "save_round_trip", test_save_round_trip, false },
	{ "hacks_stats_fail", test_hacks_stats_fail, false },
	{ "alloc_failure_sweep", test_alloc_failure_sweep, false },
	{ "empty_file", test_empty_file, false },
	{ "missing_file", test_missing_file, false },
	{ "long_lines_nand", test_long_lines_nand, false },
	{ "no_trailing_newline", test_valid_hacks_after_normal_lines, false },
	{ "fat_valid", test_fat_valid, false },
	{ "fat_long_lines", test_fat_long_lines, false },
	{ "golden_nand", test_golden_nand, false },
	{ "golden_fat", test_golden_fat, false },
	{ "real_file_is_big", test_real_file_is_big, false },
	{ "isfs_calls", test_isfs_calls, false },
	{ "too_big_file", test_too_big_file, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, setup);
}
