/*
 * tls: an HTTPS (TLS 1.2) client connection for Priiloader, on libogc's net_*
 * and mbedTLS. The caller has already done net_init() and, for a sane clock,
 * has the Wii's RTC (time()) set. Full certificate verification against the
 * embedded roots (tls_roots.c) and a hostname check are always on; there is no
 * way to turn them off.
 *
 *   tls_conn* c;
 *   int r = tls_open(&c, "api.github.com", 443, 15000);
 *   if (r < 0) { ... tls_strerror(r) ... }
 *   tls_write(c, request, len);                 // all of it, or a negative error
 *   n = tls_read(c, buf, sizeof(buf));          // > 0 bytes, 0 = closed, < 0 error
 *   tls_close(c);
 *
 * Single threaded: one connection at a time per caller, no locking.
 */
#ifndef PRIILOADER_TLS_H
#define PRIILOADER_TLS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tls_conn tls_conn;

/* Every error is negative. */
enum tls_error
{
	TLS_OK = 0,
	TLS_ERR_ARG                 = -1,   /* bad argument */
	TLS_ERR_MEMORY              = -2,   /* out of memory */
	TLS_ERR_NO_NETWORK          = -3,   /* no network: no IP address, socket() failed, unreachable */
	TLS_ERR_DNS                 = -4,   /* the host name did not resolve */
	TLS_ERR_CONNECT             = -5,   /* TCP connect refused or timed out */
	TLS_ERR_TIMEOUT             = -6,   /* no data within the timeout */
	TLS_ERR_IO                  = -7,   /* the socket failed */
	TLS_ERR_HANDSHAKE           = -8,   /* TLS handshake failed (not a certificate problem) */
	TLS_ERR_CERT_UNTRUSTED      = -9,   /* the chain does not end at an embedded root */
	TLS_ERR_CERT_EXPIRED        = -10,  /* a certificate in the chain has expired (the clock looks sane) */
	TLS_ERR_CERT_NOT_YET_VALID  = -11,  /* a certificate is not valid yet (the clock looks sane) */
	TLS_ERR_CERT_HOSTNAME       = -12,  /* the certificate is for another host name */
	TLS_ERR_CERT_OTHER          = -13,  /* any other verification failure (revoked, bad signature, key too weak...) */
	TLS_ERR_CLOCK               = -14,  /* verification failed only because of time, and the Wii's clock is not plausible */
	TLS_ERR_ENTROPY             = -15,  /* the random generator could not be seeded */
	TLS_ERR_TRUNCATED           = -16,  /* the peer closed the connection without a TLS close_notify */
	TLS_ERR_PROTOCOL            = -17   /* the peer broke the TLS protocol */
};

/*
 * The Wii's clock is plausible between these two instants (UTC): 2026-01-01
 * and 2050-01-01. Outside them, a certificate that "expired" or "is not valid
 * yet" is reported as TLS_ERR_CLOCK: the clock is the likelier culprit.
 */
#define TLS_CLOCK_MIN 1767225600
#define TLS_CLOCK_MAX 2524608000

/* Connect to host:port, handshake and verify. timeout_ms bounds every wait on the network
   (the connect, each read of the handshake and each tls_read/tls_write wait). On success *out is
   the connection (free it with tls_close()); on failure *out is NULL and the return is negative. */
int tls_open(tls_conn** out, const char* host, uint16_t port, uint32_t timeout_ms);

/* Write all len bytes. Returns len, or a negative error. */
int tls_write(tls_conn* c, const void* data, size_t len);

/* Read up to len bytes. Returns the count (> 0), 0 when the peer closed cleanly (close_notify), or a
   negative error (TLS_ERR_TIMEOUT when nothing came within the timeout; TLS_ERR_TRUNCATED when the
   connection ended without close_notify - an HTTP client that has all its Content-Length may ignore it). */
int tls_read(tls_conn* c, void* data, size_t len);

/* Send close_notify (best effort), close the socket, free the connection. NULL is allowed. */
void tls_close(tls_conn* c);

/* Free the shared state (random generator, parsed roots). Not needed at exit; tls_open() sets it up again. */
void tls_shutdown(void);

/* A short English text for a TLS_ERR_* value. */
const char* tls_strerror(int err);

/* Details of the last failure, for logs: mbedTLS's own return code, and the X.509 verification flags
   (MBEDTLS_X509_BADCERT_*) of the last handshake that reached verification. */
int tls_last_mbedtls_error(void);
uint32_t tls_last_verify_flags(void);

/* Pure helper (host-testable): turn X.509 verification flags into a TLS_ERR_* value. now is time(). */
int tls_error_from_verify_flags(uint32_t flags, int64_t now);

/* Number of embedded root certificates (tls_roots.c) and the PEM text of all of them. */
extern const char tls_root_pem[];
extern const size_t tls_root_pem_size;  /* includes the terminating NUL */

#ifdef __cplusplus
}
#endif

#endif
