#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_display_init();
void hal_display_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif
