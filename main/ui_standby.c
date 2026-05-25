#include "ui_app.h"
#include "stm32_interface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static lv_obj_t *main_val_label;
static lv_obj_t *main_unit_label;
static lv_obj_t *status_text;
static lv_obj_t *charge_icon;
static lv_obj_t *pd_badge; // 新增 PD 标识
static lv_timer_t *update_timer;

static lv_obj_t *rotate_cont;
static lv_obj_t *rotate_label;
static lv_obj_t *rotate_value;
static lv_obj_t *rotate_icon;
static lv_obj_t *psys_sw;
static lv_obj_t *psys_label;

static int rotate_idx = 0;
static volatile bool s_bms_poll_pending = false;
static volatile esp_err_t bms_last_err = ESP_ERR_INVALID_STATE;

void ui_standby_clear_bms_pending(void)
{
    s_bms_poll_pending = false;
}

void ui_standby_set_bms_err(esp_err_t err)
{
    bms_last_err = err;
}

static int battery_percent_from_mv(int vbatt_mv) {
    const int empty_mv = 6400;
    const int full_mv = 8650;

    if (vbatt_mv <= empty_mv) return 0;
    if (vbatt_mv >= full_mv) return 100;
    return (vbatt_mv - empty_mv) * 100 / (full_mv - empty_mv);
}

static void psys_sw_event_cb(lv_event_t * e) {
    lv_obj_t * sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        uint8_t psys_on[] = {0x5C, 0x0B, 0x03};
        stm32_cmd_send_action(CMD_I2C_WRITE, psys_on, 3);
        ESP_LOGI("UI_STANDBY", "PSYS Monitor Enabled");
    } else {
        uint8_t psys_off[] = {0x5C, 0x0B, 0x00};
        stm32_cmd_send_action(CMD_I2C_WRITE, psys_off, 3);
        ESP_LOGI("UI_STANDBY", "PSYS Monitor Disabled");
    }
}

static void update_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (!s_bms_poll_pending) {
        s_bms_poll_pending = true;
        if (!sys_worker_send_req(SYS_WORKER_BMS_POLL)) {
            s_bms_poll_pending = false;
        }
    }

    esp_err_t err = bms_last_err;
    stm32_state_t state;

    if (err != ESP_OK) {
        lv_label_set_text(main_val_label, "OFF");
        lv_label_set_text(status_text, "NO SIGNAL");
        return;
    }

    stm32_get_current_state(&state);

    // Toggle PSYS switch availability based on AC status
    if (state.bms.ac_ok) {
        lv_obj_add_state(psys_sw, LV_STATE_DISABLED);
        lv_obj_clear_state(psys_sw, LV_STATE_CHECKED);
        lv_label_set_text(psys_label, "PSYS (AC ON)");
    } else {
        lv_obj_remove_state(psys_sw, LV_STATE_DISABLED);
        lv_label_set_text(psys_label, "PSYS MONITOR");
    }

    if (state.bms.ac_ok) {
        lv_label_set_text_fmt(main_val_label, "%.2fV", state.bms.vin_mv / 1000.0f);
        lv_label_set_text(main_unit_label, "INPUT VOLTAGE");
        lv_obj_remove_flag(charge_icon, LV_OBJ_FLAG_HIDDEN);
        
        // PD 逻辑判断: Vin > 6.0V
        if (state.bms.vin_mv > 6000) {
            lv_obj_remove_flag(pd_badge, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(status_text, "FAST CHARGING (PD)");
            lv_obj_set_style_text_color(status_text, lv_color_hex(0x00D2FF), 0); // 科技蓝
        } else {
            lv_obj_add_flag(pd_badge, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(status_text, "STANDARD CHARGING");
            lv_obj_set_style_text_color(status_text, lv_color_hex(0x2ECC71), 0); // 绿色
        }
    } else {
        int batt_percent = battery_percent_from_mv(state.bms.vbatt_mv);
        lv_label_set_text_fmt(main_val_label, "%.2fV %d%%", state.bms.vbatt_mv / 1000.0f, batt_percent);
        lv_label_set_text(main_unit_label, "BATTERY VOLTAGE");
        lv_obj_add_flag(charge_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pd_badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status_text, "DISCHARGING");
        lv_obj_set_style_text_color(status_text, lv_color_hex(0x3498DB), 0);
    }

    static int tick_cnt = 0;
    if (++tick_cnt >= 10) {
        tick_cnt = 0;
        rotate_idx = (rotate_idx + 1) % 4;
        lv_obj_invalidate(rotate_cont); 
    }

    switch (rotate_idx) {
        case 0:
            lv_label_set_text(rotate_label, "CURRENT");
            lv_label_set_text(rotate_icon, LV_SYMBOL_CHARGE);
            if (state.bms.ac_ok) {
                lv_label_set_text_fmt(rotate_value, "+%d mA", state.bms.ichg_ma);
                lv_obj_set_style_text_color(rotate_value, lv_color_hex(0x2ECC71), 0);
            } else {
                lv_label_set_text_fmt(rotate_value, "-%d mA", state.bms.idchg_ma);
                lv_obj_set_style_text_color(rotate_value, lv_color_hex(0xE74C3C), 0);
            }
            break;
        case 1:
            lv_label_set_text(rotate_label, "TEMPERATURE");
            lv_label_set_text(rotate_icon, LV_SYMBOL_LIST);
            lv_label_set_text_fmt(rotate_value, "%.1f C", state.bms.tj_c);
            lv_obj_set_style_text_color(rotate_value, lv_color_hex(0xF39C12), 0);
            break;
        case 2:
            lv_label_set_text(rotate_label, "SYSTEM POWER");
            lv_label_set_text(rotate_icon, LV_SYMBOL_SETTINGS);
            lv_label_set_text_fmt(rotate_value, "%d mW", state.bms.psys_mw);
            lv_obj_set_style_text_color(rotate_value, lv_color_hex(0x9B59B6), 0);
            break;
        case 3:
            if (state.bms.ac_ok) {
                int batt_percent = battery_percent_from_mv(state.bms.vbatt_mv);
                lv_label_set_text(rotate_label, "BATTERY");
                lv_label_set_text_fmt(rotate_value, "%.2f V  %d%%", state.bms.vbatt_mv / 1000.0f, batt_percent);
            } else {
                lv_label_set_text(rotate_label, "INPUT VIN");
                lv_label_set_text(rotate_value, "0.00 V");
            }
            lv_label_set_text(rotate_icon, LV_SYMBOL_EYE_OPEN);
            lv_obj_set_style_text_color(rotate_value, lv_color_hex(0xBDC3C7), 0);
            break;
    }
}

void ui_standby_set_active(bool active) {
    if (!update_timer) return;

    if (active) {
        s_bms_poll_pending = true;
        if (!sys_worker_send_req(SYS_WORKER_BMS_POLL)) {
            s_bms_poll_pending = false;
        }
        lv_timer_resume(update_timer);
        lv_timer_ready(update_timer);
    } else {
        lv_timer_pause(update_timer);
    }
}

void ui_standby_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x000000), 0);

    status_text = lv_label_create(tile);
    lv_obj_set_width(status_text, 300);
    lv_obj_set_style_text_align(status_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_text, "INITIALIZING...");
    lv_obj_set_style_text_font(status_text, &lv_font_montserrat_14, 0);
    lv_obj_align(status_text, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *main_cont = lv_obj_create(tile);
    lv_obj_set_size(main_cont, 300, 120);
    lv_obj_set_style_bg_opa(main_cont, 0, 0);
    lv_obj_set_style_border_width(main_cont, 0, 0);
    lv_obj_align(main_cont, LV_ALIGN_CENTER, 0, -20);
    lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);

    charge_icon = lv_label_create(main_cont);
    lv_label_set_text(charge_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(charge_icon, lv_color_hex(0xF1C40F), 0);
    lv_obj_align(charge_icon, LV_ALIGN_TOP_MID, 0, 0);

    // PD Badge 标识
    pd_badge = lv_label_create(main_cont);
    lv_label_set_text(pd_badge, "PD");
    lv_obj_set_style_text_font(pd_badge, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pd_badge, lv_color_hex(0x00D2FF), 0);
    lv_obj_set_style_bg_color(pd_badge, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(pd_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pd_badge, 4, 0);
    lv_obj_set_style_pad_hor(pd_badge, 4, 0);
    lv_obj_align_to(pd_badge, charge_icon, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_add_flag(pd_badge, LV_OBJ_FLAG_HIDDEN); // 默认隐藏

    main_val_label = lv_label_create(main_cont);
    lv_obj_set_style_text_font(main_val_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(main_val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(main_val_label, LV_ALIGN_CENTER, 0, 10);

    main_unit_label = lv_label_create(main_cont);
    lv_obj_set_width(main_unit_label, 200);
    lv_obj_set_style_text_align(main_unit_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(main_unit_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(main_unit_label, lv_color_hex(0x666666), 0);
    lv_obj_align(main_unit_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    rotate_cont = lv_obj_create(tile);
    lv_obj_set_size(rotate_cont, 240, 90);
    lv_obj_set_style_bg_color(rotate_cont, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(rotate_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(rotate_cont, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(rotate_cont, 1, 0);
    lv_obj_set_style_radius(rotate_cont, 25, 0);
    lv_obj_align(rotate_cont, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_clear_flag(rotate_cont, LV_OBJ_FLAG_SCROLLABLE);

    rotate_icon = lv_label_create(rotate_cont);
    lv_obj_align(rotate_icon, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_color(rotate_icon, lv_color_hex(0xAAAAAA), 0);

    rotate_label = lv_label_create(rotate_cont);
    lv_obj_set_style_text_font(rotate_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rotate_label, lv_color_hex(0x888888), 0);
    lv_obj_align(rotate_label, LV_ALIGN_TOP_LEFT, 55, 15);

    rotate_value = lv_label_create(rotate_cont);
    lv_obj_set_style_text_font(rotate_value, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(rotate_value, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(rotate_value, LV_ALIGN_TOP_LEFT, 55, 40);

    lv_obj_t *psys_cont = lv_obj_create(tile);
    lv_obj_set_size(psys_cont, 240, 48);
    lv_obj_set_style_bg_opa(psys_cont, 0, 0);
    lv_obj_set_style_border_width(psys_cont, 0, 0);
    lv_obj_align(psys_cont, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_clear_flag(psys_cont, LV_OBJ_FLAG_SCROLLABLE);

    psys_label = lv_label_create(psys_cont);
    lv_label_set_text(psys_label, "PSYS MONITOR");
    lv_obj_set_style_text_font(psys_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(psys_label, lv_color_hex(0x888888), 0);
    lv_obj_align(psys_label, LV_ALIGN_LEFT_MID, 8, 0);

    psys_sw = lv_switch_create(psys_cont);
    lv_obj_set_size(psys_sw, 54, 28);
    lv_obj_align(psys_sw, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(psys_sw, psys_sw_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    update_timer = lv_timer_create(update_timer_cb, 300, NULL);
    lv_timer_pause(update_timer);
}
