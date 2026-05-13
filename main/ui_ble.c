#include "ui_app.h"
#include "ble_control.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static lv_obj_t *state_label;
static lv_obj_t *result_label;
static lv_timer_t *ble_timer;
static bool ble_page_active;
static bool ble_starting;

#define BLE_UI_FONT (&lv_font_source_han_sans_sc_16_cjk)

static void set_ble_refresh_mode(bool active) {
    lv_timer_t *refr = lv_display_get_refr_timer(NULL);
    if (refr) {
        lv_timer_set_period(refr, active ? 75 : 16);
    }
}

static void update_labels(void) {
    ble_control_state_t state;
    ble_control_get_state(&state);

    const char *status = state.connected ? "已接" :
                         state.advertising ? "等待" :
                         state.initialized ? "可用" : "停止";

    lv_label_set_text_fmt(state_label, "BT: %s", status);
    lv_label_set_text_fmt(result_label, "命令: %s\n%s\n%s",
                          state.last_command[0] ? state.last_command : "--",
                          state.last_result[0] ? state.last_result : "--",
                          state.phy_status[0] ? state.phy_status : "PHY: --");
}

static void ble_timer_cb(lv_timer_t *timer) {
    (void)timer;
    update_labels();
}

static void ble_start_task(void *arg) {
    (void)arg;
    esp_err_t err = ble_control_set_active(true);
    if (err != ESP_OK) {
        set_ble_refresh_mode(false);
    }
    ble_starting = false;
    vTaskDelete(NULL);
}

static void start_btn_event_cb(lv_event_t *e) {
    (void)e;
    ble_control_state_t state;
    ble_control_get_state(&state);

    if (ble_starting || state.active || state.advertising || state.connected) {
        if (result_label) {
            lv_label_set_text(result_label, "命令: --\nBT: 已工作");
        }
        return;
    }

    ble_starting = true;
    set_ble_refresh_mode(true);
    if (result_label) {
        lv_label_set_text(result_label, "命令: --\nBT: 起动中");
    }

    if (xTaskCreate(ble_start_task, "ble_start", 4096, NULL, 5, NULL) != pdPASS) {
        ble_starting = false;
        if (result_label) {
            lv_label_set_text(result_label, "命令: --\nBT: 起动ERR");
        }
    }
}

static void stop_btn_event_cb(lv_event_t *e) {
    (void)e;
    ble_control_set_active(false);
    set_ble_refresh_mode(false);
    update_labels();
}

void ui_ble_set_active(bool active) {
    ble_page_active = active;
    if (active) {
        set_ble_refresh_mode(true);
        if (ble_timer) {
            lv_timer_resume(ble_timer);
            lv_timer_ready(ble_timer);
        }
    } else {
        if (ble_timer) {
            lv_timer_pause(ble_timer);
        }
        ble_control_set_active(false);
        set_ble_refresh_mode(false);
    }
}

void ui_ble_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x05070A), 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "BT控制");
    lv_obj_set_style_text_font(title, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *name = lv_label_create(tile);
    lv_label_set_text(name, BLE_CONTROL_DEVICE_NAME);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0x33D17A), 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 92);

    state_label = lv_label_create(tile);
    lv_obj_set_width(state_label, 320);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state_label, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(state_label, LV_ALIGN_CENTER, 0, -28);
    lv_label_set_text(state_label, "BT: 停止");

    lv_obj_t *start_btn = lv_button_create(tile);
    lv_obj_set_size(start_btn, 120, 46);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, -70, 30);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x27AE60), 0);
    lv_obj_set_style_radius(start_btn, 23, 0);
    lv_obj_add_event_cb(start_btn, start_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "起动");
    lv_obj_set_style_text_font(start_label, BLE_UI_FONT, 0);
    lv_obj_center(start_label);

    lv_obj_t *stop_btn = lv_button_create(tile);
    lv_obj_set_size(stop_btn, 120, 46);
    lv_obj_align(stop_btn, LV_ALIGN_CENTER, 70, 30);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xC0392B), 0);
    lv_obj_set_style_radius(stop_btn, 23, 0);
    lv_obj_add_event_cb(stop_btn, stop_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "停止");
    lv_obj_set_style_text_font(stop_label, BLE_UI_FONT, 0);
    lv_obj_center(stop_label);

    lv_obj_t *svc = lv_label_create(tile);
    lv_label_set_text(svc, "服務 FFE0  寫入 FFE1  通知 FFE2");
    lv_obj_set_width(svc, 360);
    lv_obj_set_style_text_align(svc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(svc, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(svc, lv_color_hex(0x888888), 0);
    lv_obj_align(svc, LV_ALIGN_CENTER, 0, 84);

    result_label = lv_label_create(tile);
    lv_obj_set_width(result_label, 340);
    lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(result_label, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(result_label, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_label_set_text(result_label, "命令: --\n點START");

    lv_obj_t *commands = lv_label_create(tile);
    lv_label_set_text(commands, "指令 open close homing stop nfc status hi");
    lv_obj_set_width(commands, 360);
    lv_obj_set_style_text_align(commands, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(commands, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(commands, lv_color_hex(0x888888), 0);
    lv_obj_align(commands, LV_ALIGN_BOTTOM_MID, 0, -38);

    ble_timer = lv_timer_create(ble_timer_cb, 500, NULL);
    lv_timer_pause(ble_timer);
    ble_page_active = false;
}
