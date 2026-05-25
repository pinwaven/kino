#ifndef UI_APP_H
#define UI_APP_H

#include <stdbool.h>
#include "lvgl.h"
#include "esp_err.h"

// UI 初始化
void ui_init(void);

// 页面初始化函数
void ui_standby_init(lv_obj_t *tile);
void ui_standby_set_active(bool active);
void ui_motor_init(lv_obj_t *tile);
void ui_motor_set_active(bool active);
void ui_misc_init(lv_obj_t *tile);
void ui_misc_set_active(bool active);
bool ui_misc_is_deep_sleep_enabled(void);
void ui_nfc_init(lv_obj_t *tile);
void ui_nfc_init_compact(lv_obj_t *parent);
void ui_nfc_set_active(bool active);
void ui_sys_stats_init(lv_obj_t *tile);
void ui_sys_stats_set_active(bool active);
void ui_ble_init(lv_obj_t *tile);
void ui_ble_set_active(bool active);

void ui_wifi_prov_init(lv_obj_t *tile);
void ui_wifi_prov_set_active(bool active);
void ui_wifi_prov_force_stop_now(void);
void ui_wifi_prov_on_move(void);

typedef enum {
    SYS_WORKER_SLEEP,
    SYS_WORKER_WAKE,
    SYS_WORKER_WIFI_AUTO_CONNECT,
    SYS_WORKER_PROV_START,
    SYS_WORKER_PROV_STOP,
    SYS_WORKER_BMS_POLL,
} sys_worker_req_t;

bool sys_worker_send_req(sys_worker_req_t req);

// WiFi provisioning integration helpers
void ui_wifi_prov_clear_task_pending(void);
bool ui_wifi_prov_get_stop_after_task(void);
void ui_wifi_prov_clear_stop_after_task(void);
bool ui_wifi_prov_is_page_active(void);

// Standby integration helpers
void ui_standby_clear_bms_pending(void);
void ui_standby_set_bms_err(esp_err_t err);

#endif // UI_APP_H
