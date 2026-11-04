/*
 * test_password.cpp -- host tests of src/priiloader/source/password_file.cpp, the reading and splitting
 * of password.txt, against the simulated NAND and its guarded heap.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdio.h>
#include <string.h>
#include <string>

#include "gccore.h"
#include "mem2_manager.h"
#include "password_file.h"
#include "sim.h"

static void setup(void)
{
	sim_reset();
}

static void put_str(const std::string& s)
{
	sim_put(PASSWORD_FILE_PATH, s.data(), s.size());
}

static void clean(void)
{
	CHECK(sim_open_fds == 0, "%d files left open", sim_open_fds);
	CHECK(sim_live_allocs == 0, "%d heap blocks leaked", sim_live_allocs);
}

static void test_missing(void)
{
	char *buf = (char*)1, *pw = (char*)1, *file = (char*)1;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_MISSING, "not MISSING");
	CHECK(buf == NULL && pw == NULL && file == NULL, "outputs not cleared");
	clean();
}

static void test_empty(void)
{
	put_str("");
	char *buf = NULL, *pw = (char*)1, *file = (char*)1;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_OK, "an empty file failed");
	CHECK(buf != NULL && pw == NULL && file == NULL, "pw %p file %p", (void*)pw, (void*)file);
	mem_free(buf);
	clean();
}

static void test_one_line(void)
{
	put_str("secret");
	char *buf = NULL, *pw = NULL, *file = NULL;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_OK, "failed");
	CHECK(pw != NULL && strcmp(pw, "secret") == 0, "password wrong");
	CHECK(file == NULL, "a file name from one line");
	mem_free(buf);
	clean();
}

static void test_two_lines(void)
{
	put_str("pw\r\nfile\r\n");
	char *buf = NULL, *pw = NULL, *file = NULL;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_OK, "failed");
	CHECK(pw && strcmp(pw, "pw") == 0, "password wrong");
	CHECK(file && strcmp(file, "file") == 0, "file wrong");
	mem_free(buf);
	clean();
}

static void test_three_lines_and_blank_lines(void)
{
	put_str("\r\n\r\npw\n\nfile\nextra\n");
	char *buf = NULL, *pw = NULL, *file = NULL;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_OK, "failed");
	CHECK(pw && strcmp(pw, "pw") == 0, "password wrong");
	CHECK(file && strcmp(file, "file") == 0, "file wrong");
	mem_free(buf);
	clean();
}

static void test_only_newlines(void)
{
	put_str("\r\n\r\n\r\n");
	char *buf = NULL, *pw = (char*)1, *file = (char*)1;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_OK, "failed");
	CHECK(pw == NULL && file == NULL, "lines from nothing");
	mem_free(buf);
	clean();
}

/* sizes that are a multiple of 32 filled the whole buffer, so nothing ended the last line */
static void test_sizes_multiple_of_32(void)
{
	static const size_t sizes[] = {1, 31, 32, 33, 63, 64, 96, 4096, 4097, 8192};
	for (size_t n : sizes) {
		setup();
		put_str(std::string(n, 'a'));
		char *buf = NULL, *pw = NULL, *file = NULL;
		int r = ReadPasswordFile(&buf, &pw, &file);
		CHECK(r == PASSWORD_FILE_OK, "size %u failed", (unsigned)n);
		CHECK(pw != NULL && strlen(pw) == n, "size %u: password length %u", (unsigned)n, pw ? (unsigned)strlen(pw) : 0);
		CHECK(file == NULL, "size %u: file name", (unsigned)n);
		/* the buffer has room behind the file for the short text that password code writes there */
		memset(buf + n, 'x', PASSWORD_FILE_SLACK);
		mem_free(buf);
		clean();
		CHECK_OK();
	}
}

static void test_stats_fail(void)
{
	put_str("pw");
	sim_fail_stats = 1;
	char *buf = (char*)1, *pw = (char*)1, *file = (char*)1;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_READ, "not READ");
	CHECK(buf == NULL && pw == NULL && file == NULL, "outputs not cleared");
	clean();
}

static void test_short_read(void)
{
	put_str("password\r\nfile");
	sim_short_reads = 1;
	char *buf = (char*)1, *pw = (char*)1, *file = (char*)1;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_READ, "not READ");
	CHECK(buf == NULL && pw == NULL && file == NULL, "outputs not cleared");
	clean();
}

static void test_alloc_failure(void)
{
	put_str("pw\r\nfile");
	sim_alloc_fail_at = 1;
	char *buf = (char*)1, *pw = (char*)1, *file = (char*)1;
	CHECK(ReadPasswordFile(&buf, &pw, &file) == PASSWORD_FILE_NOMEM, "not NOMEM");
	CHECK(buf == NULL && pw == NULL && file == NULL, "outputs not cleared");
	clean();
}

static const struct hc_test tests[] = {
	{ "missing", test_missing, false },
	{ "empty", test_empty, false },
	{ "one_line", test_one_line, false },
	{ "two_lines", test_two_lines, false },
	{ "three_lines_and_blank_lines", test_three_lines_and_blank_lines, false },
	{ "only_newlines", test_only_newlines, false },
	{ "sizes_multiple_of_32", test_sizes_multiple_of_32, false },
	{ "stats_fail", test_stats_fail, false },
	{ "short_read", test_short_read, false },
	{ "alloc_failure", test_alloc_failure, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, setup);
}
