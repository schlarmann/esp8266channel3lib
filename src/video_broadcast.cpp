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

//Bit clock @ 80MHz = 12.5ns
//Word clock = 400ns
//Each NTSC line = 15,734.264 Hz.  63556 ns
//Each group of 4 bytes = 

#define LINETYPES 6

//I2S DMA buffer descriptors
static struct sdio_queue i2sBufDesc[DMABUFFERDEPTH];
uint32_t *i2sBD;

//Queue which contains empty DMA buffers

//This routine is called as soon as the DMA routine has something to tell us. All we
//handle here is the RX_EOF_INT status, which indicate the DMA has sent a buffer whose
//descriptor has the 'EOF' field set to 1.
LOCAL int gline = 0;
LOCAL int gframe = 0; //Current frame #
LOCAL int linescratch;
LOCAL uint16_t fbh;
LOCAL uint16_t *framebuffer;

LOCAL uint8_t *CbLookup;

LOCAL uint8_t lineBufferLen;
LOCAL uint8_t shortSyncInterval;
LOCAL uint8_t longSyncInterval;
LOCAL uint8_t normalSyncInterval;
LOCAL uint8_t lineSignalInterval;
LOCAL uint8_t colorburstInterval;

const uint32_t * tablestart = &premodulated_table[0];
const uint32_t * tablept = &premodulated_table[0];
const uint32_t * tableend = &premodulated_table[PREMOD_ENTRIES*PREMOD_SIZE];
LOCAL uint32_t * curdma;

LOCAL uint8_t pixline; //line number currently being written out.

LOCAL channel3VideoType_t videoStandard;

//Each "qty" is 32 bits, or .4us
LOCAL void fillwith( uint16_t qty, uint8_t color )
{
//	return;
	//We're using this one.
	if( qty & 1 )
	{
		*(curdma++) = tablept[color]; tablept += PREMOD_SIZE;
	}
	qty>>=1;
	for( linescratch = 0; linescratch < qty; linescratch++ )
	{
		*(curdma++) = tablept[color]; tablept += PREMOD_SIZE;
		*(curdma++) = tablept[color]; tablept += PREMOD_SIZE;
		if( tablept >= tableend ) tablept = tablept - tableend + tablestart;
	}
}


//XXX TODO: Revisit the length of time the system is at SYNC, BLACK, etc.

LOCAL void FT_STA() // Short Sync
{
	pixline = 0; //Reset the framebuffer out line count (can be done multiple times)

	fillwith( shortSyncInterval, SYNC_LEVEL );
	fillwith( longSyncInterval, BLACK_LEVEL );
	fillwith( shortSyncInterval, SYNC_LEVEL );
	fillwith( lineBufferLen - (shortSyncInterval+longSyncInterval+shortSyncInterval), BLACK_LEVEL );
}


LOCAL void FT_STB() // Long Sync
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

//Margin at top and bottom of screen (Mostly invisible)
//Closed Captioning would go somewhere in here, I guess?
LOCAL void FT_B() // Black
{
	fillwith( normalSyncInterval, SYNC_LEVEL );
	fillwith( 2, BLACK_LEVEL );
	fillwith( colorburstInterval, COLORBURST_LEVEL );
	fillwith( lineBufferLen-normalSyncInterval-2-colorburstInterval, (pixline<1)?GRAY_LEVEL:BLACK_LEVEL);
	//Gray seems to help sync if at top.  TODO: Investigate if white works even better!
}

LOCAL void FT_SRA() // Short to long
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

LOCAL void FT_SRB() // Long to short
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

LOCAL void FT_LIN() // Line Signal
{
	//TODO: Make this do something useful.
	fillwith( normalSyncInterval, SYNC_LEVEL );
	fillwith( 1, BLACK_LEVEL );
	fillwith( colorburstInterval, COLORBURST_LEVEL );
	fillwith( 11, BLACK_LEVEL );

	int fframe = gframe & 1;
	uint16_t * fbs = (uint16_t*)(&framebuffer[ ( (pixline * (FBW2/2)) + ( ((FBW2/2)*(fbh))*(fframe)) ) / 2 ]);

	for( linescratch = 0; linescratch < FBW2/4; linescratch++ )
	{
		uint16_t fbb;
		fbb = fbs[linescratch];
		*(curdma++) = tablept[(fbb>>0)&15];		tablept += PREMOD_SIZE;
		*(curdma++) = tablept[(fbb>>4)&15];		tablept += PREMOD_SIZE;
		*(curdma++) = tablept[(fbb>>8)&15];		tablept += PREMOD_SIZE;
		*(curdma++) = tablept[(fbb>>12)&15];	tablept += PREMOD_SIZE;
		if( tablept >= tableend ) tablept = tablept - tableend + tablestart;
	}

	fillwith( lineBufferLen - ((normalSyncInterval+1+colorburstInterval+11)+FBW2), BLACK_LEVEL); //WHITE_LEVEL );

	pixline++;
}

static uint32_t systimex = 0;
static uint32_t systimein = 0;
LOCAL void FT_CLOSE_M() // End Frame
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
	gline = -1;
	gframe++;

	systimex = 0;
	systimein = system_get_time();
}


void (*CbTable[FT_MAX_d])() = { FT_STA, FT_STB, FT_B, FT_SRA, FT_SRB, FT_LIN, FT_CLOSE_M };

LOCAL void slc_isr(void *unused1, void *unused2) {
	//portBASE_TYPE HPTaskAwoken=0;
	struct sdio_queue *finishedDesc;
	uint32 slc_intr_status;

	slc_intr_status = READ_PERI_REG(SLC_INT_STATUS);
	//clear all intr flags
	WRITE_PERI_REG(SLC_INT_CLR, 0xffffffff);//slc_intr_status);
	if ( (slc_intr_status & SLC_RX_EOF_INT_ST))
	{
		//The DMA subsystem is done with this block: Push it on the queue so it can be re-used.
		finishedDesc=(struct sdio_queue*)READ_PERI_REG(SLC_RX_EOF_DES_ADDR);
		curdma = (uint32_t*)finishedDesc->buf_ptr;
		if(curdma != NULL){
			//*startdma = premodulated_table[0];
			int lk = 0;
			if( gline & 1 )
				lk = (CbLookup[gline>>1]>>4)&0x0f;
			else
				lk = CbLookup[gline>>1]&0x0f;

			systimein = system_get_time();
			CbTable[lk]();
			systimex += system_get_time() - systimein;
			gline++;
		}
		
	}
}

//Initialize I2S subsystem for DMA circular buffer use
void ICACHE_FLASH_ATTR video_broadcast_init(channel3VideoType_t videoType) {
	int x = 0;
	videoStandard = videoType;
	if(videoStandard == PAL){
		lineBufferLen = LINE_BUFFER_LENGTH_PAL;
		shortSyncInterval = SHORT_SYNC_INTERVAL_PAL;
		longSyncInterval = LONG_SYNC_INTERVAL_PAL;
		normalSyncInterval = NORMAL_SYNC_INTERVAL_PAL;
		lineSignalInterval = LINE_SIGNAL_INTERVAL_PAL;
		colorburstInterval = COLORBURST_INTERVAL_PAL;
		fbh = FBH_PAL;
		CbLookup = CbLookupPAL;
	} else {
		lineBufferLen = LINE_BUFFER_LENGTH_NTSC;
		shortSyncInterval = SHORT_SYNC_INTERVAL_NTSC;
		longSyncInterval = LONG_SYNC_INTERVAL_NTSC;
		normalSyncInterval = NORMAL_SYNC_INTERVAL_NTSC;
		lineSignalInterval = LINE_SIGNAL_INTERVAL_NTSC;
		colorburstInterval = COLORBURST_INTERVAL_NTSC;
		fbh = FBH_NTSC;
		CbLookup = CbLookupNTSC;
	}

	framebuffer = (uint16_t *) malloc(sizeof(uint16_t) * ( (FBW2/4)*fbh ) *2);
	i2sBD = (uint32_t *) malloc(sizeof(uint32_t) * (lineBufferLen*DMABUFFERDEPTH));

	//Bits are shifted out

	//Initialize DMA buffer descriptors in such a way that they will form a circular
	//buffer.
	for (x=0; x<DMABUFFERDEPTH; x++) {
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

	//We use a queue to keep track of the DMA buffers that are empty. The ISR will push buffers to the back of the queue,
	//the mp3 decode will pull them from the front and fill them. For ease, the queue will contain *pointers* to the DMA
	//buffers, not the data itself. The queue depth is one smaller than the amount of buffers we have, because there's
	//always a buffer that is being used by the DMA subsystem *right now* and we don't want to be able to write to that
	//simultaneously.
	//dmaQueue=xQueueCreate(I2SDMABUFCNT-1, sizeof(int*));

	//Start transmission
	SET_PERI_REG_MASK(SLC_TX_LINK, SLC_TXLINK_START);
	SET_PERI_REG_MASK(SLC_RX_LINK, SLC_RXLINK_START);

//----

	//Init pins to i2s functions
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_I2SO_DATA);
//	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_I2SO_WS);
//	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_I2SO_BCK);

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


uint16_t *video_broadcast_get_framebuffer(){
	return framebuffer;
}
int video_broadcast_get_frame_number(){
	return gframe;
}
uint16_t video_broadcast_framebuffer_width(){
	return FBW;
}

uint16_t video_broadcast_framebuffer_height(){
	return fbh;
}
