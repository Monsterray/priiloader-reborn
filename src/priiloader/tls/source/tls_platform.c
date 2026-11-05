/*
 * tls_platform.c: what mbedTLS needs from the platform (MBEDTLS_PLATFORM_MS_TIME_ALT).
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <mbedtls/platform_util.h>

/* milliseconds on the time base: monotonic, used by mbedTLS for timeouts and session expiry. */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
	return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}
