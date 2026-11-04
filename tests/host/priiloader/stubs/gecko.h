/* stand-in for Shared/gecko.h: gprintf goes nowhere, but its arguments are checked like printf's */
#ifndef STUB_GECKO_H
#define STUB_GECKO_H
#include "gctypes.h"
#ifdef __cplusplus
extern "C" {
#endif
#define gdprintf(...)
void gprintf(const char* str, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif
#endif
