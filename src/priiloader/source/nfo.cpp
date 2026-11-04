/*

priiloader-reborn - parsing of main.nfo, the argument file of the autoboot DOL, and
building of the argument strings of a DOL.

This file is part of priiloader and is licensed under the GNU General Public License
version 2, like the rest of it. It has no libogc dependency so a host test builds it.

*/

#include <string.h>
#include "nfo.h"

int NfoParseHeader(const uint8_t* header, uint32_t file_length, int8_t* ahbprot, uint8_t* argument_count, int32_t* arg_length)
{
	if(header == NULL || file_length < NFO_HEADER_SIZE)
		return NFO_TOO_SMALL;

	//the length is stored the way the PPC keeps it : big endian
	int32_t length = (int32_t)(((uint32_t)header[2] << 24) | ((uint32_t)header[3] << 16) | ((uint32_t)header[4] << 8) | (uint32_t)header[5]);

	if(header[0] != 0 && header[0] != 1)
		return NFO_BAD_AHBPROT;

	//without arguments the length is not used
	if(header[1] == 0)
		length = 0;
	else if(length <= 0 || length > NFO_MAX_ARG_LENGTH || (uint32_t)length > file_length - NFO_HEADER_SIZE)
		return NFO_BAD_LENGTH;

	*ahbprot = (int8_t)header[0];
	*argument_count = header[1];
	*arg_length = length;
	return NFO_OK;
}

static uint32_t _argSize(const std::string& arg)
{
	return strnlen(arg.c_str(), NFO_MAX_ARG_SIZE);
}

uint32_t NfoArgumentCount(const std::vector<std::string>& args)
{
	uint32_t count = 0;
	for(const std::string& arg : args)
		if(arg.size() > 0)
			count++;
	return count;
}

uint32_t NfoArgumentsLength(const std::vector<std::string>& args)
{
	uint32_t length = 0;
	for(const std::string& arg : args)
		if(arg.size() > 0)
			length += _argSize(arg) + 1;
	return length;
}

uint32_t NfoFillArguments(char* dest, const std::vector<std::string>& args)
{
	uint32_t pos = 0;
	for(const std::string& arg : args)
	{
		if(arg.size() == 0)
			continue;
		uint32_t size = _argSize(arg);
		memcpy(dest + pos, arg.c_str(), size);
		dest[pos + size] = '\0';
		pos += size + 1;
	}
	return pos;
}
