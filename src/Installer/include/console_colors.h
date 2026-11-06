/*
Priiloader Installer - colours for the progress text.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef _CONSOLE_COLORS_H_
#define _CONSOLE_COLORS_H_

//The Mist theme's greys, plus one colour per outcome, so the steps of an install can be followed at a glance.
//libogc 3.1's console takes truecolor escapes (38;2;r;g;b). Each starts from a reset, so no attribute carries over.
#define COLOR_STEP		"\x1b[0m\x1b[38;2;232;232;232m"	//what is being done now
#define COLOR_INFO		"\x1b[0m\x1b[38;2;140;140;140m"	//details, or nothing to do
#define COLOR_OK		"\x1b[0m\x1b[38;2;120;220;140m"	//a step that worked
#define COLOR_WARN		"\x1b[0m\x1b[38;2;240;180;70m"	//it went on, but have a look
#define COLOR_DANGER	"\x1b[0m\x1b[38;2;255;90;90m"	//it failed: the Wii may need repair

#endif
