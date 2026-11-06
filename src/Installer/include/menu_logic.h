/*
Priiloader Installer - menu and command line logic, with no hardware in it so the host tests can run it.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef _MENU_LOGIC_H_
#define _MENU_LOGIC_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//What the installer was asked to do on its command line, so a script can run it with no buttons:
//  Installer.dol install | update | remove
//None: no argument (a person at the menu). Invalid: an argument that is none of those; the installer then
//refuses and changes nothing. Upper and lower case are the same.
enum class AutoAction { None, Install, Update, Remove, Invalid };

static inline AutoAction ParseAutoAction(int argc, const char* const* argv)
{
	if (argc < 2 || argv == NULL || argv[1] == NULL || argv[1][0] == '\0')
		return AutoAction::None;

	char word[8];
	size_t len = strnlen(argv[1], sizeof(word));
	if (len >= sizeof(word))
		return AutoAction::Invalid;
	for (size_t i = 0; i < len; i++)
	{
		char c = argv[1][i];
		word[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}
	word[len] = '\0';

	if (strcmp(word, "install") == 0)
		return AutoAction::Install;
	if (strcmp(word, "update") == 0)
		return AutoAction::Update;
	if (strcmp(word, "remove") == 0)
		return AutoAction::Remove;
	return AutoAction::Invalid;
}

//Moves the highlight by delta rows, wrapping at both ends.
static inline int MenuMove(int selected, int count, int delta)
{
	if (count <= 0)
		return 0;
	int next = (selected + delta) % count;
	return next < 0 ? next + count : next;
}

//The Mist theme: a pale grey bar under the selected row; the other rows are grey text on black.
#define MENU_BAR_BG		0xD8
#define MENU_BAR_FG		0x10
#define MENU_IDLE_BG	0x00
#define MENU_IDLE_FG	0x8C
//frames a row takes to fade from the bar back to idle when the highlight leaves it (60 per second)
#define MENU_FADE_FRAMES	24

struct MenuGrey
{
	uint8_t bg;
	uint8_t fg;
};

//The colours of a row step frames after the highlight left it: the bar at 0, idle at MENU_FADE_FRAMES and after.
//A negative step is the bar.
static inline MenuGrey MenuFadeGrey(int step)
{
	if (step <= 0)
		return { MENU_BAR_BG, MENU_BAR_FG };
	if (step >= MENU_FADE_FRAMES)
		return { MENU_IDLE_BG, MENU_IDLE_FG };
	MenuGrey grey;
	grey.bg = (uint8_t)(MENU_BAR_BG + (MENU_IDLE_BG - MENU_BAR_BG) * step / MENU_FADE_FRAMES);
	grey.fg = (uint8_t)(MENU_BAR_FG + (MENU_IDLE_FG - MENU_BAR_FG) * step / MENU_FADE_FRAMES);
	return grey;
}

//The static: one luma value (16 is black on the Wii's video, 235 white) per call. Mostly dark grey snow,
//now and then a brighter speck. seed must not be 0.
static inline uint8_t StaticLuma(uint32_t& seed)
{
	//xorshift32
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	if ((seed & 0x3F) == 0)
		return (uint8_t)(0x70 + ((seed >> 8) & 0x3F));
	return (uint8_t)(16 + ((seed >> 8) & 0x1F));
}

#endif
