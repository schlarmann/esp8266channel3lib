// Uncomment for PAL video output
//#define PAL
#include <esp8266channel3lib.h>

uint32_t frameCount = 0;

// This callback gets called automatically every frame
void ICACHE_FLASH_ATTR loadFrame(uint8_t * ff)
{
  if(ff != NULL){
    ets_memset( ff, 0, ((232/4)*220) );
    
    int * px = &CNFGPenX;
    int * py = &CNFGPenY;
    *px = 3;
    *py = 4;
    CNFGColor( 17 );
    //CNFGTackRectangle( 0, 0, (58*4)-1, (55/2)-1);
    CNFGDrawText("Title", 2 );
    
    *py = 190;
    CNFGColor( 17 );
    char content[255];
    sprintf(content, "Frames: %u", frameCount++);
    CNFGDrawText(content, 2 );
  }
}

void setup() {
  system_update_cpu_freq( SYS_CPU_160MHZ );
  channel3Init(&loadFrame);
}

void loop() {
  // put your main code here, to run repeatedly:
}
