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
#ifdef PAL
    #define FRAME_FREQUENCY 50
#else
    #define FRAME_FREQUENCY 60
#endif

// --- Marcos ---

// --- Typedefs ---

// --- Private Vars ---
static loadFrameCB frameCB;
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
	uint8_t tbuffer = !(gframe&1);
	if( lastframe != tbuffer )
	{
		frontframe = (uint8_t*)&framebuffer[((FBW2/4)*FBH)*tbuffer];
		loadFrame(frontframe);
		lastframe = tbuffer;
	}
}

// --- Public Vars ---

// --- Public Functions ---
void ICACHE_FLASH_ATTR channel3Init(loadFrameCB loadFrameCB){
    frameCB = loadFrameCB;
    int period = 1000 / FRAME_FREQUENCY;
    os_timer_setfn(&runTimer, (os_timer_func_t *)frameTimer, NULL);
    os_timer_arm(&runTimer, period, 1);

    video_broadcast_init();
}