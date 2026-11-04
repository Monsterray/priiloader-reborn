/*

priiloader-reborn - reading and splitting of password.txt from NAND.

This file is part of priiloader and is licensed under the GNU General Public License
version 2, like the rest of it.

*/

#include <string.h>
#include <gccore.h>
#include <ogc/machine/processor.h>

#include "password_file.h"
#include "mem2_manager.h"
#include "gecko.h"

int ReadPasswordFile(char** buffer, char** password, char** file)
{
	*buffer = NULL;
	*password = NULL;
	*file = NULL;

	s32 fd = ISFS_Open(PASSWORD_FILE_PATH, ISFS_OPEN_READ);
	if(fd < 0)
	{
		gprintf("ReadPasswordFile: ISFS_Open(password.txt) failure. error %d\r\n",fd);
		return PASSWORD_FILE_MISSING;
	}

	STACK_ALIGN(fstats, status, 1, 32);
	memset(status, 0, sizeof(fstats));
	if(ISFS_GetFileStats(fd, status) < 0)
	{
		ISFS_Close(fd);
		return PASSWORD_FILE_READ;
	}

	u32 size = status->file_length;
	char* buf = static_cast<char*>(mem_align(32, size + PASSWORD_FILE_SLACK + 1));
	if(buf == NULL)
	{
		ISFS_Close(fd);
		return PASSWORD_FILE_NOMEM;
	}
	memset(buf, 0, size + PASSWORD_FILE_SLACK + 1);

	if(size > 0 && ISFS_Read(fd, buf, size) < (s32)size)
	{
		ISFS_Close(fd);
		mem_free(buf);
		return PASSWORD_FILE_READ;
	}
	ISFS_Close(fd);

	char* pw = NULL;
	char* fl = NULL;
	char* ptr = strtok(buf, "\r\n");
	while (ptr != NULL)
	{
		if(fl == NULL && pw != NULL)
			fl = ptr;
		if(pw == NULL)
			pw = ptr;
		ptr = strtok(NULL, "\r\n");
		if(fl != NULL && pw != NULL)
			break;
	}

	*buffer = buf;
	*password = pw;
	*file = fl;
	return PASSWORD_FILE_OK;
}
