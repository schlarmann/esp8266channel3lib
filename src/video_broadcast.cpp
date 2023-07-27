/******************************************************************************
 * Copyright 2013-2015 Espressif Systems
 *           2015 <>< Charles Lohr
 * 			 2022 Paul Schlarmann
 *
 * FileName: i2s_freertos.c
 *
 * Description: I2S output routines for a FreeRTOS system. Uses DMA and a queue
 * to abstract away the nitty-gritty details.
 *
 * Modification history:
 *     2015/06/01, v1.0 File created.
 *     2015/07/23, Switch to making it a WS2812 output device.
 *     2016/01/28, Modified to re-include TX_ stuff.
 *     2022/09/26, Modified CNLohrs code to also allow PAL broadcast
*******************************************************************************

Notes:

 This is pretty badly hacked together from the MP3 example.
 I spent some time trying to strip it down to avoid a lot of the TX_ stuff. 
 That seems to work.

 Major suggestions that I couldn't figure out:
	* Use interrupts to disable DMA, so it isn't running nonstop.
    * Use interrupts to flag when new data can be sent.

 When I try using interrupts, it seems to work for a bit but things fall apart
 rather quickly and the engine just refuses to send anymore until reboot.

 The way it works right now is to keep the DMA running forever and just update
 the data in the buffer so it continues sending the frame.

Extra copyright info:
  Actually not much of this file is Copyright Espressif, comparativly little
  mostly just the stuff to make the I2S bus go.

*******************************************************************************/


#include <c_types.h>
#include "esp8266_peri.h"
#include "video_broadcast.h"
#include "user_interface.h"
//#include "pin_mux_register.h"
#include "broadcast_tables.h"
#include <slc_register.h>
#include <i2s_reg.h>
#include "CbTable.h" 
#include "dmastuff.h"

// I2S Config
#define FUNC_I2SO_DATA                      1
#define WS_I2S_BCK 1  //Can't be less than 1.
#define WS_I2S_DIV 2

//Framebuffer width/height
#define FBW 232 //Must be divisible by 8.  These are actually "double-pixels" used for double-resolution monochrome width.
#define FBW2 (FBW/2) //Actual width in true pixels.
#define FBH_PAL 264
#define FBH_NTSC 220


/* PAL signals */
#define LINE_BUFFER_LENGTH_PAL 160
#define SHORT_SYNC_INTERVAL_PAL    5
#define LONG_SYNC_INTERVAL_PAL    75
#define NORMAL_SYNC_INTERVAL_PAL  10
#define LINE_SIGNAL_INTERVAL_PAL 150
#define COLORBURST_INTERVAL_PAL 10

/* NTSC signals */
#define LINE_BUFFER_LENGTH_NTSC 159
#define SHORT_SYNC_INTERVAL_NTSC    6
#define LONG_SYNC_INTERVAL_NTSC    73
#define SERRATION_PULSE_INT_NTSC   67
#define NORMAL_SYNC_INTERVAL_NTSC  12
#define LINE_SIGNAL_INTERVAL_NTSC 147
#define COLORBURST_INTERVAL_NTSC 4

/** @brief writes COLOR to the DMA buffer at the next position */
#define WRITE_TO_DMA(COLOR) *(dma_cursor++) = tablept[(COLOR)]; tablept += PREMOD_SIZE;

//Bit clock @ 80MHz = 12.5ns
//Word clock = 400ns
//Each NTSC line = 15,734.264 Hz.  63556 ns
//Each group of 4 bytes = 

//I2S DMA buffer descriptors
static struct sdio_queue i2sBufDesc[DMABUFFERDEPTH];
uint32_t *i2sBD;

/** @brief current line number being displayed */
LOCAL int signal_line_number = 0;
/** @brief current frame number */
LOCAL int frame_number = 0;
/** @brief height of frame buffer */
LOCAL uint16_t fb_height;
/** @brief pointer to frame buffer */
LOCAL uint16_t *framebuffer;

/** @brief Line callback lookup table*/
LOCAL uint8_t *lineCbLookupTable;

LOCAL uint8_t lineBufferLen;
LOCAL uint8_t shortSyncInterval;
LOCAL uint8_t longSyncInterval;
LOCAL uint8_t normalSyncInterval;
LOCAL uint8_t lineSignalInterval;
LOCAL uint8_t colorburstInterval;

const uint32_t *tablestart = &premodulated_table[0];
const uint32_t *tablept = &premodulated_table[0];
const uint32_t *tableend = &premodulated_table[PREMOD_ENTRIES*PREMOD_SIZE];
LOCAL uint32_t *dma_cursor;

/** @brief line number in frame buffer / of actual video data currently being written out. */
LOCAL uint16_t fb_line_number;

/** @brief PAL or NTSC */
LOCAL channel3VideoType_t videoStandard;

//Each "qty" is 32 bits, or .4us
LOCAL void fillwith( uint16_t qty, uint8_t color )
{
//	return;
	//We're using this one.
	if( qty & 1 )
	{
		WRITE_TO_DMA(color);
	}
	qty>>=1;
	for(int i = 0; i < qty; i++ )
	{
		WRITE_TO_DMA(color);
		WRITE_TO_DMA(color);
		if( tablept >= tableend ) tablept = tablept - tableend + tablestart;
	}
}

/** @brief Short Sync cb */
LOCAL void FT_STA()
{
	fb_line_number = 0; //Reset the framebuffer out line count (can be done multiple times)

	fillwith( shortSyncInterval, SYNC_LEVEL );
	fillwith( longSyncInterval, BLACK_LEVEL );
	fillwith( shortSyncInterval, SYNC_LEVEL );
	fillwith( lineBufferLen - (shortSyncInterval+longSyncInterval+shortSyncInterval), BLACK_LEVEL );
}
/** @brief Long Sync cb */
LOCAL void FT_STB()
{
	fillwith( longSyncInterval, SYNC_LEVEL );
	if(videoStandard == PAL){
		fillwith( shortSyncInterval, BLACK_LEVEL );
	} else {
		fillwith( normalSyncInterval, BLACK_LEVEL );
	}
	fillwith( longSyncInterval, SYNC_LEVEL );
	if(videoStandard == PAL){
		fillwith( lineBufferLen - (longSyncInterval+shortSyncInterval+longSyncInterval), BLACK_LEVEL );
	} else {
		fillwith( lineBufferLen - (longSyncInterval+normalSyncInterval+longSyncInterval), BLACK_LEVEL );
	}
}
/** 
 * @brief Black cb. 
 * Margin at top and bottom of screen (Mostly invisible)
 * Closed Captioning would go somewhere in here, I guess?
 */
LOCAL void FT_B()
{
	fillwith( normalSyncInterval, SYNC_LEVEL );
	fillwith( 2, BLACK_LEVEL );
	fillwith( colorburstInterval, COLORBURST_LEVEL );
	fillwith( lineBufferLen-normalSyncInterval-2-colorburstInterval, (fb_line_number<1)?GRAY_LEVEL:BLACK_LEVEL);
	//Gray seems to help sync if at top.  TODO: Investigate if white works even better!
}
/** @brief Short to long cb */
LOCAL void FT_SRA()
{
	fillwith( shortSyncInterval, SYNC_LEVEL );
	fillwith( longSyncInterval, BLACK_LEVEL );
	if(videoStandard == PAL){
		fillwith( longSyncInterval, SYNC_LEVEL );
		fillwith( lineBufferLen - (shortSyncInterval+longSyncInterval+longSyncInterval), BLACK_LEVEL );
	} else {
		fillwith( SERRATION_PULSE_INT_NTSC, SYNC_LEVEL );
		fillwith( lineBufferLen - (shortSyncInterval+longSyncInterval+SERRATION_PULSE_INT_NTSC), BLACK_LEVEL );
	}
}
/** @brief Long to short cb */
LOCAL void FT_SRB()
{
	if(videoStandard == PAL){
		fillwith( longSyncInterval, SYNC_LEVEL );
		fillwith( shortSyncInterval, BLACK_LEVEL );
		fillwith( shortSyncInterval, SYNC_LEVEL );
		fillwith( lineBufferLen - (longSyncInterval+shortSyncInterval+shortSyncInterval), BLACK_LEVEL );
	} else {
		fillwith( SERRATION_PULSE_INT_NTSC, SYNC_LEVEL );
		fillwith( normalSyncInterval, BLACK_LEVEL );
		fillwith( shortSyncInterval, SYNC_LEVEL );
		fillwith( lineBufferLen - (SERRATION_PULSE_INT_NTSC+normalSyncInterval+shortSyncInterval), BLACK_LEVEL );
	}
}
/** @brief Line Signal cb */
LOCAL void FT_LIN()
{
	// Front porch / HBlank
	fillwith( normalSyncInterval, SYNC_LEVEL );
	fillwith( 1, BLACK_LEVEL );
	fillwith( colorburstInterval, COLORBURST_LEVEL );
	fillwith( 11, BLACK_LEVEL );

	int fframe = frame_number & 1; 
	uint16_t *fb_line;
	if(frame_number & 1){ // Even / Odd frame
		fb_line = (uint16_t*)(&framebuffer[ 
		( 
			(fb_line_number * (FBW2/2)) + 
			((FBW2/2)*(fb_height))
		) / 2]);
	} else {
		fb_line = (uint16_t*)(&framebuffer[ 
		( 
			(fb_line_number * (FBW2/2))
		) / 2]);
	}

	// Drawing video data
	// Each line is divided into FBW2/4 = 232/8 = 29 Blocks. 
	for(int line_block_i = 0; line_block_i < FBW2/4; line_block_i++ )
	{
		uint16_t line_block = fb_line[line_block_i];
		// Each line block is contains
		//  - 8 B/W pixels or
		//  - 4 color pixels

		WRITE_TO_DMA((line_block>>0)&0x0F);
		WRITE_TO_DMA((line_block>>4)&0x0F);
		WRITE_TO_DMA((line_block>>8)&0x0F);
		WRITE_TO_DMA((line_block>>12)&0x0F);
		if( tablept >= tableend ) tablept = tablept - tableend + tablestart;
	}

	// Back porch / HBlank
	fillwith( lineBufferLen - (normalSyncInterval+1+colorburstInterval+11+FBW2), BLACK_LEVEL);

	fb_line_number++;
}
/** @brief End Frame cb */
LOCAL void FT_CLOSE_M()
{
	if(videoStandard == PAL){
		fillwith( shortSyncInterval, SYNC_LEVEL );
		fillwith( longSyncInterval, BLACK_LEVEL );
		fillwith( shortSyncInterval, SYNC_LEVEL );
		fillwith( lineBufferLen - (shortSyncInterval+longSyncInterval+shortSyncInterval), BLACK_LEVEL );	
	} else {
		fillwith( normalSyncInterval, SYNC_LEVEL );
		fillwith( 2, BLACK_LEVEL );
		fillwith( 4, COLORBURST_LEVEL );
		fillwith( lineBufferLen-normalSyncInterval-6, WHITE_LEVEL );
	}
	signal_line_number = -1;
	frame_number++;
}

/** @brief Line type callback table */
void (*lineCbTable[FT_MAX_d])() = { FT_STA, FT_STB, FT_B, FT_SRA, FT_SRB, FT_LIN, FT_CLOSE_M };

/** @brief I2S DMA interrupt handler */
LOCAL void slc_isr(void *unused1, void *unused2) {
	struct sdio_queue *finishedDesc;
	uint32 slc_intr_status;

	slc_intr_status = READ_PERI_REG(SLC_INT_STATUS);
	//clear all intr flags
	WRITE_PERI_REG(SLC_INT_CLR, 0xffffffff);//slc_intr_status);
	if ( (slc_intr_status & SLC_RX_EOF_INT_ST))
	{
		//The DMA subsystem is done with this block: Push it on the queue so it can be re-used.
		finishedDesc=(struct sdio_queue*)READ_PERI_REG(SLC_RX_EOF_DES_ADDR);
		dma_cursor = (uint32_t*)finishedDesc->buf_ptr;
		if(dma_cursor != NULL){
			int currentLineType = 0;
			if( signal_line_number & 1 ) // Odd frame
				currentLineType = (lineCbLookupTable[signal_line_number>>1]>>4)&0x0f;
			else // Even frame
				currentLineType = lineCbLookupTable[signal_line_number>>1]&0x0f;

			lineCbTable[currentLineType]();
			signal_line_number++;
		}
		
	}
}

//Initialize I2S subsystem for DMA circular buffer use
void ICACHE_FLASH_ATTR video_broadcast_init(channel3VideoType_t videoType) {
	videoStandard = videoType;
	// Populate various constants based on video standard
	if(videoStandard == PAL){
		lineBufferLen = LINE_BUFFER_LENGTH_PAL;
		shortSyncInterval = SHORT_SYNC_INTERVAL_PAL;
		longSyncInterval = LONG_SYNC_INTERVAL_PAL;
		normalSyncInterval = NORMAL_SYNC_INTERVAL_PAL;
		lineSignalInterval = LINE_SIGNAL_INTERVAL_PAL;
		colorburstInterval = COLORBURST_INTERVAL_PAL;
		fb_height = FBH_PAL;
		lineCbLookupTable = CbLookupPAL;
	} else {
		lineBufferLen = LINE_BUFFER_LENGTH_NTSC;
		shortSyncInterval = SHORT_SYNC_INTERVAL_NTSC;
		longSyncInterval = LONG_SYNC_INTERVAL_NTSC;
		normalSyncInterval = NORMAL_SYNC_INTERVAL_NTSC;
		lineSignalInterval = LINE_SIGNAL_INTERVAL_NTSC;
		colorburstInterval = COLORBURST_INTERVAL_NTSC;
		fb_height = FBH_NTSC;
		lineCbLookupTable = CbLookupNTSC;
	}

	// Create dynamic data
	framebuffer = (uint16_t *) malloc(sizeof(uint16_t) * ( (FBW2/4)*fb_height ) *2);
	i2sBD = (uint32_t *) malloc(sizeof(uint32_t) * (lineBufferLen*DMABUFFERDEPTH));

	//Initialize DMA buffer descriptors in such a way that they will form a circular
	//buffer.
	for (int x=0; x<DMABUFFERDEPTH; x++) {
		i2sBufDesc[x].owner=1;
		i2sBufDesc[x].eof=1;
		i2sBufDesc[x].sub_sof=0;
		i2sBufDesc[x].datalen=lineBufferLen*4;
		i2sBufDesc[x].blocksize=lineBufferLen*4;
		i2sBufDesc[x].buf_ptr=(uint32_t)&i2sBD[x*lineBufferLen];
		i2sBufDesc[x].unused=0;
		i2sBufDesc[x].next_link_ptr=(int)((x<(DMABUFFERDEPTH-1))?(&i2sBufDesc[x+1]):(&i2sBufDesc[0]));
	}


	//Reset DMA
	SET_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST|SLC_TXLINK_RST);
	CLEAR_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST|SLC_TXLINK_RST);

	//Clear DMA int flags
	SET_PERI_REG_MASK(SLC_INT_CLR,  0xffffffff);
	CLEAR_PERI_REG_MASK(SLC_INT_CLR,  0xffffffff);

	//Enable and configure DMA
	CLEAR_PERI_REG_MASK(SLC_CONF0, (SLC_MODE<<SLC_MODE_S));
	SET_PERI_REG_MASK(SLC_CONF0,(1<<SLC_MODE_S));
	SET_PERI_REG_MASK(SLC_RX_DSCR_CONF,SLC_INFOR_NO_REPLACE|SLC_TOKEN_NO_REPLACE);
	CLEAR_PERI_REG_MASK(SLC_RX_DSCR_CONF, SLC_RX_FILL_EN|SLC_RX_EOF_MODE | SLC_RX_FILL_MODE);
	
	//Feed dma the 1st buffer desc addr
	//To send data to the I2S subsystem, counter-intuitively we use the RXLINK part, not the TXLINK as you might
	//expect. The TXLINK part still needs a valid DMA descriptor, even if it's unused: the DMA engine will throw
	//an error at us otherwise. Just feed it any random descriptor.
	CLEAR_PERI_REG_MASK(SLC_TX_LINK,SLC_TXLINK_DESCADDR_MASK);
	SET_PERI_REG_MASK(SLC_TX_LINK, ((uint32)&i2sBufDesc[1]) & SLC_TXLINK_DESCADDR_MASK); //any random desc is OK, we don't use TX but it needs something valid
	CLEAR_PERI_REG_MASK(SLC_RX_LINK,SLC_RXLINK_DESCADDR_MASK);
	SET_PERI_REG_MASK(SLC_RX_LINK, ((uint32)&i2sBufDesc[0]) & SLC_RXLINK_DESCADDR_MASK);

	//Attach the DMA interrupt
	ets_isr_attach(ETS_SLC_INUM, slc_isr, NULL);
	//Enable DMA operation intr
	WRITE_PERI_REG(SLC_INT_ENA,  SLC_RX_EOF_INT_ENA);
	//clear any interrupt flags that are set
	WRITE_PERI_REG(SLC_INT_CLR, 0xffffffff);
	///enable DMA intr in cpu
	ets_isr_unmask(1<<ETS_SLC_INUM);

	//Start transmission
	SET_PERI_REG_MASK(SLC_TX_LINK, SLC_TXLINK_START);
	SET_PERI_REG_MASK(SLC_RX_LINK, SLC_RXLINK_START);

	//Init pins to i2s functions. We only need data out
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_I2SO_DATA);

	//Enable clock to i2s subsystem
	i2c_writeReg_Mask_def(i2c_bbpll, i2c_bbpll_en_audio_clock_out, 1);

	//Reset I2S subsystem
	CLEAR_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);
	SET_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);
	CLEAR_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);

	//Select 16bits per channel (FIFO_MOD=0), no DMA access (FIFO only)
	CLEAR_PERI_REG_MASK(I2S_FIFO_CONF, I2S_I2S_DSCR_EN|(I2S_I2S_RX_FIFO_MOD<<I2S_I2S_RX_FIFO_MOD_S)|(I2S_I2S_TX_FIFO_MOD<<I2S_I2S_TX_FIFO_MOD_S));
	//Enable DMA in i2s subsystem
	SET_PERI_REG_MASK(I2S_FIFO_CONF, I2S_I2S_DSCR_EN);

	//tx/rx binaureal
	CLEAR_PERI_REG_MASK(I2SCONF_CHAN, (I2S_TX_CHAN_MOD<<I2S_TX_CHAN_MOD_S)|(I2S_RX_CHAN_MOD<<I2S_RX_CHAN_MOD_S));

	//Clear int
	SET_PERI_REG_MASK(I2SINT_CLR,   I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);
	CLEAR_PERI_REG_MASK(I2SINT_CLR, I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);

	//trans master&rece slave,MSB shift,right_first,msb right
	CLEAR_PERI_REG_MASK(I2SCONF, I2S_TRANS_SLAVE_MOD|
						(I2S_BITS_MOD<<I2S_BITS_MOD_S)|
						(I2S_BCK_DIV_NUM <<I2S_BCK_DIV_NUM_S)|
						(I2S_CLKM_DIV_NUM<<I2S_CLKM_DIV_NUM_S));
	SET_PERI_REG_MASK(I2SCONF, I2S_RIGHT_FIRST|I2S_MSB_RIGHT|I2S_RECE_SLAVE_MOD|
						I2S_RECE_MSB_SHIFT|I2S_TRANS_MSB_SHIFT|
						((WS_I2S_BCK&I2S_BCK_DIV_NUM )<<I2S_BCK_DIV_NUM_S)|
						((WS_I2S_DIV&I2S_CLKM_DIV_NUM)<<I2S_CLKM_DIV_NUM_S));

	//No idea if ints are needed...
	//clear int
	SET_PERI_REG_MASK(I2SINT_CLR,   I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);
	CLEAR_PERI_REG_MASK(I2SINT_CLR,   I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);
	//enable int
	SET_PERI_REG_MASK(I2SINT_ENA,   I2S_I2S_TX_REMPTY_INT_ENA|I2S_I2S_TX_WFULL_INT_ENA|
	I2S_I2S_RX_REMPTY_INT_ENA|I2S_I2S_TX_PUT_DATA_INT_ENA|I2S_I2S_RX_TAKE_DATA_INT_ENA);

	//Start transmission
	SET_PERI_REG_MASK(I2SCONF,I2S_I2S_TX_START);
}


void video_broadcast_deinit() {
	// Reset I2S subsystem
	CLEAR_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);
	SET_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);
	CLEAR_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);

	// disable DMA intr in cpu
	ets_isr_mask(1<<ETS_SLC_INUM);
	// Deattach the DMA interrupt
	ets_isr_attach(ETS_SLC_INUM, NULL, NULL);
	// Clear DMA Interrupt
	WRITE_PERI_REG(SLC_INT_ENA,  0x00);
	//Clear DMA int flags
	SET_PERI_REG_MASK(SLC_INT_CLR,  0xffffffff);
	CLEAR_PERI_REG_MASK(SLC_INT_CLR,  0xffffffff);
	// Reset DMA
	SET_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST|SLC_TXLINK_RST);
	CLEAR_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST|SLC_TXLINK_RST);

	// free dynamic data
	free(framebuffer);
	free(i2sBD);
}


uint16_t *video_broadcast_get_framebuffer(){
	return framebuffer;
}
int video_broadcast_get_frame_number(){
	return frame_number;
}
uint16_t video_broadcast_framebuffer_width(){
	return FBW;
}
uint16_t video_broadcast_framebuffer_height(){
	return fb_height;
}

uint8_t * video_broadcast_get_frame(){
	bool isOddFrame = frame_number&0x01;
	if(!isOddFrame) return (uint8_t*)&framebuffer[0];
	return (uint8_t*)&framebuffer[(FBW2/4) *fb_height];
}

void video_broadcast_clear_frame(){
	bool isOddFrame = frame_number&0x01;
	if(!isOddFrame) ets_memset( (uint8_t*)&framebuffer[0], 0, ((FBW/4)*fb_height) );
	else ets_memset( (uint8_t*)&framebuffer[(FBW2/4) *fb_height], 0, ((FBW/4)*fb_height) );
}

void video_tack_dd_pixel(uint8_t *current_frame, int x, int y, uint8_t color){
	// Check for illegal pixels
	if(x > FBW) return;
	if(y > fb_height) return;
	if(color > C3_COL_DD_WHITE) return;
	
	// Put color in buffer
	uint8_t *half_block = &(current_frame[(x+y*FBW)>>2]);
	if(color == C3_COL_DD_WHITE) {
		*half_block |= 0b10 <<((x&0b11)<<1);
	} else {
		*half_block &= ~( 0b10 <<((x&0b11)<<1) );
	}
}
void video_tack_pixel(uint8_t *current_frame, int x, int y, uint8_t color){
	// Check for illegal pixels
	if(x > FBW) return;
	if(y > fb_height) return;
	if(color > C3_COL_DD_WHITE) return;

	// Call DD function if needed
	if(color >= C3_COL_DD_BLACK){
		video_tack_dd_pixel(current_frame, x, y, color);
		return;
	}

	// Put color in buffer
	uint8_t *half_block = &(current_frame[(x+y*(FBW/2) )>>1]);
	if( x & 1 ) {
		*half_block = (*half_block & 0x0f) | color<<4;
	} else {
		*half_block = (*half_block & 0xf0 ) | color;
	}
}

void video_broadcast_tack_pixel(int x, int y, uint8_t color){
	bool isOddFrame = frame_number&0x01;
	if(!isOddFrame) video_tack_pixel((uint8_t*)&framebuffer[0], x, y, color);
	else video_tack_pixel((uint8_t*)&framebuffer[(FBW2/4) *fb_height], x, y, color);
}