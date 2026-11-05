/*
 * tls.c: an HTTPS client connection on libogc's net_* and mbedTLS (TLS 1.2). See tls.h.
 */
#include <gccore.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include "tls.h"
#include "tls_internal.h"

#define IOS_O_NONBLOCK 0x04
#define FCNTL_SETFL    4     /* F_SETFL */

struct tls_conn
{
	s32 sock;
	uint32_t timeout_ms;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
};

//state shared by every connection: seeding the generator and parsing the roots are the slow parts
static struct
{
	int ready;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_x509_crt roots;
} shared;

static int last_mbedtls_error;
static uint32_t last_verify_flags;

int tls_last_mbedtls_error(void)
{
	return last_mbedtls_error;
}

uint32_t tls_last_verify_flags(void)
{
	return last_verify_flags;
}

static int shared_init(void)
{
	if (shared.ready)
		return TLS_OK;

	mbedtls_entropy_init(&shared.entropy);
	mbedtls_ctr_drbg_init(&shared.drbg);
	mbedtls_x509_crt_init(&shared.roots);

	int ret = tls_entropy_register(&shared.entropy);
	if (ret == 0)
		ret = mbedtls_ctr_drbg_seed(&shared.drbg, mbedtls_entropy_func, &shared.entropy, (const unsigned char*)"priiloader", 10);
	if (ret != 0)
	{
		last_mbedtls_error = ret;
		tls_shutdown();
		return TLS_ERR_ENTROPY;
	}

	ret = mbedtls_x509_crt_parse(&shared.roots, (const unsigned char*)tls_root_pem, tls_root_pem_size);
	if (ret != 0)
	{
		//a positive value is the number of roots that failed to parse: the build is broken, do not trust a partial set
		last_mbedtls_error = ret;
		tls_shutdown();
		return TLS_ERR_CERT_OTHER;
	}

	shared.ready = 1;
	return TLS_OK;
}

void tls_shutdown(void)
{
	mbedtls_x509_crt_free(&shared.roots);
	mbedtls_ctr_drbg_free(&shared.drbg);
	mbedtls_entropy_free(&shared.entropy);
	shared.ready = 0;
}

//one poll of at most this long. a real Wii's IOS answers a poll for POLLOUT only when its timeout ends, even when the
//socket was ready at once (a connect polled with 250 ms took 250 ms; Dolphin answers at once). with one poll for the
//whole timeout every TLS connect took 20 s, and api.github.com closed the idle connection before the ClientHello.
#define POLL_SLICE_MS 10

//wait for the socket: events is POLLIN or POLLOUT. returns 1 ready, 0 timed out, negative on a socket error.
static int wait_socket(s32 sock, u32 events, uint32_t timeout_ms)
{
	const u64 start = gettime();
	for (;;)
	{
		u32 elapsed = ticks_to_millisecs(diff_ticks(start, gettime()));
		if (elapsed >= timeout_ms)
			return 0;
		u32 slice = timeout_ms - elapsed;
		if (slice > POLL_SLICE_MS)
			slice = POLL_SLICE_MS;
		struct pollsd sd;
		sd.socket = sock;
		sd.events = events;
		sd.revents = 0;
		s32 r = net_poll(&sd, 1, (s32)slice);
		if (r < 0)
			return -1;
		if (r == 0)
			continue;
		if (sd.revents & (POLLERR | POLLNVAL))
			return -1;
		return 1;
	}
}

static int bio_send(void* ctx, const unsigned char* buf, size_t len)
{
	tls_conn* c = (tls_conn*)ctx;
	for (;;)
	{
		s32 r = net_send(c->sock, buf, (s32)len, 0);
		if (r >= 0)
			return r;
		if (r != -EAGAIN)
			return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
		int w = wait_socket(c->sock, POLLOUT, c->timeout_ms);
		if (w == 0)
			return MBEDTLS_ERR_SSL_TIMEOUT;
		if (w < 0)
			return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
	}
}

//timeout 0 means "ours": mbedTLS passes the conf's read timeout, which we always set
static int bio_recv_timeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout)
{
	tls_conn* c = (tls_conn*)ctx;
	uint32_t wait = timeout ? timeout : c->timeout_ms;
	for (;;)
	{
		s32 r = net_recv(c->sock, buf, (s32)len, 0);
		if (r > 0)
			return r;
		if (r == 0)
			return MBEDTLS_ERR_SSL_CONN_EOF;
		if (r != -EAGAIN)
			return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
		int w = wait_socket(c->sock, POLLIN, wait);
		if (w == 0)
			return MBEDTLS_ERR_SSL_TIMEOUT;
		if (w < 0)
			return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
	}
}

//connect a TCP socket to host:port with a timeout. returns the socket, or a negative TLS_ERR_*.
static s32 tcp_connect(const char* host, uint16_t port, uint32_t timeout_ms)
{
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_len = 8;
	addr.sin_port = htons(port);

	if (net_gethostip() == 0)
		return TLS_ERR_NO_NETWORK;

	u32 ip = inet_addr(host);
	if (ip != 0 && ip != 0xFFFFFFFFu)
	{
		addr.sin_addr.s_addr = ip;
	}
	else
	{
		struct hostent* he = net_gethostbyname(host);
		if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL || he->h_length != 4)
			return TLS_ERR_DNS;
		memcpy(&addr.sin_addr, he->h_addr_list[0], 4);
	}

	s32 sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (sock < 0)
		return TLS_ERR_NO_NETWORK;
	if (net_fcntl(sock, FCNTL_SETFL, IOS_O_NONBLOCK) < 0)
	{
		net_close(sock);
		return TLS_ERR_IO;
	}

	s32 r = net_connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (r < 0 && r != -EISCONN)
	{
		if (r == -ENETUNREACH || r == -EHOSTUNREACH)
		{
			net_close(sock);
			return TLS_ERR_NO_NETWORK;
		}
		//a non-blocking connect answers "in progress" with an IOS error number: -119 in Dolphin, which is neither lwIP's
		//EINPROGRESS (115) nor anything else in libogc's headers. Whatever it is, the poll and SO_ERROR below decide.
		int w = wait_socket(sock, POLLOUT, timeout_ms);
		if (w <= 0)
		{
			net_close(sock);
			return w == 0 ? TLS_ERR_CONNECT : TLS_ERR_IO;
		}
		s32 soerr = 0;
		socklen_t len = sizeof(soerr);
		//Dolphin's IOS answers SO_ERROR with -22; then the poll (POLLERR is a failure above) is all we have, and a
		//connection that is not up fails in the handshake with TLS_ERR_IO instead
		if (net_getsockopt(sock, SOL_SOCKET, SO_ERROR, &soerr, &len) >= 0 && soerr != 0)
		{
			net_close(sock);
			return TLS_ERR_CONNECT;
		}
	}
	return sock;
}

static void conn_free(tls_conn* c)
{
	if (c == NULL)
		return;
	mbedtls_ssl_free(&c->ssl);
	mbedtls_ssl_config_free(&c->conf);
	if (c->sock >= 0)
		net_close(c->sock);
	memset(c, 0, sizeof(*c));
	free(c);
}

static int map_handshake_error(tls_conn* c, int ret)
{
	last_mbedtls_error = ret;
	if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
	{
		last_verify_flags = mbedtls_ssl_get_verify_result(&c->ssl);
		int e = tls_error_from_verify_flags(last_verify_flags, (int64_t)time(NULL));
		return e == TLS_OK ? TLS_ERR_CERT_OTHER : e;
	}
	switch (ret)
	{
		case MBEDTLS_ERR_SSL_TIMEOUT:
			return TLS_ERR_TIMEOUT;
		case MBEDTLS_ERR_SSL_CONN_EOF:
		case MBEDTLS_ERR_SSL_INTERNAL_ERROR:
			return TLS_ERR_IO;
		case MBEDTLS_ERR_SSL_ALLOC_FAILED:
			return TLS_ERR_MEMORY;
		default:
			return TLS_ERR_HANDSHAKE;
	}
}

int tls_open(tls_conn** out, const char* host, uint16_t port, uint32_t timeout_ms)
{
	if (out == NULL)
		return TLS_ERR_ARG;
	*out = NULL;
	if (host == NULL || host[0] == '\0' || port == 0 || timeout_ms == 0)
		return TLS_ERR_ARG;
	last_mbedtls_error = 0;
	last_verify_flags = 0;

	int e = shared_init();
	if (e != TLS_OK)
		return e;

	s32 sock = tcp_connect(host, port, timeout_ms);
	if (sock < 0)
		return sock;

	tls_conn* c = (tls_conn*)calloc(1, sizeof(*c));
	if (c == NULL)
	{
		net_close(sock);
		return TLS_ERR_MEMORY;
	}
	c->sock = sock;
	c->timeout_ms = timeout_ms;
	mbedtls_ssl_init(&c->ssl);
	mbedtls_ssl_config_init(&c->conf);

	int ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0)
	{
		last_mbedtls_error = ret;
		conn_free(c);
		return TLS_ERR_MEMORY;
	}
	mbedtls_ssl_conf_min_tls_version(&c->conf, MBEDTLS_SSL_VERSION_TLS1_2);
	mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&c->conf, &shared.roots, NULL);
	mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &shared.drbg);
	mbedtls_ssl_conf_read_timeout(&c->conf, timeout_ms);

	ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
	if (ret == 0)
		ret = mbedtls_ssl_set_hostname(&c->ssl, host);   //SNI and the certificate's name check
	if (ret != 0)
	{
		last_mbedtls_error = ret;
		conn_free(c);
		return TLS_ERR_MEMORY;
	}
	mbedtls_ssl_set_bio(&c->ssl, c, bio_send, NULL, bio_recv_timeout);

	do
	{
		ret = mbedtls_ssl_handshake(&c->ssl);
	} while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

	if (ret != 0)
	{
		e = map_handshake_error(c, ret);
		conn_free(c);
		return e;
	}

	last_verify_flags = mbedtls_ssl_get_verify_result(&c->ssl);
	if (last_verify_flags != 0)
	{
		//cannot happen with VERIFY_REQUIRED, but never go on with a flagged certificate
		e = tls_error_from_verify_flags(last_verify_flags, (int64_t)time(NULL));
		conn_free(c);
		return e == TLS_OK ? TLS_ERR_CERT_OTHER : e;
	}

	*out = c;
	return TLS_OK;
}

int tls_write(tls_conn* c, const void* data, size_t len)
{
	if (c == NULL || (data == NULL && len > 0))
		return TLS_ERR_ARG;
	if (len > 0x7FFFFFFF)
		return TLS_ERR_ARG;

	size_t done = 0;
	while (done < len)
	{
		int r = mbedtls_ssl_write(&c->ssl, (const unsigned char*)data + done, len - done);
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		if (r < 0)
		{
			last_mbedtls_error = r;
			return r == MBEDTLS_ERR_SSL_TIMEOUT ? TLS_ERR_TIMEOUT : TLS_ERR_IO;
		}
		done += (size_t)r;
	}
	return (int)done;
}

int tls_read(tls_conn* c, void* data, size_t len)
{
	if (c == NULL || data == NULL || len == 0)
		return TLS_ERR_ARG;
	if (len > 0x7FFFFFFF)
		len = 0x7FFFFFFF;

	for (;;)
	{
		int r = mbedtls_ssl_read(&c->ssl, (unsigned char*)data, len);
		if (r > 0)
			return r;
		if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
			return 0;
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		last_mbedtls_error = r;
		switch (r)
		{
			case MBEDTLS_ERR_SSL_TIMEOUT:
				return TLS_ERR_TIMEOUT;
			case MBEDTLS_ERR_SSL_CONN_EOF:
				return TLS_ERR_TRUNCATED;
			case MBEDTLS_ERR_SSL_INVALID_RECORD:
			case MBEDTLS_ERR_SSL_BAD_INPUT_DATA:
			case MBEDTLS_ERR_SSL_INVALID_MAC:
				return TLS_ERR_PROTOCOL;
			default:
				return TLS_ERR_IO;
		}
	}
}

void tls_close(tls_conn* c)
{
	if (c == NULL)
		return;
	//best effort: a server that does not answer must not hold us up
	mbedtls_ssl_conf_read_timeout(&c->conf, 1);
	c->timeout_ms = 1000;
	mbedtls_ssl_close_notify(&c->ssl);
	conn_free(c);
}
