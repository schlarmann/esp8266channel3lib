//Copyright 2016 <>< Charles Lohr, See LICENSE file.
// COPYRIGHT 2022 Paul Schlarmann

#ifndef _VIDEO_BROADCAST_TEST
#define _VIDEO_BROADCAST_TEST

/*
	This is the Video Broadcast code.  To set it up, call testi2s_init.
	This will set up the DMA engine and all the chains for outputting 
	broadcast.

	This is tightly based off of SpriteTM's ESP8266 MP3 Decoder.

	If you change the RF Maps, please call redo_maps, this will make
	the system update all the non-frame data to use the right bit patterns.
*/


//Stuff that should be for the header:

#include <c_types.h>
#include "common.h"

#define DMABUFFERDEPTH 3

void ICACHE_FLASH_ATTR video_broadcast_init(channel3VideoType_t videoType);

uint16_t *video_broadcast_get_framebuffer();
int video_broadcast_get_frame_number();
uint16_t video_broadcast_framebuffer_width();
uint16_t video_broadcast_framebuffer_height();

#endif

