/**
 * @file common.h
 * @author Paul Schlarmann (paul.schlarmann@makerspace-minden.de)
 * @brief Common library for tv broadcasting
 * @version 0.2.0
 * @date 2023-02-27
 * 
 * @copyright Copyright (c) Paul Schlarmann 2023
 * 
 */
#ifndef ESP8266CHANNEL3COMMON_H
#define ESP8266CHANNEL3COMMON_H

// --- Includes ---
#include <Arduino.h>

// --- Defines ---

// --- Marcos ---

// --- Typedefs ---
/**
 * @brief Video output type
 */
typedef enum {NTSC, PAL} channel3VideoType_t;

/**
 * @brief Colors
 */
typedef enum {
	// Standard Density Colors
	// Names based on https://youtu.be/bcez5pcp55w?t=201. Since its NTSC, the colors may be different on your tv.
	C3_COL_BLACK = 0,
	C3_COL_DARK_GRAY,
	C3_COL_STRIPED_GRAY_1,
	C3_COL_GREEN_1,
	C3_COL_TURQUOISE,
	C3_COL_LIGHT_BLUE,
	C3_COL_DARK_BLUE,
	C3_COL_RED,
	C3_COL_STRIPED_GRAY_2,
	C3_COL_GREEN_2,
	C3_COL_WHITE,
	C3_COL_LIGHT_YELLOW,
	C3_COL_BABYBLUE_1,
	C3_COL_BABYBLUE_2,
	C3_COL_LIGHT_GRAY,
	C3_COL_LIGHT_PINK,

	// Double Density Colors: Black and White
	C3_COL_DD_BLACK = 16,
	C3_COL_DD_WHITE,
} channel3ColorType_t;

// --- Public Vars ---

// --- Public Functions ---

#endif /* ESP8266CHANNEL3LIB_H */
