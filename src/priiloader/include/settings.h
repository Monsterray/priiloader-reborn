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

#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <stddef.h>
#include "Global.h"
#include "version.h"

/*
 * The settings file (/title/00000001/00000002/data/loader.ini) is this struct, as it is in memory, written as a
 * binary file. It can only grow, and only at the end:
 *  - a new field goes after the last one, never in between and never in the padding behind it, and gets a new
 *    SETTINGS_FILE_SIZE_Vn that is bigger than the one before (see below);
 *  - the file is written at SETTINGS_FILE_SIZE (the end of the last field), not at sizeof(Settings), which is
 *    rounded up to 32 by the alignment;
 *  - a file of a known older size is read into a struct that has the defaults, the old bytes are copied over
 *    them and the file is written back at the new size (LoadSettings). Any other size resets the settings.
 * The offsets and sizes are checked below, so a change that moves a field does not build. docs/settings.ini
 * lists them for people, and tests/host/priiloader/test_settings.cpp checks that list against this struct.
 */
typedef struct {
	u8 autoboot;				// offset 0
	u32 version;				// 4
	u8 ReturnTo;				// 8
	u8 ShutdownTo;				// 9
	u8 StopDisc;				// 10
	u8 LidSlotOnError;			// 11
	u8 IgnoreShutDownMode;		// 12
	u32 RCVersion;				// 16
	u8 SystemMenuIOS;			// 20
	u8 UseSystemMenuIOS;		// 21
	u8 BlackBackground;			// 22
	u8 DumpGeckoText;			// 23
	u8 PasscheckPriiloader;		// 24
	u8 PasscheckMenu;			// 25
	u8 ShowRCUpdates;			// 26
	u8 PreferredMountPoint;		// 27
	//the padding of the first layout, which was a 32 byte file. kept as a field so that the next field starts
	//behind the end of that file: this is what makes the old layout recognisable by its size. always 0.
	u8 LegacyPadding[4];		// 28
	u8 AutobootWait;			// 32, an AUTOBOOTWAIT_* value
	u8 UpdateSource;			// 33, an UPDATESOURCE_* value
} ATTRIBUTE_ALIGN(32) Settings;

//the sizes the settings file has had. the first layout (up to PreferredMountPoint) was written as sizeof(Settings),
//which the 32 byte alignment made 32. older layouts (before PreferredMountPoint was added in the upstream project,
//and before the struct was aligned) put fields at other offsets and cannot be told from this one by size: they are not migrated.
#define SETTINGS_FILE_SIZE_V1		32
#define SETTINGS_FILE_SIZE_V2		(offsetof(Settings, UpdateSource) + sizeof(u8))
//what is written, and what the file has to be to be read as it is
#define SETTINGS_FILE_SIZE			SETTINGS_FILE_SIZE_V2
//the bytes of a V1 file that hold settings (the rest of the 32 bytes is padding)
#define SETTINGS_V1_USED_SIZE		(offsetof(Settings, PreferredMountPoint) + sizeof(u8))

static_assert(offsetof(Settings, autoboot) == 0, "settings layout moved");
static_assert(offsetof(Settings, version) == 4, "settings layout moved");
static_assert(offsetof(Settings, ReturnTo) == 8, "settings layout moved");
static_assert(offsetof(Settings, ShutdownTo) == 9, "settings layout moved");
static_assert(offsetof(Settings, StopDisc) == 10, "settings layout moved");
static_assert(offsetof(Settings, LidSlotOnError) == 11, "settings layout moved");
static_assert(offsetof(Settings, IgnoreShutDownMode) == 12, "settings layout moved");
static_assert(offsetof(Settings, RCVersion) == 16, "settings layout moved");
static_assert(offsetof(Settings, SystemMenuIOS) == 20, "settings layout moved");
static_assert(offsetof(Settings, UseSystemMenuIOS) == 21, "settings layout moved");
static_assert(offsetof(Settings, BlackBackground) == 22, "settings layout moved");
static_assert(offsetof(Settings, DumpGeckoText) == 23, "settings layout moved");
static_assert(offsetof(Settings, PasscheckPriiloader) == 24, "settings layout moved");
static_assert(offsetof(Settings, PasscheckMenu) == 25, "settings layout moved");
static_assert(offsetof(Settings, ShowRCUpdates) == 26, "settings layout moved");
static_assert(offsetof(Settings, PreferredMountPoint) == 27, "settings layout moved");
static_assert(offsetof(Settings, LegacyPadding) == 28, "settings layout moved");
static_assert(offsetof(Settings, AutobootWait) == 32, "settings layout moved");
static_assert(offsetof(Settings, UpdateSource) == 33, "settings layout moved");
static_assert(SETTINGS_V1_USED_SIZE == 28, "the first layout's used size changed");
static_assert(SETTINGS_FILE_SIZE_V1 == 32, "the first layout's file size changed");
static_assert(SETTINGS_FILE_SIZE_V2 == 34, "the file size changed: add the old size as a SETTINGS_FILE_SIZE_Vn and migrate it");
static_assert(SETTINGS_FILE_SIZE_V2 > SETTINGS_FILE_SIZE_V1, "a new layout has to be bigger than the one before");
static_assert(sizeof(Settings) % 32 == 0 && sizeof(Settings) >= SETTINGS_FILE_SIZE, "settings does not fit its own file");

enum {
		SETTING_AUTBOOT,
		SETTING_RETURNTO,
		SETTING_SHUTDOWNTO,
		SETTING_STOPDISC,
		SETTING_LIDSLOTONERROR,
		SETTING_IGNORESHUTDOWNMODE,
		SETTING_RCVERSION,
		SETTING_SYSTEMMENUIOS,
		SETTING_USESYSTEMMENUIOS,
		SETTING_BLACKBACKGROUND,
		SETTING_DUMPGECKOTEXT,
		SETTING_PASSCHECKPRII,
		SETTING_PASSCHECKMENU,
		SETTING_SHOWRCUPDATES,
		SETTING_AUTOBOOTWAIT,
		SETTING_UPDATESOURCE,
};

enum {
		AUTOBOOT_DISABLED,
		AUTOBOOT_HBC,
		AUTOBOOT_BOOTMII_IOS,
		AUTOBOOT_SYS,
		AUTOBOOT_FILE,
		AUTOBOOT_ERROR
};
enum {	
		RETURNTO_SYSMENU,
		RETURNTO_PRIILOADER,
		RETURNTO_AUTOBOOT,
		RETURNTO_FILE,
};

enum {
	SHUTDOWNTO_NONE,
	SHUTDOWNTO_PRIILOADER,
	SHUTDOWNTO_AUTOBOOT
};

enum PreferredMountPoint {
	MOUNT_AUTO,
	MOUNT_SD,
	MOUNT_USB
};

//how long to look for a key (B held, a USB keyboard) before an autoboot decision. A choice, not a number of ms: see GetAutobootWaitMs
enum AutobootWait {
	AUTOBOOTWAIT_0MS,
	AUTOBOOTWAIT_100MS,
	AUTOBOOTWAIT_250MS,
	AUTOBOOTWAIT_500MS,
	AUTOBOOTWAIT_COUNT
};
#define AUTOBOOTWAIT_DEFAULT AUTOBOOTWAIT_500MS

//where the updater looks for new versions
enum UpdateSource {
	UPDATESOURCE_DACOTACO,	// upstream's server (plain HTTP), what Priiloader always did
	UPDATESOURCE_GITHUB,	// the releases of priiloader-reborn on GitHub
	UPDATESOURCE_COUNT
};
#define UPDATESOURCE_DEFAULT UPDATESOURCE_GITHUB

enum LoadSettingsResult {
	LOADSETTINGS_OK,
	LOADSETTINGS_FAIL,
	LOADSETTINGS_INI_CREATED
};

extern Settings *settings;

u32 GetSysMenuVersion( void );
u32 GetSysMenuIOS( void );
u32 SGetSetting( u32 s );
u32 GetAutobootWaitMs( void );
LoadSettingsResult LoadSettings( void );
int SaveSettings( void );

#endif
