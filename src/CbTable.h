#ifndef _CBTABLE_H
#define _CBTABLE_H

#include <c_types.h>

#define FT_STA_d 0
#define FT_STB_d 1
#define FT_B_d 2
#define FT_SRA_d 3
#define FT_SRB_d 4
#define FT_LIN_d 5
#define FT_CLOSE 6
#define FT_MAX_d 7

#define VIDEO_LINES_PAL 625
extern uint8_t CbLookupPAL[313];
#define VIDEO_LINES_NTSC 525
extern uint8_t CbLookupNTSC[263];


#endif

