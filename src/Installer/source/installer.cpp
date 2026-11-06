/*
Preloader/Priiloader Installer - An installation utiltiy for preloader (c) 2008-2020 crediar

Copyright (c) 2020 DacoTaco

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <malloc.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <ogc/machine/processor.h>

#include "gecko.h"
#include "title.h"
#include "installer.h"
#include "console_colors.h"
#include "nand.h"
#include "priiloader_app.h"

const char* _titleAppFormat = "/title/%08x/%08x/content/%08x.app";
char _originalBootAppPath[ISFS_MAXPATH] = {0};
char _copyBootAppPath[ISFS_MAXPATH] = {0};

// only used for vwii
bool _isvWii = false;
char _vwiiAdditionalApp[ISFS_MAXPATH] = {0};
char _nandloaderAppPath[ISFS_MAXPATH] = {0};

char _tmdPath[ISFS_MAXPATH] = {};
char _tmdBackupPath[ISFS_MAXPATH] = {};

static u32 _tmdSize;
static u8 _tmdBuffer[MAX_SIGNED_TMD_SIZE] ATTRIBUTE_ALIGN(64);
static signed_blob *_tmdBlob = (signed_blob *)_tmdBuffer;
static tmd *_smTmd = NULL; //set later, when TMD is loaded in
static u64 _targetTitleId = 0;

// only used for vwii
u32 _nandloaderTmdSize;
static u8 _nandloaderTmdBuffer[MAX_SIGNED_TMD_SIZE] ATTRIBUTE_ALIGN(64);
static signed_blob *_nandloaderTmdBlob = (signed_blob *)_nandloaderTmdBuffer;
static tmd *_nandloaderTmd = NULL;
static const u64 _nandloaderTitleId = 0x0000000100000200ll;

static bool NandFileExists(const char* path)
{
	s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
	if(fd < 0)
		return false;

	ISFS_Close(fd);
	return true;
}

static NandPermissions GetTmdPermissions(void)
{
	NandPermissions filePermissions = { 0 };
	s32 ret = ISFS_GetAttr(_tmdPath, &filePermissions.owner, &filePermissions.group, &filePermissions.attributes, &filePermissions.ownerperm, &filePermissions.groupperm, &filePermissions.otherperm);
	if(ret < 0 )
	{
		//attribute getting failed. returning to default
		printf(COLOR_WARN);
		printf("\nWARNING : failed to get file's permissions. using defaults\r\n");
		printf(COLOR_STEP);
		gprintf("permission failure on desination! error %d", ret);
		gprintf("writing with max permissions");
		filePermissions = {
			.ownerperm = 3,
			.groupperm = 3,
			.otherperm = 0
		};
	}

	gprintf("ret %d owner %d group %d attributes %X perm:%X-%X-%X", ret, filePermissions.owner, filePermissions.group, filePermissions.attributes, filePermissions.ownerperm, filePermissions.groupperm, filePermissions.otherperm);
	return filePermissions;
}

//puts the original System Menu back (boot content and, if asked, its TMD). returns true only if everything that
//had to be restored was restored. the callers delete their backups only when this returns true.
static bool RestoreSystemMenu(const NandPermissions& appPermissions, bool restoreTmd)
{
	bool restored = true;
	if(NandCopy(_copyBootAppPath, _originalBootAppPath, appPermissions) < 0)
	{
		restored = false;
		printf(COLOR_DANGER);
		printf("\nUNABLE TO RESTORE THE SYSTEM MENU APP!!!\nTHE BACKUP (%s) WAS KEPT.\r\n", _copyBootAppPath);
		printf(COLOR_STEP);
	}

	if(restoreTmd && NandFileExists(_tmdBackupPath))
	{
		if(NandCopy(_tmdBackupPath, _tmdPath, GetTmdPermissions()) < 0)
		{
			restored = false;
			printf(COLOR_DANGER);
			printf("\nUNABLE TO RESTORE THE SYSTEM MENU TMD!!!\nTHE BACKUP (%s) WAS KEPT.\r\n", _tmdBackupPath);
			printf(COLOR_STEP);
		}
	}

	if(!restored)
	{
		printf(COLOR_DANGER);
		printf("THIS COULD BRICK THE WII SO PLEASE REINSTALL SYSTEM MENU\nWHEN RETURNING TO THE HOMEBREW CHANNEL!!!\r\n\r\n");
		printf(COLOR_STEP);
	}
	return restored;
}

s32 DeletePriiloaderFile(const std::string& pathFormat, const std::string& fileDescription)
{
	char file_path[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32) = {0};
	sprintf(file_path, pathFormat.c_str(), TITLE_UPPER(_targetTitleId), TITLE_LOWER(_targetTitleId));
	s32 ret = ISFS_Delete(file_path);

	gprintf("deleting %s : %d", file_path, ret);
	printf("%s: ", fileDescription.c_str());
	if(ret == -106)
	{
		printf(COLOR_INFO);
		printf("Not found\r\n");
		printf(COLOR_STEP);
	}
	else if(ret == -102)
	{
		printf(COLOR_WARN);
		printf("Error deleting file: access denied\r\n");
		printf(COLOR_STEP);
	}
	else if (ret < 0)
	{
		printf(COLOR_WARN);
		printf("Error deleting file. error %d\r\n",ret);
		printf(COLOR_STEP);
	}
	else
	{
		printf(COLOR_OK);
		printf("Deleted\r\n");
		printf(COLOR_STEP);
	}

	return ret;
}

void DeletePriiloaderFiles(InstallerAction action)
{
	const std::string dataFolder = "/title/%08x/%08x/data/";

	bool settings = false;
	bool hacks = false;
	bool password = false;
	bool main_bin = false;
	bool ticket = false;
	//an update keeps the settings and the password: LoadSettings migrates an older settings file (upstream deleted
	//both on every update, which also reset the autoboot setting). install and remove clear them as before.
	switch(action)
	{
		case InstallerAction::Remove: //remove
			main_bin = true;
			ticket = true;
		case InstallerAction::Install: //install
			hacks = true;
			settings = true;
			password = true;
		default:
			break;
	}

	if(action == InstallerAction::Install)
		printf("Attempting to delete leftover files...\r\n");
	else
		printf("Deleting extra Priiloader files...\r\n");

	if(password)
		DeletePriiloaderFile(dataFolder + "password.txt", "password");

	if(settings)
		DeletePriiloaderFile(dataFolder + "loader.ini", "Settings file");
	
	//its best we delete that ticket but its completely useless and will only get in our 
	//way when installing again later...
	if(ticket)
		DeletePriiloaderFile("/title/%08x/%08x/content/ticket", "Ticket");
	
	if(hacks)
	{
		DeletePriiloaderFile(dataFolder + "hacks_s.ini", "Hacks_s.ini");
		DeletePriiloaderFile(dataFolder + "hacks.ini", "Hacks.ini");
		DeletePriiloaderFile(dataFolder + "hacksh_s.ini", "Hacksh_s.ini");
		DeletePriiloaderFile(dataFolder + "hackshas.ini", "system_hacks");
	}
	if(main_bin)
	{
		DeletePriiloaderFile(dataFolder + "main.nfo", "autoboot binary info");
		DeletePriiloaderFile(dataFolder + "main.bin", "autoboot binary");
	}
	return;
}

bool PriiloaderInstalled(void)
{
	printf("Checking for Priiloader...\r\n");
	gprintf("Checking for SystemMenu Dol");
	s32 fd = ISFS_Open(_copyBootAppPath, ISFS_OPEN_RW);
	if (fd == -106) //ISFS_ENOENT: the copy does not exist, Priiloader is not installed
		return false;

	//any other error is not an answer: reading it as "not installed" would turn an update into an install over the vanilla copy
	if (fd < 0)
		throw "Unable to check for the System Menu copy. error " + std::to_string(fd);

	ISFS_Close(fd);
	return true;
}

bool CheckvWiiNandLoader(void)
{
	s32 ret;
	u32 x;

	//check if BC-NAND is installed
	ret = ES_GetTitleContentsCount(_nandloaderTitleId, &x);

	if (ret < 0)
		return false; // title was never installed

	if (x <= 0)
		return false; // title was installed but deleted via Channel Management

	return true;
}

void InitializeInstaller(u64 titleId, bool isvWii)
{
	_isvWii = isvWii;
	_targetTitleId = titleId;

	if (_isvWii && !CheckvWiiNandLoader())
		throw "BC-NAND not installed!";

	//read TMD so we can get the main booting dol
	memset(_tmdPath, 0, ISFS_MAXPATH);
	memset(_tmdBackupPath, 0, ISFS_MAXPATH);
	sprintf(_tmdPath, "/title/%08x/%08x/content/title.tmd",TITLE_UPPER(_targetTitleId),TITLE_LOWER(_targetTitleId));
	sprintf(_tmdBackupPath, "/title/%08x/%08x/content/title_or.tmd",TITLE_UPPER(_targetTitleId),TITLE_LOWER(_targetTitleId));

	s32 ret = ES_GetStoredTMDSize(_targetTitleId, &_tmdSize);
	if(ret < 0)
		throw "Unable to get stored tmd size";
	
	ret = ES_GetStoredTMD(_targetTitleId, _tmdBlob, _tmdSize);
	if (ret < 0)
		throw "Unable to get stored tmd";
	
	_smTmd = reinterpret_cast<tmd*>(SIGNATURE_PAYLOAD(_tmdBlob));
	gprintf("sm version : %d", _smTmd->title_version);

	if(_smTmd->title_version > 0x2000)
		throw "System Menu version invalid or not vanilla (v" + std::to_string(_smTmd->title_version) + ")";
	
	// For vWii only the 5.2.0 SM is supported, everything else is untested and probably doesn't work with the builtin hacks
	if (_isvWii && (_smTmd->title_version & 0xFFF0) != 608)
		throw "System Menu version not supported (v" + std::to_string(_smTmd->title_version) + ")";

	u32 fileId = 0;
	for(u16 i=0; i < _smTmd->num_contents; ++i)
	{
		if (_smTmd->contents[i].index == _smTmd->boot_index)
		{
			fileId = _smTmd->contents[i].cid;
			break;
		}
	}

	if (fileId == 0)
		throw "Unable to retrieve title booting app";

	//guards against a TMD whose boot entry carries the 2XXXXXXX prefix: the System Menu's file is the one with the
	//original id, and using the prefixed id would make an update write the nandloader over Priiloader.
	//(a real vWii update keeps the boot entry as is and puts Priiloader in content 1, so this normally does nothing)
	if (_isvWii && (fileId >> 28) == 2)
		fileId &= 0x0FFFFFFF;

	memset(_originalBootAppPath, 0, ISFS_MAXPATH);
	memset(_copyBootAppPath, 0, ISFS_MAXPATH);
	sprintf(_originalBootAppPath, _titleAppFormat, TITLE_UPPER(_targetTitleId), TITLE_LOWER(_targetTitleId), fileId);
	fileId = 0x10000000 | (fileId & 0x0FFFFFFF);
	sprintf(_copyBootAppPath, _titleAppFormat, TITLE_UPPER(_targetTitleId), TITLE_LOWER(_targetTitleId), fileId);

	// For vWii use additional content containing priiloader, which replaces content 1
	if (_isvWii)
	{
		// This is where the priiloader app will be written to (2XXXXXXX.app)
		fileId = 0x20000000 | (fileId & 0x0FFFFFFF);
		memset(_vwiiAdditionalApp, 0, ISFS_MAXPATH);
		sprintf(_vwiiAdditionalApp, _titleAppFormat, TITLE_UPPER(_targetTitleId), TITLE_LOWER(_targetTitleId), fileId);	

		// Verify the BC-NAND tmd
		ret = ES_GetStoredTMDSize(_nandloaderTitleId, &_nandloaderTmdSize);
		if(ret < 0)
			throw "Unable to get stored nandloader tmd size";

		ret = ES_GetStoredTMD(_nandloaderTitleId, _nandloaderTmdBlob, _nandloaderTmdSize);
		if (ret < 0)
			throw "Unable to get stored nandloader tmd";

		_nandloaderTmd = reinterpret_cast<tmd*>(SIGNATURE_PAYLOAD(_nandloaderTmdBlob));
		fileId = 0; //still holds the SM's id here, so the check below would never fire
		for(u16 i=0; i < _nandloaderTmd->num_contents; ++i)
		{
			if (_nandloaderTmd->contents[i].index == _nandloaderTmd->boot_index)
			{
				fileId = _nandloaderTmd->contents[i].cid;
				break;
			}
		}
		if (fileId == 0)
			throw "Unable to retrieve nandloader booting app";

		// Write the nandloader app path
		memset(_nandloaderAppPath, 0, ISFS_MAXPATH);
		sprintf(_nandloaderAppPath, _titleAppFormat, TITLE_UPPER(_nandloaderTitleId), TITLE_LOWER(_nandloaderTitleId), fileId);
	}

	gprintf("original file: %s", _originalBootAppPath);
	gprintf("copy file    : %s", _copyBootAppPath);
	gprintf("vwii file    : %s", _vwiiAdditionalApp);
	gprintf("nandloader   : %s", _nandloaderAppPath);
}

s32 RemovePriiloader(void)
{
	std::string errorMsg = "";
	try
	{
		NandPermissions appPermissions = {
			.ownerperm = 3,
			.groupperm = 3,
			.otherperm = 3,
		};
		
		printf("Restoring System menu app...");
		s32 ret = NandCopy(_copyBootAppPath, _originalBootAppPath, appPermissions);
		if(ret < 0)
			throw "Failed to restore System Menu";

		printf(COLOR_OK "Done!\r\n" COLOR_STEP);
		//the System Menu copy (and on vWii the Priiloader content) stay until PatchTMD restored the TMD: if that fails,
		//the TMD still wants Priiloader's hash and the copy is what the Wii can be repaired from
	}
	catch (const std::string& ex)
	{
		errorMsg = ex;
	}
	catch (char const* ex)
	{
		errorMsg = ex;
	}
	catch(...)
	{
		errorMsg = "Unknown Error Occurred";
	}

	if(!errorMsg.empty())
	{
		//the restore failed. if the boot content is gone put back what the TMD still holds the hash of:
		//Priiloader, or BC-NAND on vWii.
		//the System Menu copy is kept either way.
		if(!NandFileExists(_originalBootAppPath))
		{
			NandPermissions appPermissions = {
				.ownerperm = 3,
				.groupperm = 3,
				.otherperm = 3,
			};
			//on vWii the TMD's boot content hash is BC-NAND's; elsewhere it is Priiloader's
			s32 putBack = _isvWii
				? NandCopy(_nandloaderAppPath, _originalBootAppPath, appPermissions)
				: NandWrite(_originalBootAppPath, priiloader_app, priiloader_app_size, appPermissions);
			if(putBack < 0)
			{
				printf(COLOR_DANGER);
				printf("\nUNABLE TO PUT BACK THE BOOT CONTENT!!!\nTHE SYSTEM MENU COPY (%s) WAS KEPT.\r\n", _copyBootAppPath);
				printf(COLOR_STEP);
			}
		}
		throw errorMsg;
	}

	return 1;
}

s32 WritePriiloader(InstallerAction action)
{
	if(action != InstallerAction::Install && action != InstallerAction::Update)
		return -1;

	s32 ret;
	NandPermissions appPermissions = {
		.ownerperm = 3,
		.groupperm = 3,
		.otherperm = 3,
	};
	if(action == Install)
	{
		//system menu coping
		printf("Moving System Menu app...");
		ret = NandCopy(_originalBootAppPath, _copyBootAppPath, appPermissions);
		if(ret < 0)
		{
			ISFS_Delete(_copyBootAppPath);
			throw "Failed to create System menu app copy";
		}

		gprintf("Moved System Menu App");
		printf(COLOR_OK "Done!\r\n" COLOR_STEP);
	}

	//sys_menu app moved. lets write priiloader
	ret = 0;
	printf("Writing Priiloader app...");
	gprintf("Writing Priiloader");

	if (_isvWii)
	{
		// The additional content (2XXXXXXX.app) holds Priiloader, the boot content becomes BC-NAND.
		// On an install the additional content goes first: if it fails nothing was touched. On an update the TMD
		// already holds the hash of the old additional content, so it is written last, right before PatchTMD:
		// a failure of the nandloader copy then leaves the old content and TMD in step.
		for(int step = 0; step < 2; step++)
		{
			bool writeApp = (step == 0) == (action == Install);
			if(writeApp)
			{
				ret = NandWrite(_vwiiAdditionalApp, priiloader_app, priiloader_app_size, appPermissions);
				if (ret < 0)
				{
					//the boot content was not touched yet (install) or is already BC-NAND (update): the update leaves
					//the old additional content in place, so only a fresh install removes anything.
					if(action == Install && RestoreSystemMenu(appPermissions, false))
					{
						ISFS_Delete(_vwiiAdditionalApp);
						ISFS_Delete(_copyBootAppPath);
					}
					throw "Unable to write Priiloader app. error " + std::to_string(ret);
				}

				gprintf("Priiloader app written.");
				printf(COLOR_OK "Done!\r\n" COLOR_STEP);
				continue;
			}

			// On vWii we replace the SM boot content with BC-NAND
			printf("Replacing System Menu app...");
			ret = NandCopy(_nandloaderAppPath, _originalBootAppPath, appPermissions);
			if (ret < 0 && action == Update && !NandFileExists(_originalBootAppPath))
			{
				//the boot content is gone: try once more before falling back to the original System Menu
				ret = NandCopy(_nandloaderAppPath, _originalBootAppPath, appPermissions);
			}

			if (ret < 0)
			{
				bool restored = false;
				if(action == Install)
				{
					restored = RestoreSystemMenu(appPermissions, false);
					if(restored)
					{
						ISFS_Delete(_vwiiAdditionalApp);
						ISFS_Delete(_copyBootAppPath);
					}
				}
				else if(!NandFileExists(_originalBootAppPath))
					restored = RestoreSystemMenu(appPermissions, true); //TMD too: it points to the additional content
				else
					restored = true; //the old boot content is still there

				throw "Unable to replace the system menu. error " + std::to_string(ret) + (restored ? ", changes reverted." : ", RESTORE FAILED, the backup was kept.");
			}

			gprintf("System Menu replaced.");
			printf(COLOR_OK "Done!\r\n" COLOR_STEP);
		}
	}
	else
	{
		ret = NandWrite(_originalBootAppPath, priiloader_app, priiloader_app_size, appPermissions);
		if(ret < 0 && action == Update && !NandFileExists(_originalBootAppPath))
		{
			//the boot content is gone: try once more before falling back to the original System Menu
			ret = NandWrite(_originalBootAppPath, priiloader_app, priiloader_app_size, appPermissions);
		}

		if(ret < 0)
		{
			//the System Menu copy is only deleted after a verified restore on a fresh install. on an update it is the only
			//real System Menu, and the TMD holds Priiloader's hash: if the boot content is gone, restore the TMD too.
			bool restored = false;
			if(action == Install)
			{
				restored = RestoreSystemMenu(appPermissions, false);
				if(restored)
					ISFS_Delete(_copyBootAppPath);
			}
			else if(!NandFileExists(_originalBootAppPath))
				restored = RestoreSystemMenu(appPermissions, true);
			else
				restored = true; //the old Priiloader is still there

			throw "Failed to write Priiloader : error " + std::to_string(ret) + (restored ? "" : ", RESTORE FAILED, the backup was kept.");
		}
	}

	gprintf("Priiloader Update Complete");
	printf(COLOR_OK "Done!\r\n\r\n" COLOR_STEP);
	return 1;
}

s32 CopyTicket(void)
{
	s32 ret = 0;
	char ticketDestinationPath[ISFS_MAXPATH];
	memset(ticketDestinationPath, 0, ISFS_MAXPATH);
	sprintf(ticketDestinationPath, "/title/%08x/%08x/content/ticket", TITLE_UPPER(_targetTitleId), TITLE_LOWER(_targetTitleId));
	gprintf("Checking for copy ticket...");

	ret = ISFS_Open(ticketDestinationPath, ISFS_OPEN_READ);
	if (ret >= 0)
	{
		ISFS_Close(ret);
		printf(COLOR_INFO "Skipping copy of system menu ticket...\r\n" COLOR_STEP);
		gprintf("ticket copy exists.");
		return 1;
	}

	gprintf("failed to open ticket, creating copy");
	char ticketSourcePath[ISFS_MAXPATH];
	NandPermissions appPermissions = {
		.ownerperm = 3,
		.groupperm = 3,
		.otherperm = 3,
	};

	memset(ticketSourcePath, 0, ISFS_MAXPATH);
	sprintf(ticketSourcePath, "/ticket/%08x/%08x.tik", TITLE_UPPER(_targetTitleId), TITLE_LOWER(_targetTitleId));
	ISFS_Delete(ticketDestinationPath);
	ret = NandCopy(ticketSourcePath, ticketDestinationPath, appPermissions);
	if(ret < 0)
		throw "Failed to make ticket copy!";
	
	printf(COLOR_OK "Done!\r\n" COLOR_STEP);
	return 1;
}

//Remove: the System Menu copy and on vWii the Priiloader content go only once the TMD is back to the original
static void DeleteRemovedSystemMenuCopies(void)
{
	ISFS_Delete(_copyBootAppPath);
	if(_isvWii)
	{
		printf("Deleting Priiloader app...");
		ISFS_Delete(_vwiiAdditionalApp);
		printf(COLOR_OK "Done!\r\n" COLOR_STEP);
	}
}

s32 PatchTMD( InstallerAction action )
{
	NandPermissions filePermissions = GetTmdPermissions();
	s32 ret = 0;

	s32 fd = ISFS_Open(_tmdBackupPath, ISFS_OPEN_READ);
	bool backupFound = fd >= 0;
	bool backupSuspect = false; //the backup is there but not trustworthy: never restore from it
	if(fd < 0 && action == InstallerAction::Remove)
	{
		printf(COLOR_INFO "TMD backup not found. leaving TMD alone...\r\n" COLOR_STEP);
		DeleteRemovedSystemMenuCopies();
		return 1;
	}
	if(fd >= 0)
	{
		ISFS_Close(fd);
		fd = -1;
	}

	switch(action)
	{
		//not so sure why we'd want to delete the tmd modification but just to remove all traces of priiloader... :)
		case InstallerAction::Remove:
			gprintf("restoring TMD...");
			printf("Restoring System Menu TMD...\r\n");

			ret = NandCopy(_tmdBackupPath, _tmdPath, filePermissions);
			if ( ret >= 0)
			{
				ISFS_Delete(_tmdBackupPath);
				DeleteRemovedSystemMenuCopies();
				return 1;
			}
			else
			{
				printf(COLOR_DANGER);
				printf("UNABLE TO RESTORE THE SYSTEM MENU TMD!!!\n\nTHIS COULD BRICK THE WII SO PLEASE REINSTALL SYSTEM MENU (0x%04X)\nWHEN RETURNING TO THE HOMEBREW CHANNEL!!!\n\r\n", _smTmd->title_version);
				printf(COLOR_STEP);
				throw "TMD Restoration failed";
			}

			return 1;
		default:
			gprintf("patching TMD...");
			printf("Patching TMD...");
			break;
	}

	void* backupTmdBuffer = NULL;
	void* applicationBuffer = NULL;
	std::string errorMsg = "";
	try
	{
		bool createBackup = !backupFound;

		//read tmd
		ret = ISFS_Open(_tmdPath, ISFS_OPEN_READ);
		if (ret < 0)
			throw "failed to open TMD";

		fd = ret;
		STACK_ALIGN(fstats, fileStatus, 1, 32);
		ret = ISFS_GetFileStats(fd, fileStatus);
		if (ret < 0)
			throw "failed to get TMD information";

		//room is needed for the content vWii adds
		if(fileStatus->file_length < 1 || fileStatus->file_length + sizeof(tmd_content) > MAX_SIGNED_TMD_SIZE)
			throw "TMD has an invalid size";

		memset(_tmdBuffer, 0, MAX_SIGNED_TMD_SIZE);
		_tmdSize = fileStatus->file_length;
		ret = ISFS_Read(fd, _tmdBuffer, _tmdSize);
		if(ret < 0)
			throw "Failed to read TMD data";
		if((u32)ret != _tmdSize)
			throw "Short read of TMD data";

		ISFS_Close(fd);
		fd = -1;

		//read tmd backup, if it exists ofcourse
		backupTmdBuffer = memalign(32, MAX_SIGNED_TMD_SIZE);
		if(backupTmdBuffer == NULL)
			throw "Failed to allocate backup TMD buffer";
		
		memset(backupTmdBuffer, 0, MAX_SIGNED_TMD_SIZE);
		fd = ISFS_Open(_tmdBackupPath, ISFS_OPEN_READ);
		if(fd >= 0)
		{
			ret = ISFS_GetFileStats(fd, fileStatus);
			if (ret < 0)
				throw "failed to get TMD backup information";

			if(fileStatus->file_length < 1 || fileStatus->file_length > MAX_SIGNED_TMD_SIZE)
			{
				backupSuspect = true;
				throw "TMD backup has an invalid size";
			}

			ret = ISFS_Read(fd, backupTmdBuffer, fileStatus->file_length);
			if(ret < 0)
				throw "Failed to read TMD Backup data";
			if((u32)ret != fileStatus->file_length)
			{
				backupSuspect = true;
				throw "Short read of TMD Backup data";
			}

			ISFS_Close(fd);
			fd = -1;
		}

		signed_blob * backupTmdBlob = static_cast<signed_blob *>(backupTmdBuffer);
		const tmd* backupTmd = reinterpret_cast<tmd*>(SIGNATURE_PAYLOAD(backupTmdBlob));
		//create TMD backup if needed
		if(createBackup || backupTmd->title_version != _smTmd->title_version)
		{
			gprintf("Making tmd backup...");
			ret = NandWrite(_tmdBackupPath, _tmdBuffer, _tmdSize, filePermissions); //replaces an old backup in place
			if(ret < 0)
				throw "Failed to make TMD Backup";
		}

		//copy tmd content over so that we will be comparing with our state before patching tmd later on.
		memcpy(backupTmdBuffer, _tmdBuffer, _tmdSize);
		gprintf("TMD Check : backup TMD is correct.");
		gprintf("detected access rights : 0x%08X", _smTmd->access_rights);
		if(_smTmd->access_rights == 0x03)
			gprintf("no AHBPROT modification needed");
		else
			_smTmd->access_rights = 0x03;

		u32 appHash[5] ATTRIBUTE_ALIGN(32) = {0};
		if (_isvWii)
		{
			// First read the original app (which is now BC-NAND)
			memset(fileStatus, 0, sizeof(fstats));
			ret = ISFS_Open(_originalBootAppPath, ISFS_OPEN_READ);
			if(ret < 0)
				throw "Failed to open original app";
			
			fd = ret;
			ret = ISFS_GetFileStats(fd, fileStatus);
			if(ret < 0)
				throw "Failed to get original app stats";

			if(fileStatus->file_length < 1)
				throw "Original app is empty";

			applicationBuffer = memalign(32, ALIGN32(fileStatus->file_length));
			if(applicationBuffer == NULL)
				throw "Failed to allocate memory for original app";
			
			ret = ISFS_Read(fd, applicationBuffer, fileStatus->file_length);
			if(ret < 0)
				throw "Failed to read original app";
			if((u32)ret != fileStatus->file_length)
				throw "Short read of original app";

			ISFS_Close(fd);
			fd = -1;

			//Get Hashes and do some changes
			ret = SHA_Calculate(applicationBuffer, fileStatus->file_length, appHash);
			if(ret < 0)
				throw "Failed to calculate application app hash";

			if (!memcmp(_smTmd->contents[_smTmd->boot_index].hash, appHash, sizeof(appHash) ) )
			{
				gprintf("no SHA hash change needed");
			}
			else
			{
				gprintf("altering app sha to %08x %08x %08x %08x %08x", appHash[0], appHash[1], appHash[2], appHash[3], appHash[4]);
				memcpy(_smTmd->contents[_smTmd->boot_index].hash, appHash, sizeof(appHash));
			}

			// Check if we already have additional contents
			if (_smTmd->num_contents == 9)
			{
				// Move content 1 to the end
				memcpy(&_smTmd->contents[_smTmd->num_contents], &_smTmd->contents[1], sizeof(tmd_content));
				_smTmd->contents[_smTmd->num_contents].index = _smTmd->num_contents;
				_smTmd->num_contents++;
				_tmdSize += sizeof(tmd_content);

				// Now replace content 1 with priiloader
				_smTmd->contents[1].cid = 0x20000000 | (_smTmd->contents[_smTmd->boot_index].cid & 0x0FFFFFFF);
				_smTmd->contents[1].index = 1;
				_smTmd->contents[1].type = 1;
			}
			// If we have more than 9 contents already make sure content 1 is priiloader
			else if (_smTmd->contents[1].cid >> 28 != 2)
				throw "Invalid SM TMD";

			// Update content 1 size
			_smTmd->contents[1].size = priiloader_app_size;
			
			// Update content 1 hash
			memset(appHash, 0, sizeof(appHash));
			ret = SHA_Calculate(priiloader_app, priiloader_app_size, appHash);
			if(ret < 0)
				throw "Failed to compute Priiloader hash";
			
			if (!memcmp(_smTmd->contents[1].hash, appHash, sizeof(appHash))) 
				gprintf("no SHA hash change needed");
			else
			{
				memcpy(_smTmd->contents[1].hash, appHash, sizeof(appHash));
				gprintf("new hash: %08x %08x %08x %08x %08x", appHash[0], appHash[1], appHash[2], appHash[3], appHash[4]);
				DCFlushRange(_smTmd,sizeof(tmd));
			}			
		}
		else
		{
			gprintf("checking Boot app SHA1 hash...");
			gprintf("bootapp ( %d ) SHA1 hash = %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",_smTmd->boot_index
				,_smTmd->contents[_smTmd->boot_index].hash[0],_smTmd->contents[_smTmd->boot_index].hash[1],_smTmd->contents[_smTmd->boot_index].hash[2],_smTmd->contents[_smTmd->boot_index].hash[3]
				,_smTmd->contents[_smTmd->boot_index].hash[4],_smTmd->contents[_smTmd->boot_index].hash[5],_smTmd->contents[_smTmd->boot_index].hash[6],_smTmd->contents[_smTmd->boot_index].hash[7]
				,_smTmd->contents[_smTmd->boot_index].hash[8],_smTmd->contents[_smTmd->boot_index].hash[9],_smTmd->contents[_smTmd->boot_index].hash[10],_smTmd->contents[_smTmd->boot_index].hash[11]
				,_smTmd->contents[_smTmd->boot_index].hash[12],_smTmd->contents[_smTmd->boot_index].hash[13],_smTmd->contents[_smTmd->boot_index].hash[14],_smTmd->contents[_smTmd->boot_index].hash[15]
				,_smTmd->contents[_smTmd->boot_index].hash[16],_smTmd->contents[_smTmd->boot_index].hash[17],_smTmd->contents[_smTmd->boot_index].hash[18],_smTmd->contents[_smTmd->boot_index].hash[19]);
			gprintf("generated priiloader SHA1 : ");

			ret = SHA_Calculate(priiloader_app, priiloader_app_size, appHash);
			if(ret < 0)
				throw "Failed to compute Priiloader hash";
			
			if (!memcmp(_smTmd->contents[_smTmd->boot_index].hash, appHash, sizeof(appHash) ) )
			{
				gprintf("no SHA hash change needed");
			}
			else
			{
				memcpy(_smTmd->contents[_smTmd->boot_index].hash, appHash, sizeof(appHash));
				gprintf("new hash: %08x %08x %08x %08x %08x", appHash[0], appHash[1], appHash[2], appHash[3], appHash[4]);
				DCFlushRange(_smTmd,sizeof(tmd));
			}
		}

		//check if we need to write the tmd changes
		if (memcmp(_tmdBuffer, backupTmdBuffer, _tmdSize) != 0)
		{
			gprintf("saving TMD");
			ret = NandWrite(_tmdPath, _tmdBuffer, _tmdSize, filePermissions);
			if(ret < 0)
				throw "Failed to write TMD patch";
			printf(COLOR_OK "done!\r\n" COLOR_STEP);
		}
		else
		{
			gprintf("no TMD mods needed");
			printf(COLOR_INFO "no TMD modifications needed\r\n" COLOR_STEP);
		}
	}
	catch (const std::string& ex)
	{
		errorMsg = ex;
	}
	catch (char const* ex)
	{
		errorMsg = ex;
	}
	catch(...)
	{
		errorMsg = "Unknown Error Occurred";
	}

	if(fd >= 0)
	{
		ISFS_Close(fd);
		fd = -1;
	}

	if(applicationBuffer)
	{
		free(applicationBuffer);
		applicationBuffer = NULL;
	}

	if(backupTmdBuffer)
	{
		free(backupTmdBuffer);
		backupTmdBuffer = NULL;
	}

	if(!errorMsg.empty())
	{
		printf(COLOR_DANGER);
		printf("UNABLE TO PATCH THE SYSTEM MENU TMD!!!\n\nTHIS COULD BRICK THE WII SO PLEASE REINSTALL SYSTEM MENU (v%d)\nWHEN RETURNING TO THE HOMEBREW CHANNEL!!!\n\r\n", _smTmd->title_version);
		printf(COLOR_STEP);

		//the boot content is already Priiloader's, so the original System Menu must come back as a whole: its app and
		//its TMD. the backups (TMD backup, System Menu copy) are deleted only after both were restored.
		NandPermissions appPermissions = {
			.ownerperm = 3,
			.groupperm = 3,
			.otherperm = 3,
		};
		bool restored = false;
		if(backupSuspect)
		{
			printf(COLOR_DANGER);
			printf("THE TMD BACKUP (%s) IS NOT USABLE AND WAS KEPT.\r\n", _tmdBackupPath);
			printf(COLOR_STEP);
		}
		else
			restored = RestoreSystemMenu(appPermissions, true);

		if(restored)
		{
			ISFS_Delete(_tmdBackupPath);
			if(action == InstallerAction::Install)
			{
				if(_isvWii)
					ISFS_Delete(_vwiiAdditionalApp);
				ISFS_Delete(_copyBootAppPath);
			}
		}
		throw errorMsg;
	}

	return ret;
}