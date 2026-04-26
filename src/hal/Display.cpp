#include "Display.h"
#include "Arduino_GFX_Library.h"
#include "../../pin_config.h"

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_SDIO0 /* SDIO0 */, LCD_SDIO1 /* SDIO1 */,
  LCD_SDIO2 /* SDIO2 */, LCD_SDIO3 /* SDIO3 */);

static Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RESET /* RST */, 0 /* rotation */, LCD_WIDTH /* width */, LCD_HEIGHT /* height */, 6, 0, 0, 0);

static void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

static void rounder_event_cb(lv_event_t * e) {
    lv_area_t * area = (lv_area_t *)lv_event_get_param(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

void hal_display_init() {
  if (!gfx->begin()) {
    Serial.println("GFX Error");
  }
  gfx->fillScreen(RGB565_BLACK);
  
  lv_display_t * disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
  
  // Use PSRAM for buffers if possible. LVGL v9 handles this via malloc if configured.
  // We allocate 40 lines of buffer as in the original kino.ino
  void * buf1 = malloc(LCD_WIDTH * 40 * sizeof(lv_color_t));
  lv_display_set_buffers(disp, buf1, NULL, LCD_WIDTH * 40, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void hal_display_set_brightness(uint8_t brightness) {
  gfx->setBrightness(brightness);
}
