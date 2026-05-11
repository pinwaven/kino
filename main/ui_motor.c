#include "ui_app.h"
#include "stm32_interface.h"
#include "esp_log.h"
#include <string.h>

static lv_obj_t *action_label;
static lv_obj_t *status_label;
static lv_timer_t *motor_timer;
static bool motor_page_active;
static bool motor_polling;

#define MOTOR_ACTION_DRAIN_MS 180

static bool text_has(const char *text, const char *needle) {
    return text && needle && strstr(text, needle) != NULL;
}

static bool motor_status_is_terminal(const stm32_state_t *state) {
    return text_has(state->motor_action, "idle") ||
           text_has(state->motor_action, "none") ||
           text_has(state->motor_status, "idle") ||
           text_has(state->motor_status, "done") ||
           text_has(state->motor_status, "finish") ||
           text_has(state->motor_status, "stop") ||
           text_has(state->motor_status, "cancel") ||
           text_has(state->motor_status, "error") ||
           text_has(state->motor_status, "timeout");
}

static void motor_poll_start(void) {
    motor_polling = true;
    if (motor_page_active && motor_timer) {
        lv_timer_resume(motor_timer);
        lv_timer_reset(motor_timer);
    }
}

static void motor_poll_stop(void) {
    motor_polling = false;
    if (motor_timer) {
        lv_timer_pause(motor_timer);
    }
}

static void btn_event_cb(lv_event_t * e) {
    uintptr_t type = (uintptr_t)lv_event_get_user_data(e);
    esp_err_t err = ESP_OK;

    switch(type) {
        case 0: // Open: move_dur(15000, 3750)
            {
                uint8_t payload[8];
                int32_t speed = 15000;
                uint32_t duration = 3250;
                memcpy(&payload[0], &speed, 4);
                memcpy(&payload[4], &duration, 4);
                err = stm32_cmd_send_action_drain(CMD_ACTION_MOVE_DUR, payload, 8, MOTOR_ACTION_DRAIN_MS);
            }
            break;
        case 1: // Close: move_to_ss(-12000, 8000)
            {
                uint8_t payload[9];
                int32_t speed = -12000;
                uint32_t timeout = 8000;
                uint8_t sensor = 0x00;
                memcpy(&payload[0], &speed, 4);
                memcpy(&payload[4], &timeout, 4);
                payload[8] = sensor;
                err = stm32_cmd_send_action_drain(CMD_ACTION_MOVE_SS, payload, 9, MOTOR_ACTION_DRAIN_MS);
            }
            break;
        case 2: // Homing
            {
                uint32_t timeout = 30000;
                err = stm32_cmd_send_action_drain(CMD_ACTION_HOMING, (uint8_t*)&timeout, 4, MOTOR_ACTION_DRAIN_MS);
            }
            break;
        case 3: // Stop
            err = stm32_cmd_send_action_drain(CMD_ACTION_CANCEL, NULL, 0, MOTOR_ACTION_DRAIN_MS);
            motor_poll_stop();
            lv_label_set_text(action_label, "ACT: cancel");
            lv_label_set_text(status_label, "STS: stopped");
            break;
    }

    if (err != ESP_OK) {
        lv_label_set_text_fmt(status_label, "SEND ERR: %s", esp_err_to_name(err));
        return;
    }

    if (type != 3) {
        motor_poll_start();
    }
}

static void motor_timer_cb(lv_timer_t *timer) {
    esp_err_t err = stm32_update_motor_poll();
    stm32_state_t state;

    if (err != ESP_OK) {
        lv_label_set_text_fmt(status_label, "POLL ERR: %s", esp_err_to_name(err));
        return;
    }

    stm32_get_current_state(&state);
    
    if(state.is_connected) {
        lv_label_set_text_fmt(action_label, "ACT: %s", state.motor_action);
        lv_label_set_text_fmt(status_label, "STS: %s", state.motor_status);

        if (motor_status_is_terminal(&state)) {
            motor_poll_stop();
        }
    }
}

void ui_motor_set_active(bool active) {
    motor_page_active = active;
    if (!motor_timer) return;

    if (active && motor_polling) {
        lv_timer_resume(motor_timer);
        lv_timer_ready(motor_timer);
    } else {
        lv_timer_pause(motor_timer);
    }
}

void ui_motor_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    const char *btn_names[] = {"CLOSE", "HOMING", "OPEN", "STOP"};
    lv_color_t btn_colors[] = {lv_color_hex(0xE67E22), lv_color_hex(0x2980B9), lv_color_hex(0x27AE60), lv_color_hex(0xC0392B)};
    uintptr_t btn_actions[] = {1, 2, 0, 3};
    lv_coord_t btn_w[] = {170, 170, 170, 82};
    lv_coord_t btn_h[] = {54, 54, 54, 70};
    lv_align_t btn_align[] = {LV_ALIGN_CENTER, LV_ALIGN_CENTER, LV_ALIGN_CENTER, LV_ALIGN_RIGHT_MID};
    lv_coord_t btn_x[] = {0, 0, 0, -20};
    lv_coord_t btn_y[] = {-94, -22, 50, -20};

    for(int i=0; i<4; i++) {
        lv_obj_t *btn = lv_button_create(tile);
        lv_obj_set_size(btn, btn_w[i], btn_h[i]);
        lv_obj_align(btn, btn_align[i], btn_x[i], btn_y[i]);
        lv_obj_set_style_bg_color(btn, btn_colors[i], 0);
        lv_obj_set_style_radius(btn, i == 3 ? 12 : 27, 0);
        
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, btn_names[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
        
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void*)btn_actions[i]);
    }

    ui_nfc_init_compact(tile);

    action_label = lv_label_create(tile);
    lv_obj_set_width(action_label, 320);
    lv_obj_set_style_text_align(action_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(action_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(action_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(action_label, LV_ALIGN_BOTTOM_MID, 0, -82);
    lv_label_set_text(action_label, "ACT: --");

    status_label = lv_label_create(tile);
    lv_obj_set_width(status_label, 320);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -62);
    lv_label_set_text(status_label, "STS: --");

    motor_timer = lv_timer_create(motor_timer_cb, 300, NULL);
    lv_timer_pause(motor_timer);
    motor_page_active = false;
    motor_polling = false;
}
