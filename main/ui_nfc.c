#include "ui_app.h"
#include "stm32_interface.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_NFC";

static lv_obj_t *scan_switch;
static lv_obj_t *state_label;
static lv_obj_t *uid_label;
static lv_timer_t *nfc_timer;

static void nfc_timer_stop(void) {
    if (nfc_timer) {
        lv_timer_delete(nfc_timer);
        nfc_timer = NULL;
    }
}

static void nfc_read_once(void) {
    char resp[96];
    esp_err_t err = stm32_cmd_request_timeout(CMD_NFC_UUID, NULL, 0, resp, sizeof(resp), 1200);

    if (err != ESP_OK) {
        lv_label_set_text_fmt(state_label, "NFC ERR: %s", esp_err_to_name(err));
        lv_label_set_text(uid_label, "UID: --");
        ESP_LOGW(TAG, "NFC request failed: %s", esp_err_to_name(err));
        return;
    }

    if (strncmp(resp, "nfc_uuid:", 9) != 0) {
        lv_label_set_text(state_label, "NFC: UNEXPECTED");
        lv_label_set_text_fmt(uid_label, "UID: %s", resp);
        return;
    }

    const char *payload = resp + 9;
    if (strncmp(payload, "err:", 4) == 0) {
        lv_label_set_text_fmt(state_label, "NFC %s", payload);
        lv_label_set_text(uid_label, "UID: --");
        return;
    }

    lv_label_set_text(state_label, "NFC: UID");
    lv_label_set_text_fmt(uid_label, "UID: %s", payload);
}

static void nfc_timer_cb(lv_timer_t *timer) {
    (void)timer;
    nfc_read_once();
}

static void nfc_timer_start(void) {
    if (!nfc_timer) {
        nfc_timer = lv_timer_create(nfc_timer_cb, 1000, NULL);
    }
    nfc_read_once();
}

static void scan_switch_event_cb(lv_event_t * e) {
    lv_obj_t *sw = lv_event_get_target(e);

    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        lv_label_set_text(state_label, "NFC: SCANNING");
        nfc_timer_start();
    } else {
        nfc_timer_stop();
        lv_label_set_text(state_label, "NFC: STOPPED");
    }
}

void ui_nfc_set_active(bool active) {
    if (!active) {
        nfc_timer_stop();
        if (scan_switch) {
            lv_obj_remove_state(scan_switch, LV_STATE_CHECKED);
        }
        if (state_label) {
            lv_label_set_text(state_label, "NFC: STOPPED");
        }
    }
}

static void nfc_build(lv_obj_t *parent, bool compact) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, compact ? 300 : 300, compact ? 150 : 260);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_align(cont, compact ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, compact ? 0 : -20);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cont, compact ? 8 : 16, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "NFC UID");
    lv_obj_set_style_text_font(title, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    scan_switch = lv_switch_create(cont);
    lv_obj_set_size(scan_switch, compact ? 64 : 84, compact ? 32 : 42);
    lv_obj_add_event_cb(scan_switch, scan_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    state_label = lv_label_create(cont);
    lv_obj_set_width(state_label, 280);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state_label, compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x33D17A), 0);
    lv_label_set_text(state_label, "STOPPED");

    uid_label = lv_label_create(cont);
    lv_obj_set_width(uid_label, 280);
    lv_obj_set_style_text_align(uid_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(uid_label, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(uid_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(uid_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(uid_label, "--");

    ui_nfc_set_active(false);
}

void ui_nfc_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x061410), 0);
    nfc_build(tile, false);
}

void ui_nfc_init_compact(lv_obj_t *parent) {
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "NFC");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x33D17A), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 34, -48);

    scan_switch = lv_switch_create(parent);
    lv_obj_set_size(scan_switch, 58, 30);
    lv_obj_align(scan_switch, LV_ALIGN_LEFT_MID, 18, -18);
    lv_obj_add_event_cb(scan_switch, scan_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    state_label = lv_label_create(parent);
    lv_obj_set_width(state_label, 300);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x33D17A), 0);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_label_set_text(state_label, "NFC: STOPPED");

    uid_label = lv_label_create(parent);
    lv_obj_set_width(uid_label, 320);
    lv_obj_set_style_text_align(uid_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(uid_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(uid_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(uid_label, LV_LABEL_LONG_DOT);
    lv_obj_align(uid_label, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_label_set_text(uid_label, "UID: --");

    ui_nfc_set_active(false);
}
