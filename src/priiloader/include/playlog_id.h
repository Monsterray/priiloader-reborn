#ifndef _PLAYLOG_ID_H_
#define _PLAYLOG_ID_H_

#include <stdint.h>

//The 6 byte title id of play_rec.dat (Playlog_Update's ID[6]): the four ASCII characters of the
//lower half of the title id (the game id, "HAXX" for 0x48415858), most significant byte first,
//then two zero bytes.
static inline void BuildPlaylogId(uint32_t titleIdLower, char id[6])
{
	id[0] = (char)((titleIdLower >> 24) & 0xFF);
	id[1] = (char)((titleIdLower >> 16) & 0xFF);
	id[2] = (char)((titleIdLower >> 8) & 0xFF);
	id[3] = (char)(titleIdLower & 0xFF);
	id[4] = 0;
	id[5] = 0;
}

#endif
