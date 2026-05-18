#include "ui_app.h"
#include "esp_log.h"
#include "bsp/display.h"
#include "stm32_interface.h"
#include "wifi_prov.h"
#include "test_flow.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define DIAG_PASSCODE "123456"

static const char *TAG = "UI_MANAGER";

static lv_obj_t *main_page;
static lv_obj_t *diagnostic_page;
static lv_obj_t *main_tv;
static lv_obj_t *main_tile;
static lv_obj_t *pass_tile;
static lv_obj_t *tv;
static lv_obj_t *t1;
static lv_obj_t *t2;
static lv_obj_t *t3;
static lv_obj_t *t4;
static lv_obj_t *t5;
static lv_obj_t *t6;
static lv_obj_t *t7;
static lv_obj_t *overlay;
static lv_obj_t *pass_code_label;

static lv_obj_t *flow_status_label;
static lv_obj_t *flow_hint_label;
static lv_obj_t *flow_wifi_label;
static lv_obj_t *flow_batt_label;
static lv_obj_t *eye_lid;
static lv_obj_t *eye_pupil;
static lv_obj_t *eye_iris;
static lv_obj_t *eye_brow_l;
static lv_obj_t *eye_brow_r;
static lv_obj_t *eye_cheek_l;
static lv_obj_t *eye_cheek_r;
static lv_obj_t *eye_mood_label;
static lv_obj_t *sleep_label;
static lv_timer_t *flow_timer;
static int eye_tick;

static bool is_dimmed = false;
static bool allow_auto_dim = true;
static const int BRIGHTNESS_NORMAL = 66;
static const int BRIGHTNESS_DIMMED = 20;
static const uint32_t INACTIVITY_TIMEOUT_MS = 10000;
static const uint32_t CARD_AWAKE_HOLD_MS = 60000;
static const uint32_t REFR_PERIOD_NORMAL = 16;
static const uint32_t REFR_PERIOD_DIMMED = 200;
static uint32_t keep_awake_until_ms;
static test_flow_state_t last_flow_state = TEST_FLOW_PREP_HOMING;
static test_flow_state_t current_flow_state = TEST_FLOW_PREP_HOMING;

static void update_active_page(lv_obj_t *active_tile);
static void show_main_flow(void);
static void show_diagnostic(void);

typedef enum {
    EYE_MOOD_IDLE,
    EYE_MOOD_FOCUS,
    EYE_MOOD_HAPPY,
    EYE_MOOD_ERROR,
    EYE_MOOD_SLEEP,
} eye_mood_t;

static void wifi_auto_connect_task(void *arg)
{
    (void)arg;
    wifi_prov_auto_connect_saved();
    vTaskDelete(NULL);
}

static int battery_percent_from_mv(int vbatt_mv)
{
    const int empty_mv = 6400;
    const int full_mv = 8650;

    if (vbatt_mv <= empty_mv) return 0;
    if (vbatt_mv >= full_mv) return 100;
    return (vbatt_mv - empty_mv) * 100 / (full_mv - empty_mv);
}

static void restore_screen_now(void)
{
    bsp_display_brightness_set(BRIGHTNESS_NORMAL);
    lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_NORMAL);
    is_dimmed = false;
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_display_trigger_activity(NULL);
}

static void keep_awake_for(uint32_t ms)
{
    keep_awake_until_ms = lv_tick_get() + ms;
    lv_display_trigger_activity(NULL);
}

static bool keep_awake_active(void)
{
    return keep_awake_until_ms != 0 && (int32_t)(keep_awake_until_ms - lv_tick_get()) > 0;
}

static eye_mood_t mood_for_state(test_flow_state_t state)
{
    switch (state) {
    case TEST_FLOW_WAIT_CARD:
    case TEST_FLOW_PREP_HOMING:
    case TEST_FLOW_PREP_OPENING:
        return EYE_MOOD_IDLE;
    case TEST_FLOW_DONE:
    case TEST_FLOW_SUCCESS_EJECTING:
        return EYE_MOOD_HAPPY;
    case TEST_FLOW_NFC_ERROR:
    case TEST_FLOW_API_ERROR:
    case TEST_FLOW_CARD_DETECT_ERROR:
    case TEST_FLOW_RECOVERY_CLOSING:
    case TEST_FLOW_RECOVERY_OPENING:
        return EYE_MOOD_ERROR;
    default:
        return EYE_MOOD_FOCUS;
    }
}

static void set_eye_mood(eye_mood_t mood)
{
    lv_color_t iris = lv_color_hex(0x43C6AC);
    lv_color_t border = lv_color_hex(0x77D6C8);
    const char *mark = "";
    bool cheeks = false;
    lv_coord_t brow_y = 18;
    lv_coord_t brow_l_x = 46;
    lv_coord_t brow_r_x = 134;

    switch (mood) {
    case EYE_MOOD_FOCUS:
        iris = lv_color_hex(0x58A6FF);
        border = lv_color_hex(0x6EA8FE);
        mark = "...";
        brow_y = 8;
        break;
    case EYE_MOOD_HAPPY:
        iris = lv_color_hex(0xFFD166);
        border = lv_color_hex(0xFFB703);
        mark = "OK";
        cheeks = true;
        brow_y = 28;
        break;
    case EYE_MOOD_ERROR:
        iris = lv_color_hex(0xFF6B6B);
        border = lv_color_hex(0xFF8787);
        mark = "!";
        brow_y = 4;
        brow_l_x = 60;
        brow_r_x = 120;
        break;
    case EYE_MOOD_SLEEP:
        iris = lv_color_hex(0x5E6B78);
        border = lv_color_hex(0x5E6B78);
        mark = "Zzz";
        break;
    case EYE_MOOD_IDLE:
    default:
        mark = "";
        break;
    }

    lv_obj_set_style_bg_color(eye_iris, iris, 0);
    lv_obj_set_style_border_color(eye_lid, border, 0);
    lv_label_set_text(eye_mood_label, mark);
    lv_obj_align(eye_brow_l, LV_ALIGN_TOP_LEFT, brow_l_x, brow_y);
    lv_obj_align(eye_brow_r, LV_ALIGN_TOP_LEFT, brow_r_x, brow_y);

    if (cheeks) {
        lv_obj_remove_flag(eye_cheek_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(eye_cheek_r, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(eye_cheek_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye_cheek_r, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_auto_dim(bool enabled)
{
    allow_auto_dim = enabled;
    if (!allow_auto_dim && is_dimmed) {
        restore_screen_now();
    }
}

static void overlay_event_cb(lv_event_t *e)
{
    (void)e;
    if (is_dimmed) {
        restore_screen_now();
        ESP_LOGI(TAG, "Screen restored by touch on overlay");
    }
}

static void inactivity_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t inactive_time = lv_display_get_inactive_time(NULL);

    if (!allow_auto_dim) {
        if (is_dimmed) {
            restore_screen_now();
        }
        lv_display_trigger_activity(NULL);
        return;
    }

    if (current_flow_state != TEST_FLOW_WAIT_CARD) {
        if (is_dimmed) {
            restore_screen_now();
        }
        lv_display_trigger_activity(NULL);
        return;
    }

    if (keep_awake_active()) {
        if (is_dimmed) {
            restore_screen_now();
        }
        lv_display_trigger_activity(NULL);
        return;
    }

    if (inactive_time >= INACTIVITY_TIMEOUT_MS && !is_dimmed) {
        bsp_display_brightness_set(BRIGHTNESS_DIMMED);
        lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_DIMMED);
        is_dimmed = true;
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Screen dimmed due to %dms inactivity", (int)inactive_time);
    }
}

static void flow_set_active(bool active)
{
    if (!flow_timer) return;

    if (active) {
        lv_timer_resume(flow_timer);
        lv_timer_ready(flow_timer);
    } else {
        lv_timer_pause(flow_timer);
    }
}

static void update_test_flow_ui(void)
{
    test_flow_snapshot_t flow;
    test_flow_update();
    test_flow_get_snapshot(&flow);

    lv_label_set_text(flow_status_label, test_flow_status_text(flow.state));
    lv_label_set_text(flow_hint_label, test_flow_hint_text(&flow));
    lv_obj_set_style_text_color(flow_hint_label,
                                flow.state == TEST_FLOW_CARD_DETECT_ERROR ? lv_color_hex(0xFF6B6B) :
                                flow.state == TEST_FLOW_CARD_DETECTED ? lv_color_hex(0x73E0C4) :
                                lv_color_hex(0xB6C2CF),
                                0);

    if (last_flow_state != TEST_FLOW_CARD_DETECTED && flow.state == TEST_FLOW_CARD_DETECTED) {
        keep_awake_for(CARD_AWAKE_HOLD_MS);
        if (is_dimmed) {
            restore_screen_now();
        }
        ESP_LOGI(TAG, "Card inserted, screen awake for %ums", (unsigned)CARD_AWAKE_HOLD_MS);
    }
    last_flow_state = flow.state;
    current_flow_state = flow.state;
}

static void flow_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    eye_tick++;

    if (is_dimmed) {
        if ((eye_tick % 10) == 1) {
            update_test_flow_ui();
        }

        set_eye_mood(EYE_MOOD_SLEEP);
        lv_obj_add_flag(eye_pupil, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(sleep_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(sleep_label, (eye_tick % 12 < 6) ? "Zzz" : "zzZ");
        lv_obj_set_style_bg_color(eye_lid, lv_color_hex(0x1B2630), 0);
        lv_obj_set_style_border_color(eye_lid, lv_color_hex(0x5E6B78), 0);
        lv_obj_set_height(eye_lid, 24);
        lv_obj_align(eye_lid, LV_ALIGN_CENTER, 0, 8);

        if ((eye_tick % 20) == 1) {
            lv_label_set_text(flow_wifi_label, LV_SYMBOL_WIFI " --");
            lv_label_set_text(flow_batt_label, "--%");
        }
        return;
    }

    lv_obj_remove_flag(eye_pupil, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sleep_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(eye_lid, lv_color_hex(0xF7FBFF), 0);
    eye_mood_t mood = mood_for_state(current_flow_state);
    set_eye_mood(mood);

    static const int8_t pupil_x_path[] = {
        0, 2, 4, 7, 10, 12, 14, 13,
        11, 7, 3, 0, -2, -5, -9, -12,
        -14, -13, -10, -6, -2, 0, 3, 7,
        10, 8, 5, 2, 0, -1, 0, 1
    };
    static const int8_t pupil_y_path[] = {
        5, 5, 4, 4, 5, 6, 7, 7,
        6, 5, 4, 4, 5, 6, 7, 7,
        6, 5, 4, 4, 5, 6, 7, 8,
        7, 6, 5, 4, 4, 5, 6, 5
    };
    int path_idx = (eye_tick / (mood == EYE_MOOD_FOCUS ? 4 : 2)) % (int)(sizeof(pupil_x_path) / sizeof(pupil_x_path[0]));
    int pupil_x = pupil_x_path[path_idx];
    int pupil_y = pupil_y_path[path_idx];
    if (mood == EYE_MOOD_FOCUS) {
        pupil_x = (eye_tick % 18 < 9) ? -4 : 4;
        pupil_y = 5;
    } else if (mood == EYE_MOOD_HAPPY) {
        pupil_y = 1;
    } else if (mood == EYE_MOOD_ERROR) {
        pupil_x = (eye_tick % 4 < 2) ? -7 : 7;
        pupil_y = 7;
    }
    lv_obj_align(eye_pupil, LV_ALIGN_CENTER, pupil_x, pupil_y);

    int blink = eye_tick % 118;
    int lid_h = 116;
    if (mood == EYE_MOOD_HAPPY) {
        lid_h = 76;
    }
    if (blink >= 96 && blink <= 101) {
        static const int blink_h[] = {86, 52, 24, 42, 78, 116};
        lid_h = blink_h[blink - 96];
    } else if (blink >= 104 && blink <= 106) {
        static const int blink_h[] = {72, 28, 116};
        lid_h = blink_h[blink - 104];
    }

    lv_obj_set_height(eye_lid, lid_h);
    lv_obj_align(eye_lid, LV_ALIGN_CENTER, 0, (eye_tick % 80 < 40) ? -1 : 1);

    if ((eye_tick % 10) == 1) {
        update_test_flow_ui();
    }

    if ((eye_tick % 20) != 1) {
        return;
    }

    wifi_prov_status_t wifi_status;
    wifi_prov_get_status(&wifi_status);
    switch (wifi_status.state) {
    case WIFI_PROV_STATE_CONNECTED:
        lv_label_set_text_fmt(flow_wifi_label, LV_SYMBOL_WIFI " %s", wifi_status.got_ip);
        break;
    case WIFI_PROV_STATE_CONNECTING:
        lv_label_set_text(flow_wifi_label, LV_SYMBOL_WIFI " ...");
        break;
    default:
        lv_label_set_text(flow_wifi_label, LV_SYMBOL_WIFI " --");
        break;
    }

    stm32_state_t state;
    if (stm32_update_bmsinfo() == ESP_OK) {
        stm32_get_current_state(&state);
        int batt_percent = battery_percent_from_mv(state.bms.vbatt_mv);
        if (state.bms.ac_ok) {
            lv_label_set_text_fmt(flow_batt_label, LV_SYMBOL_CHARGE " %d%%", batt_percent);
        } else {
            lv_label_set_text_fmt(flow_batt_label, "%d%%", batt_percent);
        }
    } else {
        lv_label_set_text(flow_batt_label, "--%");
    }
}

static void pass_overlay_close(void)
{
    if (pass_code_label) {
        lv_label_set_text(pass_code_label, "");
    }

    if (main_tv) {
        lv_obj_set_tile_id(main_tv, 0, 0, LV_ANIM_ON);
    }

}

static void pass_digit_cb(lv_event_t *e)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    const char *digit = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    const char *old = lv_label_get_text(label);
    char code[8] = {0};

    if (strlen(old) >= 6) return;

    snprintf(code, sizeof(code), "%s%s", old, digit);
    lv_label_set_text(label, code);

    if (strlen(code) == 6) {
        if (strcmp(code, DIAG_PASSCODE) == 0) {
            pass_overlay_close();
            show_diagnostic();
        } else {
            lv_label_set_text(label, "");
        }
    }
}

static void pass_backspace_cb(lv_event_t *e)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    char code[8];
    strncpy(code, lv_label_get_text(label), sizeof(code) - 1);
    code[sizeof(code) - 1] = '\0';
    size_t len = strlen(code);
    if (len > 0) {
        code[len - 1] = '\0';
        lv_label_set_text(label, code);
    }
}

static void pass_reset_cb(lv_event_t *e)
{
    (void)e;
    pass_overlay_close();
}

static lv_obj_t *create_key(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 64, 48);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1F2933), 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);
    return btn;
}

static void build_pass_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x05080D), 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "DIAGNOSTIC");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    pass_code_label = lv_label_create(tile);
    lv_label_set_text(pass_code_label, "");
    lv_obj_set_width(pass_code_label, 220);
    lv_obj_set_style_text_align(pass_code_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(pass_code_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pass_code_label, lv_color_hex(0x58A6FF), 0);
    lv_obj_align(pass_code_label, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *grid = lv_obj_create(tile);
    lv_obj_set_size(grid, 230, 230);
    lv_obj_set_style_bg_opa(grid, 0, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 1; i <= 9; i++) {
        char text[2] = {(char)('0' + i), '\0'};
        lv_obj_t *btn = create_key(grid, text);
        lv_obj_add_event_cb(btn, pass_digit_cb, LV_EVENT_CLICKED, pass_code_label);
    }

    lv_obj_t *close = create_key(grid, "X");
    lv_obj_add_event_cb(close, pass_reset_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *zero = create_key(grid, "0");
    lv_obj_add_event_cb(zero, pass_digit_cb, LV_EVENT_CLICKED, pass_code_label);

    lv_obj_t *back = create_key(grid, "<");
    lv_obj_add_event_cb(back, pass_backspace_cb, LV_EVENT_CLICKED, pass_code_label);
}

static void build_eye_wizard(lv_obj_t *parent)
{
    lv_obj_t *eye = lv_obj_create(parent);
    lv_obj_set_size(eye, 220, 150);
    lv_obj_align(eye, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(eye, 0, 0);
    lv_obj_set_style_border_width(eye, 0, 0);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);

    eye_lid = lv_obj_create(eye);
    lv_obj_set_size(eye_lid, 190, 116);
    lv_obj_center(eye_lid);
    lv_obj_set_style_radius(eye_lid, 58, 0);
    lv_obj_set_style_bg_color(eye_lid, lv_color_hex(0xF7FBFF), 0);
    lv_obj_set_style_border_color(eye_lid, lv_color_hex(0x77D6C8), 0);
    lv_obj_set_style_border_width(eye_lid, 5, 0);
    lv_obj_clear_flag(eye_lid, LV_OBJ_FLAG_SCROLLABLE);

    eye_brow_l = lv_obj_create(eye);
    lv_obj_set_size(eye_brow_l, 38, 8);
    lv_obj_align(eye_brow_l, LV_ALIGN_TOP_LEFT, 46, 18);
    lv_obj_set_style_radius(eye_brow_l, 4, 0);
    lv_obj_set_style_bg_color(eye_brow_l, lv_color_hex(0x73E0C4), 0);
    lv_obj_set_style_border_width(eye_brow_l, 0, 0);

    eye_brow_r = lv_obj_create(eye);
    lv_obj_set_size(eye_brow_r, 38, 8);
    lv_obj_align(eye_brow_r, LV_ALIGN_TOP_LEFT, 134, 18);
    lv_obj_set_style_radius(eye_brow_r, 4, 0);
    lv_obj_set_style_bg_color(eye_brow_r, lv_color_hex(0x73E0C4), 0);
    lv_obj_set_style_border_width(eye_brow_r, 0, 0);

    eye_iris = lv_obj_create(eye_lid);
    lv_obj_set_size(eye_iris, 76, 76);
    lv_obj_center(eye_iris);
    lv_obj_set_style_radius(eye_iris, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_iris, lv_color_hex(0x43C6AC), 0);
    lv_obj_set_style_border_width(eye_iris, 0, 0);
    lv_obj_clear_flag(eye_iris, LV_OBJ_FLAG_SCROLLABLE);

    eye_pupil = lv_obj_create(eye_iris);
    lv_obj_set_size(eye_pupil, 34, 34);
    lv_obj_center(eye_pupil);
    lv_obj_set_style_radius(eye_pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_pupil, lv_color_hex(0x071016), 0);
    lv_obj_set_style_border_width(eye_pupil, 0, 0);
    lv_obj_clear_flag(eye_pupil, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *shine = lv_obj_create(eye_pupil);
    lv_obj_set_size(shine, 10, 10);
    lv_obj_align(shine, LV_ALIGN_TOP_LEFT, 6, 5);
    lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shine, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(shine, 0, 0);

    eye_cheek_l = lv_obj_create(eye);
    lv_obj_set_size(eye_cheek_l, 34, 12);
    lv_obj_align(eye_cheek_l, LV_ALIGN_CENTER, -74, 46);
    lv_obj_set_style_radius(eye_cheek_l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_cheek_l, lv_color_hex(0xFF8FAB), 0);
    lv_obj_set_style_bg_opa(eye_cheek_l, LV_OPA_70, 0);
    lv_obj_set_style_border_width(eye_cheek_l, 0, 0);
    lv_obj_add_flag(eye_cheek_l, LV_OBJ_FLAG_HIDDEN);

    eye_cheek_r = lv_obj_create(eye);
    lv_obj_set_size(eye_cheek_r, 34, 12);
    lv_obj_align(eye_cheek_r, LV_ALIGN_CENTER, 74, 46);
    lv_obj_set_style_radius(eye_cheek_r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_cheek_r, lv_color_hex(0xFF8FAB), 0);
    lv_obj_set_style_bg_opa(eye_cheek_r, LV_OPA_70, 0);
    lv_obj_set_style_border_width(eye_cheek_r, 0, 0);
    lv_obj_add_flag(eye_cheek_r, LV_OBJ_FLAG_HIDDEN);

    eye_mood_label = lv_label_create(eye);
    lv_label_set_text(eye_mood_label, "");
    lv_obj_set_style_text_font(eye_mood_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(eye_mood_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(eye_mood_label, LV_ALIGN_TOP_MID, 0, 0);

    sleep_label = lv_label_create(eye);
    lv_label_set_text(sleep_label, "Zzz");
    lv_obj_set_style_text_font(sleep_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sleep_label, lv_color_hex(0xB6C2CF), 0);
    lv_obj_align(sleep_label, LV_ALIGN_TOP_RIGHT, -18, 6);
    lv_obj_add_flag(sleep_label, LV_OBJ_FLAG_HIDDEN);
}

static void build_main_flow(lv_obj_t *scr)
{
    main_page = lv_obj_create(scr);
    lv_obj_set_size(main_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_page, lv_color_hex(0x02070A), 0);
    lv_obj_set_style_border_width(main_page, 0, 0);
    lv_obj_set_style_pad_all(main_page, 0, 0);
    lv_obj_clear_flag(main_page, LV_OBJ_FLAG_SCROLLABLE);

    main_tv = lv_tileview_create(main_page);
    lv_obj_set_size(main_tv, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_tv, lv_color_hex(0x02070A), 0);
    lv_obj_set_scrollbar_mode(main_tv, LV_SCROLLBAR_MODE_OFF);

    main_tile = lv_tileview_add_tile(main_tv, 0, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(main_tile, lv_color_hex(0x02070A), 0);
    lv_obj_clear_flag(main_tile, LV_OBJ_FLAG_SCROLLABLE);

    pass_tile = lv_tileview_add_tile(main_tv, 1, 0, LV_DIR_HOR);
    build_pass_tile(pass_tile);

    flow_wifi_label = lv_label_create(main_tile);
    lv_label_set_text(flow_wifi_label, LV_SYMBOL_WIFI " --");
    lv_obj_set_width(flow_wifi_label, 110);
    lv_obj_set_style_text_font(flow_wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(flow_wifi_label, lv_color_hex(0xB6C2CF), 0);
    lv_label_set_long_mode(flow_wifi_label, LV_LABEL_LONG_DOT);
    lv_obj_align(flow_wifi_label, LV_ALIGN_TOP_LEFT, 92, 58);

    flow_batt_label = lv_label_create(main_tile);
    lv_label_set_text(flow_batt_label, "--%");
    lv_obj_set_width(flow_batt_label, 110);
    lv_obj_set_style_text_align(flow_batt_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(flow_batt_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(flow_batt_label, lv_color_hex(0xB6C2CF), 0);
    lv_label_set_long_mode(flow_batt_label, LV_LABEL_LONG_DOT);
    lv_obj_align(flow_batt_label, LV_ALIGN_TOP_RIGHT, -92, 58);

    lv_obj_t *title = lv_label_create(main_tile);
    lv_label_set_text(title, "KINO");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_move_foreground(title);

    build_eye_wizard(main_tile);

    flow_status_label = lv_label_create(main_tile);
    lv_label_set_text(flow_status_label, "Insert card");
    lv_obj_set_width(flow_status_label, 320);
    lv_obj_set_style_text_align(flow_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(flow_status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(flow_status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(flow_status_label, LV_ALIGN_BOTTOM_MID, 0, -112);

    flow_hint_label = lv_label_create(main_tile);
    lv_label_set_text(flow_hint_label, "Waiting for reagent card");
    lv_obj_set_width(flow_hint_label, 300);
    lv_obj_set_style_text_align(flow_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(flow_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(flow_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(flow_hint_label, lv_color_hex(0x73E0C4), 0);
    lv_obj_align(flow_hint_label, LV_ALIGN_BOTTOM_MID, 0, -76);

    flow_timer = lv_timer_create(flow_timer_cb, 50, NULL);
    lv_obj_set_tile_id(main_tv, 0, 0, LV_ANIM_OFF);
}

static void exit_btn_cb(lv_event_t *e)
{
    (void)e;
    show_main_flow();
}

static void tileview_event_cb(lv_event_t *e)
{
    update_active_page(lv_tileview_get_tile_active(lv_event_get_target(e)));
}

static void ui_exit_init(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x080A0D), 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "EXIT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *btn = lv_button_create(tile);
    lv_obj_set_size(btn, 180, 70);
    lv_obj_set_style_radius(btn, 35, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2ECC71), 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_event_cb(btn, exit_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);
}

static void build_diagnostic(lv_obj_t *scr)
{
    diagnostic_page = lv_obj_create(scr);
    lv_obj_set_size(diagnostic_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(diagnostic_page, 0, 0);
    lv_obj_set_style_pad_all(diagnostic_page, 0, 0);
    lv_obj_add_flag(diagnostic_page, LV_OBJ_FLAG_HIDDEN);

    tv = lv_tileview_create(diagnostic_page);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x000000), 0);
    lv_obj_set_size(tv, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    t1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    ui_standby_init(t1);

    t2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    ui_motor_init(t2);

    t3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    ui_misc_init(t3);

    t4 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);
    ui_sys_stats_init(t4);

    t5 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_HOR);
    ui_ble_init(t5);

    t6 = lv_tileview_add_tile(tv, 5, 0, LV_DIR_HOR);
    ui_wifi_prov_init(t6);

    t7 = lv_tileview_add_tile(tv, 6, 0, LV_DIR_HOR);
    ui_exit_init(t7);

    lv_obj_add_event_cb(tv, tileview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    update_active_page(NULL);
}

static void update_active_page(lv_obj_t *active_tile)
{
    set_auto_dim(active_tile != t5);
    ui_standby_set_active(active_tile == t1);
    ui_motor_set_active(active_tile == t2);
    ui_nfc_set_active(active_tile == t2);
    ui_sys_stats_set_active(active_tile == t4);
    ui_ble_set_active(active_tile == t5);
    ui_wifi_prov_set_active(active_tile == t6);
}

static void show_main_flow(void)
{
    ESP_LOGI(TAG, "show main flow");
    update_active_page(NULL);
    lv_obj_add_flag(diagnostic_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(main_page);
    pass_overlay_close();
    set_auto_dim(true);
    flow_set_active(true);
}

static void show_diagnostic(void)
{
    ESP_LOGI(TAG, "show diagnostic");
    flow_set_active(false);
    lv_obj_add_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(diagnostic_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(diagnostic_page);
    lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    update_active_page(t1);
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);

    build_main_flow(scr);
    build_diagnostic(scr);
    test_flow_init();
    last_flow_state = TEST_FLOW_PREP_HOMING;
    current_flow_state = TEST_FLOW_PREP_HOMING;
    keep_awake_until_ms = 0;

    overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, overlay_event_cb, LV_EVENT_PRESSED, NULL);

    lv_timer_create(inactivity_timer_cb, 500, NULL);
    show_main_flow();
    xTaskCreate(wifi_auto_connect_task, "wifi_auto", 4096, NULL, 5, NULL);
}
