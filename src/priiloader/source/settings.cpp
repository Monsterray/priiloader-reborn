/*

priiloader/preloader 0.30 - A tool which allows to change the default boot up sequence on the Wii console

Copyright (C) 2008-2019  crediar

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
#include <unistd.h>
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <string.h>

#include "settings.h"
#include "error.h"
#include "gecko.h"
#include "mem2_manager.h"

Settings *settings=NULL;

//the settings of a new install; also what is used when the file cannot be read
static void SetDefaultSettings( void )
{
	memset( settings, 0, sizeof( Settings ) );
	settings->RCVersion = VERSION_RC;
	settings->version = VERSION_MERGED;
	settings->UseSystemMenuIOS = 1;
	settings->autoboot = AUTOBOOT_SYS;
	settings->BlackBackground = 1;
	settings->AutobootWait = AUTOBOOTWAIT_DEFAULT;
	settings->UpdateSource = UPDATESOURCE_DEFAULT;
}

//writes the default settings to a new file. returns 1 on success, a negative number on failure (error is set)
static s32 Create_Settings_File( void )
{
	if(settings == NULL)
	{
		return -99;
	}
	s32 fd = 0;
	ISFS_CreateFile("/title/00000001/00000002/data/loader.ini", 0, 3, 3, 3);
	//set a few default settings
	SetDefaultSettings();
	fd = ISFS_Open("/title/00000001/00000002/data/loader.ini", ISFS_OPEN_WRITE );
	if( fd < 0 )
	{
		error = ERROR_SETTING_OPEN;
		return fd;
	}
	s32 written = ISFS_Write( fd, settings, SETTINGS_FILE_SIZE );
	ISFS_Close( fd );
	if( written != (s32)SETTINGS_FILE_SIZE )
	{
		error = ERROR_SETTING_WRITE;
		return written < 0 ? written : -1;
	}
	return 1;
}

u32 SGetSetting( u32 s )
{
	if( settings == NULL )
		return 0;

	switch( s )
	{
		case SETTING_AUTBOOT:
			return settings->autoboot;
		case SETTING_RETURNTO:
			return settings->ReturnTo;
		case SETTING_SHUTDOWNTO:
			return settings->ShutdownTo;
		case SETTING_STOPDISC:
			return settings->StopDisc;
		case SETTING_LIDSLOTONERROR:
			return settings->LidSlotOnError;
		case SETTING_IGNORESHUTDOWNMODE:
			return settings->IgnoreShutDownMode;
		case SETTING_SYSTEMMENUIOS:
			return settings->SystemMenuIOS;
		case SETTING_USESYSTEMMENUIOS:
			return settings->UseSystemMenuIOS;
		case SETTING_BLACKBACKGROUND:
			return settings->BlackBackground;
		case SETTING_DUMPGECKOTEXT:
			return settings->DumpGeckoText;
		case SETTING_PASSCHECKPRII:
			return settings->PasscheckPriiloader;
		case SETTING_PASSCHECKMENU:
			return settings->PasscheckMenu;
		case SETTING_SHOWRCUPDATES:
			return settings->ShowRCUpdates;
		case SETTING_AUTOBOOTWAIT:
			return settings->AutobootWait;
		case SETTING_UPDATESOURCE:
			return settings->UpdateSource;
		default:
			return 0;
		break;
	}
}

//the time to look for a key before an autoboot decision, in ms. a value that is not one of the choices gives the default
u32 GetAutobootWaitMs( void )
{
	u32 choice = (settings == NULL) ? AUTOBOOTWAIT_DEFAULT : settings->AutobootWait;
	switch( choice )
	{
		case AUTOBOOTWAIT_0MS:
			return 0;
		case AUTOBOOTWAIT_100MS:
			return 100;
		case AUTOBOOTWAIT_250MS:
			return 250;
		case AUTOBOOTWAIT_500MS:
		default:
			return 500;
	}
}

//writes the settings to the file at the size they have now. the file was opened for reading, so it is opened again for writing.
//best effort: the settings in memory are right either way. returns true when it was written
static bool WriteSettingsFile( void )
{
	s32 fd = ISFS_Open("/title/00000001/00000002/data/loader.ini", ISFS_OPEN_RW );
	if( fd < 0 )
		return false;
	bool ok = ISFS_Seek( fd, 0, 0 ) >= 0 && ISFS_Write( fd, settings, SETTINGS_FILE_SIZE ) == (s32)SETTINGS_FILE_SIZE;
	ISFS_Close( fd );
	return ok;
}

LoadSettingsResult LoadSettings( void )
{
	if(settings == NULL)
	{
		//the settings still need to be aligned/allocated. so lets do that
		settings = static_cast<Settings*>(mem_align( 32, ALIGN32( sizeof( Settings ) ) ));
	}
	if(settings == NULL)
		return LOADSETTINGS_FAIL;
	memset( settings, 0, sizeof( Settings ) );
	
	s32 fd = ISFS_Open("/title/00000001/00000002/data/loader.ini", ISFS_OPEN_READ );
	if( fd < 0 )
	{
		//file not found create a new one
		// settings was created from scratch. no need to do it all over
		return Create_Settings_File() > 0 ? LOADSETTINGS_INI_CREATED : LOADSETTINGS_FAIL;
	}

	STACK_ALIGN(fstats, status, 1, 32);
	memset(status,0,sizeof(fstats));
	if ( ISFS_GetFileStats(fd,status) < 0 )
	{
		//we do not know what is in the file; leave it alone and run on the defaults
		ISFS_Close(fd);
		gprintf("LoadSettings : ISFS_GetFileStats failed, using the defaults");
		SetDefaultSettings();
		error = ERROR_SETTING_READ;
		return LOADSETTINGS_FAIL;
	}
	u32 file_length = status->file_length;
	if ( file_length != SETTINGS_FILE_SIZE && file_length != SETTINGS_FILE_SIZE_V1 )
	{
		ISFS_Close(fd);
		gprintf("LoadSettings : the file is %u bytes, not a size we know , resetting...", file_length);
		//recreate settings file
		ISFS_Delete("/title/00000001/00000002/data/loader.ini");
		return Create_Settings_File() > 0 ? LOADSETTINGS_INI_CREATED : LOADSETTINGS_FAIL;
	}

	if(ISFS_Read( fd, settings, file_length ) != (s32)file_length)
	{
		ISFS_Close( fd );
		SetDefaultSettings();
		error = ERROR_SETTING_READ;
		return LOADSETTINGS_FAIL;
	}
	ISFS_Close( fd );

	bool rewrite = false;
	if( file_length == SETTINGS_FILE_SIZE_V1 )
	{
		//an older layout: keep the settings it has, the new ones get their defaults. the rest of its 32 bytes is padding
		u8 old[SETTINGS_V1_USED_SIZE];
		memcpy( old, settings, SETTINGS_V1_USED_SIZE );
		SetDefaultSettings();
		memcpy( settings, old, SETTINGS_V1_USED_SIZE );
		gprintf("LoadSettings : migrated the settings file from %u to %u bytes", (u32)SETTINGS_FILE_SIZE_V1, (u32)SETTINGS_FILE_SIZE);
		rewrite = true;
	}
	//what is behind the last field is never a setting
	memset( settings->LegacyPadding, 0, sizeof( settings->LegacyPadding ) );
	memset( reinterpret_cast<u8*>( settings ) + SETTINGS_FILE_SIZE, 0, sizeof( Settings ) - SETTINGS_FILE_SIZE );

	if( settings->version == 0 || settings->version != VERSION_MERGED || settings->RCVersion != VERSION_RC )
	{
		settings->version = VERSION_MERGED;
		settings->RCVersion = VERSION_RC;
		rewrite = true;
	}
	if( rewrite && !WriteSettingsFile() )
		gprintf("LoadSettings : updating the settings file failed");
	return LOADSETTINGS_OK;
}
int SaveSettings( void )
{
	if(settings == NULL)
	{
		error = ERROR_SETTING_WRITE;
		return -1;
	}
	s32 fd = ISFS_Open("/title/00000001/00000002/data/loader.ini", 1|2 );
	
	if( fd < 0 )
	{
		// musn't happen!
		error = ERROR_SETTING_OPEN;
		return 0;
	}

	ISFS_Seek( fd, 0, 0 );

	s32 r = ISFS_Write( fd, settings, SETTINGS_FILE_SIZE );
	
	ISFS_Close( fd );

	if( r == (s32)SETTINGS_FILE_SIZE )
		return 1;
	error = ERROR_SETTING_WRITE;
	return r;
}
