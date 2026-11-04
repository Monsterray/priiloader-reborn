/*
 * test_nfo.cpp -- host tests of src/priiloader/source/nfo.cpp: the main.nfo header check and the
 * argument strings of an installed DOL.
 *
 * main.nfo as InstallLoadDOL writes it (every released priiloader does): s8 AHBPROT bit, u8 argument
 * count, s32 length (big endian, the PPC's order), then `length` bytes of 0 terminated arguments.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdint.h>
#include <string>
#include <vector>

#include "nfo.h"

static void hdr(uint8_t out[6], uint8_t ahb, uint8_t argc, uint32_t len)
{
	out[0] = ahb;
	out[1] = argc;
	out[2] = (uint8_t)(len >> 24);
	out[3] = (uint8_t)(len >> 16);
	out[4] = (uint8_t)(len >> 8);
	out[5] = (uint8_t)len;
}

static int parse(uint8_t ahb, uint8_t argc, uint32_t len, uint32_t file_length, int32_t* out_len = NULL)
{
	uint8_t h[6];
	hdr(h, ahb, argc, len);
	int8_t a = 99;
	uint8_t c = 99;
	int32_t l = 99;
	int r = NfoParseHeader(h, file_length, &a, &c, &l);
	if (out_len)
		*out_len = l;
	return r;
}

static void test_valid(void)
{
	int32_t len = 0;
	CHECK(parse(0, 2, 10, 16, &len) == NFO_OK && len == 10, "valid file refused");
	CHECK(parse(1, 1, 1, 7, &len) == NFO_OK && len == 1, "ahbprot 1 refused");
}

static void test_big_endian(void)
{
	uint8_t h[6] = {1, 3, 0x00, 0x00, 0x01, 0x02}; /* 258 */
	int8_t a = 0;
	uint8_t c = 0;
	int32_t l = 0;
	CHECK(NfoParseHeader(h, 6 + 258, &a, &c, &l) == NFO_OK, "refused");
	CHECK(l == 258 && a == 1 && c == 3, "got length %d ahb %d count %d", (int)l, (int)a, (int)c);
	/* the same bytes read little endian would be 0x02010000: not acceptable */
	CHECK(NfoParseHeader(h, 6 + 257, &a, &c, &l) == NFO_BAD_LENGTH, "one byte short accepted");
}

static void test_length_equals_file_minus_header(void)
{
	CHECK(parse(0, 1, 100, 106) == NFO_OK, "length == file - 6 refused");
	CHECK(parse(0, 1, 101, 106) == NFO_BAD_LENGTH, "length == file - 5 accepted");
	CHECK(parse(0, 1, 100, 105) == NFO_BAD_LENGTH, "file one byte short accepted");
}

static void test_negative_and_huge_length(void)
{
	CHECK(parse(0, 1, 0xFFFFFFFFu, 100) == NFO_BAD_LENGTH, "length -1 accepted");
	CHECK(parse(0, 1, 0x80000000u, 100) == NFO_BAD_LENGTH, "length INT_MIN accepted");
	CHECK(parse(0, 1, 0x7FFFFFFFu, 0xFFFFFFFFu) == NFO_BAD_LENGTH, "length INT_MAX accepted");
	CHECK(parse(0, 1, 0, 100) == NFO_BAD_LENGTH, "length 0 with arguments accepted");
}

static void test_oversized(void)
{
	CHECK(parse(0, 1, NFO_MAX_ARG_LENGTH, 6 + NFO_MAX_ARG_LENGTH) == NFO_OK, "the largest block refused");
	CHECK(parse(0, 1, NFO_MAX_ARG_LENGTH + 1, 6 + NFO_MAX_ARG_LENGTH + 1) == NFO_BAD_LENGTH, "a block over the cap accepted");
	CHECK(parse(0, 1, 1u << 20, 6 + (1u << 20)) == NFO_BAD_LENGTH, "a 1 MB block accepted");
}

static void test_too_small(void)
{
	uint8_t h[6];
	hdr(h, 0, 0, 0);
	int8_t a;
	uint8_t c;
	int32_t l;
	for (uint32_t n = 0; n < 6; n++)
		CHECK(NfoParseHeader(h, n, &a, &c, &l) == NFO_TOO_SMALL, "file of %u bytes accepted", (unsigned)n);
	CHECK(NfoParseHeader(NULL, 100, &a, &c, &l) == NFO_TOO_SMALL, "NULL header accepted");
	CHECK(NfoParseHeader(h, 6, &a, &c, &l) == NFO_OK, "a header only file without arguments refused");
}

/* what a file that upstream's priiloader wrote looks like must still pass */
static void test_ahbprot_values(void)
{
	CHECK(parse(0, 0, 0, 6) == NFO_OK, "ahbprot 0 refused");
	CHECK(parse(1, 0, 0, 6) == NFO_OK, "ahbprot 1 refused");
	/* the old reader deleted the file for any other value, so no release writes one */
	CHECK(parse(2, 0, 0, 6) == NFO_BAD_AHBPROT, "ahbprot 2 accepted");
	CHECK(parse(0xFF, 0, 0, 6) == NFO_BAD_AHBPROT, "ahbprot -1 accepted");
	CHECK(parse(0x80, 0, 0, 6) == NFO_BAD_AHBPROT, "ahbprot -128 accepted");
}

static void test_argc_zero(void)
{
	int32_t len = 77;
	/* the length is not used when there are no arguments, whatever it holds */
	CHECK(parse(0, 0, 0xDEADBEEFu, 6, &len) == NFO_OK && len == 0, "argc 0 with a junk length refused or not zeroed");
	CHECK(parse(1, 0, 5, 6, &len) == NFO_OK && len == 0, "argc 0 with length 5 refused or not zeroed");
}

static std::vector<uint8_t> writer_file(uint8_t ahb, const std::vector<std::string>& args, bool count_empty_args)
{
	/* InstallLoadDOL: argument_count, then a length that holds one more byte than the arguments */
	uint32_t len = NfoArgumentsLength(args) + 1;
	uint8_t count = count_empty_args ? (uint8_t)args.size() : (uint8_t)NfoArgumentCount(args);
	std::vector<uint8_t> f(6 + len, 0);
	hdr(f.data(), ahb, count, len);
	NfoFillArguments((char*)f.data() + 6, args);
	return f;
}

static void test_files_priiloader_writes(void)
{
	const std::vector<std::string> cases[] = {
		{},
		{"-x"},
		{"a", "", "b"},
		{"", ""},
		{std::string(300, 'z')},
	};
	for (const auto& args : cases)
		for (int upstream = 0; upstream < 2; upstream++)
			for (uint8_t ahb = 0; ahb < 2; ahb++) {
				std::vector<uint8_t> f = writer_file(ahb, args, upstream == 1);
				int8_t a = 9;
				uint8_t c = 9;
				int32_t l = 0;
				int r = NfoParseHeader(f.data(), (uint32_t)f.size(), &a, &c, &l);
				CHECK(r == NFO_OK, "a file the installer wrote was refused (%d args, upstream count %d, ahb %d): %d", (int)args.size(), upstream, (int)ahb, r);
				CHECK(a == ahb, "ahbprot changed");
				if (c > 0)
					CHECK((uint32_t)l == f.size() - 6, "length %d for a file of %u", (int)l, (unsigned)f.size());
			}
}

static void test_count_and_length(void)
{
	std::vector<std::string> args = {"abc", "", std::string(128, 'x'), std::string(129, 'y'), std::string(1000, 'q')};
	CHECK(NfoArgumentCount(args) == 4, "count %u", (unsigned)NfoArgumentCount(args));
	/* 3 + 1, 128 + 1, 128 + 1 (cut), 128 + 1 (cut) */
	CHECK(NfoArgumentsLength(args) == 4 + 129 * 3, "length %u", (unsigned)NfoArgumentsLength(args));
	CHECK(NfoArgumentCount({}) == 0 && NfoArgumentsLength({}) == 0, "empty list");
}

static void check_fill(const std::vector<std::string>& args, const char* what)
{
	uint32_t need = NfoArgumentsLength(args);
	std::vector<char> buf(need + 64, (char)0x5A);
	uint32_t wrote = NfoFillArguments(buf.data(), args);
	CHECK(wrote == need, "%s: wrote %u bytes, length said %u", what, (unsigned)wrote, (unsigned)need);
	for (size_t i = need; i < buf.size(); i++)
		CHECK(buf[i] == (char)0x5A, "%s: byte %u behind the block was written", what, (unsigned)(i - need));
	/* what was written splits back into the arguments, each cut and 0 terminated */
	size_t pos = 0;
	for (const auto& a : args) {
		if (a.empty())
			continue;
		std::string kept(a.c_str());
		if (kept.size() > NFO_MAX_ARG_SIZE)
			kept.resize(NFO_MAX_ARG_SIZE);
		CHECK(pos + kept.size() < need, "%s: ran out of block", what);
		CHECK(std::string(buf.data() + pos) == kept, "%s: argument changed", what);
		pos += kept.size() + 1;
	}
	CHECK(pos == need, "%s: arguments cover %u of %u bytes", what, (unsigned)pos, (unsigned)need);
}

static void test_fill(void)
{
	check_fill({}, "none"); CHECK_OK();
	check_fill({""}, "one empty"); CHECK_OK();
	check_fill({"a"}, "one"); CHECK_OK();
	check_fill({"a", "", "bc"}, "empty between"); CHECK_OK();
	check_fill({std::string(127, 'x'), std::string(128, 'y'), std::string(129, 'z')}, "around the cut"); CHECK_OK();
	check_fill({std::string(5000, 'L')}, "5000 characters"); CHECK_OK();
	check_fill({std::string("ab\0cd", 5), "next"}, "NUL inside"); CHECK_OK();
	check_fill({std::string("\0abc", 4), "next"}, "NUL first"); CHECK_OK();
}

static const struct hc_test tests[] = {
	{ "valid", test_valid, false },
	{ "big_endian", test_big_endian, false },
	{ "length_equals_file_minus_header", test_length_equals_file_minus_header, false },
	{ "negative_and_huge_length", test_negative_and_huge_length, false },
	{ "oversized", test_oversized, false },
	{ "too_small", test_too_small, false },
	{ "ahbprot_values", test_ahbprot_values, false },
	{ "argc_zero", test_argc_zero, false },
	{ "files_priiloader_writes", test_files_priiloader_writes, false },
	{ "count_and_length", test_count_and_length, false },
	{ "fill", test_fill, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, NULL);
}
