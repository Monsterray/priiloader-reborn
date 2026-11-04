/* stand-in for the one POSIX call the installer uses: sleep() */
#pragma once

static inline unsigned int sleep(unsigned int seconds)
{
	(void)seconds;
	return 0;
}
