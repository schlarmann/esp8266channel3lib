#include <esp8266channel3lib.h>

uint32_t frameCount = 0;

// This callback gets called automatically every frame
void ICACHE_FLASH_ATTR loadFrame() {
    video_broadcast_clear_frame();
    
    int * px = &CNFGPenX;
    int * py = &CNFGPenY;
    *px = 3;
    *py = 4;
    CNFGColor( C3_COL_DD_WHITE );
    //CNFGTackRectangle( 0, 0, (58*4)-1, (55/2)-1);
    CNFGDrawText("Title", 2 );
    
    *py = 190;
    CNFGColor( C3_COL_DD_WHITE );
    char content[255];
    sprintf(content, "Frames: %u", frameCount++);
    CNFGDrawText(content, 2 );
}

void setup() {
  system_update_cpu_freq( SYS_CPU_160MHZ );
  channel3Init(PAL, &loadFrame);
}

void loop() {
  // put your main code here, to run repeatedly:
}