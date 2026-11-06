/*

priiloader - A tool which allows to change the default boot up sequence on the Wii console

Copyright (C) 2008-2019  DacoTaco

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
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <string.h>
#include <vector>
#include <unistd.h>
#include <algorithm>

#include "gecko.h"
#include "hacks.h"
#include "settings.h"
#include "error.h"
#include "mem2_manager.h"
#include "mount.h"

std::vector<system_hack> system_hacks;
std::vector<u8> states_hash;

FILE* fat_file_handler = NULL;
s32 nand_file_handler = -1;

s32 GetMasterHackIndexByID(const std::string& ID )
{
	s32 index = -1;
	u32 sysver = GetSysMenuVersion();
	auto it = std::find_if(system_hacks.begin(), system_hacks.end(), [&](const system_hack& obj) 
		{
			return obj.masterID == ID && 
			obj.max_version >= sysver &&
			obj.min_version <= sysver;
		});
	// if we have an iterator , then we will retrieve the index of the iterator
	if (it != system_hacks.end())  
		index = std::distance(system_hacks.begin(), it);
	return index;
}

std::string trim(const std::string& str)
{
    size_t first = str.find_first_not_of("\t ");
    if (std::string::npos == first)
        return str;

    size_t last = str.find_last_not_of("\t ");
    return str.substr(first, (last - first + 1));
}

//the longest line (and the biggest block of text without a newline) we accept
#define HACKS_MAX_LINE 0x2000
//the biggest hacks file we read. the real one is about 30 KB. a bigger file is not parsed: the System Menu then boots without hacks
#define HACKS_MAX_FILE_SIZE (1024*1024)
#define HACKS_BLOCK_SIZE 32

//we use our own line reader instead of ifstream because ifstream would increase priiloader's size with 300KB...
//the file is read into memory once, this reads the next line from it. it keeps the behaviour of the reader that used to read
//the file in 32 byte blocks from NAND/SD: the text read for a line is cut at the first NUL, a line ending is \n or \r
//(an empty first segment falls back to the \r split), and a line of HACKS_MAX_LINE bytes or more ends the reading.
//pos is moved past the line; returns false at the end of the data, on an empty line or on a line that is too long
static bool GetLine(const u8* data, u32 size, u32& pos, std::string& line)
{
	line.clear();
	const u32 start = pos;
	u32 read_cnt = 0;
	u32 scanned = 0;		//bytes of the read text looked at for a line ending
	bool ending = false;	//a \n or \r was seen before any NUL
	bool nul = false;		//a NUL was seen: the rest is hidden, like it was from strstr

	while( start < size && start + read_cnt < size && !ending )
	{
		//are we dealing with a potential overflow?
		if( read_cnt >= HACKS_MAX_LINE )
		{
			gprintf("GetLine Exception : line to long");
			return false;
		}
		u32 len = size - (start + read_cnt);
		if( len > HACKS_BLOCK_SIZE )
			len = HACKS_BLOCK_SIZE;
		if( len > HACKS_MAX_LINE - read_cnt )
			len = HACKS_MAX_LINE - read_cnt;
		read_cnt += len;
		while( scanned < read_cnt && !nul && !ending )
		{
			u8 c = data[start + scanned];
			if( c == 0 )
				nul = true;
			else
			{
				scanned++;
				if( c == '\n' || c == '\r' )
					ending = true;
			}
		}
		//the text, up to its first NUL, is as long as the longest line there can be
		if( read_cnt >= HACKS_MAX_LINE && memchr(data + start, 0, read_cnt) == NULL )
		{
			gprintf("GetLine Exception : buf has overflown");
			return false;
		}
	}
	//the text up to the first NUL, or all of what was read
	u32 text_len = read_cnt;
	const u8* nulpos = (read_cnt > 0) ? static_cast<const u8*>(memchr(data + start, 0, read_cnt)) : NULL;
	if( nulpos != NULL )
		text_len = static_cast<u32>(nulpos - (data + start));
	std::string read_line(reinterpret_cast<const char*>(data + start), text_len);

	//find the newline and split the string
	std::string cut_string = read_line.substr(0,read_line.find("\n"));
	if(cut_string.length() == 0)
		cut_string = read_line.substr(0,read_line.find("\r"));
	//set the new position untill after the \r\n, \r or \n
	pos = start + static_cast<u32>(cut_string.size()) + 1;
	//cut off any carriage returns
	if(cut_string.size() > 0 && cut_string.back() == '\r')
		cut_string.pop_back();
	if(cut_string.size() > 0 && cut_string.front() == '\r')
		cut_string.erase(0, 1);
	line = cut_string;
	return line.length() > 0;
}

//reads the whole file in one go. the buffer has a NUL after the data. returns NULL (error is set when it ran out of memory)
//when the file cannot be read in full
static u8* ReadHacksFile(bool reading_nand, u32 fileSize)
{
	u8* buf = static_cast<u8*>(mem_align(32, ALIGN32(fileSize+1)));
	if (!buf)
	{
		error = ERROR_MALLOC;
		gprintf("LoadHacks : could not allocate %u bytes", fileSize);
		return NULL;
	}
	memset(buf, 0, ALIGN32(fileSize+1));
	s32 ret = -1;
	if (reading_nand)
	{
		ret = ISFS_Read(nand_file_handler, buf, fileSize);
	}
	else
	{
		size_t cnt = fread(buf, 1, fileSize, fat_file_handler);
		ret = static_cast<s32>(cnt);
	}
	if (ret != static_cast<s32>(fileSize))
	{
		gprintf("LoadHacks : reading the hacks file failed (%d of %u bytes)", ret, fileSize);
		mem_free(buf);
		return NULL;
	}
	DCFlushRange(buf, ALIGN32(fileSize+1));
	return buf;
}

bool _processLine(system_hack& hack, std::string &line)
{
	try
	{
		gdprintf("processing '%s'",line.c_str());

		//if the line has a comment we will strip everything after the comment character
		auto commentPos = line.find("#");
		if(commentPos != std::string::npos)
			line.erase(commentPos);

		//trim all trailing spaces. if we end up with an empty line we just return
		line = trim(line);
		if(line.size() == 0) 
			return true;

		if(line.front() == '[' && line.back() == ']')
		{
			line = line.substr(1,line.length()-2);
			hack.desc = line;
			return true;
		}

		if(line.find("=") == std::string::npos)
			throw "Invalid line";	

		std::string type = line.substr(0,line.find("="));
		std::string value = line.substr(line.find("=")+1);
		system_patch* temp_patch;

		if(hack.patches.size() <= 0)
		{
			hack.patches.push_back(system_patch());
		}
		temp_patch = &hack.patches.back();

		if(type.length() == 0 || value.size() == 0)
			throw "failed to split line";

		if(type == "amount")
		{
			//not needed line, but we used to have it. we do nothing with it though...
			//hack.amount = strtoul(values.front().c_str(),NULL, 10);
		}
		else if(type == "master")
		{
			// Hacks with a "master" parameter are hidden, and can be enabled
			// by other hacks using the "require" parameter
			hack.masterID = value;
			return true; 
		}
		else if(type == "require")
		{
			hack.requiredID = value;
			return true; 
		}
		else if(type == "maxversion")
		{
			hack.max_version = strtoul(value.c_str(),NULL, 10);
			return true;
		}
		else if(type == "minversion")
		{
			hack.min_version = strtoul(value.c_str(),NULL, 10);
			return true;
		}
		else if(type == "patch" || type == "hash")
		{
			//process patch or hash
			// patch example : patch=0x38000001,0x2c000000,0x900DA5D8,0x38000032
			//  hash example : hash=0x38000000,0x2c000000,0x40820010,0x38000036,0x900da9c8,0x480017
			//should get in the struct as 0x380000002c0000004082001038000036900da9c8480017

			//start new patch if the last patch is already a complete one
			if(	type == "hash" && 
				temp_patch->patch.size() > 0 && 
				(
					temp_patch->hash.size() > 0 || 
					temp_patch->offset != 0 
				)
			  )
			{
				hack.patches.push_back(system_patch());
				temp_patch = &hack.patches.back();
			}

			//split the value string into seperate bits
			while(value.length() > 0)
			{			
				std::string segment;
				if(value.find(",") != std::string::npos)
				{
					segment = value.substr(0,value.find(","));
					value = value.substr(value.find(",")+1);
				}
				else
				{
					segment = value;
					value.clear();
				}			

				while(segment.find(" ") != std::string::npos)
						segment = segment.replace(segment.find(" "),1,"");	

				if(segment.length() < 4 || segment.length() > 10 || segment.compare(0,2,"0x"))
					throw "Invalid " + type + " : " + segment;

				//cut off the '0x'
				segment = segment.substr(2);

				while(segment.size() > 0)
				{
					std::string subSegment;
					if(segment.size() > 2)
					{
						subSegment = segment.substr(0,2);
						segment = segment.substr(2);
					}
					else
					{
						subSegment = segment;
						segment.clear();
					}

					if(subSegment.substr(2) != "0x")
						subSegment.insert(0,"0x");

					if(type == "patch")
						temp_patch->patch.push_back(strtoul(subSegment.c_str(),NULL, 16));
					else if(type == "hash")
						temp_patch->hash.push_back(strtoul(subSegment.c_str(),NULL, 16));					
				}
			}
		}
		else if(type == "offset")
		{
			//process offset
			//example : offset=0x81000000
			if(value.size() != 10)
				throw ("Invalid offset : " + std::to_string(value.size()));

			//save the old patch and start a new one.
			if(temp_patch->patch.size() > 0 && ( temp_patch->hash.size() > 0 || temp_patch->offset != 0 ))
			{
				hack.patches.push_back(system_patch());
				temp_patch = &hack.patches.back();
			}

			u32 offset = strtoul(value.c_str(),NULL, 16);
			temp_patch->offset = offset;
		}
		else
		{
			throw "unknown type : " + type;
		}
		return true;
	}
	catch (const std::string& ex)
	{
		gprintf("_processLine Exception: %s",ex.c_str());
	}
	catch (char const* ex)
	{
		gprintf("_processLine Exception: %s",ex);
	}
	catch(...)
	{
		gprintf("_processLine General Exception: invalid line");		
	}

	return false;
}
bool _addOrRejectHack(system_hack& hack)
{
	try
	{
		if(
			hack.min_version > 0 &&
			hack.max_version > 0 &&
			hack.desc.length() > 0 &&
			hack.patches.size() > 0 &&
			( hack.patches.back().hash.size() > 0 || hack.patches.back().offset > 0) && 
			hack.patches.back().patch.size() > 0 &&
			( hack.requiredID.size() == 0 || GetMasterHackIndexByID(hack.requiredID) >= 0 )
			)
		{			
			gdprintf("loaded %s (v%d - v%d)",hack.desc.c_str(),hack.min_version,hack.max_version);
			system_hacks.push_back(hack);
			return true;
		}

		gdprintf("dropping invalid hack %s",hack.desc.c_str());
		if( hack.requiredID.size() != 0 && GetMasterHackIndexByID(hack.requiredID) < 0 )
			gdprintf("did not find ID");
		return false;
	}
	catch(...)
	{
		gprintf("General _addOrRejectHack exception thrown");
		return false;
	}
}
//which hacks are on, one name per line. a hack list that changes order or size keeps the right hacks on
#define STATES_FILE "/title/00000001/00000002/data/hackstat.ini"
//the old states file: one byte per hack, by position in the hack list. only read to move its states to STATES_FILE
#define LEGACY_STATES_FILE "/title/00000001/00000002/data/hacksh_s.ini"
#define STATES_MAX_FILE_SIZE (64*1024)

//reads a whole NAND file into a buffer with a NUL after the data. found says whether the file exists.
//returns NULL when it is missing, empty, too big or cannot be read
static u8* _readNandFile(const char* path, u32 maxSize, bool& found, u32& size)
{
	found = false;
	size = 0;
	s32 fd = ISFS_Open(path, 1);
	if (fd < 0)
		return NULL;
	found = true;

	STACK_ALIGN(fstats, status, 1, 32);
	memset(status, 0, sizeof(fstats));
	u8* buf = NULL;
	if (ISFS_GetFileStats(fd, status) >= 0 && status->file_length > 0 && status->file_length <= maxSize)
	{
		const u32 length = status->file_length;
		buf = static_cast<u8*>(mem_align(32, ALIGN32(length + 1)));
		if (buf == NULL)
			error = ERROR_MALLOC;
		else
		{
			memset(buf, 0, ALIGN32(length + 1));
			if (ISFS_Read(fd, buf, length) == static_cast<s32>(length))
				size = length;
			else
			{
				mem_free(buf);
				buf = NULL;
			}
		}
	}
	ISFS_Close(fd);
	return buf;
}

//the names in a states file: one per line, '#' starts a comment line (a hack name cannot hold a '#')
static std::vector<std::string> _parseStateNames(const u8* data, u32 size)
{
	std::vector<std::string> names;
	const char* text = reinterpret_cast<const char*>(data);
	const u32 length = static_cast<u32>(strnlen(text, size));
	u32 start = 0;
	while (start < length)
	{
		u32 end = start;
		while (end < length && text[end] != '\n')
			end++;
		std::string name(text + start, end - start);
		if (!name.empty() && name.back() == '\r')
			name.pop_back();
		name = trim(name);
		if (!name.empty() && name.front() != '#' && std::find(names.begin(), names.end(), name) == names.end())
			names.push_back(name);
		start = end + 1;
	}
	return names;
}

static s8 _parseSystemHacks(StorageDevice source);

//the names of the hacks that were on in the old states file. its positions belong to the hack list on NAND (hackshas.ini),
//the one saved with it, whichever list is loaded now (the hacks menu reads the card's first)
static bool _legacyStateNames(StorageDevice source, const u8* data, u32 size, std::vector<std::string>& names)
{
	std::vector<system_hack> nandHacks;
	if (source == StorageDevice::NAND)
		nandHacks = system_hacks;
	else
	{
		std::vector<system_hack> shown;
		std::vector<u8> shownStates;
		shown.swap(system_hacks);
		shownStates.swap(states_hash);
		if (_parseSystemHacks(StorageDevice::NAND) != 0)
			nandHacks.swap(system_hacks);
		system_hacks.swap(shown);
		states_hash.swap(shownStates);
	}

	if (size != nandHacks.size())
	{
		gprintf("LoadHacks : the old states file has %u states for %u hacks on NAND: all hacks are off", size, (u32)nandHacks.size());
		return false;
	}

	//only the hacks for this System Menu: a state left on for another version must not turn on this version's hack of that name
	const u32 sysver = GetSysMenuVersion();
	for (u32 i = 0; i < size; i++)
	{
		const system_hack& hack = nandHacks[i];
		if (data[i] && hack.masterID.length() == 0 && hack.min_version <= sysver && hack.max_version >= sysver)
			names.push_back(hack.desc);
	}
	return true;
}

static void _loadHackStates(StorageDevice source)
{
	states_hash.assign(system_hacks.size(), 0);

	bool found = false;
	bool migrate = false;
	u32 size = 0;
	std::vector<std::string> names;
	u8* data = _readNandFile(STATES_FILE, STATES_MAX_FILE_SIZE, found, size);
	if (found)
	{
		if (data != NULL)
			names = _parseStateNames(data, size);
	}
	else
	{
		//an older priiloader kept the states by position: moved over by name once, then saved by name
		data = _readNandFile(LEGACY_STATES_FILE, STATES_MAX_FILE_SIZE, found, size);
		if (data != NULL)
			migrate = _legacyStateNames(source, data, size, names);
	}
	if (data != NULL)
		mem_free(data);

	for (u32 i = 0; i < system_hacks.size(); i++)
	{
		if (std::find(names.begin(), names.end(), system_hacks[i].desc) != names.end())
			states_hash[i] = 1;
	}

	//hacks for another System Menu version are off, and so are master hacks: they are turned on when a hack requires one
	const u32 sysver = GetSysMenuVersion();
	for (u32 i = 0; i < system_hacks.size(); i++)
	{
		if (system_hacks[i].masterID.length() > 0 ||
			system_hacks[i].max_version < sysver ||
			system_hacks[i].min_version > sysver)
		{
			states_hash[i] = 0;
		}
	}

	if (migrate)
	{
		const s32 ret = SaveHackStates();
		gprintf("LoadHacks : moved the hack states to hackstat.ini (%d)", ret);
	}
}

s32 SaveHackStates(void)
{
	std::string text = "# priiloader: the System Menu hacks that are on, one name per line\n";
	std::vector<std::string> saved;
	const u32 count = std::min(states_hash.size(), system_hacks.size());
	for (u32 i = 0; i < count; i++)
	{
		const std::string& name = system_hacks[i].desc;
		if (states_hash[i] == 0 || system_hacks[i].masterID.length() > 0 || std::find(saved.begin(), saved.end(), name) != saved.end())
			continue;
		saved.push_back(name);
		text += name + "\n";
	}

	const u32 length = static_cast<u32>(text.size());
	u8* buf = static_cast<u8*>(mem_align(32, ALIGN32(length)));
	if (buf == NULL)
	{
		error = ERROR_MALLOC;
		return -1;
	}
	memcpy(buf, text.data(), length);

	//a lost write leaves no states file, and then every hack is off: the safe way round
	ISFS_Delete(STATES_FILE);
	s32 ret = ISFS_CreateFile(STATES_FILE, 0, 3, 3, 3);
	if (ret >= 0)
	{
		s32 fd = ISFS_Open(STATES_FILE, 2);
		if (fd < 0)
			ret = fd;
		else
		{
			ret = ISFS_Write(fd, buf, length);
			ISFS_Close(fd);
			ret = (ret == static_cast<s32>(length)) ? 0 : (ret < 0 ? ret : -1);
		}
	}
	mem_free(buf);

	//the old file must not outlive the names: an older priiloader would read its positions against a newer hack list
	if (ret == 0)
		ISFS_Delete(LEGACY_STATES_FILE);
	return ret;
}

static s8 _loadSystemHacks(StorageDevice source);
s8 LoadSystemHacks(StorageDevice source)
{
	s8 ret = _loadSystemHacks(source);

	//callers index states_hash with the index of a hack. whatever way the load ended, there is one state per hack
	states_hash.resize(system_hacks.size(), 0);

	//the early returns leave the states file open
	if (nand_file_handler >= 0)
	{
		ISFS_Close(nand_file_handler);
		nand_file_handler = -1;
	}
	return ret;
}

//reads and parses a hacks file into system_hacks, without the states. returns 0 when there is no usable file
static s8 _parseSystemHacks(StorageDevice source)
{
	//system_hacks already loaded
	if (system_hacks.size() || states_hash.size()) 
	{
		system_hacks.clear();
		states_hash.clear();
	}

	//clear any possible open handlers
	if (nand_file_handler >= 0)
	{
		ISFS_Close(nand_file_handler);
		nand_file_handler = -1;
	}
	if (fat_file_handler != NULL)
	{
		fclose(fat_file_handler);
		fat_file_handler = NULL;
	}

	//read the hacks file size
	STACK_ALIGN(fstats, status, 1, 32);
	memset(status, 0, sizeof(fstats));
	u32 fileSize = 0;
	switch(source)
	{
		case StorageDevice::Auto:		
		default:
			fat_file_handler = fopen(BuildPath("/apps/priiloader/hacks_hash.ini", source).c_str(),"rb");
			if(!fat_file_handler)
			{
				gprintf("fopen error : %s", strerror(errno));
			}
			else
			{
				fseek( fat_file_handler, 0, SEEK_END );
				fileSize = ftell(fat_file_handler);
				fseek( fat_file_handler, 0, SEEK_SET);
			}
			break;
		case StorageDevice::NAND:
			nand_file_handler = ISFS_Open("/title/00000001/00000002/data/hackshas.ini", 1);
			if (nand_file_handler < 0)
			{
				gprintf("LoadHacks : hacks_hash.ini not on FAT or Nand. ISFS_Open error %d", nand_file_handler);
				break;
			}

			ISFS_GetFileStats(nand_file_handler, status);
			fileSize = status->file_length;
			break;
	}

	if (fileSize == 0)
	{
		if (source != StorageDevice::NAND)
			gprintf("Error \"hacks_hash.ini\" is 0 byte!");

		if (source == StorageDevice::NAND)
		{
			if (nand_file_handler >= 0)
				ISFS_Close(nand_file_handler);
			nand_file_handler = -1;
		}			
		else
		{
			if(fat_file_handler)
				fclose(fat_file_handler);
			fat_file_handler = NULL;
		}

		return 0;
	}

	if (fileSize > HACKS_MAX_FILE_SIZE)
	{
		gprintf("LoadHacks : the hacks file is too big (%u bytes)", fileSize);
		fileSize = 0;
	}
	//read the whole file in one go, close it and parse from memory
	u8* fileData = (fileSize > 0) ? ReadHacksFile(source == StorageDevice::NAND, fileSize) : NULL;
	if (source == StorageDevice::NAND)
	{
		if (nand_file_handler >= 0)
			ISFS_Close(nand_file_handler);
		nand_file_handler = -1;
	}
	else
	{
		if (fat_file_handler)
			fclose(fat_file_handler);
		fat_file_handler = NULL;
	}
	if (fileData == NULL)
		return 0;

	//Process File
	std::string line;
	u32 filePos = 0;
	system_hack new_hack;
	while (GetLine(fileData, fileSize, filePos, line))
	{
		//Specs of this loop/function : 
		// - read the line and put it in the correct part of the hack
		// - if we read a description while we already have one, we will check if the hack is complete, and push it to the list if it is
		//																								-> if it isn't complete, drop it
		// - the same will be done with patches
		// - if hash or offset is found -> we don't have a patch yet		-> drop patch and start anew
		//								-> we have a patch					-> we start a new one
		// - if patch is found			-> and we don't have a hash/offset	-> ignore
		//								-> we have a hash/offset			-> append
		// - amount will be ignored. its not really needed...
		//_processLine will place everything in the correct spot in the struct/class & add new patches if needed

		//new hack? process previous one and add it
		if(line.front() == '[' && line.back() == ']')
		{
			_addOrRejectHack(new_hack);
			//new_hack.amount = 0;
			new_hack.desc.clear();
			new_hack.masterID.clear(); 
			new_hack.requiredID.clear(); 
			new_hack.max_version = 0;
			new_hack.min_version = 0;
			new_hack.patches.clear();
		}

		_processLine(new_hack,line);
	}

	//add or reject the last hack we were working on
	_addOrRejectHack(new_hack);

	mem_free(fileData);
	return 1;
}

static s8 _loadSystemHacks(StorageDevice source)
{
	if (_parseSystemHacks(source) == 0)
		return 0;
	_loadHackStates(source);
	return 1;
}
