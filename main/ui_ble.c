#include "ui_app.h"
#include "ble_control.h"
#include "esp_err.h"
#include <stdio.h>

static lv_obj_t *state_label;
static lv_obj_t *result_label;
static lv_timer_t *ble_timer;
static bool ble_page_active;

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

void ui_ble_set_active(bool active) {
    ble_page_active = active;
    if (active) {
        esp_err_t err = ble_control_set_active(true);
        if (err != ESP_OK && result_label) {
            lv_label_set_text_fmt(result_label, "BLE init error:\n%s", esp_err_to_name(err));
        }
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

    lv_obj_t *svc = lv_label_create(tile);
    lv_label_set_text(svc, "SVC FFE0  WRITE FFE1  READ/NOTIFY FFE2");
    lv_obj_set_width(svc, 360);
    lv_obj_set_style_text_align(svc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(svc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(svc, lv_color_hex(0x888888), 0);
    lv_obj_align(svc, LV_ALIGN_CENTER, 0, 16);

    result_label = lv_label_create(tile);
    lv_obj_set_width(result_label, 340);
    lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(result_label, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_label_set_text(result_label, "CMD: --\nBLE: off");

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
