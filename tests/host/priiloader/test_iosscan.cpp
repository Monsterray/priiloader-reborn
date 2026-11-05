/*
 * test_iosscan.cpp -- host tests of src/Shared/IOSScan.hpp, the pattern scan of PatchIOS.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdint.h>
#include <string.h>
#include <vector>

#include "IOSScan.hpp"

typedef uint8_t u8;

struct TestPatch
{
	std::vector<u8> Pattern;
	std::vector<u8> Replacement;
	mutable int Applied;
	mutable std::vector<size_t> Where;
	u8* Base;

	TestPatch(std::vector<u8> pattern, std::vector<u8> replacement, u8* base)
		: Pattern(pattern), Replacement(replacement), Applied(0), Base(base) {}

	void Patch(u8* address) const
	{
		Applied++;
		Where.push_back((size_t)(address - Base));
		memcpy(address, &Replacement[0], Replacement.size());
	}
};

/* a buffer with a guard of 0xA5 bytes behind it, so a read past the end is seen (AddressSanitizer sees it too
 * when the memory is not inside a larger block, so the guard is the check that always runs) */
struct Buffer
{
	std::vector<u8> mem;
	size_t size;
	explicit Buffer(size_t n) : mem(n + 64, 0xA5), size(n) { memset(&mem[0], 0, n); }
	u8* begin() { return &mem[0]; }
	u8* end() { return &mem[0] + size; }
	void put(size_t at, std::vector<u8> bytes) { memcpy(begin() + at, &bytes[0], bytes.size()); }
	bool guard_intact() const
	{
		for (size_t i = size; i < mem.size(); i++)
			if (mem[i] != 0xA5)
				return false;
		return true;
	}
};

static std::vector<u8> B(std::initializer_list<u8> l) { return std::vector<u8>(l); }

typedef std::vector<TestPatch> Patches;

static uint32_t scan(Buffer& b, const Patches& p, int* callbacks = NULL)
{
	return ScanAndApplyPatches(b.begin(), b.end(), p, [callbacks](u8*, size_t) { if (callbacks) (*callbacks)++; });
}

static void test_all_applied(void)
{
	Buffer b(256);
	b.put(10, B({1, 2, 3}));
	b.put(100, B({4, 5}));
	b.put(200, B({6, 7, 8, 9}));
	Patches p = {
		TestPatch(B({1, 2, 3}), B({0xA, 0xB, 0xC}), b.begin()),
		TestPatch(B({4, 5}), B({0xD, 0xE}), b.begin()),
		TestPatch(B({6, 7, 8, 9}), B({0, 0, 0, 0}), b.begin()),
	};
	int cb = 0;
	CHECK(scan(b, p, &cb) == 3, "not 3 applied");
	CHECK(cb == 3, "callback ran %d times", cb);
	CHECK(b.mem[10] == 0xA && b.mem[12] == 0xC, "first patch not written");
	CHECK(b.mem[100] == 0xD && b.mem[101] == 0xE, "second patch not written");
	CHECK(b.mem[200] == 0 && b.mem[203] == 0, "third patch not written");
	CHECK(p[0].Where[0] == 10 && p[1].Where[0] == 100 && p[2].Where[0] == 200, "wrong addresses");
	CHECK(b.guard_intact(), "guard written");
}

static void test_stops_once_each_applied(void)
{
	Buffer b(256);
	b.put(10, B({1, 2}));
	b.put(20, B({3, 4}));
	b.put(30, B({1, 2})); /* a second site of the first pattern, after both were applied */
	b.put(40, B({3, 4}));
	Patches p = { TestPatch(B({1, 2}), B({9, 9}), b.begin()), TestPatch(B({3, 4}), B({8, 8}), b.begin()) };
	CHECK(scan(b, p) == 2, "applied more than once each after both were found");
	CHECK(b.mem[30] == 1 && b.mem[40] == 3, "the scan went on after every patch was applied");
}

static void test_repeat_before_other_found(void)
{
	Buffer b(256);
	b.put(10, B({1, 2}));
	b.put(20, B({1, 2})); /* matches again before the other pattern is found */
	b.put(50, B({3, 4}));
	b.put(60, B({1, 2})); /* after the stop */
	Patches p = { TestPatch(B({1, 2}), B({9, 9}), b.begin()), TestPatch(B({3, 4}), B({8, 8}), b.begin()) };
	CHECK(scan(b, p) == 3, "expected 3 applications");
	CHECK(p[0].Applied == 2 && p[1].Applied == 1, "applied %d and %d times", p[0].Applied, p[1].Applied);
	CHECK(b.mem[10] == 9 && b.mem[20] == 9, "both early sites patched");
	CHECK(b.mem[60] == 1, "site after the stop patched");
}

static void test_pattern_at_very_end(void)
{
	Buffer b(64);
	b.put(61, B({7, 8, 9}));
	Patches p = { TestPatch(B({7, 8, 9}), B({1, 1, 1}), b.begin()) };
	CHECK(scan(b, p) == 1, "the last bytes of the buffer were not matched");
	CHECK(b.mem[61] == 1 && b.mem[63] == 1, "not written");
	CHECK(b.guard_intact(), "guard written");
}

static void test_pattern_longer_than_buffer(void)
{
	Buffer b(4);
	Patches p = { TestPatch(B({0, 0, 0, 0, 0}), B({1, 1, 1, 1, 1}), b.begin()) };
	CHECK(scan(b, p) == 0, "a pattern longer than the buffer matched");
	CHECK(b.guard_intact(), "guard written");
}

static void test_no_read_past_end(void)
{
	/* the pattern's first byte is the buffer's last byte and its second byte is the guard's: it must not match */
	Buffer b(32);
	b.mem[31] = 0x77;
	Patches p = { TestPatch(B({0x77, 0xA5}), B({0, 0}), b.begin()) };
	CHECK(scan(b, p) == 0, "matched across the end of the buffer");
	CHECK(p[0].Applied == 0, "patched across the end");
	CHECK(b.guard_intact(), "guard written");
}

static void test_empty_pattern_ignored(void)
{
	Buffer b(64);
	b.put(5, B({1, 2}));
	Patches p = { TestPatch(B({}), B({0}), b.begin()), TestPatch(B({1, 2}), B({3, 3}), b.begin()) };
	CHECK(scan(b, p) == 1, "applied count");
	CHECK(p[0].Applied == 0, "empty pattern applied");
	CHECK(p[1].Applied == 1 && b.mem[5] == 3, "real patch not applied (the empty one held the scan up or hid it)");
}

static void test_only_empty_patterns(void)
{
	Buffer b(16);
	Patches p = { TestPatch(B({}), B({0}), b.begin()) };
	CHECK(scan(b, p) == 0, "applied");
	CHECK(p[0].Applied == 0, "empty pattern applied");
}

static void test_no_patches_or_bad_range(void)
{
	Buffer b(16);
	Patches none;
	CHECK(scan(b, none) == 0, "no patches");
	Patches p = { TestPatch(B({0}), B({1}), b.begin()) };
	CHECK(ScanAndApplyPatches(b.begin(), b.begin(), p, [](u8*, size_t) {}) == 0, "empty range");
	CHECK(ScanAndApplyPatches(b.end(), b.begin(), p, [](u8*, size_t) {}) == 0, "reversed range");
	CHECK(ScanAndApplyPatches((u8*)NULL, b.end(), p, [](u8*, size_t) {}) == 0, "NULL begin");
	CHECK(p[0].Applied == 0, "patched");
}

static void test_first_matching_patch_wins(void)
{
	Buffer b(32);
	b.put(8, B({1, 2, 3}));
	Patches p = { TestPatch(B({1, 2}), B({9, 9}), b.begin()), TestPatch(B({1, 2, 3}), B({7, 7, 7}), b.begin()) };
	scan(b, p);
	CHECK(p[0].Applied == 1 && p[1].Applied == 0, "only the first matching patch applies at an address (%d, %d)", p[0].Applied, p[1].Applied);
}

static void test_not_all_found(void)
{
	Buffer b(64);
	b.put(5, B({1, 2}));
	Patches p = { TestPatch(B({1, 2}), B({3, 3}), b.begin()), TestPatch(B({5, 5, 5}), B({0, 0, 0}), b.begin()) };
	CHECK(scan(b, p) == 1, "applied count");
	CHECK(p[1].Applied == 0, "second patch applied");
	CHECK(b.guard_intact(), "guard written");
}

static const struct hc_test tests[] = {
	{ "all_applied", test_all_applied, false },
	{ "stops_once_each_applied", test_stops_once_each_applied, false },
	{ "repeat_before_other_found", test_repeat_before_other_found, false },
	{ "pattern_at_very_end", test_pattern_at_very_end, false },
	{ "pattern_longer_than_buffer", test_pattern_longer_than_buffer, false },
	{ "no_read_past_end", test_no_read_past_end, false },
	{ "empty_pattern_ignored", test_empty_pattern_ignored, false },
	{ "only_empty_patterns", test_only_empty_patterns, false },
	{ "no_patches_or_bad_range", test_no_patches_or_bad_range, false },
	{ "first_matching_patch_wins", test_first_matching_patch_wins, false },
	{ "not_all_found", test_not_all_found, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, NULL);
}
