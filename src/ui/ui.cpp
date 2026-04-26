#include "ui.h"
#include "lvgl.h"
#include "../hal/WiFi.h"
#include <stdlib.h>

static lv_obj_t * wifi_label;

static void wifi_timer_cb(lv_timer_t * timer) {
  wifi_hal_status_t status = hal_wifi_get_status();
  if (status == WIFI_STATUS_CONNECTED) {
    String ip = hal_wifi_get_ip();
    lv_label_set_text_fmt(wifi_label, "IP: %s", ip.c_str());
    static bool printed = false;
    if (!printed) {
      Serial.print("WiFi Connected. IP: ");
      Serial.println(ip);
      printed = true;
    }
  } else if (status == WIFI_STATUS_CONNECTING) {
    lv_label_set_text(wifi_label, "WiFi: Connecting...");
  } else if (status == WIFI_STATUS_ERROR) {
    lv_label_set_text(wifi_label, "WiFi: Error");
  } else {
    lv_label_set_text(wifi_label, "WiFi: Disconnected");
  }
}

void ui_init() {
  lv_obj_t * screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

  // KINO Large Label
  lv_obj_t * kino_label = lv_label_create(screen);
  lv_label_set_text(kino_label, "KINO");
  lv_obj_set_style_text_font(kino_label, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(kino_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(kino_label, LV_ALIGN_CENTER, 0, -40);

  // Larger WiFi Status Label
  wifi_label = lv_label_create(screen);
  lv_label_set_text(wifi_label, "WiFi: Init...");
  lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x00FF00), 0);
  lv_obj_align_to(wifi_label, kino_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

  // Create a timer to update WiFi status
  lv_timer_create(wifi_timer_cb, 1000, NULL);
}
