/*
 * test_playlog_id.cpp -- host tests of src/priiloader/include/playlog_id.h, the 6 byte id of play_rec.dat.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdint.h>
#include <string.h>
#include <string>

#include "playlog_id.h"

/* what LaunchTitle did before: one byte, the low byte of the title id's lower half, in a std::string
 * whose c_str() Playlog_Update then read as 6 bytes */
static std::string OldId(uint32_t lower)
{
	std::string id;
	id.push_back((char)lower);
	return id;
}

static void test_haxx(void)
{
	char id[6];
	memset(id, 0x55, sizeof(id));
	BuildPlaylogId(0x48415858, id);
	CHECK(memcmp(id, "HAXX\0\0", 6) == 0, "id is %02X %02X %02X %02X %02X %02X",
		(unsigned char)id[0], (unsigned char)id[1], (unsigned char)id[2], (unsigned char)id[3], (unsigned char)id[4], (unsigned char)id[5]);
}

static void test_zero_and_high_bytes(void)
{
	char id[6];
	BuildPlaylogId(0, id);
	CHECK(memcmp(id, "\0\0\0\0\0\0", 6) == 0, "zero id");
	BuildPlaylogId(0xFF80017F, id);
	CHECK((unsigned char)id[0] == 0xFF && (unsigned char)id[1] == 0x80 && id[2] == 1 && id[3] == 0x7F, "high bytes sign-extended or lost");
	CHECK(id[4] == 0 && id[5] == 0, "padding");
}

static void test_old_code_kept_one_byte(void)
{
	/* documents the bug: the old id was the single character 'X', not the game id */
	std::string old = OldId(0x48415858);
	CHECK(old.size() == 1 && old[0] == 'X', "old id");
	char id[6];
	BuildPlaylogId(0x48415858, id);
	CHECK(strncmp(id, "HAXX", 4) == 0, "new id does not start with the game id");
	CHECK(old[0] == id[3] && id[0] != old[0], "the old id was only the last of the four characters");
}

static const struct hc_test tests[] = {
	{ "haxx", test_haxx, false },
	{ "zero_and_high_bytes", test_zero_and_high_bytes, false },
	{ "old_code_kept_one_byte", test_old_code_kept_one_byte, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, NULL);
}
