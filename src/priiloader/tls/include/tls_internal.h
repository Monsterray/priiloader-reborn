/*
 * tls_internal.h: what the TLS layer's own files share. Not for callers of tls.h.
 */
#ifndef PRIILOADER_TLS_INTERNAL_H
#define PRIILOADER_TLS_INTERNAL_H

#include <stdint.h>
#include <mbedtls/entropy.h>

#ifdef __cplusplus
extern "C" {
#endif

/* what the last entropy gather found (for the probe and for logs; none of it is secret) */
typedef struct tls_entropy_stats
{
	uint32_t gathers;          /* completed gathers */
	uint32_t jitter_samples;   /* time base samples hashed in the last gather */
	uint32_t distinct_deltas;  /* how many different low bytes of the sample deltas there were (max 256) */
	uint32_t isfs_reads_ok;    /* NAND reads that worked in the last gather */
	int32_t  es_sign_result;   /* ES_Sign (IOS's ECDSA signer, random nonce): 0 = used, negative = IOS error / not available */
	int32_t  mac_result;       /* net_get_mac_address(): 0 = used */
	int32_t  device_id_result; /* ES_GetDeviceID(): 0 = used */
} tls_entropy_stats;

/* Add our entropy source to ctx (MBEDTLS_NO_PLATFORM_ENTROPY leaves it with none). */
int tls_entropy_register(mbedtls_entropy_context* ctx);

const tls_entropy_stats* tls_entropy_get_stats(void);

#ifdef __cplusplus
}
#endif

#endif
