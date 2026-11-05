/*

priiloader(preloader mod) - A tool which allows to change the default boot up sequence on the Wii console

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/

//The Wii Remotes' command queues, from the normal heap. Taken from HBC reborn (channel/channelapp/source/wpadqueue.c).
//
//libogc 3.1 gives each of its six Bluetooth slots (four Wii Remotes, the Balance Board, one more) a queue of 256
//commands from its kernel heap, 64 KB shared with its threads. Only the first three fit; slots 3 to 5 keep an empty
//queue, and the first command sent to a device there (its LEDs, as it connects) reads address 0. A Balance Board left
//on, or a fourth Wii Remote, crashes the program as it connects. In priiloader that happens before the video is up
//(autoboot, return to menu): a black screen until RESET.
//
//Both Makefiles link with -Wl,--wrap=wiiuse_init_cmd_queue, so libogc's call from wiiuse_init lands here.

#include <malloc.h>
#include <gccore.h>
#include <wiiuse/wiiuse.h>

#define SLOTS 6			//WPAD_MAX_DEVICES in libogc 3.1
#define COMMANDS 256	//libogc's own queue length

void __real_wiiuse_init_cmd_queue(struct wiimote_t *wm);
void __wrap_wiiuse_init_cmd_queue(struct wiimote_t *wm);

//once per slot, kept across WPAD_Shutdown and WPAD_Init
static void *buffers[SLOTS];

void __wrap_wiiuse_init_cmd_queue(struct wiimote_t *wm)
{
	int slot = wm->unid;

	if (slot >= 0 && slot < SLOTS && !buffers[slot])
		buffers[slot] = memalign(32, COMMANDS * sizeof(struct cmd_blk_t));

	//wiiuse.h marks the queue const for apps; libogc's own code sets it
	if (slot >= 0 && slot < SLOTS && buffers[slot])
	{
		__lwp_queue_initialize((lwp_queue *)&wm->cmdq, buffers[slot], COMMANDS, sizeof(struct cmd_blk_t));
		return;
	}

	//no memory here either: libogc's own queue, and if that fails too an empty one,
	//so commands to the device are dropped instead of read from 0
	__real_wiiuse_init_cmd_queue(wm);
	if (!wm->cmdq.first)
		__lwp_queue_initialize((lwp_queue *)&wm->cmdq, NULL, 0, sizeof(struct cmd_blk_t));
}
