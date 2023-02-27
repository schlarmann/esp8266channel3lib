/**
 * @file esp8266channel3lib.cpp
 * @author Paul Schlarmann (paul.schlarmann@makerspace-minden.de)
 * @brief A library to emulate an analogue tv station on channel 3 with an ESP8266
 * @version 0.1.0
 * @date 2023-02-26
 * 
 * @copyright Copyright (c) Paul Schlarmann 2023
 * 
 */

// --- Includes ---
#include "esp8266channel3lib.h"

// --- Defines ---
#define FRAME_FREQUENCY_PAL 50
#define FRAME_FREQUENCY_NTSC 60

// --- Marcos ---

// --- Typedefs ---

// --- Private Vars ---
static loadFrameCB frameCB;
static channel3VideoType_t videoStandard;
static os_timer_t runTimer;

// --- Private Functions ---
LOCAL void ICACHE_FLASH_ATTR loadFrame(uint8_t * ff)
{
   ets_memset( ff, 0, ((232/4)*220) );
   if(frameCB != NULL){
    frameCB(ff); //callback
   }
}

LOCAL void ICACHE_FLASH_ATTR frameTimer(){
	static uint8_t lastframe = 0;
	uint8_t tbuffer = !(video_broadcast_get_frame_number()&1);
	if( lastframe != tbuffer )
	{
		frontframe = (uint8_t*)&video_broadcast_get_framebuffer()
            [ ( (video_broadcast_framebuffer_width()/8) *video_broadcast_framebuffer_height() ) * tbuffer];
		loadFrame(frontframe);
		lastframe = tbuffer;
	}
}

// --- Public Vars ---

// --- Public Functions ---
void ICACHE_FLASH_ATTR channel3Init(channel3VideoType_t videoType, loadFrameCB loadFrameCB){
    videoStandard = videoType;
    frameCB = loadFrameCB;
    int period;
    if(videoStandard == PAL){
        period = 1000 / FRAME_FREQUENCY_PAL;
    } else {
        period = 1000 / FRAME_FREQUENCY_NTSC;
    }
    os_timer_setfn(&runTimer, (os_timer_func_t *)frameTimer, NULL);
    os_timer_arm(&runTimer, period, 1);

    video_broadcast_init(videoStandard);
}