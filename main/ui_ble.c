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

static void update_labels(void) {
    ble_control_state_t state;
    ble_control_get_state(&state);

    const char *status = state.connected ? "CONNECTED" :
                         state.advertising ? "ADVERTISING" :
                         state.initialized ? "READY" : "OFF";

    lv_label_set_text_fmt(state_label, "BLE: %s", status);
    lv_label_set_text_fmt(result_label, "CMD: %s\n%s",
                          state.last_command[0] ? state.last_command : "--",
                          state.last_result[0] ? state.last_result : "--");
}

static void ble_timer_cb(lv_timer_t *timer) {
    (void)timer;
    update_labels();
}

static void ble_start_task(void *arg) {
    (void)arg;
    ble_control_set_active(true);
    ble_starting = false;
    vTaskDelete(NULL);
}

static void start_btn_event_cb(lv_event_t *e) {
    (void)e;
    ble_control_state_t state;
    ble_control_get_state(&state);

    if (ble_starting || state.initialized || state.advertising || state.connected) {
        if (result_label) {
            lv_label_set_text(result_label, "CMD: --\nBLE: already running");
        }
        return;
    }

    ble_starting = true;
    if (result_label) {
        lv_label_set_text(result_label, "CMD: --\nBLE: starting");
    }

    if (xTaskCreate(ble_start_task, "ble_start", 4096, NULL, 5, NULL) != pdPASS) {
        ble_starting = false;
        if (result_label) {
            lv_label_set_text(result_label, "CMD: --\nBLE: start task failed");
        }
    }
}

static void stop_btn_event_cb(lv_event_t *e) {
    (void)e;
    ble_control_set_active(false);
    update_labels();
}

void ui_ble_set_active(bool active) {
    ble_page_active = active;
    if (active) {
        if (ble_timer) {
            lv_timer_resume(ble_timer);
            lv_timer_ready(ble_timer);
        }
    } else {
        if (ble_timer) {
            lv_timer_pause(ble_timer);
        }
        ble_control_set_active(false);
    }
}

void ui_ble_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x05070A), 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "BLE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
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
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(state_label, LV_ALIGN_CENTER, 0, -28);
    lv_label_set_text(state_label, "BLE: OFF");

    lv_obj_t *start_btn = lv_button_create(tile);
    lv_obj_set_size(start_btn, 120, 46);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, -70, 30);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x27AE60), 0);
    lv_obj_set_style_radius(start_btn, 23, 0);
    lv_obj_add_event_cb(start_btn, start_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "START");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);
    lv_obj_center(start_label);

    lv_obj_t *stop_btn = lv_button_create(tile);
    lv_obj_set_size(stop_btn, 120, 46);
    lv_obj_align(stop_btn, LV_ALIGN_CENTER, 70, 30);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xC0392B), 0);
    lv_obj_set_style_radius(stop_btn, 23, 0);
    lv_obj_add_event_cb(stop_btn, stop_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);
    lv_obj_center(stop_label);

    lv_obj_t *svc = lv_label_create(tile);
    lv_label_set_text(svc, "SVC FFE0  WRITE FFE1  READ/NOTIFY FFE2");
    lv_obj_set_width(svc, 360);
    lv_obj_set_style_text_align(svc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(svc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(svc, lv_color_hex(0x888888), 0);
    lv_obj_align(svc, LV_ALIGN_CENTER, 0, 84);

    result_label = lv_label_create(tile);
    lv_obj_set_width(result_label, 340);
    lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(result_label, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_label_set_text(result_label, "CMD: --\nTap START");

    lv_obj_t *commands = lv_label_create(tile);
    lv_label_set_text(commands, "open close homing stop nfc status");
    lv_obj_set_width(commands, 360);
    lv_obj_set_style_text_align(commands, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(commands, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(commands, lv_color_hex(0x888888), 0);
    lv_obj_align(commands, LV_ALIGN_BOTTOM_MID, 0, -38);

    ble_timer = lv_timer_create(ble_timer_cb, 500, NULL);
    lv_timer_pause(ble_timer);
    ble_page_active = false;
}
