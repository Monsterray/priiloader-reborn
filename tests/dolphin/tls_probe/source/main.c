/*
 * tls_probe: exercises Priiloader's TLS layer (src/priiloader/tls) in Dolphin or on a Wii.
 *
 *   1. GET https://api.github.com/repos/Monsterray/priiloader-reborn/releases?per_page=5 (must succeed)
 *   2. connections to hosts with a bad certificate (must FAIL verification, each with its own error)
 *
 * Every line the test is judged by starts with "TLSPROBE:" and goes to the screen and to the debug log
 * (SYS_Report, which Dolphin writes to its log). The last line is "TLSPROBE: RESULT PASS" or "... FAIL".
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <network.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#ifdef PROBE_NETLOG
#include "hbc_netlog.h"
#endif
#include "tls.h"
#include "tls_internal.h"
#include <poll.h>
#include "mbedtls/ecp.h"

#define FIRST_BYTES 300

static void* xfb = NULL;
static GXRModeObj* rmode = NULL;

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

static void say(const char* fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	printf("%s\n", line);
	SYS_Report("%s\n", line);
}

static void video_init(void)
{
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
}

//timing of each network step on its own, outside the TLS layer: name lookup, a non-blocking connect polled in 250 ms
//steps (each poll's result is shown), a blocking connect
static void net_timing(const char* host)
{
	u64 t0 = gettime();
	struct hostent* he = net_gethostbyname(host);
	u64 t1 = gettime();
	if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
	{
		say("TLSPROBE: timing %s: lookup failed after %u ms", host, (unsigned)ticks_to_millisecs(t1 - t0));
		return;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_len = 8;
	addr.sin_port = htons(443);
	memcpy(&addr.sin_addr, he->h_addr_list[0], 4);
	u32 a = addr.sin_addr.s_addr;
	say("TLSPROBE: timing %s: lookup %u ms -> %u.%u.%u.%u", host, (unsigned)ticks_to_millisecs(t1 - t0),
		(unsigned)(a >> 24) & 0xff, (unsigned)(a >> 16) & 0xff, (unsigned)(a >> 8) & 0xff, (unsigned)a & 0xff);

	s32 sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	s32 f = net_fcntl(sock, 4 /* F_SETFL */, 0x04 /* IOS O_NONBLOCK */);
	t0 = gettime();
	s32 r = net_connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	t1 = gettime();
	say("TLSPROBE: timing %s: non-blocking (fcntl %d) connect returned %d after %u ms", host, (int)f, (int)r,
		(unsigned)ticks_to_millisecs(t1 - t0));
	for (int i = 0; i < 40; i++)
	{
		struct pollsd sd;
		sd.socket = sock;
		sd.events = POLLOUT;
		sd.revents = 0;
		u64 p0 = gettime();
		s32 pr = net_poll(&sd, 1, 250);
		u64 p1 = gettime();
		if (pr != 0 || i < 2 || i % 8 == 0)
			say("TLSPROBE: timing %s: poll %d returned %d revents 0x%x after %u ms (%u ms since connect)", host, i, (int)pr,
				(unsigned)sd.revents, (unsigned)ticks_to_millisecs(p1 - p0), (unsigned)ticks_to_millisecs(p1 - t0));
		if (pr != 0)
			break;
	}
	s32 soerr = 0;
	socklen_t len = sizeof(soerr);
	s32 gr = net_getsockopt(sock, SOL_SOCKET, SO_ERROR, &soerr, &len);
	say("TLSPROBE: timing %s: SO_ERROR getsockopt %d value %d", host, (int)gr, (int)soerr);
	net_close(sock);

	sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	t0 = gettime();
	r = net_connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	t1 = gettime();
	say("TLSPROBE: timing %s: blocking connect returned %d after %u ms", host, (int)r, (unsigned)ticks_to_millisecs(t1 - t0));
	net_close(sock);
}

static int probe_rng(void* ctx, unsigned char* out, size_t len)
{
	u32* s = (u32*)ctx;
	for (size_t i = 0; i < len; i++)
	{
		*s ^= *s << 13;
		*s ^= *s >> 17;
		*s ^= *s << 5;
		out[i] = (unsigned char)*s;
	}
	return 0;
}

//the CPU cost of the handshake's largest steps: one P-256 and one P-384 scalar multiplication
static void crypto_timing(void)
{
	static const mbedtls_ecp_group_id ids[] = { MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_SECP384R1 };
	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
	{
		mbedtls_ecp_group grp;
		mbedtls_mpi d;
		mbedtls_ecp_point q;
		u32 seed = 0x12345678u;
		mbedtls_ecp_group_init(&grp);
		mbedtls_mpi_init(&d);
		mbedtls_ecp_point_init(&q);
		u64 t0 = gettime();
		int r = mbedtls_ecp_group_load(&grp, ids[i]);
		if (r == 0)
			r = mbedtls_ecp_gen_keypair(&grp, &d, &q, probe_rng, &seed);
		u64 t1 = gettime();
		say("TLSPROBE: timing crypto: %s key pair (one scalar multiplication) %u ms, result %d",
			i == 0 ? "P-256" : "P-384", (unsigned)ticks_to_millisecs(t1 - t0), r);
		mbedtls_ecp_point_free(&q);
		mbedtls_mpi_free(&d);
		mbedtls_ecp_group_free(&grp);
	}
}

//GET path on host over TLS. returns 0 when the reply was read, else the TLS error.
static int https_get(const char* host, const char* path, int* status_ok)
{
	tls_conn* c = NULL;
	int r = tls_open(&c, host, 443, 20000);
	if (r < 0)
	{
		say("TLSPROBE: %s open failed: %d (%s) mbedtls=%d verify_flags=0x%x", host, r, tls_strerror(r),
			tls_last_mbedtls_error(), (unsigned)tls_last_verify_flags());
		return r;
	}

	char req[512];
	int n = snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: priiloader-reborn-tls-probe\r\nAccept: application/vnd.github+json\r\nConnection: close\r\n\r\n",
		path, host);
	r = tls_write(c, req, (size_t)n);
	if (r < 0)
	{
		say("TLSPROBE: %s write failed: %d (%s)", host, r, tls_strerror(r));
		tls_close(c);
		return r;
	}

	enum { BUFSZ = 4096 };
	char* buf = (char*)malloc(BUFSZ + 1);
	size_t total = 0;
	while (total < BUFSZ)
	{
		r = tls_read(c, buf + total, BUFSZ - total);
		if (r <= 0)
			break;
		total += (size_t)r;
		if (total >= FIRST_BYTES + 400)
			break;
	}
	buf[total] = '\0';
	say("TLSPROBE: %s read %u bytes, last result %d (%s)", host, (unsigned)total, r, r < 0 ? tls_strerror(r) : "ok");
	tls_close(c);

	//the status line
	char* eol = strstr(buf, "\r\n");
	if (eol != NULL)
		*eol = '\0';
	say("TLSPROBE: %s status line: %s", host, buf);
	if (strncmp(buf, "HTTP/1.1 200", 12) == 0 || strncmp(buf, "HTTP/1.0 200", 12) == 0)
		*status_ok = 1;

	//the first bytes of the body
	char* body = (eol != NULL) ? strstr(eol + 2, "\r\n\r\n") : NULL;
	if (body != NULL)
	{
		body += 4;
		size_t len = strlen(body);
		if (len > FIRST_BYTES)
			body[FIRST_BYTES] = '\0';
		for (char* p = body; *p; p++)
			if (*p == '\n' || *p == '\r')
				*p = ' ';
		say("TLSPROBE: %s body begins: %s", host, body);
	}
	free(buf);
	return 0;
}

//a host whose certificate is bad: the open must fail with want (a TLS_ERR_*), or any certificate error when want is 0.
static int expect_cert_failure(const char* host, int want)
{
	tls_conn* c = NULL;
	int r = tls_open(&c, host, 443, 20000);
	if (r >= 0)
	{
		say("TLSPROBE: %s CONNECTED - verification did not fail: FAIL", host);
		tls_close(c);
		return 0;
	}
	say("TLSPROBE: %s refused: %d (%s) mbedtls=%d verify_flags=0x%x", host, r, tls_strerror(r),
		tls_last_mbedtls_error(), (unsigned)tls_last_verify_flags());
	int is_cert = (r <= TLS_ERR_CERT_UNTRUSTED && r >= TLS_ERR_CLOCK);
	int ok = want ? (r == want) : is_cert;
	say("TLSPROBE: %s negative test %s", host, ok ? "PASS (certificate rejected)" : "FAIL (wrong error)");
	return ok;
}

int main(void)
{
	int pass = 1;

	video_init();
#ifdef PROBE_NETLOG
	hbc_netlog_init();
#endif
	SYS_STDIO_Report(false);
	say("TLSPROBE: start");

	ISFS_Initialize();

	s32 ret;
	while ((ret = net_init()) == -EAGAIN)
		;
	if (ret < 0)
	{
		say("TLSPROBE: net_init failed: %d", (int)ret);
		say("TLSPROBE: RESULT FAIL");
		return 1;
	}
	u32 ip = net_gethostip();
	say("TLSPROBE: network up, ip %u.%u.%u.%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
	time_t now = time(NULL);
	char tbuf[64];
	strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	say("TLSPROBE: clock %s UTC (%lld)", tbuf, (long long)now);

	net_timing("api.github.com");
	net_timing("github.com");
	crypto_timing();

	u64 t0 = gettime();
	int status_ok = 0;
	int r = https_get("api.github.com", "/repos/Monsterray/priiloader-reborn/releases?per_page=5", &status_ok);
	u64 t1 = gettime();
	say("TLSPROBE: api.github.com took %u ms in total (handshake included)", (unsigned)ticks_to_millisecs(t1 - t0));
	if (r != 0 || !status_ok)
	{
		say("TLSPROBE: api.github.com FAIL (r=%d status_ok=%d)", r, status_ok);
		pass = 0;
	}
	else
		say("TLSPROBE: api.github.com PASS");

	const tls_entropy_stats* st = tls_entropy_get_stats();
	say("TLSPROBE: entropy: gathers=%u samples=%u distinct_low_bytes=%u isfs_reads_ok=%u es_sign=%d mac=%d device_id=%d",
		(unsigned)st->gathers, (unsigned)st->jitter_samples, (unsigned)st->distinct_deltas, (unsigned)st->isfs_reads_ok,
		(int)st->es_sign_result, (int)st->mac_result, (int)st->device_id_result);

	//a second connection reuses the generator and the roots
	t0 = gettime();
	status_ok = 0;
	r = https_get("github.com", "/Monsterray/priiloader-reborn", &status_ok);
	t1 = gettime();
	say("TLSPROBE: github.com took %u ms (r=%d status_ok=%d)", (unsigned)ticks_to_millisecs(t1 - t0), r, status_ok);
	if (r != 0)
		pass = 0;

	//negative tests: each must fail, and with the error that names the problem
	pass &= expect_cert_failure("expired.badssl.com", 0);
	pass &= expect_cert_failure("wrong.host.badssl.com", TLS_ERR_CERT_HOSTNAME);
	pass &= expect_cert_failure("self-signed.badssl.com", TLS_ERR_CERT_UNTRUSTED);
	pass &= expect_cert_failure("untrusted-root.badssl.com", TLS_ERR_CERT_UNTRUSTED);

	say("TLSPROBE: RESULT %s", pass ? "PASS" : "FAIL");
	tls_shutdown();
#ifdef PROBE_NETLOG
	hbc_netlog_close();
#endif
	net_deinit();
	return pass ? 0 : 1;
}
