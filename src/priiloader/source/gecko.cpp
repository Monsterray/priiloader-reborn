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

//Lines logged while no SD card is mounted wait here: before LoadSettings has read the setting, while the mount
//thread is still mounting the card, and while a title launch has it unmounted. The next line written to a mounted
//card writes them first, after a header for the log. Turning the dump off drops them.
#define PENDING_LOG_SIZE 8192
static char _pendingLog[PENDING_LOG_SIZE];
static u32 _pendingLength = 0;
static bool _pendingOverflow = false;
static bool _pendingOn = true;
static bool _headerDue = true;
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

static void __write_pending(FILE* fd)
{
	if (_headerDue)
	{
		//the lines carry the time of day from the Wii's clock: the header adds its date, as the log spans many boots
		char date[16] = "?";
		const time_t now = time(0);
		const struct tm* localtm = localtime(&now);
		if (localtm != NULL)
			strftime(date, sizeof(date), "%Y-%m-%d", localtm);
		char header[160];
		const int length = snprintf(header, sizeof(header), "\r\n======== priiloader %s: log opened %s (Wii clock) ========\r\n", GIT_REV_STR, date);
		if (length > 0 && (size_t)length < sizeof(header))
			fwrite(header, 1, length, fd);
		_headerDue = false;
	}
	if (_pendingLength > 0)
		fwrite(_pendingLog, 1, _pendingLength, fd);
	if (_pendingOverflow)
	{
		const char full[] = "(log buffer was full while unmounted: some lines are missing)\r\n";
		fwrite(full, 1, strlen(full), fd);
	}
	_pendingLength = 0;
	_pendingOverflow = false;
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
			__write_pending(fd);
			fwrite(str, 1, len, fd);
			fclose(fd);
			return;
		}
	}

	if (!_pendingOn)
		return;
	if (_pendingLength + len <= PENDING_LOG_SIZE)
	{
		memcpy(_pendingLog + _pendingLength, str, len);
		_pendingLength += len;
	}
	else
		_pendingOverflow = true;
}

void gprintf( const char *str, ... )
{
	if(!GeckoFound && !_pendingOn && (!DumpDebug || GetMountedFlags() <= 0))
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
	if (!DumpDebug)
	{
		_pendingOn = false;
		_pendingLength = 0;
		_pendingOverflow = false;
		return;
	}

	//keep collecting until a card is mounted; write now if one is
	_pendingOn = true;
	_headerDue = true;
	if (GetMountedFlags() <= 0)
		return;

	FILE* fd = fopen(BuildPath("/prii.log").c_str(), "ab");
	if (fd != NULL)
	{
		__write_pending(fd);
		fclose(fd);
	}
}
