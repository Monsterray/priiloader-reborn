/*

priiloader/preloader 0.30 - A tool which allows to change the default boot up sequence on the Wii console

Copyright (C) 2008-2019  crediar & DacoTaco

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <gccore.h>
#include <ogc/usbgecko.h>
#include <ogc/lwp_watchdog.h>
#include <debug.h>

#include "gecko.h"
#include "mount.h"
#include "gitrev.h"

static u8 GeckoFound = 0;
static u8 DumpDebug = 0;

//Lines logged before the SD card is mounted (version, boot state, magic word...) wait here, and SetDumpDebug writes
//them to prii.log first. Before, they were lost: the dump only starts once LoadSettings has read the setting.
//Once SetDumpDebug has run the buffer is off for good.
#define EARLY_LOG_SIZE 8192
static char _earlyLog[EARLY_LOG_SIZE];
static u32 _earlyLogLength = 0;
static bool _earlyLogOn = true;
//every line carries the milliseconds since priiloader's first line, to time each step of a boot
static u64 _firstLineTicks = 0;

bool IsUsbGeckoDetected()
{
	return GeckoFound;
}

void InitGDBDebug(void)
{
	DEBUG_Init(GDBSTUB_DEVICE_USB, 1);
	return;
}

void CheckForGecko(void)
{
	GeckoFound = usb_isgeckoalive(EXI_CHANNEL_1);
	if (GeckoFound)
		usb_flush(EXI_CHANNEL_1);
	return;
}

static void __write_str(const char* str, const u32 len)
{
	if(GeckoFound)
	{
		usb_sendbuffer( EXI_CHANNEL_1, str, len );
		usb_flush(EXI_CHANNEL_1);
	}
	
	if (DumpDebug > 0 && GetMountedFlags() > 0)
	{
		//opened and closed per line, so a hang or RESET loses nothing that was logged
		FILE* fd = fopen(BuildPath("/prii.log").c_str(), "ab");
		if(fd != NULL)
		{
			fwrite(str, 1, len, fd);
			fclose(fd);
		}
	}
	else if (_earlyLogOn && _earlyLogLength + len <= EARLY_LOG_SIZE)
	{
		memcpy(_earlyLog + _earlyLogLength, str, len);
		_earlyLogLength += len;
	}
}

void gprintf( const char *str, ... )
{
	if(!GeckoFound && !_earlyLogOn && (!DumpDebug || GetMountedFlags() <= 0))
		return;

	const u64 now = gettime();
	if (_firstLineTicks == 0)
		_firstLineTicks = now;
	const u32 sinceFirst = ticks_to_millisecs(diff_ticks(_firstLineTicks, now));

	//lets first compile the input
	char* inputBuffer = NULL;
	char* outputBuffer = NULL;
	u32 bufferLength = 0x100;
	u32 length;
	va_list args;

	// Current date/time based on current system, converted to tm struct for local timezone
	time_t LeTime = time(0);
	const struct tm* localtm = localtime(&LeTime);

	while(true)
	{
		char* tmp = static_cast<char*>(realloc(inputBuffer, bufferLength));
		if(!tmp)
		{
			const char* err = "Failed to allocate input prefix\r\n";
			__write_str(err, strlen(err));
			return;
		}

		inputBuffer = tmp;
		length = snprintf(inputBuffer, bufferLength, "%02d:%02d:%02d +%6ums : %s\r\n", localtm->tm_hour, localtm->tm_min, localtm->tm_sec, sinceFirst, str);
		if(length > 0 && length >= bufferLength)
		{
			bufferLength = length+1;
			continue;
		}

		break;
	}

	if(length <= 0)
	{
		const char* err = "Failed to prepare input prefix\r\n";
		__write_str(err, strlen(err));
		return;
	}

	//string prefix has been created, now lets plug in the args
	while(true)
	{
		char* tmp = static_cast<char*>(realloc(outputBuffer, bufferLength));
		if(!tmp)
		{
			const char* err = "Failed to allocate input prefix\r\n";
			__write_str(err, strlen(err));
			return;
		}

		outputBuffer = tmp;
		va_start(args, str);
		length = vsnprintf(outputBuffer, bufferLength, inputBuffer, args);
		va_end(args);
		if(length > 0 && length >= bufferLength)
		{
			bufferLength = length+1;
			continue;
		}
		
		break;
	}

	if(length <= 0)
	{
		const char* err = "Failed to prepare output prefix\r\n";
		__write_str(err, strlen(err));
		return;
	}
	
	__write_str(outputBuffer, length);
	free(inputBuffer);
	free(outputBuffer);
}

void SetDumpDebug( u8 value )
{
	DumpDebug = value > 0 ? 1 : 0;
	
	//don't dump or no devices mounted?
	if (!DumpDebug || GetMountedFlags() <= 0)
	{
		_earlyLogOn = false;
		_earlyLogLength = 0;
		return;
	}
	
	//create file, or re-open: a header for this boot, then what was logged before the mount
	FILE* fd = fopen(BuildPath("/prii.log").c_str(), "ab");
	if (fd != NULL)
	{
		char header[160];
		const int headerLength = snprintf(header, sizeof(header),
			"\r\n======== priiloader %s: boot log (%u bytes logged before the mount) ========\r\n", GIT_REV_STR,
			(unsigned)_earlyLogLength);
		if (headerLength > 0)
			fwrite(header, 1, (size_t)headerLength < sizeof(header) ? (size_t)headerLength : sizeof(header) - 1, fd);
		if (_earlyLogLength > 0)
			fwrite(_earlyLog, 1, _earlyLogLength, fd);
		if (_earlyLogLength > EARLY_LOG_SIZE - 256)
		{
			const char full[] = "(early log buffer full: some early lines are missing)\r\n";
			fwrite(full, 1, strlen(full), fd);
		}
		fclose(fd);
	}
	else
	{
		//we failed. fuck this shit
		DumpDebug = 0;
	}
	_earlyLogOn = false;
	_earlyLogLength = 0;

	return;
}