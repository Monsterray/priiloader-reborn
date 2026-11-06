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
#include <wiiuse/wpad.h>
#include <fat.h>
#include <sys/stat.h>
#include <ogc/lwp_watchdog.h>

//project includes
#include "title.h"
#include "nand.h"
#include "installer.h"
#include "gecko.h"
#include "IOS.hpp"
#include "Video.h"
#include "vWii.h"
#include "menu_logic.h"
#include "console_colors.h"

//rev version
#include "gitrev.h"
#include "version.h"

// Application binaries
#include "certs_bin.h"
#include "su_tmd.h"
#include "su_tik.h"

static const u64 _targetTitleId = 0x0000000100000002LL;

//Automatic mode: a command line argument (install, update or remove; see menu_logic.h) runs that action with no
//buttons, writes the outcome to RESULT_FILE and returns to the loader. Reloading IOS drops the network, so the
//SD card is how a script learns what happened. Without an argument the installer shows its menu as before.
#define RESULT_DIR	"sd:/priiloader"
#define RESULT_FILE	RESULT_DIR "/installer_result.txt"
static bool _automatic = false;
static std::string _automaticArgument;

static void WriteResult(bool ok, const char* message)
{
	if (!fatInitDefault())
	{
		gprintf("no SD card for the result");
		return;
	}

	//one line per field: the message without its line breaks
	std::string oneLine;
	for (const char* c = message; *c != '\0'; c++)
	{
		if (*c == '\r' || *c == '\n')
		{
			if (!oneLine.empty() && oneLine.back() != ' ')
				oneLine += ' ';
			continue;
		}
		oneLine += *c;
	}
	while (!oneLine.empty() && oneLine.back() == ' ')
		oneLine.pop_back();

	mkdir(RESULT_DIR, 0777);
	FILE* file = fopen(RESULT_FILE, "wb");
	if (file != NULL)
	{
		fprintf(file, "action=%s\nresult=%s\nmessage=%s\nversion=%d.%d.%d\nrev=0x%08x\n", _automaticArgument.c_str(),
			ok ? "ok" : "error", oneLine.c_str(), VERSION.major, VERSION.minor, VERSION.patch, GIT_REV);
		fclose(file);
	}
	else
		gprintf("failed to write " RESULT_FILE);
	fatUnmount("sd:");
}

bool UserYesNoStop()
{
	u16 pDown;
	u16 GCpDown;
	while(1)
	{
		WPAD_ScanPads();
		PAD_ScanPads();
		pDown = WPAD_ButtonsDown(0);
		GCpDown = PAD_ButtonsDown(0);
		if (pDown & WPAD_BUTTON_A || GCpDown & PAD_BUTTON_A)
		{
			return true;
		}
		if (pDown & WPAD_BUTTON_B || GCpDown & PAD_BUTTON_B)
		{
			return false;
		}
		if (pDown & WPAD_BUTTON_HOME || GCpDown & PAD_BUTTON_START)
		{
			return false;
		}
	}
	//it should never get here, but to kill that silly warning... :)
	return false;
}
//The end of every run: the message, then back to the loader. In automatic mode the result goes to the SD card
//and the installer returns by itself after 3 seconds; otherwise it waits for A.
static void Finish(bool ok, const char* msg, va_list args)
{
	char text[4096];
	vsnprintf(text, sizeof(text), msg, args);
	printf("%s", ok ? COLOR_OK : COLOR_DANGER);
	gprintf("%s -> %s", text, ok ? "done" : "aborting mission...");
	if (_automatic)
	{
		WriteResult(ok, text);
		printf("%s\n" COLOR_INFO "Returning to the loader in 3 seconds...\r\n", text);
		sleep(3);
	}
	else
	{
		printf("%s\n" COLOR_INFO "Press A to exit back to loader...\r\n", text);
		UserYesNoStop();
	}
	printf("exiting...\r\n");
	printf("\x1b[0m");
	VIDEO_WaitVSync();
	exit(0);
}
static void Done(const char* msg, ...)
{
	va_list args;
	va_start(args, msg);
	Finish(true, msg, args);
	va_end(args);
}
void abort(const char* msg, ...)
{
	va_list args;
	va_start(args, msg);
	Finish(false, msg, args);
	va_end(args);
}
bool HaveNandPermissions( void )
{
	gprintf("testing permissions...");
	s32 temp = ISFS_Open("/title/00000001/00000002/content/title.tmd",ISFS_OPEN_RW);
	if ( temp < 0 )
	{
		gprintf("no permissions.error %d",temp);
		return false;
	}
	else
	{
		ISFS_Close(temp);
		gprintf("and bingo was his name-O");
		return true;
	}
}
void ClearScreen()
{
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_WaitVSync();
	printf("\x1b[2J");
	fflush(stdout);
	return;
}

//The menu, in the Mist theme: grey text on black, a pale grey bar under the selected row, a strip of static under
//the title, and the row the bar leaves fading back to black (button 1 turns the fade off and on, to compare).
//Console rows and columns count from 1; a character is 8x16 pixels.
#define MENU_ROW		13
#define MENU_COL		14
#define MENU_WIDTH		52
#define MENU_ITEMS		3
#define TITLE_ROW		2
#define STATIC_ROW		4
#define STATIC_COL		5
#define STATIC_COLS		72

static void PrintGrey(int row, int col, u8 grey, const char* text)
{
	printf("\x1b[%d;%dH\x1b[0m\x1b[38;2;%u;%u;%um%s\x1b[0m", row, col, grey, grey, grey, text);
}

static void DrawMenuRow(int row, const char* text, MenuGrey grey)
{
	printf("\x1b[%d;%dH\x1b[48;2;%u;%u;%um\x1b[38;2;%u;%u;%um  %-*s\x1b[0m", row, MENU_COL, grey.bg, grey.bg, grey.bg,
		grey.fg, grey.fg, grey.fg, MENU_WIDTH - 2, text);
}

//a band of snow 4 pixels high across the middle of a console row, straight into xfb (CON_Init draws there too)
static void DrawStatic(int row, int col, int cols, u32& seed)
{
	int consoleX = 0;
	int consoleY = 0;
	GetConsoleOrigin(&consoleX, &consoleY);
	u32* framebuffer = reinterpret_cast<u32*>(xfb);
	const int pairsPerLine = rmode->fbWidth / 2;
	const int top = consoleY + (row - 1) * 16 + 6;
	const int left = (consoleX + (col - 1) * 8) / 2;
	for (int y = top; y < top + 4; y++)
	{
		for (int x = left; x < left + cols * 4; x++)
		{
			const u32 y1 = StaticLuma(seed);
			const u32 y2 = StaticLuma(seed);
			framebuffer[y * pairsPerLine + x] = (y1 << 24) | (0x80 << 16) | (y2 << 8) | 0x80;
		}
	}
}

static InstallerAction RunMenu(bool isvWii)
{
	char title[96];
#if RCVERSION > 0
	snprintf(title, sizeof(title), "Priiloader v%d.%d.%dRC%d (r0x%08x) installer", VERSION.major, VERSION.minor,
		VERSION.patch, VERSION.sub_version, GIT_REV);
#else
	snprintf(title, sizeof(title), "Priiloader v%d.%d.%d (r0x%08x) installer", VERSION.major, VERSION.minor,
		VERSION.patch, GIT_REV);
#endif
	char iosLine[64];
	snprintf(iosLine, sizeof(iosLine), "IOS %d rev %d%s", IOS_GetVersion(), IOS_GetRevision(), isvWii ? "  (vWii)" : "");

	const char* items[MENU_ITEMS] = {
		PriiloaderInstalled() ? "Update Priiloader" : "Install Priiloader",
		"Remove Priiloader and restore the System Menu",
		"Exit to the loader",
	};
	const InstallerAction actions[MENU_ITEMS] = { InstallerAction::Install, InstallerAction::Remove, InstallerAction::None };

	ClearScreen();
	PrintGrey(TITLE_ROW, STATIC_COL, 0xE8, title);
	PrintGrey(TITLE_ROW + 1, STATIC_COL, 0x5A, iosLine);
	PrintGrey(6, STATIC_COL, 0x6E, "This tool comes without any warranties. You install it at your own risk;");
	PrintGrey(7, STATIC_COL, 0x6E, "the authors can't be held liable for any damage it might cause.");
	PrintGrey(8, STATIC_COL, 0x6E, "If you don't agree with these terms, turn your Wii off.");
	PrintGrey(MENU_ROW + MENU_ITEMS + 2, STATIC_COL, 0x5A,
		"D-pad move   A select   + install   - remove   HOME exit   1 fade");

	int selected = 0;
	int fade[MENU_ITEMS];
	for (int i = 0; i < MENU_ITEMS; i++)
	{
		fade[i] = i == selected ? 0 : MENU_FADE_FRAMES;
		DrawMenuRow(MENU_ROW + i, items[i], MenuFadeGrey(fade[i]));
	}

	bool fadeOn = true;
	u32 seed = 0x2F6E2B1u ^ gettick();
	if (seed == 0)
		seed = 1;
	int titleDim = 0;
	while (true)
	{
		VIDEO_WaitVSync();
		WPAD_ScanPads();
		PAD_ScanPads();
		const u32 pDown = WPAD_ButtonsDown(0);
		const u16 GCpDown = PAD_ButtonsDown(0);

		//the old shortcuts still work
		if (pDown & WPAD_BUTTON_PLUS)
			return InstallerAction::Install;
		if (pDown & WPAD_BUTTON_MINUS || GCpDown & PAD_BUTTON_Y)
			return InstallerAction::Remove;
		if (pDown & WPAD_BUTTON_HOME || GCpDown & PAD_BUTTON_START)
			return InstallerAction::None;
		if (pDown & WPAD_BUTTON_A || GCpDown & PAD_BUTTON_A)
			return actions[selected];
		if (pDown & WPAD_BUTTON_1)
			fadeOn = !fadeOn;

		int delta = 0;
		if (pDown & WPAD_BUTTON_UP || GCpDown & PAD_BUTTON_UP)
			delta = -1;
		else if (pDown & WPAD_BUTTON_DOWN || GCpDown & PAD_BUTTON_DOWN)
			delta = 1;
		if (delta != 0)
		{
			const int previous = selected;
			selected = MenuMove(selected, MENU_ITEMS, delta);
			fade[previous] = fadeOn ? 1 : MENU_FADE_FRAMES;
			fade[selected] = 0;
			DrawMenuRow(MENU_ROW + previous, items[previous], MenuFadeGrey(fade[previous]));
			DrawMenuRow(MENU_ROW + selected, items[selected], MenuFadeGrey(fade[selected]));
		}

		for (int i = 0; i < MENU_ITEMS; i++)
		{
			if (i == selected || fade[i] <= 0 || fade[i] >= MENU_FADE_FRAMES)
				continue;
			fade[i]++;
			DrawMenuRow(MENU_ROW + i, items[i], MenuFadeGrey(fade[i]));
		}

		//the static, and now and then a flicker of the title
		DrawStatic(STATIC_ROW, STATIC_COL, STATIC_COLS, seed);
		if (titleDim == 0 && (seed & 0xFF) == 0)
		{
			titleDim = 4;
			PrintGrey(TITLE_ROW, STATIC_COL, 0x9A, title);
		}
		else if (titleDim > 0 && --titleDim == 0)
			PrintGrey(TITLE_ROW, STATIC_COL, 0xE8, title);
	}
}
void InitializeSystem()
{
	s32 dolphinFd = -1; 
	s32 ret = 0;

	//we reload IOS, losing all access rights we might have had,
	//but cleaning up all left over so we don't end up with possible shit from the loading application ( looking at you HBC! >_< )
	ReloadIOS(IOS_GetVersion(), 0);

	//we don't need it in dolphin though, despite it meaning we write stuff with the wrong UID...
	dolphinFd = IOS_Open("/dev/dolphin", IPC_OPEN_NONE); 
	if (dolphinFd >= 0)
	{
		IOS_Close(dolphinFd);
		gprintf("Dolphin detected. We don't need AHBPROT");
	}
	else
	{
		if( IOS_GetVersion() >= 200 || ( IOS_GetRevision() < 200 ) || ( IOS_GetRevision() > 0xFF01 ))
			throw "non-valid IOS or IOSPAGHETTI(Cios Infected)(" + std::to_string(IOS_GetVersion()) + ") detected";
		
		//patch and reload IOS 	
		//we will need to disable AHBPROT ourselves now, which we do with the sha exploit. 
		//drop it and hope for the best
		//use exploit to disable AHBPROT
		if(!DisableAHBProt())
			throw "Failed to disable AHBPROT";

		//check if it worked
		if(ReadRegister32(0x0d800064) != 0xFFFFFFFF)
		{
			ReloadIOS(IOS_GetVersion(), 0);
			throw "Failed to disable AHBPROT";
		}

		if (PatchIOS({ SetUidPatcher, NandAccessPatcher, FakeSignOldPatch, FakeSignPatch, EsIdentifyPatch }) < 0)
			throw "failed to patch IOS. is AHBPROT set correctly?";

		ret = ES_SetUID(_targetTitleId);
		gprintf("ES_SetUID : %d",ret);
		if(ret < 0)
		{
			//kinda unstable attempt to indentify as SM. it *seems* to work sometimes. but its nice if it does. dont know what triggers it to work tho :/
			//if it works, ES_Identify says ticket/tmd is invalid but identifes us as SM anyway X'D
			u32 keyId = 0;
			ret = ES_Identify( reinterpret_cast<const signed_blob*>(certs_bin), certs_bin_size, reinterpret_cast<const signed_blob*>(su_tmd), su_tmd_size, reinterpret_cast<const signed_blob*>(su_tik), su_tik_size, &keyId);
			gprintf("ES_Identify : %d",ret);
			if(ret < 0)
				throw "Failed to Identify as SU, error " + std::to_string(ret);
		}
	}

	u64 titleID = 0;
	ES_GetTitleID(&titleID);
	gprintf("identified as = 0x%08X%08X", TITLE_UPPER(titleID), TITLE_LOWER(titleID));

	if (ISFS_Initialize() < 0)
		throw "Failed to init ISFS";

	if(!HaveNandPermissions())
		throw "missing nand access on IOS" + std::to_string(IOS_GetVersion()) + ".is it patched?";

	ret = SHA_Init();
	if(ret < 0)
		throw "Failed to initialise SHA engine";
}
int main(int argc, char **argv)
{
	s32 ret;
	_automatic = ParseAutoAction(argc, argv) != AutoAction::None;
	if (_automatic)
		_automaticArgument = argv[1];
	CheckForGecko();
	InitVideoWithConsole();
	bool isvWii = CheckvWii();

	printf("IOS %d rev %d\t%s\r\n\r\n", IOS_GetVersion(), IOS_GetRevision(), isvWii ? "(vWii detected)" : "");

#if RCVERSION > 0
	printf("\t\tPriiloader v%d.%d.%dRC%d(r0x%08x) Installation/Removal Tool\n\n\n\n", VERSION.major, VERSION.minor, VERSION.patch, VERSION.sub_version, GIT_REV);
#else
	printf("\t\tPriiloader v%d.%d.%d(r0x%08x) Installation / Removal Tool\n\n\n\n", VERSION.major, VERSION.minor, VERSION.patch, GIT_REV);
#endif

	printf("\t                   PLEASE READ THIS CAREFULLY\n");
	printf("\t         THIS PROGRAM/TOOL COMES WITHOUT ANY WARRANTIES!\n");
	printf("\t        YOU ACCEPT THAT YOU INSTALL THIS AT YOUR OWN RISK\n");
	printf("\tTHE AUTHOR(S) CANNOT BE HELD LIABLE FOR ANY DAMAGE IT MIGHT CAUSE\n");
	printf("\t      IF YOU DO NOT AGREE WITH THESE TERMS TURN YOUR WII OFF\n\n\n\n");
	printf("\t                   Please wait while we init...");

	std::string msg = "";
	try
	{
		//init & investigate IOS
		InitializeSystem();

		//IOS Shit Done, prepare installer
		InitializeInstaller(_targetTitleId, isvWii);
	}
	catch (const std::string& ex)
	{
		msg = ex;
	}
	catch (char const* ex)
	{
		msg = std::string(ex);
	}
	catch(...)
	{
		msg = "Unknown Error Occurred";
	}

	WPAD_Init();
	PAD_Init();

	//did an error occur
	if(!msg.empty())
	{
		ClearScreen();
		abort("%s", msg.c_str());
	}

	InstallerAction action = InstallerAction::None;
	if (_automatic)
	{
		switch (ParseAutoAction(argc, argv))
		{
			case AutoAction::Install:
				action = InstallerAction::Install;
				break;
			case AutoAction::Update:
				if (!PriiloaderInstalled())
					abort("\r\nPriiloader not found: nothing to update\r\n");
				action = InstallerAction::Install;
				break;
			case AutoAction::Remove:
				action = InstallerAction::Remove;
				break;
			default:
				abort("\r\nUnknown argument \"%s\": use install, update or remove\r\n", _automaticArgument.c_str());
		}
		printf("\r\n\r\n\tAutomatic %s: no buttons needed.\r\n", _automaticArgument.c_str());
	}
	else
	{
		//wait a bit for the system to settle and have all unexpected events to have happened (if any)
		sleep(3);
		action = RunMenu(isvWii);
	}

	ClearScreen();
	printf(COLOR_INFO "IOS %d rev %d\t%s\r\n\r\n" COLOR_STEP, IOS_GetVersion(), IOS_GetRevision(), isvWii ? "(vWii detected)" : "");
	try
	{
		const char* installMsg;
		switch(action)
		{
			case InstallerAction::Install:
#if RCVERSION > 0
				if (!_automatic)
				{
				printf(COLOR_WARN);
				printf("\nWARNING : ");
				printf(COLOR_STEP);
				printf("this is a Release Canidate. are you SURE you want to install this?\n+/A to confirm, Home/Start to abort\r\n");
				sleep(1);
				if(!UserYesNoStop())
					abort("user cancelled");
				}
#endif
				action = PriiloaderInstalled() ? InstallerAction::Update : InstallerAction::Install;
				installMsg = action == InstallerAction::Update 
					? "Priiloader installation found.\r\nUpdating Priiloader...\r\n"
					: "Priiloader not found.\r\nInstalling Priiloader...\r\n";
				printf("%s", installMsg);
				CopyTicket();
				WritePriiloader(action);
				ret = PatchTMD(action);
				if(ret < 0)
					abort("\npatching TMD error %d!\r\n",ret);

				DeletePriiloaderFiles(action);
				Done("\r\n%s\r\n", action == InstallerAction::Update ? "Update done!" : "Install done!");
				break;
			case InstallerAction::Remove:
				if(!PriiloaderInstalled())
					abort("Priiloader not found");
				
				printf("Priiloader installation found.\n\r\n");
				RemovePriiloader();
				PatchTMD(action);
				DeletePriiloaderFiles(action);
				printf("Done!\r\n\r\n");
				Done("Removal done.\r\n");
				break;
			case InstallerAction::None:
			default:		
				abort("Installation cancelled");
		}
	}
	catch (const std::string& ex)
	{
		abort("%s", ex.c_str());
	}
	catch (char const* ex)
	{
		abort(ex);
	}
	catch(...)
	{
		abort("Unknown Error Occurred");
	}

	return 0;
}
