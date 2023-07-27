#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <esp8266channel3lib.h>
/* 
    ESP8266 Channel 3 OTA / softAP example

    If you get an "No respose" error when updating you should try again,
    this usually happens because the Update is started while the ESP is in
    the video line interrupt.
*/

uint32_t frameCount = 0;

// This callback gets called automatically every frame
void ICACHE_FLASH_ATTR loadFrame() {
  video_broadcast_clear_frame();
  
  int * px = &CNFGPenX;
  int * py = &CNFGPenY;
  *px = 10;
  *py = 4;
  CNFGColor( C3_COL_DD_WHITE );
  CNFGDrawText("ESP8266 Channel 3 OTA Demo", 2 );
  *py = 14;
  CNFGDrawText("SSID: ESP_Channel3", 2 );
  *py = 24;
  CNFGDrawText("PW:   ESP_Channel3", 2 );

  // Draw a line
  for(int i=0; i<video_broadcast_framebuffer_width(); i+=2){
    video_broadcast_tack_pixel(i, 40, C3_COL_DD_WHITE);
    video_broadcast_tack_pixel(i+1, 40, C3_COL_DD_BLACK);
  }
  
  *py = 190;
  CNFGColor( C3_COL_DD_WHITE );
  char content[255];
  sprintf(content, "Frames: %u", frameCount++);
  CNFGDrawText(content, 2 );
}

void setup() {
  system_update_cpu_freq( SYS_CPU_160MHZ );  

  Serial.begin(115200);
  Serial.println("Starting ESP8266 channel 3!");
  WiFi.softAP("ESP_Channel3", "ESP_Channel3");
  Serial.println("WiFi AP Init: \n\tSSID: ESP_Channel3\n\tPW: ESP_Channel3");

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname("ESP_Channel3");

  ArduinoOTA.onStart([]() {
    channel3Deinit(); 
    /* 
        Important: Deinit library before updating, otherwise
        the Update gets stopped by the i2s interrupt of the
        channel3 library!
    */
  });

  ArduinoOTA.begin();
  Serial.println("OTA started!");
  Serial.printf("Update on %s:8266!", WiFi.softAPIP().toString().c_str());

  channel3Init(NTSC /* or PAL */, &loadFrame); 
}

void loop() {
  // Handle OTA requests
  ArduinoOTA.handle();
}