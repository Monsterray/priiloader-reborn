/*
 * test_menu.cpp -- host tests of src/Installer/include/menu_logic.h: the command line of the automatic mode,
 * the menu's highlight, its fade and the static.
 */
#define HOSTCHECK_MAIN
#include "../hostcheck.h"

#include <stdint.h>

#include "menu_logic.h"

static AutoAction parse(const char* arg)
{
	const char* argv[] = { "sd:/apps/priiloader/boot.dol", arg, NULL };
	return ParseAutoAction(arg == NULL ? 1 : 2, argv);
}

static void test_no_argument_is_the_menu(void)
{
	CHECK(parse(NULL) == AutoAction::None, "no argument");
	CHECK(parse("") == AutoAction::None, "empty argument");
	CHECK(ParseAutoAction(0, NULL) == AutoAction::None, "argc 0");
	const char* argv[] = { "boot.dol", NULL };
	CHECK(ParseAutoAction(2, argv) == AutoAction::None, "argv[1] NULL");
}

static void test_actions(void)
{
	CHECK(parse("install") == AutoAction::Install, "install");
	CHECK(parse("update") == AutoAction::Update, "update");
	CHECK(parse("remove") == AutoAction::Remove, "remove");
	CHECK(parse("UPDATE") == AutoAction::Update, "upper case");
	CHECK(parse("Remove") == AutoAction::Remove, "mixed case");
}

static void test_anything_else_is_refused(void)
{
	CHECK(parse("updat") == AutoAction::Invalid, "prefix");
	CHECK(parse("updates") == AutoAction::Invalid, "longer");
	CHECK(parse("install ") == AutoAction::Invalid, "trailing space");
	CHECK(parse("uninstall") == AutoAction::Invalid, "too long for the buffer");
	CHECK(parse("delete") == AutoAction::Invalid, "other word");
	CHECK(parse("-") == AutoAction::Invalid, "dash");
}

static void test_menu_wraps(void)
{
	CHECK(MenuMove(0, 3, 1) == 1, "down");
	CHECK(MenuMove(2, 3, 1) == 0, "down from the last row wraps to the first");
	CHECK(MenuMove(0, 3, -1) == 2, "up from the first row wraps to the last");
	CHECK(MenuMove(1, 3, -1) == 0, "up");
	CHECK(MenuMove(0, 0, 1) == 0, "no rows");
}

static void test_fade(void)
{
	MenuGrey bar = MenuFadeGrey(0);
	MenuGrey idle = MenuFadeGrey(MENU_FADE_FRAMES);
	CHECK(bar.bg == MENU_BAR_BG && bar.fg == MENU_BAR_FG, "step 0 is the bar");
	CHECK(MenuFadeGrey(-3).bg == MENU_BAR_BG, "a negative step is the bar");
	CHECK(idle.bg == MENU_IDLE_BG && idle.fg == MENU_IDLE_FG, "the last step is idle");
	CHECK(MenuFadeGrey(MENU_FADE_FRAMES + 50).bg == MENU_IDLE_BG, "past the end stays idle");

	/* the background darkens and the text brightens, a step at a time, never past either end */
	int lastBg = MENU_BAR_BG + 1;
	int lastFg = MENU_BAR_FG - 1;
	for (int step = 0; step <= MENU_FADE_FRAMES; step++)
	{
		MenuGrey grey = MenuFadeGrey(step);
		CHECK(grey.bg < lastBg, "background did not darken at step %d (%d after %d)", step, grey.bg, lastBg);
		CHECK(grey.fg > lastFg, "text did not brighten at step %d (%d after %d)", step, grey.fg, lastFg);
		CHECK(grey.bg <= MENU_BAR_BG && grey.bg >= MENU_IDLE_BG, "background %d out of range", grey.bg);
		CHECK(grey.fg >= MENU_BAR_FG && grey.fg <= MENU_IDLE_FG, "text %d out of range", grey.fg);
		CHECK_OK();
		lastBg = grey.bg;
		lastFg = grey.fg;
	}
}

static void test_static_is_video_safe(void)
{
	/* luma on the Wii's video runs 16 (black) to 235 (white): every value must be inside, mostly dark */
	uint32_t seed = 1;
	int bright = 0;
	for (int i = 0; i < 100000; i++)
	{
		uint8_t luma = StaticLuma(seed);
		CHECK(luma >= 16 && luma <= 235, "luma %d out of range", luma);
		CHECK(seed != 0, "the generator reached 0 and would stay there");
		CHECK_OK();
		if (luma >= 0x70)
			bright++;
	}
	CHECK(bright > 0 && bright < 100000 / 20, "%d bright specks in 100000", bright);
}

static const struct hc_test tests[] = {
	{ "no_argument_is_the_menu", test_no_argument_is_the_menu, false },
	{ "actions", test_actions, false },
	{ "anything_else_is_refused", test_anything_else_is_refused, false },
	{ "menu_wraps", test_menu_wraps, false },
	{ "fade", test_fade, false },
	{ "static_is_video_safe", test_static_is_video_safe, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, NULL);
}
