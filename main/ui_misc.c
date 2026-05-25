#include "ui_app.h"
#include "stm32_interface.h"
#include "test_flow.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static lv_obj_t *mock_sw;
static lv_obj_t *fps_sw;
static lv_obj_t *sleep_sw;
static bool fps_show = true;
static bool s_deep_sleep_enabled = true;

bool ui_misc_is_deep_sleep_enabled(void)
{
    return s_deep_sleep_enabled;
}

static void apply_fps_show_setting(void)
{
#if LV_USE_PERF_MONITOR
    if (fps_show) {
        lv_sysmon_show_performance(NULL);
    } else {
        lv_sysmon_hide_performance(NULL);
    }
#endif
}

static void load_fps_show_setting(void)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 1;
        if (nvs_get_u8(nvs, "fps_show", &val) == ESP_OK) {
            fps_show = (val != 0);
        }
        nvs_close(nvs);
    }
    apply_fps_show_setting();
    ESP_LOGI("UI_MISC", "FPS show loaded: %d", fps_show);
}

static void save_fps_show_setting(void)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "fps_show", fps_show ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI("UI_MISC", "FPS show saved: %d", fps_show);
}

static void load_deep_sleep_setting(void)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 1;
        if (nvs_get_u8(nvs, "deep_sleep", &val) == ESP_OK) {
            s_deep_sleep_enabled = (val != 0);
        }
        nvs_close(nvs);
    }
    ESP_LOGI("UI_MISC", "Deep sleep loaded: %d", s_deep_sleep_enabled);
}

static void save_deep_sleep_setting(void)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "deep_sleep", s_deep_sleep_enabled ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI("UI_MISC", "Deep sleep saved: %d", s_deep_sleep_enabled);
}

static void sync_fps_switch_from_flash(void)
{
    load_fps_show_setting();
    if (!fps_sw) {
        return;
    }

    if (fps_show) {
        lv_obj_add_state(fps_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(fps_sw, LV_STATE_CHECKED);
    }
}

static void sync_deep_sleep_switch_from_flash(void)
{
    load_deep_sleep_setting();
    if (!sleep_sw) {
        return;
    }

    if (s_deep_sleep_enabled) {
        lv_obj_add_state(sleep_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(sleep_sw, LV_STATE_CHECKED);
    }
}

static void sync_motor_mock_switch_from_flash(void)
{
    test_flow_reload_settings();

    if (!mock_sw) {
        return;
    }

    bool mock = test_flow_is_motor_mock();
    if (mock) {
        lv_obj_add_state(mock_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(mock_sw, LV_STATE_CHECKED);
    }
    ESP_LOGI("UI_MISC", "Motor mock loaded: %d", mock);
}

static void btn_event_cb(lv_event_t * e) {
    uintptr_t type = (uintptr_t)lv_event_get_user_data(e);

    static int toggle_state = 0;

    switch(type) {
        case 0: // Toggle Analog Power
            {
                toggle_state = !toggle_state;

                uint8_t payload[2];
                payload[0] = 0;
                payload[1] = (uint8_t)toggle_state;
                stm32_cmd_send_action(CMD_GPIO_WRITE, payload, 2);

                // set label text
                lv_obj_t *label = lv_obj_get_child(lv_event_get_target(e), 0);
                if (toggle_state) {
                    lv_label_set_text(label, "ANALOG PWR ON");
                    lv_obj_set_style_bg_color(lv_event_get_target(e), lv_color_hex(0xC0392B), 0);
                } else {
                    lv_label_set_text(label, "ANALOG PWR OFF");
                    lv_obj_set_style_bg_color(lv_event_get_target(e), lv_color_hex(0x27AE60), 0);
                }
            }
            break;
        case 1: // Hi
            {
                stm32_cmd_send_action(CMD_HI, NULL, 0);
            }
            break;
    }
}

static void mock_sw_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool mock = lv_obj_has_state(sw, LV_STATE_CHECKED);
    test_flow_set_motor_mock(mock);
    ESP_LOGI("UI_MISC", "Motor mock set to %d", mock);
}

static void fps_sw_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    fps_show = lv_obj_has_state(sw, LV_STATE_CHECKED);
    apply_fps_show_setting();
    save_fps_show_setting();
    ESP_LOGI("UI_MISC", "FPS show set to %d", fps_show);
}

static void sleep_sw_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    s_deep_sleep_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_deep_sleep_setting();
    ESP_LOGI("UI_MISC", "Deep Sleep set to %d", s_deep_sleep_enabled);
}

static lv_obj_t *create_switch_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 260, 58);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1F2933), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    return lv_switch_create(row);
}

void ui_misc_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1f0f0f), 0);
    
    lv_obj_t *btn_cont = lv_obj_create(tile);
    lv_obj_set_size(btn_cont, 300, 320);
    lv_obj_set_style_bg_opa(btn_cont, 0, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_align(btn_cont, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_cont, 10, 0);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Horizontal button row for PWR Ctrl and Hi button
    lv_obj_t *button_row = lv_obj_create(btn_cont);
    lv_obj_set_size(button_row, 260, 52);
    lv_obj_set_style_bg_opa(button_row, 0, 0);
    lv_obj_set_style_border_width(button_row, 0, 0);
    lv_obj_set_style_pad_all(button_row, 0, 0);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn0 = lv_button_create(button_row);
    lv_obj_set_size(btn0, 165, 48);
    lv_obj_set_style_bg_color(btn0, lv_color_hex(0x27AE60), 0); // Default Analog PWR OFF (Green)
    lv_obj_set_style_radius(btn0, 14, 0);
    
    lv_obj_t *l0 = lv_label_create(btn0);
    lv_label_set_text(l0, "ANALOG PWR OFF");
    lv_obj_set_style_text_font(l0, &lv_font_montserrat_12, 0);
    lv_obj_center(l0);
    lv_obj_add_event_cb(btn0, btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)0);

    lv_obj_t *btn1 = lv_button_create(button_row);
    lv_obj_set_size(btn1, 85, 48);
    lv_obj_set_style_bg_color(btn1, lv_color_hex(0x9B59B6), 0); // Purple
    lv_obj_set_style_radius(btn1, 14, 0);
    
    lv_obj_t *l1 = lv_label_create(btn1);
    lv_label_set_text(l1, "Hi");
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    lv_obj_center(l1);
    lv_obj_add_event_cb(btn1, btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)1);

    mock_sw = create_switch_row(btn_cont, "Motor Mock");
    lv_obj_add_event_cb(mock_sw, mock_sw_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    sync_motor_mock_switch_from_flash();

    fps_sw = create_switch_row(btn_cont, "FPS Show");
    lv_obj_add_event_cb(fps_sw, fps_sw_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    sync_fps_switch_from_flash();
#if !LV_USE_PERF_MONITOR
    lv_obj_add_state(fps_sw, LV_STATE_DISABLED);
#endif

    sleep_sw = create_switch_row(btn_cont, "Deep Sleep");
    lv_obj_add_event_cb(sleep_sw, sleep_sw_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    sync_deep_sleep_switch_from_flash();
}

void ui_misc_set_active(bool active)
{
    if (active) {
        sync_motor_mock_switch_from_flash();
        sync_fps_switch_from_flash();
        sync_deep_sleep_switch_from_flash();
    }
}
