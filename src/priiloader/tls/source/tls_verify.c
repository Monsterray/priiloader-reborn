/*
 * tls_verify.c: the mapping from mbedTLS's X.509 verification flags to our error codes.
 * Pure (no libogc), so a host test builds it unchanged.
 */
#include <mbedtls/x509.h>
#include "tls.h"

int tls_error_from_verify_flags(uint32_t flags, int64_t now)
{
	const uint32_t time_flags = MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE;
	uint32_t other = flags & ~time_flags;

	if (flags == 0)
		return TLS_OK;

	if (other == 0)
	{
		//verification failed only on validity dates: is the Wii's clock to blame?
		if (now < TLS_CLOCK_MIN || now >= TLS_CLOCK_MAX)
			return TLS_ERR_CLOCK;
		return (flags & MBEDTLS_X509_BADCERT_FUTURE) ? TLS_ERR_CERT_NOT_YET_VALID : TLS_ERR_CERT_EXPIRED;
	}

	//a wrong clock with an untrusted chain is still an untrusted chain: report the harder problem
	if (other & MBEDTLS_X509_BADCERT_CN_MISMATCH)
		return TLS_ERR_CERT_HOSTNAME;
	if (other & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
		return TLS_ERR_CERT_UNTRUSTED;
	return TLS_ERR_CERT_OTHER;
}

const char* tls_strerror(int err)
{
	switch (err)
	{
		case TLS_OK:                     return "ok";
		case TLS_ERR_ARG:                return "bad argument";
		case TLS_ERR_MEMORY:             return "out of memory";
		case TLS_ERR_NO_NETWORK:         return "no network connection";
		case TLS_ERR_DNS:                return "the host name did not resolve";
		case TLS_ERR_CONNECT:            return "could not connect to the server";
		case TLS_ERR_TIMEOUT:            return "the server did not answer in time";
		case TLS_ERR_IO:                 return "network error";
		case TLS_ERR_HANDSHAKE:          return "TLS handshake failed";
		case TLS_ERR_CERT_UNTRUSTED:     return "certificate not trusted";
		case TLS_ERR_CERT_EXPIRED:       return "certificate expired";
		case TLS_ERR_CERT_NOT_YET_VALID: return "certificate not valid yet";
		case TLS_ERR_CERT_HOSTNAME:      return "certificate is for another host";
		case TLS_ERR_CERT_OTHER:         return "certificate rejected";
		case TLS_ERR_CLOCK:              return "the Wii's clock is wrong (certificate dates do not fit)";
		case TLS_ERR_ENTROPY:            return "could not seed the random generator";
		case TLS_ERR_TRUNCATED:          return "connection closed without close_notify";
		case TLS_ERR_PROTOCOL:           return "TLS protocol error";
		default:                         return "unknown TLS error";
	}
}
