/*
 * Priiloader's mbedTLS 3.6 configuration: an HTTPS client, TLS 1.2, for the
 * updater (GitHub). Selected with -DMBEDTLS_CONFIG_FILE. Everything not
 * listed here is off. No server side, no file system, no threads, no PSA, no
 * TLS 1.3 (it needs PSA and the extra size is not worth it: GitHub still
 * speaks TLS 1.2).
 */
#ifndef PRIILOADER_MBEDTLS_CONFIG_H
#define PRIILOADER_MBEDTLS_CONFIG_H

/* System */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE      /* certificate validity needs the clock */
#define MBEDTLS_PLATFORM_MS_TIME_ALT /* mbedtls_ms_time() is in tls_platform.c (libogc timebase) */
#define MBEDTLS_NO_PLATFORM_ENTROPY /* we provide our own source (tls_entropy.c) */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_ERROR_C

/* Hash, cipher, MAC */
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C            /* the roots and GitHub use SHA-384 signatures */
#define MBEDTLS_SHA512_C
#define MBEDTLS_MD_C
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C

/* Public key */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21           /* RSA-PSS handshake signatures */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_WINDOW_SIZE 2   /* RAM over speed */
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

/* ASN.1, OIDs, PEM, X.509 (parse and verify only) */
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_REMOVE_INFO

/* TLS 1.2 client */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384 /* GitHub sends full 16 KiB records */
#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048  /* we only send small requests */
#define MBEDTLS_SSL_CIPHERSUITES \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, \
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256, \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384

#endif /* PRIILOADER_MBEDTLS_CONFIG_H */
