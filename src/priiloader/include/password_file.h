/*

priiloader-reborn - reading and splitting of password.txt from NAND.

This file is part of priiloader and is licensed under the GNU General Public License
version 2, like the rest of it.

*/

#ifndef _PASSWORD_FILE_H_
#define _PASSWORD_FILE_H_

#include <gccore.h>

#define PASSWORD_FILE_PATH "/title/00000001/00000002/data/password.txt"
//bytes after the end of the file that belong to the buffer, for code that writes a (short) text over the password
#define PASSWORD_FILE_SLACK 16

enum {
	PASSWORD_FILE_OK = 0,
	PASSWORD_FILE_MISSING = -1,
	PASSWORD_FILE_NOMEM = -2,
	PASSWORD_FILE_READ = -3
};

//reads password.txt. on PASSWORD_FILE_OK, *buffer is a heap buffer (mem_free it) that holds the file, 0 terminated,
//and *password and *file point to the first and second line in it (NULL when the line is not there).
//on any other result *buffer, *password and *file are NULL
int ReadPasswordFile(char** buffer, char** password, char** file);

#endif
