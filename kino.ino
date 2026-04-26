#include <lvgl.h>
#include "src/hal/Display.h"
#include "src/hal/Touch.h"
#include "src/hal/WiFi.h"
#include "src/hal/local_config.h"
#include "src/ui/ui.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Kino Biomarker Analyzer v1.0");

  // Initialize LVGL
  lv_init();

  // Initialize Hardware Abstraction Layer
  hal_display_init();
  hal_display_set_brightness(200);
  hal_touch_init();
  
  hal_wifi_init();
  hal_wifi_connect(WIFI_SSID, WIFI_PASSWORD);

  // Initialize User Interface
  ui_init();

  Serial.println("Setup Complete");
}

void loop() {
  lv_timer_handler();
  lv_tick_inc(5);
  delay(5);
}
