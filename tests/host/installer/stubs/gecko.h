/* stand-in for src/Shared/gecko.h: USB Gecko logging goes nowhere */
#pragma once

static inline void gprintf(const char* str, ...)
{
	(void)str;
}
