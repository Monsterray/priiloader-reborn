/*
 * tls_entropy.c: the random seed for mbedTLS's CTR_DRBG, on a console with no hardware RNG for the PPC.
 *
 * WHAT WE LOOKED FOR
 *   IOS has an RNG (IOSC, error code IOSC_INVALID_RNG), but no /dev/es or /dev/sha ioctl that returns random bytes
 *   to the PPC, and libogc 3.1.0 exports nothing for it. The one command that uses it is ES_Sign (/dev/es ioctlv
 *   0x30): it signs the data with the identified title's ECDSA key, and an ECDSA signature needs a fresh random
 *   nonce, so the 60-byte signature changes on every call. We call it and hash the result in. It needs the running
 *   title to be identified; where it is not, IOS answers with an error and we go on without it (es_sign_result in
 *   the stats says which). Dolphin's HLE implements it. It has NOT been run on a real Wii by this code yet, so
 *   nothing below depends on it: it is one more input, never the only one.
 *
 * WHAT WE MIX (all through SHA-256, chained with the previous output)
 *   - Time base jitter. 256 samples of gettime() deltas around real I/O: NAND reads through IOS (an IPC round trip
 *     whose latency depends on IOS's scheduling, the NAND controller and the other IOS tasks), VIDEO_WaitVSync() (the
 *     phase of the call against the vsync), and cache-missing memory walks. Each sample contributes its low bits.
 *   - Per console data: the device ID (ES_GetDeviceID), the Wi-Fi MAC address, and the first bytes of SYSCONF read
 *     from NAND (serial number, counter bias, per console settings).
 *   - The RTC (time()), the time base itself, and a few addresses (stack, heap) that vary with what ran before.
 *   - ES_Sign output, if IOS gives it.
 *
 * WHY WE BELIEVE IT IS ADEQUATE
 *   The jitter of an IPC call to IOS is the same kind of source as interrupt/IO timing jitter that operating systems
 *   used for /dev/random before hardware RNGs: tens of microseconds of variation measured at a 60 MHz time base is
 *   many bits per sample, even if only 1 or 2 bits of each are unpredictable to an attacker who cannot observe the
 *   console's internal timing. 256 samples at 1 bit each already exceed the 128 bits we need; 64 bytes of seed are
 *   gathered for each CTR_DRBG seeding (threshold 64) from two independent gathers, and the per console data makes
 *   two consoles that run the same code from the same state diverge. The result seeds a CTR_DRBG, and only the TLS
 *   client random, the ephemeral ECDHE key and the nonces come from it.
 *
 * ITS WEAKNESS
 *   There is no measured min-entropy: we have no physical noise source and no way to test one on a Wii here. The
 *   jitter is a sampled clock against a deterministic machine, so an attacker who could run code on the console
 *   at the same time, or who knows its exact boot state, could narrow it. Right after a boot, with nothing else
 *   running, the samples are more regular than later. The per console data (MAC, device ID) is not secret: anyone
 *   on the LAN can read the MAC. Our tolerance: the secrets protected are the TLS session of an update check, not
 *   a signing key; a break lets an on-path attacker read it, not forge it (certificate verification does not use
 *   the RNG). Improvements if this matters: seed from /dev/es ES_Sign on a title known to be identified; keep a
 *   seed file on the NAND or SD card, updated after each run.
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <network.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <mbedtls/sha256.h>
#include "tls_internal.h"

#define JITTER_ROUNDS     256
#define ISFS_EVERY        4      /* a NAND read every 4th sample */
#define VSYNC_EVERY       64     /* a vsync wait every 64th sample */
#define SYSCONF_PATH      "/shared2/sys/SYSCONF"
#define ES_IOCTL_SIGN     0x30
#define SOURCE_THRESHOLD  64     /* bytes of "entropy" the entropy context wants from us per seeding */

static tls_entropy_stats stats;
static unsigned char chain[32];   /* previous output, so the gathers are not independent restarts */
static uint32_t counter;
static unsigned char walk[8192];  /* a buffer to walk for cache-miss jitter */

static unsigned char sample_buf[64] __attribute__((aligned(32)));
static unsigned char es_data[32] __attribute__((aligned(32)));
static unsigned char es_sig[64] __attribute__((aligned(32)));      /* 60 used */
static unsigned char es_cert[0x180] __attribute__((aligned(32)));
static ioctlv es_vec[3] __attribute__((aligned(32)));

static void mix(mbedtls_sha256_context* h, const void* data, size_t len)
{
	mbedtls_sha256_update(h, (const unsigned char*)data, len);
}

static void mix_u64(mbedtls_sha256_context* h, uint64_t v)
{
	mix(h, &v, sizeof(v));
}

//read the first bytes of SYSCONF through IOS. returns the byte count or a negative error.
static int read_sysconf(void)
{
	s32 fd = ISFS_Open(SYSCONF_PATH, ISFS_OPEN_READ);
	if (fd < 0)
		return fd;
	s32 r = ISFS_Read(fd, sample_buf, sizeof(sample_buf));
	ISFS_Close(fd);
	return r;
}

//ES_Sign over some data: the signature has a random nonce. 0 on success.
static s32 es_sign(const unsigned char* seed, size_t seed_len, size_t* sig_len)
{
	s32 fd = IOS_Open("/dev/es", 0);
	if (fd < 0)
		return fd;

	memset(es_data, 0, sizeof(es_data));
	memcpy(es_data, seed, seed_len > sizeof(es_data) ? sizeof(es_data) : seed_len);
	memset(es_sig, 0, sizeof(es_sig));
	memset(es_cert, 0, sizeof(es_cert));
	es_vec[0].data = es_data;
	es_vec[0].len = sizeof(es_data);
	es_vec[1].data = es_sig;
	es_vec[1].len = 60;
	es_vec[2].data = es_cert;
	es_vec[2].len = sizeof(es_cert);
	DCFlushRange(es_data, sizeof(es_data));
	DCFlushRange(es_vec, sizeof(es_vec));
	DCFlushRange(es_sig, sizeof(es_sig));
	DCFlushRange(es_cert, sizeof(es_cert));

	s32 r = IOS_Ioctlv(fd, ES_IOCTL_SIGN, 1, 2, es_vec);
	IOS_Close(fd);
	DCInvalidateRange(es_sig, sizeof(es_sig));
	DCInvalidateRange(es_cert, sizeof(es_cert));
	*sig_len = 60;
	return r;
}

//one gather: 32 bytes into out.
static void gather(unsigned char out[32])
{
	mbedtls_sha256_context h;
	unsigned char seen[256];
	uint32_t distinct = 0, isfs_ok = 0, samples = 0;
	unsigned char mac[8] = {0};
	u32 device_id = 0;
	volatile unsigned char sink = 0;
	uint32_t pos = 0;
	size_t sig_len = 0;

	memset(seen, 0, sizeof(seen));
	mbedtls_sha256_init(&h);
	mbedtls_sha256_starts(&h, 0);

	mix(&h, chain, sizeof(chain));
	mix(&h, &counter, sizeof(counter));
	counter++;

	//the clock and a few addresses
	mix_u64(&h, gettime());
	mix_u64(&h, (uint64_t)time(NULL));
	void* heap_probe = malloc(1);
	mix_u64(&h, (uint64_t)(uintptr_t)heap_probe);
	free(heap_probe);
	mix_u64(&h, (uint64_t)(uintptr_t)&h);

	//per console data
	stats.device_id_result = ES_GetDeviceID(&device_id);
	if (stats.device_id_result == 0)
		mix(&h, &device_id, sizeof(device_id));
	stats.mac_result = net_get_mac_address(mac);
	if (stats.mac_result == 0)
		mix(&h, mac, sizeof(mac));

	//jitter around I/O
	for (uint32_t i = 0; i < JITTER_ROUNDS; i++)
	{
		uint64_t t0 = gettime();
		if (i % VSYNC_EVERY == VSYNC_EVERY - 1)
		{
			VIDEO_WaitVSync();
		}
		else if (i % ISFS_EVERY == 0)
		{
			memset(sample_buf, 0, sizeof(sample_buf));
			DCFlushRange(sample_buf, sizeof(sample_buf));
			int r = read_sysconf();
			if (r > 0)
			{
				isfs_ok++;
				DCInvalidateRange(sample_buf, sizeof(sample_buf));
				mix(&h, sample_buf, sizeof(sample_buf));
			}
		}
		else
		{
			//a data dependent walk over a buffer bigger than the L1: the loads miss in varying ways
			for (int k = 0; k < 16; k++)
			{
				pos = (pos * 1103515245u + 12345u + (uint32_t)(t0 & 0xff)) % sizeof(walk);
				walk[pos] = (unsigned char)(walk[pos] + (unsigned char)k);
				sink ^= walk[pos];
			}
		}
		uint64_t t1 = gettime();
		uint32_t delta = (uint32_t)(t1 - t0);
		mix(&h, &delta, sizeof(delta));
		mix(&h, &t1, sizeof(t1));
		unsigned char low = (unsigned char)delta;
		if (!seen[low])
		{
			seen[low] = 1;
			distinct++;
		}
		samples++;
	}
	mix(&h, (const void*)&sink, 1);

	//IOS's own random nonce, if it signs for us
	unsigned char so_far[32];
	mbedtls_sha256_context hc;
	mbedtls_sha256_init(&hc);
	mbedtls_sha256_clone(&hc, &h);
	mbedtls_sha256_finish(&hc, so_far);
	mbedtls_sha256_free(&hc);
	stats.es_sign_result = es_sign(so_far, sizeof(so_far), &sig_len);
	if (stats.es_sign_result == 0)
		mix(&h, es_sig, sig_len);
	mix_u64(&h, gettime());

	mbedtls_sha256_finish(&h, out);
	mbedtls_sha256_free(&h);
	memcpy(chain, out, sizeof(chain));

	stats.gathers++;
	stats.jitter_samples = samples;
	stats.distinct_deltas = distinct;
	stats.isfs_reads_ok = isfs_ok;
}

//mbedtls_entropy_f_source_ptr
static int poll_source(void* data, unsigned char* output, size_t len, size_t* olen)
{
	(void)data;
	unsigned char block[32];
	gather(block);
	size_t n = len < sizeof(block) ? len : sizeof(block);
	memcpy(output, block, n);
	*olen = n;
	memset(block, 0, sizeof(block));
	return 0;
}

int tls_entropy_register(mbedtls_entropy_context* ctx)
{
	//declared STRONG: with no platform entropy mbedTLS refuses to run without one strong source. See the weakness above.
	return mbedtls_entropy_add_source(ctx, poll_source, NULL, SOURCE_THRESHOLD, MBEDTLS_ENTROPY_SOURCE_STRONG);
}

const tls_entropy_stats* tls_entropy_get_stats(void)
{
	return &stats;
}
