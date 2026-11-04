/*
 * sim_nand.h -- a simulated NAND (ISFS) and ES for the Installer host tests.
 *
 * Files live in memory. Every ISFS call is counted, and a failure can be injected:
 *  - at the Nth call (fail_at), once or from then on (sticky: a NAND that stays broken);
 *  - on every rename to one destination (fail_rename_dest);
 *  - a rename onto an existing file that fails (rename_no_replace) instead of replacing it;
 *  - as a silent or visible short read/write.
 * It also records closes of file descriptors that are not open, and descriptors left open.
 */
#ifndef SIM_NAND_H
#define SIM_NAND_H

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

struct SimFile
{
	std::vector<uint8_t> data;
	uint8_t attributes = 0;
	uint8_t ownerperm = 3;
	uint8_t groupperm = 3;
	uint8_t otherperm = 3;
};

struct SimFd
{
	bool open = false;
	std::string path;
	size_t pos = 0;
};

struct SimNand
{
	std::map<std::string, SimFile> files;
	std::vector<SimFd> fds;

	// fault injection
	int calls = 0;
	int fail_at = 0; // 1-based index of the call to fail, 0 = none
	bool sticky = false; // every call from fail_at on fails
	std::string fail_rename_dest; // renames to this path fail (the first fail_rename_dest_times of them)
	int fail_rename_dest_times = -1; // -1 = always
	bool rename_no_replace = false; // rename onto an existing file fails (-105); default: it replaces, as in Dolphin's IOS
	bool short_read = false; // ISFS_Read returns length - 1 and copies length - 1 bytes
	bool short_write = false; // ISFS_Write returns length - 1 and stores length - 1 bytes
	bool silent_short_write = false; // ISFS_Write reports length but stores length - 1 bytes

	// what the code under test did wrong
	int bad_closes = 0; // ISFS_Close of a descriptor that is not open
	int bad_paths = 0; // calls with a path IOS refuses: a component over 12 characters, a path over 63
	int guard_violations = 0; // writes behind a memalign() block, found when it is freed

	// stored TMD of the vWii nandloader title (0x100000200); empty = not installed
	std::vector<uint8_t> nandloader_tmd;

	void reset();
	int open_fds() const;
	bool exists(const std::string& path) const;
	const std::vector<uint8_t>* get(const std::string& path) const;
	void put(const std::string& path, const std::vector<uint8_t>& data);
};

extern SimNand g_nand;

// printf of the code under test goes here: silent unless g_verbose is set
extern bool g_verbose;
// the value reset() gives rename_no_replace: the whole suite runs in both modes
extern bool g_rename_no_replace_default;
int hc_quiet_printf(const char* fmt, ...);

#endif
