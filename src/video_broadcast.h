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

/**
 * @brief Initialize the video broadcast. Generates video of the specified type.
 * 
 * @param videoType Type, either NTSC or PAL
 */
void ICACHE_FLASH_ATTR video_broadcast_init(channel3VideoType_t videoType);
/**
 * @brief Deinitialize the video broadcast
 */
void video_broadcast_deinit();

/**
 * @brief Gets the current frame number
 * 
 * @return int current frame number
 */
int video_broadcast_get_frame_number();
/**
 * @return uint16_t Width of the framebuffer
 */
uint16_t video_broadcast_framebuffer_width();
/**
 * @return uint16_t Height of the framebuffer
 */
uint16_t video_broadcast_framebuffer_height();

/**
 * @brief Get the framebuffer
 * 
 * @return uint8_t* Pointer to the framebuffer
 */
uint8_t *video_broadcast_get_frame();
/**
 * @brief Clear the framebuffer
 */
void video_broadcast_clear_frame();
/**
 * @brief Puts a pixel onto the screen
 * 
 * @param x X-Coordinate
 * @param y Y-Coordinate
 * @param color Color as specified in enum channel3ColorType_t
 */
void video_broadcast_tack_pixel(int x, int y, uint8_t color);

#endif

