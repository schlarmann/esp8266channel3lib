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
static int runTimerPeriod;
static bool runFlag;

// --- Private Functions ---
/**
 * @brief Timer callback to load a frame
 */
LOCAL void ICACHE_FLASH_ATTR frameTimer(){
	static uint8_t lastframe = 0;
	uint8_t tbuffer = !(video_broadcast_get_frame_number()&1);
	if( lastframe != tbuffer ) // New frame
	{
        if(frameCB != NULL){
            frameCB(); //callback
        }
		lastframe = tbuffer;
	}
}

// --- Public Vars ---

// --- Public Functions ---
void ICACHE_FLASH_ATTR channel3Init(channel3VideoType_t videoType, loadFrameCB loadFrameCB){
    videoStandard = videoType;
    frameCB = loadFrameCB;
    if(videoStandard == PAL){
        runTimerPeriod = 1000 / FRAME_FREQUENCY_PAL;
    } else {
        runTimerPeriod = 1000 / FRAME_FREQUENCY_NTSC;
    }
    os_timer_setfn(&runTimer, (os_timer_func_t *)frameTimer, NULL);
    os_timer_arm(&runTimer, runTimerPeriod, 1);
    runFlag = true;

    video_broadcast_init(videoStandard);
}

void channel3Deinit(){
    channel3StopBroadcast();
    video_broadcast_deinit();
}

void channel3StopBroadcast(){
    if(runFlag){
        os_timer_disarm(&runTimer);
        runFlag = false;
    }
}

void channel3StartBroadcast(){
    if(!runFlag){
        os_timer_setfn(&runTimer, (os_timer_func_t *)frameTimer, NULL);
        os_timer_arm(&runTimer, runTimerPeriod, 1);
        runFlag = true;
    }
}