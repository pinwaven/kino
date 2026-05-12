#ifndef UI_APP_H
#define UI_APP_H

#include <stdbool.h>
#include "lvgl.h"

// UI 初始化
void ui_init(void);

// 页面初始化函数
void ui_standby_init(lv_obj_t *tile);
void ui_standby_set_active(bool active);
void ui_motor_init(lv_obj_t *tile);
void ui_motor_set_active(bool active);
void ui_misc_init(lv_obj_t *tile);
void ui_nfc_init(lv_obj_t *tile);
void ui_nfc_init_compact(lv_obj_t *parent);
void ui_nfc_set_active(bool active);
void ui_sys_stats_init(lv_obj_t *tile);
void ui_sys_stats_set_active(bool active);
void ui_ble_init(lv_obj_t *tile);
void ui_ble_set_active(bool active);

#endif // UI_APP_H
