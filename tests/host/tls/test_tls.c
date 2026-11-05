/*
 * Host tests for the TLS layer's pure parts (src/priiloader/tls): the verify-flag to error mapping, the embedded
 * roots, and certificate chain verification against them with the same mbedTLS configuration as the Wii build.
 * tls_verify.c and tls_roots.c are built unchanged. Run as: test_tls DIR_WITH_CHAINS
 */
#define HOSTCHECK_MAIN
#include "hostcheck.h"
#include <stdint.h>
#include <stdlib.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/error.h>
#include "tls.h"

static const char* chain_dir;

mbedtls_ms_time_t mbedtls_ms_time(void)
{
	return 0;
}

#define NOW_OK 1793811422LL  /* 2026-11-04 */

static void test_flag_mapping(void)
{
	CHECK(tls_error_from_verify_flags(0, NOW_OK) == TLS_OK, "no flags");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_EXPIRED, NOW_OK) == TLS_ERR_CERT_EXPIRED, "expired, sane clock");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_FUTURE, NOW_OK) == TLS_ERR_CERT_NOT_YET_VALID, "future, sane clock");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE, NOW_OK) == TLS_ERR_CERT_NOT_YET_VALID,
		"both time flags");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_FUTURE, 946684800LL) == TLS_ERR_CLOCK, "future, RTC at 2000");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_FUTURE, 0) == TLS_ERR_CLOCK, "future, RTC at 1970");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_EXPIRED, TLS_CLOCK_MAX) == TLS_ERR_CLOCK, "expired, RTC far ahead");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_EXPIRED, TLS_CLOCK_MIN) == TLS_ERR_CERT_EXPIRED, "expired at the lower bound");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_EXPIRED, TLS_CLOCK_MIN - 1) == TLS_ERR_CLOCK, "expired just below it");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_NOT_TRUSTED, NOW_OK) == TLS_ERR_CERT_UNTRUSTED, "untrusted");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_NOT_TRUSTED | MBEDTLS_X509_BADCERT_EXPIRED, 0) == TLS_ERR_CERT_UNTRUSTED,
		"untrusted beats a wrong clock");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_CN_MISMATCH, NOW_OK) == TLS_ERR_CERT_HOSTNAME, "hostname");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_CN_MISMATCH | MBEDTLS_X509_BADCERT_NOT_TRUSTED, NOW_OK) == TLS_ERR_CERT_HOSTNAME,
		"hostname beats untrusted");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_BAD_KEY, NOW_OK) == TLS_ERR_CERT_OTHER, "weak key");
	CHECK(tls_error_from_verify_flags(MBEDTLS_X509_BADCERT_REVOKED, NOW_OK) == TLS_ERR_CERT_OTHER, "revoked");
	CHECK(tls_error_from_verify_flags(0xFFFFFFFFu, NOW_OK) == TLS_ERR_CERT_HOSTNAME, "everything at once");
}

static void test_error_texts(void)
{
	for (int e = 0; e >= TLS_ERR_PROTOCOL; e--)
	{
		const char* s = tls_strerror(e);
		CHECK(s != NULL && s[0] != '\0', "text for %d", e);
		CHECK(strcmp(s, "unknown TLS error") != 0, "error %d has no text", e);
	}
	CHECK(strcmp(tls_strerror(-999), "unknown TLS error") == 0, "unknown value");
}

static void test_roots_parse(void)
{
	mbedtls_x509_crt roots;
	mbedtls_x509_crt_init(&roots);
	CHECK(tls_root_pem_size == strlen(tls_root_pem) + 1, "size covers the NUL");
	int ret = mbedtls_x509_crt_parse(&roots, (const unsigned char*)tls_root_pem, tls_root_pem_size);
	CHECK(ret == 0, "parse returned %d (number of failed roots when positive)", ret);

	int count = 0;
	for (const mbedtls_x509_crt* c = &roots; c != NULL && c->version != 0; c = c->next)
	{
		count++;
		CHECK(c->private_ca_istrue, "root %d is not a CA", count);
		CHECK(c->issuer_raw.len == c->subject_raw.len && memcmp(c->issuer_raw.p, c->subject_raw.p, c->subject_raw.len) == 0,
			"root %d is not self-signed", count);
		CHECK(mbedtls_x509_time_is_future(&c->valid_from) == 0, "root %d is not valid yet on this PC", count);
		CHECK(mbedtls_x509_time_is_past(&c->valid_to) == 0, "root %d has expired", count);
	}
	CHECK(count == 6, "expected 6 roots, found %d", count);
	mbedtls_x509_crt_free(&roots);
}

//verify chains captured from the real servers (2026-11-04) with the real roots: only the dates may be wrong
static void verify_chain(const char* file, const char* host, uint32_t expect_cn_mismatch)
{
	mbedtls_x509_crt roots, chain;
	mbedtls_x509_crt_init(&roots);
	mbedtls_x509_crt_init(&chain);
	CHECK(mbedtls_x509_crt_parse(&roots, (const unsigned char*)tls_root_pem, tls_root_pem_size) == 0, "roots");

	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", chain_dir, file);
	FILE* f = fopen(path, "rb");
	CHECK(f != NULL, "cannot open %s", path);
	static unsigned char buf[16384];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	int ret = mbedtls_x509_crt_parse(&chain, buf, n + 1);
	CHECK(ret == 0, "%s: parse %d", file, ret);

	uint32_t flags = 0;
	ret = mbedtls_x509_crt_verify(&chain, &roots, NULL, host, &flags, NULL, NULL);
	uint32_t other = flags & ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
	if (expect_cn_mismatch)
	{
		CHECK(flags & MBEDTLS_X509_BADCERT_CN_MISMATCH, "%s as %s: no mismatch (flags 0x%x)", file, host, (unsigned)flags);
		CHECK((other & ~(uint32_t)MBEDTLS_X509_BADCERT_CN_MISMATCH) == 0, "%s as %s: more than a mismatch (0x%x)", file, host, (unsigned)flags);
	}
	else
		CHECK(other == 0, "%s as %s: verification flags 0x%x (ret -0x%04x)", file, host, (unsigned)flags, (unsigned)-ret);

	//without the roots nothing is trusted
	mbedtls_x509_crt none;
	mbedtls_x509_crt_init(&none);
	flags = 0;
	mbedtls_x509_crt_verify(&chain, &none, NULL, host, &flags, NULL, NULL);
	CHECK(flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED, "%s: trusted without roots (0x%x)", file, (unsigned)flags);
	CHECK(tls_error_from_verify_flags(flags, NOW_OK) == (expect_cn_mismatch ? TLS_ERR_CERT_HOSTNAME : TLS_ERR_CERT_UNTRUSTED),
		"%s: mapping %d", file, tls_error_from_verify_flags(flags, NOW_OK));

	mbedtls_x509_crt_free(&none);
	mbedtls_x509_crt_free(&chain);
	mbedtls_x509_crt_free(&roots);
}

static void test_chain_api_github_com(void)
{
	verify_chain("api.github.com.pem", "api.github.com", 0);
}

static void test_chain_github_com_name(void)
{
	verify_chain("api.github.com.pem", "github.com", 0);   //the SAN lists the bare name too
	verify_chain("api.github.com.pem", "github.com.evil.example", 1);
}

static void test_chain_wrong_host(void)
{
	verify_chain("api.github.com.pem", "evil.example.com", 1);
}

static void test_chain_lets_encrypt(void)
{
	//the leaf is for *.github.io, and its SAN also lists *.githubusercontent.com
	verify_chain("objects.githubusercontent.com.pem", "objects.githubusercontent.com", 0);
	verify_chain("objects.githubusercontent.com.pem", "release-assets.githubusercontent.com", 0);
	verify_chain("objects.githubusercontent.com.pem", "anything.github.io", 0);
	verify_chain("objects.githubusercontent.com.pem", "a.b.githubusercontent.com", 1);   //a wildcard covers one label
}

static const struct hc_test tests[] = {
	{ "flag_mapping", test_flag_mapping, false },
	{ "error_texts", test_error_texts, false },
	{ "roots_parse", test_roots_parse, false },
	{ "chain_api_github_com", test_chain_api_github_com, false },
	{ "chain_github_com_name", test_chain_github_com_name, false },
	{ "chain_wrong_host", test_chain_wrong_host, false },
	{ "chain_lets_encrypt", test_chain_lets_encrypt, false },
};

int main(int argc, char** argv)
{
	chain_dir = argc > 1 ? argv[1] : ".";
	return hc_main(tests, HC_COUNT(tests), argc > 1 ? 1 : argc, argv, NULL);
}
