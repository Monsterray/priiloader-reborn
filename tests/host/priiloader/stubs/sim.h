/*
 * sim.h -- the simulated NAND (ISFS) and MEM2 heap of the priiloader host tests.
 *
 * The heap puts a guard after every block, so a write past the end is found when the block is
 * freed (and by AddressSanitizer too, when the test is built with it). Every file is a whole
 * in-memory buffer; ISFS_Read refuses an unaligned buffer like IOS does.
 */
#ifndef PRIILOADER_SIM_H
#define PRIILOADER_SIM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void sim_reset(void);
void sim_put(const char* path, const void* data, size_t len);
/* returns the file's data, NULL when there is no such file */
const uint8_t* sim_get(const char* path, size_t* len);

/* failure injection; sim_reset clears them */
extern int sim_fail_stats;		/* ISFS_GetFileStats fails */
/* when set, sim_fail_stats and sim_short_reads apply only to the files whose path contains this */
extern const char* sim_fail_stats_path;
extern int sim_short_reads;		/* ISFS_Read returns one byte less than asked */
extern int sim_alloc_fail_at;	/* the Nth (1 based) allocation from now fails */

extern int sim_live_allocs;		/* blocks allocated and not freed */
extern int sim_open_fds;		/* files opened and not closed */

#ifdef __cplusplus
}
#endif
#endif
