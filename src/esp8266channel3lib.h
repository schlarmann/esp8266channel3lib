/**
 * @file esp8266channel3lib.h
 * @author Paul Schlarmann (paul.schlarmann@makerspace-minden.de)
 * @brief A library to emulate an analogue tv station on channel 3 with an ESP8266
 * @version 0.1.0
 * @date 2023-02-26
 * 
 * @copyright Copyright (c) Paul Schlarmann 2023
 * 
 */
#ifndef ESP8266CHANNEL3LIB_H
#define ESP8266CHANNEL3LIB_H

// --- Includes ---
#include <Arduino.h>

#include "common.h"
#include "video_broadcast.h"
#include "3d.h"

// --- Defines ---

// --- Marcos ---

// --- Typedefs ---
/**
 * @brief Callback function to load a frame
 * 
 * @param frame Pointer to the frame buffer
 */ 
typedef void (*loadFrameCB)();
// --- Public Vars ---

// --- Public Functions ---

/**
 * @brief Initialize the channel 3 library
 * 
 * @param videoType The video type to use
 * @param loadFrameCB The callback function to load a frame
 */
void ICACHE_FLASH_ATTR channel3Init(channel3VideoType_t videoType, loadFrameCB loadFrameCB);
/**
 * @brief Deinitialize the channel 3 library
 */
void channel3Deinit();

/**
 * @brief Stop the broadcast
 */
void channel3StopBroadcast();
/**
 * @brief Start the broadcast
 */
void channel3StartBroadcast();

#endif /* ESP8266CHANNEL3LIB_H */
