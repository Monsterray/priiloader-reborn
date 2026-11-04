/*
 * support.cpp -- stand-ins for the parts of priiloader that hacks.cpp calls but the host tests don't build.
 */
#include <string>

#include "gccore.h"
#include "mount.h"
#include "settings.h"

u32 g_sysmenu_version = 514;
std::string g_fat_path;

u32 GetSysMenuVersion(void)
{
	return g_sysmenu_version;
}

void InitMounts(mountChangedCallback callback)
{
	(void)callback;
}

u8 GetMountedFlags()
{
	return 0;
}

/* the test points this at a file it made */
std::string BuildPath(const char* path, StorageDevice device)
{
	(void)path; (void)device;
	return g_fat_path;
}
