/*

priiloader-reborn - parsing of main.nfo, the argument file of the autoboot DOL, and
building of the argument strings of a DOL.

This file is part of priiloader and is licensed under the GNU General Public License
version 2, like the rest of it. It has no libogc dependency so a host test builds it.

*/

#ifndef _NFO_H_
#define _NFO_H_

#include <stdint.h>
#include <string>
#include <vector>

//main.nfo : s8 HW_AHBPROT_bit, u8 argument_count, s32 arg_cli_length (big endian), then arg_cli_length bytes of arguments
#define NFO_HEADER_SIZE			6
//longest argument block priiloader writes or accepts
#define NFO_MAX_ARG_LENGTH		8192
//longest single argument that is kept
#define NFO_MAX_ARG_SIZE		128

enum {
	NFO_OK = 0,
	NFO_TOO_SMALL = -1,
	NFO_BAD_AHBPROT = -2,
	NFO_BAD_LENGTH = -3
};

//checks the 6 header bytes against the size of the file. on NFO_OK, *arg_length is how many argument bytes
//follow (0 when there are no arguments) and is never more than file_length - NFO_HEADER_SIZE
int NfoParseHeader(const uint8_t* header, uint32_t file_length, int8_t* ahbprot, uint8_t* argument_count, int32_t* arg_length);

//the number of arguments that take part (empty ones are skipped)
uint32_t NfoArgumentCount(const std::vector<std::string>& args);
//the bytes needed for the arguments, each one cut to NFO_MAX_ARG_SIZE and followed by a 0
uint32_t NfoArgumentsLength(const std::vector<std::string>& args);
//writes the arguments as NfoArgumentsLength() bytes to dest, which must be that large. returns the bytes written
uint32_t NfoFillArguments(char* dest, const std::vector<std::string>& args);

#endif
