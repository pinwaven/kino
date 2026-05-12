#include "ui_app.h"
#include "esp_log.h"
#include "bsp/display.h"

static lv_obj_t *tv;
static lv_obj_t *t1;
static lv_obj_t *t2;
static lv_obj_t *t3;
static lv_obj_t *t4;
static lv_obj_t *t5;
static lv_obj_t *overlay;

static const char *TAG = "UI_MANAGER";
static bool is_dimmed = false;
static bool allow_auto_dim = true;
static const int BRIGHTNESS_NORMAL = 66;
static const int BRIGHTNESS_DIMMED = 20;
static const uint32_t INACTIVITY_TIMEOUT_MS = 10000;
static const uint32_t REFR_PERIOD_NORMAL = 16;   // ~60 FPS
static const uint32_t REFR_PERIOD_DIMMED = 200;  // 5 FPS

static void overlay_event_cb(lv_event_t * e) {
    if (is_dimmed) {
        bsp_display_brightness_set(BRIGHTNESS_NORMAL);
        lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_NORMAL);
        is_dimmed = false;
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Screen restored by touch on overlay (FPS restored)");
        // Reset inactive time to prevent immediate re-dimming if timer runs right after
        lv_display_trigger_activity(NULL);
    }
}

static void inactivity_timer_cb(lv_timer_t * t) {
    uint32_t inactive_time = lv_display_get_inactive_time(NULL);

    if (!allow_auto_dim) {
        if (is_dimmed) {
            bsp_display_brightness_set(BRIGHTNESS_NORMAL);
            lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_NORMAL);
            is_dimmed = false;
            lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        }
        lv_display_trigger_activity(NULL);
        return;
    }

    if (inactive_time >= INACTIVITY_TIMEOUT_MS) {
        if (!is_dimmed) {
            bsp_display_brightness_set(BRIGHTNESS_DIMMED);
            lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_DIMMED);
            is_dimmed = true;
            lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Screen dimmed due to %dms inactivity (FPS lowered)", (int)inactive_time);
        }
    }
    // We don't need the 'else' here because the overlay handles restoration
}

static void update_active_page(lv_obj_t *active_tile) {
    allow_auto_dim = active_tile != t5;
    if (!allow_auto_dim && is_dimmed) {
        bsp_display_brightness_set(BRIGHTNESS_NORMAL);
        lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_NORMAL);
        is_dimmed = false;
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_display_trigger_activity(NULL);
    }

    ui_standby_set_active(active_tile == t1);
    ui_motor_set_active(active_tile == t2);
    ui_nfc_set_active(active_tile == t2);
    ui_sys_stats_set_active(active_tile == t4);
    ui_ble_set_active(active_tile == t5);
}

static void tileview_event_cb(lv_event_t * e) {
    update_active_page(lv_tileview_get_tile_active(lv_event_get_target(e)));
}

void ui_init(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);

    // 创建 Tileview 用于左右滑动
    tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x000000), 0);
    
    // 隐藏滚动条
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    // 第一页：待机页 (0,0)
    t1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    ui_standby_init(t1);

    // 第二页：电机控制页 (1,0)
    t2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    ui_motor_init(t2);

    // 第三页：杂项页 (2,0)
    t3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    ui_misc_init(t3);

    // 第四页：系统信息页 (3,0)
    t4 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);
    ui_sys_stats_init(t4);

    // 第五页：蓝牙控制页 (4,0)
    t5 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_HOR);
    ui_ble_init(t5);

    lv_obj_add_event_cb(tv, tileview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // 默认跳转到第一页
    lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    update_active_page(t1);

    // 创建全屏透明覆盖层，用于在变暗时拦截第一次触摸
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, overlay_event_cb, LV_EVENT_PRESSED, NULL);

    // 创建不活跃检测定时器，每 500ms 检查一次
    lv_timer_create(inactivity_timer_cb, 500, NULL);
}
