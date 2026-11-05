/* stand-in for libogc's ogc/lwp_watchdog.h: the time base HTTP_Parser's deadline reads. The test's fake clock answers */
#ifndef STUB_LWP_WATCHDOG_H
#define STUB_LWP_WATCHDOG_H
#include "gctypes.h"

#define ticks_to_millisecs(ticks) ((u32)((u64)(ticks) / 60750))
#ifdef __cplusplus
extern "C" {
#endif
u64 gettime(void);
#ifdef __cplusplus
}
#endif
#endif
