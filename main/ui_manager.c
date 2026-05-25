#include "ui_app.h"
#include "esp_log.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "wifi_prov.h"
#include "test_flow.h"
#include "stm32_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>

#define DIAG_PASSCODE "123"
#define DIAG_PASSCODE_LEN 3
#define DIAG_LONG_PRESS_MS 3140
#define DIAG_LONG_PRESS_FEEDBACK_MS 1000
#define DIAG_LONG_PRESS_TICK_MS 50

static const char *TAG = "UI_MANAGER";

static lv_obj_t *main_page;
static lv_obj_t *diagnostic_page;
static lv_obj_t *main_tile;
static lv_obj_t *pass_tile;
static lv_obj_t *tv;
static lv_obj_t *t_wrap_l;
static lv_obj_t *t_wrap_r;
static lv_obj_t *t1;
static lv_obj_t *t2;
static lv_obj_t *t3;
static lv_obj_t *t4;
static lv_obj_t *t5;
static lv_obj_t *t6;
static lv_obj_t *overlay;
static lv_obj_t *overlay_logo_label;
static lv_obj_t *pass_code_label;
static lv_obj_t *diag_exit_overlay;
static lv_obj_t *diag_exit_panel;
static lv_obj_t *diag_exit_btn;
static lv_obj_t *diag_cancel_btn;
static lv_point_t diag_touch_start;
static bool diag_touch_tracking = false;

typedef struct {
    lv_obj_t *box;
    lv_obj_t *value;
    lv_obj_t *name;
} report_metric_view_t;

enum {
    REPORT_METRIC_COUNT = 4,
    REPORT_PAGE_SIZE = REPORT_METRIC_COUNT,
    REPORT_ITEM_MAX = 12,
    REPORT_TAP_HITBOX_W = 164,
    REPORT_TAP_HITBOX_H = 34,
};

static lv_obj_t *flow_status_label;
static lv_obj_t *flow_hint_label;
static lv_obj_t *flow_halo;
static lv_obj_t *flow_ring_bg;
static lv_obj_t *flow_ring_main;
static lv_obj_t *flow_glint_dots[7];
static lv_obj_t *flow_logo_label;
static lv_obj_t *flow_logo_sweep_mask;
static lv_obj_t *flow_logo_sweep_label;
static lv_color_t flow_logo_last_color;
static int flow_logo_sweep_phase = -1;
static int flow_logo_sweep_start_tick;
static int flow_logo_sweep_wave_phase;
static bool flow_logo_sweep_running = false;
static bool flow_logo_recolor_on = false;
static lv_obj_t *flow_phase_label;
static lv_obj_t *flow_phase_arrow_label;
static lv_obj_t *flow_status_arrow_label;
static lv_obj_t *flow_report_title_label;
static lv_obj_t *flow_report_chip_label;
static report_metric_view_t flow_report_metrics[REPORT_METRIC_COUNT];
static lv_obj_t *flow_report_tap_hitbox;
static lv_obj_t *flow_report_tap_label;
static int flow_report_page;
static lv_timer_t *flow_timer;
static int flow_tick;
static bool flow_timer_slow = false;
static test_flow_state_t last_visual_state = (test_flow_state_t)-1;
static bool last_visual_dimmed = false;
static bool last_hint_visible = false;
static bool last_status_visible = false;
static uint32_t last_hint_color_full = 0;
static bool flow_render_paused = false;
static bool flow_wait_card_armed = false;
static bool flow_glint_paused = false;
static bool flow_stage_scroll_hidden = false;
static lv_timer_t *diag_long_press_timer;
static bool diag_long_press_tracking;
static bool diag_long_press_fired;
static bool diag_long_press_feedback;
static uint32_t diag_long_press_start_ms;

static bool is_dimmed = false;
static bool allow_auto_dim = true;
static bool s_in_diagnostic = false;
static uint32_t last_dim_restore_ms;
static const int BRIGHTNESS_NORMAL = 66;
static const int BRIGHTNESS_DIMMED = 20;
static const uint32_t INACTIVITY_TIMEOUT_MS = 20000;
static const uint32_t WAKE_ARM_GUARD_MS = 500;
static const uint32_t INSERT_CARD_TIMEOUT_MS = 30000;
static const uint32_t ERROR_INACTIVITY_TIMEOUT_MS = 30000;
static const uint32_t CARD_AWAKE_HOLD_MS = 60000;
static const uint32_t REFR_PERIOD_NORMAL = 16;
static const uint32_t REFR_PERIOD_DIMMED = 1000;
static const uint32_t REFR_PERIOD_AFTER_NETWORK = 80;
static const uint32_t FLOW_TIMER_PERIOD_MS = 80;
static const int FLOW_GLINT_DOT_COUNT = 7;
static const int FLOW_GLINT_RADIUS = 213;
static const int LOGO_SWEEP_STEPS = 8;
static const int LOGO_SWEEP_TRAVERSALS = 6;
static const BaseType_t APP_WORKER_TASK_CORE = 1;
static const int WAKE_HI_REQUIRED_SUCCESSES = 2;
static const int WAKE_HI_MAX_ATTEMPTS = 8;
static const uint32_t WAKE_HI_GAP_MS = 120;
static const uint32_t WAKE_READY_SETTLE_MS = 300;
static const uint32_t OVERLAY_LOGO_IDLE_COLOR = 0x92A2B8;
static const lv_opa_t OVERLAY_LOGO_IDLE_OPA = (lv_opa_t)240;
static const uint32_t OVERLAY_LOGO_WAKE_COLOR = 0x536170;
static const lv_opa_t OVERLAY_LOGO_WAKE_OPA = LV_OPA_70;
static const int OVERLAY_LOGO_BASE_X = 3;
static const int OVERLAY_LOGO_BASE_Y = -18;
static const int OVERLAY_LOGO_DRIFT_RADIUS_X = 16;
static const int OVERLAY_LOGO_DRIFT_RADIUS_Y = 9;
static const int OVERLAY_LOGO_DRIFT_PHASES = 24;
static uint32_t network_recover_until_ms;
static uint32_t keep_awake_until_ms;
static uint32_t insert_card_wait_until_ms;
static test_flow_state_t last_flow_state = TEST_FLOW_PREP_HOMING;
static test_flow_state_t current_flow_state = TEST_FLOW_PREP_HOMING;
static uint32_t dimmed_start_time_ms = 0;
static volatile bool sleep_command_sent = false;
static esp_pm_lock_handle_t s_light_sleep_lock = NULL;
static volatile bool s_pm_lock_held = true;

static QueueHandle_t s_sys_worker_queue = NULL;
static volatile bool s_sleep_request_pending = false;
static volatile bool s_wake_request_pending = false;
static volatile bool s_wake_sequence_active = false;
static uint32_t s_overlay_logo_drift_step = 0;

static void update_active_page(lv_obj_t *active_tile);
static void show_main_flow(void);
static void show_diagnostic(void);
static void main_flow_press_cb(lv_event_t *e);
static void main_flow_click_cb(lv_event_t *e);
static void diag_long_press_timer_cb(lv_timer_t *timer);
static void cancel_diag_long_press(void);
static void set_diag_long_press_feedback(bool active);
static void disarm_wait_card_ui(void);
static bool arm_wait_card_ui(void);
static bool flow_retryable_error_state(test_flow_state_t state);
static void set_obj_hidden(lv_obj_t *obj, bool hidden);
static void build_kino_stage(lv_obj_t *parent);
static void update_flow_stage_visuals(test_flow_state_t state);
static void update_flow_logo_effect(test_flow_state_t state);
static void start_flow_logo_sweep(void);
static void stop_flow_logo_sweep(void);
static void build_flow_report(lv_obj_t *parent);
static void update_flow_report(const test_flow_snapshot_t *flow);
static void set_flow_report_visible(bool visible);
static void report_metric_click_cb(lv_event_t *e);
static void report_eject_click_cb(lv_event_t *e);
static void set_flow_glint_paused(bool paused);
static void set_flow_stage_scroll_hidden(bool hidden);
static void diag_pointer_event_cb(lv_event_t *e);
static void diag_exit_overlay_hide(void);
static void diag_exit_overlay_show(void);
static lv_obj_t *diag_normalize_active_tile(lv_obj_t *active_tile);

bool sys_worker_send_req(sys_worker_req_t req)
{
    if (!s_sys_worker_queue) {
        ESP_LOGW(TAG, "sys worker queue unavailable");
        return false;
    }

    volatile bool *pending = NULL;
    if (req == SYS_WORKER_SLEEP) {
        pending = &s_sleep_request_pending;
    } else if (req == SYS_WORKER_WAKE) {
        pending = &s_wake_request_pending;
    }

    if (pending) {
        if (*pending) {
            return true;
        }
        *pending = true;
    }

    if (xQueueSend(s_sys_worker_queue, &req, 0) == pdTRUE) {
        return true;
    }

    if (pending) {
        *pending = false;
    }
    ESP_LOGW(TAG, "sys worker queue full for request %d", req);
    return false;
}

static void sys_worker_task(void *arg)
{
    (void)arg;
    sys_worker_req_t req;

    while (xQueueReceive(s_sys_worker_queue, &req, portMAX_DELAY) == pdTRUE) {
        switch (req) {
            case SYS_WORKER_SLEEP: {
                ESP_LOGI(TAG, "Sending sleep command to STM32");
                esp_err_t err = stm32_cmd_send_action(CMD_SLEEP, NULL, 0);
                if (err == ESP_OK) {
                    sleep_command_sent = true;
                    ESP_LOGI(TAG, "Sleep command sent to STM32 successfully");

                    ESP_LOGI(TAG, "Pausing Wi-Fi radio for deep sleep");
                    wifi_prov_pause_radio();
                } else {
                    ESP_LOGE(TAG, "Failed to send sleep command to STM32: %s", esp_err_to_name(err));
                }
                s_sleep_request_pending = false;
                break;
            }

            case SYS_WORKER_WAKE: {
                if (s_light_sleep_lock && !s_pm_lock_held) {
                    esp_pm_lock_acquire(s_light_sleep_lock);
                    s_pm_lock_held = true;
                    ESP_LOGI(TAG, "Acquired PM lock, preventing ESP32 Light Sleep");
                }

                if (sleep_command_sent) {
                    ESP_LOGI(TAG, "Waking up STM32 after sleep");
                    s_wake_sequence_active = true;
                    int success_count = 0;
                    char resp_buf[64];

                    for (int retry = 0; retry < WAKE_HI_MAX_ATTEMPTS && success_count < WAKE_HI_REQUIRED_SUCCESSES; retry++) {
                        ESP_LOGI(TAG,
                                 "Sending CMD_HI to wake up (attempt %d/%d success %d/%d)",
                                 retry + 1,
                                 WAKE_HI_MAX_ATTEMPTS,
                                 success_count,
                                 WAKE_HI_REQUIRED_SUCCESSES);
                        esp_err_t err = stm32_cmd_request_timeout(CMD_HI, NULL, 0, resp_buf, sizeof(resp_buf), 150);
                        if (err == ESP_OK && strncmp(resp_buf, "ver:", 4) == 0) {
                            success_count++;
                            ESP_LOGI(TAG, "STM32 wake HI ok %d/%d, response: %s",
                                     success_count,
                                     WAKE_HI_REQUIRED_SUCCESSES,
                                     resp_buf);
                        } else {
                            success_count = 0;
                        }

                        if (success_count < WAKE_HI_REQUIRED_SUCCESSES) {
                            vTaskDelay(pdMS_TO_TICKS(WAKE_HI_GAP_MS));
                        }
                    }

                    if (success_count >= WAKE_HI_REQUIRED_SUCCESSES) {
                        vTaskDelay(pdMS_TO_TICKS(WAKE_READY_SETTLE_MS));
                        sleep_command_sent = false;
                        ESP_LOGI(TAG, "STM32 wake confirmed by %d HI responses", success_count);
                    } else {
                        ESP_LOGE(TAG,
                                 "Failed to wake up STM32 after %d attempts with %d required HI responses",
                                 WAKE_HI_MAX_ATTEMPTS,
                                 WAKE_HI_REQUIRED_SUCCESSES);
                        test_flow_trigger_external_error(TEST_FLOW_CARD_DETECT_ERROR, "awake mcu failed\nreboot please..");
                    }
                    s_wake_sequence_active = false;
                }

                ESP_LOGI(TAG, "Resuming Wi-Fi radio after STM32 wake");
                wifi_prov_resume_radio();
                s_wake_request_pending = false;
                break;
            }

            case SYS_WORKER_WIFI_AUTO_CONNECT: {
                wifi_prov_auto_connect_saved();
                break;
            }

            case SYS_WORKER_PROV_START: {
                esp_err_t err = wifi_prov_start();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "prov start failed: %s", esp_err_to_name(err));
                }
                if (ui_wifi_prov_get_stop_after_task() || !ui_wifi_prov_is_page_active()) {
                    wifi_prov_stop();
                    ui_wifi_prov_clear_stop_after_task();
                }
                ui_wifi_prov_clear_task_pending();
                break;
            }

            case SYS_WORKER_PROV_STOP: {
                ui_wifi_prov_clear_stop_after_task();
                wifi_prov_stop();
                ui_wifi_prov_clear_task_pending();
                break;
            }

            case SYS_WORKER_BMS_POLL: {
                esp_err_t err = stm32_update_bmsinfo();
                ui_standby_set_bms_err(err);
                ui_standby_clear_bms_pending();
                break;
            }

            default:
                break;
        }
    }
}

static void update_overlay_logo_idle_motion(void)
{
    if (!overlay_logo_label) {
        return;
    }

    uint32_t phase = s_overlay_logo_drift_step++ % OVERLAY_LOGO_DRIFT_PHASES;
    int angle = (int)(phase * 360 / OVERLAY_LOGO_DRIFT_PHASES);
    int wave_angle = (angle + 90) % 360;
    int ox = OVERLAY_LOGO_BASE_X + ((OVERLAY_LOGO_DRIFT_RADIUS_X * lv_trigo_sin(angle)) >> LV_TRIGO_SHIFT);
    int oy = OVERLAY_LOGO_BASE_Y + ((OVERLAY_LOGO_DRIFT_RADIUS_Y * lv_trigo_sin(wave_angle)) >> LV_TRIGO_SHIFT);
    lv_opa_t opa = (lv_opa_t)(OVERLAY_LOGO_IDLE_OPA - 8 +
                              ((16 * (lv_trigo_sin((angle + 180) % 360) + LV_TRIGO_SIN_MAX)) >>
                               (LV_TRIGO_SHIFT + 1)));

    lv_obj_align(overlay_logo_label, LV_ALIGN_CENTER, ox, oy);
    lv_obj_set_style_text_color(overlay_logo_label, lv_color_hex(OVERLAY_LOGO_IDLE_COLOR), 0);
    lv_obj_set_style_text_opa(overlay_logo_label, opa, 0);
}



static void flow_pause_render(void)
{
    lv_timer_t *refr_timer = lv_display_get_refr_timer(NULL);
    if (!flow_render_paused && refr_timer) {
        lv_timer_pause(refr_timer);
        flow_render_paused = true;
        ESP_LOGI(TAG, "Display refresh paused for network stage");
    }
}

static void flow_resume_render(void)
{
    lv_timer_t *refr_timer = lv_display_get_refr_timer(NULL);
    if (flow_render_paused && refr_timer) {
        network_recover_until_ms = lv_tick_get() + 1200;
        lv_timer_set_period(refr_timer, is_dimmed ? REFR_PERIOD_DIMMED : REFR_PERIOD_AFTER_NETWORK);
        lv_timer_resume(refr_timer);
        lv_timer_ready(refr_timer);
        flow_render_paused = false;
        ESP_LOGI(TAG, "Display refresh resumed after network stage");
    }
}

static void restore_screen_now(void)
{
    bool was_dimmed = is_dimmed;

    if (s_light_sleep_lock && !s_pm_lock_held) {
        esp_pm_lock_acquire(s_light_sleep_lock);
        s_pm_lock_held = true;
        ESP_LOGI(TAG, "Acquired PM lock before display restore");
    }

    bsp_display_brightness_set(BRIGHTNESS_NORMAL);
    if (!flow_render_paused) {
        lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_NORMAL);
    }
    is_dimmed = false;
    if (overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (was_dimmed) {
        last_dim_restore_ms = lv_tick_get();
        if (overlay_logo_label) {
            lv_obj_set_style_text_color(overlay_logo_label, lv_color_hex(OVERLAY_LOGO_WAKE_COLOR), 0);
            lv_obj_set_style_text_opa(overlay_logo_label, OVERLAY_LOGO_WAKE_OPA, 0);
            lv_obj_align(overlay_logo_label, LV_ALIGN_CENTER, 3, -18);
        }
        if (sleep_command_sent || s_sleep_request_pending || !s_pm_lock_held) {
            sys_worker_send_req(SYS_WORKER_WAKE);
        }
        set_flow_stage_scroll_hidden(false);
        if (current_flow_state == TEST_FLOW_WAIT_CARD) {
            disarm_wait_card_ui();
            update_flow_stage_visuals(current_flow_state);
            update_flow_logo_effect(current_flow_state);
        } else if (flow_retryable_error_state(current_flow_state)) {
            set_flow_glint_paused(false);
        }
        if (flow_timer) {
            lv_timer_set_period(flow_timer, FLOW_TIMER_PERIOD_MS);
            lv_timer_ready(flow_timer);
        }
    }
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

static bool flow_network_busy_state(test_flow_state_t state)
{
    return state == TEST_FLOW_GETTING_CHIP || state == TEST_FLOW_POSTING_BIOMARKERS;
}

static bool flow_retryable_error_state(test_flow_state_t state)
{
    return state == TEST_FLOW_NFC_ERROR ||
           state == TEST_FLOW_API_ERROR ||
           state == TEST_FLOW_CARD_DETECT_ERROR ||
           state == TEST_FLOW_MOTOR_ERROR;
}

static bool flow_report_display_state(test_flow_state_t state)
{
    return state == TEST_FLOW_UPLOAD_REVIEW;
}

static bool flow_unified_error_layout(const test_flow_snapshot_t *flow)
{
    return flow && flow_retryable_error_state(flow->state);
}

static void summary_value_after(const char *summary, const char *prefix, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!summary || !prefix) {
        return;
    }

    const char *p = strstr(summary, prefix);
    if (!p) {
        return;
    }
    p += strlen(prefix);
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
}

typedef struct {
    char name[24];
    char value[32];
} report_item_t;

static bool summary_skip_report_line(const char *line, size_t len)
{
    return len == 0 ||
           strncmp(line, "Report uploaded", len) == 0 ||
           strncmp(line, "Chip:", 5) == 0 ||
           strncmp(line, "Kino result:", 12) == 0 ||
           strncmp(line, "Tap to eject", len) == 0;
}

static int summary_report_items(const char *summary, report_item_t *items, int max_items)
{
    int count = 0;
    const char *line = summary;

    while (line && *line && count < max_items) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        const char *colon = memchr(line, ':', len);

        if (colon && !summary_skip_report_line(line, len)) {
            size_t n_len = (size_t)(colon - line);
            size_t v_len = len - n_len - 1;
            const char *v = colon + 1;
            while (v_len > 0 && *v == ' ') {
                v++;
                v_len--;
            }
            if (n_len >= sizeof(items[count].name)) n_len = sizeof(items[count].name) - 1;
            if (v_len >= sizeof(items[count].value)) v_len = sizeof(items[count].value) - 1;
            memcpy(items[count].name, line, n_len);
            items[count].name[n_len] = '\0';
            memcpy(items[count].value, v, v_len);
            items[count].value[v_len] = '\0';
            count++;
        }

        line = end ? end + 1 : NULL;
    }

    return count;
}

static lv_color_t flow_state_color(test_flow_state_t state)
{
    if (flow_retryable_error_state(state)) {
        return lv_color_hex(0xFF5F6D);
    }

    if (flow_report_display_state(state)) {
        return lv_color_hex(0x10B981);
    }

    if (state == TEST_FLOW_CARD_DETECTED ||
        state == TEST_FLOW_PREP_HOMING ||
        state == TEST_FLOW_PREP_OPENING ||
        state == TEST_FLOW_CLOSING ||
        state == TEST_FLOW_READING_NFC ||
        state == TEST_FLOW_GETTING_CHIP ||
        state == TEST_FLOW_MOCK_TESTING ||
        state == TEST_FLOW_POSTING_BIOMARKERS ||
        state == TEST_FLOW_SUCCESS_EJECTING) {
        return lv_color_hex(0x6375EC);
    }

    return lv_color_hex(0xA6C4E5);
}

static const char *flow_phase_text(test_flow_state_t state)
{
    if (flow_retryable_error_state(state)) return "ATTENTION";
    if (flow_report_display_state(state)) return "COMPLETE";
    if (state == TEST_FLOW_WAIT_CARD) return flow_wait_card_armed ? "Insert Card" : "Ready";
    return "PROCESSING";
}

static uint8_t prompt_pulse_opa(void)
{
    int16_t sine = lv_trigo_sin((flow_tick * 24) % 360);
    if (sine > 0) return 255;
    return 30;
}

static void set_prompt_arrow(lv_obj_t *arrow, lv_obj_t *anchor, const char *symbol, bool visible)
{
    if (!arrow || !anchor) {
        return;
    }

    if (!visible) {
        set_obj_hidden(arrow, true);
        return;
    }

    set_obj_hidden(arrow, false);
    lv_label_set_text(arrow, symbol);
    lv_obj_align_to(arrow, anchor, LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_set_style_text_opa(arrow, prompt_pulse_opa(), 0);
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

    if (is_dimmed) {
        update_overlay_logo_idle_motion();

        bool deep_sleep_enabled = ui_misc_is_deep_sleep_enabled();
        if (current_flow_state == TEST_FLOW_WAIT_CARD && !s_in_diagnostic && deep_sleep_enabled &&
            !sleep_command_sent && !s_sleep_request_pending) {
            uint32_t dimmed_duration = lv_tick_get() - dimmed_start_time_ms;
            if (dimmed_duration >= 50000) { // 50 seconds (50,000 ms)
                ESP_LOGI(TAG, "Screen dimmed for >50 seconds, queueing sleep command");
                if (overlay_logo_label) {
                    lv_obj_set_style_text_color(overlay_logo_label, lv_color_hex(OVERLAY_LOGO_IDLE_COLOR), 0);
                    lv_obj_set_style_text_opa(overlay_logo_label, OVERLAY_LOGO_IDLE_OPA, 0);
                }
                sys_worker_send_req(SYS_WORKER_SLEEP);
            }
        }
    }

    if (!allow_auto_dim) {
        if (is_dimmed) {
            restore_screen_now();
        }
        lv_display_trigger_activity(NULL);
        return;
    }

    if (current_flow_state != TEST_FLOW_WAIT_CARD && !flow_retryable_error_state(current_flow_state)) {
        if (is_dimmed) {
            restore_screen_now();
        }
        lv_display_trigger_activity(NULL);
        return;
    }

    if (keep_awake_active() && !flow_retryable_error_state(current_flow_state)) {
        if (is_dimmed) {
            restore_screen_now();
        }
        lv_display_trigger_activity(NULL);
        return;
    }

    if (current_flow_state == TEST_FLOW_WAIT_CARD && flow_wait_card_armed) {
        if (insert_card_wait_until_ms != 0 &&
            (int32_t)(insert_card_wait_until_ms - lv_tick_get()) <= 0) {
            disarm_wait_card_ui();
            ESP_LOGI(TAG, "Insert-card wait timed out after %ums", (unsigned)INSERT_CARD_TIMEOUT_MS);
        } else {
            return;
        }
    }

    uint32_t dim_timeout = flow_retryable_error_state(current_flow_state) ?
                           ERROR_INACTIVITY_TIMEOUT_MS :
                           INACTIVITY_TIMEOUT_MS;

    if (inactive_time >= dim_timeout && !is_dimmed) {
        if (flow_retryable_error_state(current_flow_state)) {
            set_flow_glint_paused(true);
        } else {
            set_flow_stage_scroll_hidden(true);
        }
        bsp_display_brightness_set(BRIGHTNESS_DIMMED);
        if (!flow_render_paused) {
            lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_DIMMED);
        }
        if (flow_timer) {
            lv_timer_set_period(flow_timer, REFR_PERIOD_DIMMED);
        }
        is_dimmed = true;
        dimmed_start_time_ms = lv_tick_get();
        s_overlay_logo_drift_step = 0;
        sleep_command_sent = false;
        if (flow_logo_label) {
            flow_logo_last_color = lv_color_hex(0x7B8794);
            lv_obj_set_style_text_color(flow_logo_label, flow_logo_last_color, 0);
        }
        if (flow_logo_sweep_mask) {
            stop_flow_logo_sweep();
        }
        if (flow_phase_label && current_flow_state == TEST_FLOW_WAIT_CARD) {
            lv_label_set_text(flow_phase_label, "");
        }
        bool main_flow_visible = main_page && !lv_obj_has_flag(main_page, LV_OBJ_FLAG_HIDDEN);
        if (overlay) {
            if (main_flow_visible) {
                lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(overlay);
            } else {
                lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (overlay_logo_label) {
            update_overlay_logo_idle_motion();
            set_obj_hidden(overlay_logo_label, !main_flow_visible);
        }
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
    const char *status_text;
    const char *hint_text;
    lv_color_t hint_color;

    test_flow_get_snapshot(&flow);
    current_flow_state = flow.state;

    if (flow_network_busy_state(flow.state)) {
        flow_pause_render();
        last_flow_state = flow.state;
        return;
    }

    flow_resume_render();

    if (last_flow_state != TEST_FLOW_WAIT_CARD && flow.state == TEST_FLOW_WAIT_CARD && flow_wait_card_armed) {
        disarm_wait_card_ui();
    }

    if (flow_retryable_error_state(flow.state) && !flow_retryable_error_state(last_flow_state)) {
        keep_awake_until_ms = 0;
    }

    status_text = test_flow_status_text(flow.state);
    if (flow.state == TEST_FLOW_WAIT_CARD && !flow_wait_card_armed) {
        status_text = "Ready";
    }
    hint_text = test_flow_hint_text(&flow);
    hint_color = flow_retryable_error_state(flow.state) ? lv_color_hex(0xFF6B6B) :
                 flow.state == TEST_FLOW_UPLOAD_REVIEW ? lv_color_hex(0xFFD166) :
                 flow.state == TEST_FLOW_CARD_DETECTED ? lv_color_hex(0x73E0C4) :
                 lv_color_hex(0xB6C2CF);

    if (strcmp(lv_label_get_text(flow_status_label), status_text) != 0) {
        lv_label_set_text(flow_status_label, status_text);
    }

    bool report_visible = flow_report_display_state(flow.state);
    bool status_visible = !flow_unified_error_layout(&flow) && !report_visible;
    if (flow.state == TEST_FLOW_WAIT_CARD && !flow_wait_card_armed) {
        status_visible = false;
    }

    if (status_visible != last_status_visible) {
        if (status_visible) {
            lv_obj_remove_flag(flow_status_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(flow_status_label, LV_OBJ_FLAG_HIDDEN);
        }
        last_status_visible = status_visible;
    }

    if (strcmp(lv_label_get_text(flow_hint_label), hint_text) != 0) {
        lv_label_set_text(flow_hint_label, hint_text);
    }

    bool hint_visible = flow_retryable_error_state(flow.state) && !report_visible;
    if (hint_visible != last_hint_visible) {
        if (hint_visible) {
            lv_obj_remove_flag(flow_hint_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(flow_hint_label, 330);
            if (flow_unified_error_layout(&flow)) {
                lv_obj_align(flow_hint_label, LV_ALIGN_CENTER, 0, 104);
            } else {
                lv_obj_align(flow_hint_label, LV_ALIGN_BOTTOM_MID, 0, -72);
            }
        } else {
            lv_obj_add_flag(flow_hint_label, LV_OBJ_FLAG_HIDDEN);
        }
        last_hint_visible = hint_visible;
    } else if (hint_visible) {
        if (flow_unified_error_layout(&flow)) {
            lv_obj_align(flow_hint_label, LV_ALIGN_CENTER, 0, 104);
        } else {
            lv_obj_align(flow_hint_label, LV_ALIGN_BOTTOM_MID, 0, -72);
        }
    }

    if (lv_color_to_u32(hint_color) != last_hint_color_full) {
        lv_obj_set_style_text_color(flow_hint_label, hint_color, 0);
        last_hint_color_full = lv_color_to_u32(hint_color);
    }

    set_flow_report_visible(report_visible);
    if (report_visible) {
        update_flow_report(&flow);
    }

    if (last_flow_state != TEST_FLOW_CARD_DETECTED && flow.state == TEST_FLOW_CARD_DETECTED) {
        keep_awake_for(CARD_AWAKE_HOLD_MS);
        if (is_dimmed) {
            restore_screen_now();
        }
        ESP_LOGI(TAG, "Card inserted, screen awake for %ums", (unsigned)CARD_AWAKE_HOLD_MS);
    }

    if (last_flow_state != TEST_FLOW_WAIT_CARD && flow.state == TEST_FLOW_WAIT_CARD && keep_awake_until_ms != 0) {
        keep_awake_until_ms = 0;
        ESP_LOGI(TAG, "Flow returned to wait-card, cleared keep-awake hold");
    }

    if (last_flow_state != TEST_FLOW_WAIT_CARD && flow.state == TEST_FLOW_WAIT_CARD && !flow_wait_card_armed && !is_dimmed) {
        start_flow_logo_sweep();
    }

    last_flow_state = flow.state;
}

static void disarm_wait_card_ui(void)
{
    flow_wait_card_armed = false;
    insert_card_wait_until_ms = 0;
    test_flow_set_wait_card_enabled(false);
    if (flow_status_label) {
        lv_obj_add_flag(flow_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    last_status_visible = false;
    last_visual_state = (test_flow_state_t)-1;
    if (flow_phase_label && current_flow_state == TEST_FLOW_WAIT_CARD) {
        lv_label_set_text(flow_phase_label, "");
    }
    if (!is_dimmed && current_flow_state == TEST_FLOW_WAIT_CARD) {
        start_flow_logo_sweep();
    }
}

static bool arm_wait_card_ui(void)
{
    if (current_flow_state != TEST_FLOW_WAIT_CARD || flow_wait_card_armed) {
        return false;
    }
    if (sleep_command_sent || s_sleep_request_pending || s_wake_request_pending ||
        s_wake_sequence_active || !s_pm_lock_held) {
        sys_worker_send_req(SYS_WORKER_WAKE);
        ESP_LOGI(TAG, "Insert Card ignored while STM32 wake confirmation is pending");
        return false;
    }

    flow_wait_card_armed = true;
    stop_flow_logo_sweep();
    insert_card_wait_until_ms = lv_tick_get() + INSERT_CARD_TIMEOUT_MS;
    if (flow_status_label) {
        lv_obj_add_flag(flow_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    last_status_visible = false;
    last_visual_state = (test_flow_state_t)-1;
    update_test_flow_ui();
    update_flow_stage_visuals(current_flow_state);
    stm32_cmd_send_action(CMD_HI, NULL, 0);
    ESP_LOGI(TAG, "Sent HI after Insert Card prompt");
    test_flow_set_wait_card_enabled(true);
    ESP_LOGI(TAG, "Wait-card armed by main flow tap");
    return true;
}

static bool diag_long_press_allowed(void)
{
    if (pass_tile && !lv_obj_has_flag(pass_tile, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    return !s_in_diagnostic && current_flow_state == TEST_FLOW_WAIT_CARD;
}

static void set_diag_long_press_feedback(bool active)
{
    if (diag_long_press_feedback == active) {
        return;
    }

    diag_long_press_feedback = active;
    last_visual_state = (test_flow_state_t)-1;

    if (active) {
        lv_color_t accent = lv_color_hex(0xFF2EC4);
        if (flow_ring_main) {
            lv_obj_set_style_arc_color(flow_ring_main, accent, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(flow_ring_main, accent, LV_PART_MAIN);
            lv_obj_set_style_arc_opa(flow_ring_main, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(flow_ring_main, LV_OPA_COVER, LV_PART_MAIN);
        }
        if (flow_halo) {
            lv_obj_set_style_shadow_width(flow_halo, 22, 0);
            lv_obj_set_style_shadow_color(flow_halo, accent, 0);
            lv_obj_set_style_shadow_opa(flow_halo, LV_OPA_50, 0);
        }
        for (int i = 0; i < FLOW_GLINT_DOT_COUNT; i++) {
            if (flow_glint_dots[i]) {
                lv_obj_add_flag(flow_glint_dots[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (flow_logo_label) {
            lv_obj_set_style_text_color(flow_logo_label, accent, 0);
            lv_obj_set_style_text_opa(flow_logo_label, LV_OPA_COVER, 0);
        }
        stop_flow_logo_sweep();
        return;
    }

    if (flow_halo) {
        lv_obj_set_style_shadow_width(flow_halo, 0, 0);
        lv_obj_set_style_shadow_color(flow_halo, lv_color_hex(0x6375EC), 0);
        lv_obj_set_style_shadow_opa(flow_halo, LV_OPA_TRANSP, 0);
    }
    if (!flow_stage_scroll_hidden && !flow_glint_paused) {
        for (int i = 0; i < FLOW_GLINT_DOT_COUNT; i++) {
            if (flow_glint_dots[i]) {
                lv_obj_remove_flag(flow_glint_dots[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    update_flow_stage_visuals(current_flow_state);
    update_flow_logo_effect(current_flow_state);
}

static void cancel_diag_long_press(void)
{
    diag_long_press_tracking = false;
    set_diag_long_press_feedback(false);
    if (diag_long_press_timer) {
        lv_timer_pause(diag_long_press_timer);
    }
}

static void diag_long_press_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();

    if (!diag_long_press_tracking ||
        !diag_long_press_allowed() ||
        !pass_tile ||
        is_dimmed) {
        cancel_diag_long_press();
        return;
    }

    uint32_t elapsed = now - diag_long_press_start_ms;
    if (elapsed >= DIAG_LONG_PRESS_FEEDBACK_MS) {
        set_diag_long_press_feedback(true);
    }

    if (elapsed < DIAG_LONG_PRESS_MS) {
        return;
    }

    cancel_diag_long_press();
    stop_flow_logo_sweep();
    set_flow_glint_paused(true);
    set_flow_stage_scroll_hidden(true);
    diag_long_press_fired = true;
    lv_obj_remove_flag(pass_tile, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pass_tile);
    ESP_LOGI(TAG, "Diagnostic passcode page opened by long press");
}

static void set_flow_glint_paused(bool paused)
{
    if (flow_glint_paused == paused) {
        return;
    }

    flow_glint_paused = paused;
    for (int i = 0; i < FLOW_GLINT_DOT_COUNT; i++) {
        if (!flow_glint_dots[i]) {
            continue;
        }
        if (paused) {
            lv_obj_add_flag(flow_glint_dots[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(flow_glint_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void set_obj_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_flow_stage_scroll_hidden(bool hidden)
{
    if (flow_stage_scroll_hidden == hidden) {
        return;
    }

    flow_stage_scroll_hidden = hidden;
    set_obj_hidden(flow_halo, hidden);
    set_obj_hidden(flow_ring_bg, hidden);
    set_obj_hidden(flow_ring_main, hidden);
    for (int i = 0; i < FLOW_GLINT_DOT_COUNT; i++) {
        set_obj_hidden(flow_glint_dots[i], hidden);
    }

    if (!hidden) {
        set_flow_glint_paused(false);
        update_test_flow_ui();
        update_flow_stage_visuals(current_flow_state);
    }
}

static void flow_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    flow_tick++;
    update_test_flow_ui();
    update_flow_stage_visuals(current_flow_state);
    update_flow_logo_effect(current_flow_state);

    if (flow_network_busy_state(current_flow_state)) {
        if (!flow_timer_slow) {
            lv_timer_set_period(timer, 200);
            flow_timer_slow = true;
        }
        return;
    }

    if (flow_timer_slow) {
        lv_timer_set_period(timer, FLOW_TIMER_PERIOD_MS);
        flow_timer_slow = false;
    }

    if (network_recover_until_ms != 0) {
        if ((int32_t)(network_recover_until_ms - lv_tick_get()) > 0) {
            return;
        }
        lv_timer_set_period(lv_display_get_refr_timer(NULL), REFR_PERIOD_NORMAL);
        network_recover_until_ms = 0;
    }
}

static void pass_overlay_close(void)
{
    cancel_diag_long_press();
    diag_long_press_fired = false;

    if (pass_code_label) {
        lv_label_set_text(pass_code_label, "");
    }

    if (pass_tile) {
        lv_obj_add_flag(pass_tile, LV_OBJ_FLAG_HIDDEN);
    }

    if (!s_in_diagnostic && main_page && !lv_obj_has_flag(main_page, LV_OBJ_FLAG_HIDDEN)) {
        set_flow_stage_scroll_hidden(false);
        update_flow_logo_effect(current_flow_state);
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

    if (strlen(code) == DIAG_PASSCODE_LEN) {
        // if (strcmp(code, DIAG_PASSCODE) == 0) {
        //     pass_overlay_close();
        //     show_diagnostic();
        // } else {
        //     lv_label_set_text(label, "");
        // }

        // For development convenience, directly enter diagnostic on any 3-digit code
        pass_overlay_close();
        show_diagnostic();
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

#if 0
static void build_eye_wizard(lv_obj_t *parent)
{
    lv_obj_t *eye = lv_obj_create(parent);
    eye_root = eye;
    lv_obj_set_size(eye, 220, 150);
    lv_obj_align(eye, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(eye, 0, 0);
    lv_obj_set_style_border_width(eye, 0, 0);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_glow = lv_obj_create(eye);
    lv_obj_set_size(eye_glow, 214, 136);
    lv_obj_center(eye_glow);
    lv_obj_set_style_radius(eye_glow, 68, 0);
    lv_obj_set_style_bg_color(eye_glow, lv_color_hex(0x0F3635), 0);
    lv_obj_set_style_bg_opa(eye_glow, LV_OPA_60, 0);
    lv_obj_set_style_border_width(eye_glow, 0, 0);
    lv_obj_clear_flag(eye_glow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eye_glow, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_lid = lv_obj_create(eye);
    lv_obj_set_size(eye_lid, 190, 116);
    lv_obj_center(eye_lid);
    lv_obj_set_style_radius(eye_lid, 58, 0);
    lv_obj_set_style_bg_color(eye_lid, lv_color_hex(0xF7FBFF), 0);
    lv_obj_set_style_border_color(eye_lid, lv_color_hex(0x77D6C8), 0);
    lv_obj_set_style_border_width(eye_lid, 5, 0);
    lv_obj_clear_flag(eye_lid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eye_lid, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_iris = lv_obj_create(eye_lid);
    lv_obj_set_size(eye_iris, 76, 76);
    lv_obj_center(eye_iris);
    lv_obj_set_style_radius(eye_iris, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_iris, lv_color_hex(0x43C6AC), 0);
    lv_obj_set_style_border_width(eye_iris, 0, 0);
    lv_obj_clear_flag(eye_iris, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eye_iris, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_pupil = lv_obj_create(eye_iris);
    lv_obj_set_size(eye_pupil, 34, 34);
    lv_obj_center(eye_pupil);
    lv_obj_set_style_radius(eye_pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_pupil, lv_color_hex(0x071016), 0);
    lv_obj_set_style_border_width(eye_pupil, 0, 0);
    lv_obj_clear_flag(eye_pupil, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(eye_pupil, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *shine = lv_obj_create(eye_pupil);
    lv_obj_set_size(shine, 10, 10);
    lv_obj_align(shine, LV_ALIGN_TOP_LEFT, 6, 5);
    lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shine, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(shine, 0, 0);
    lv_obj_add_flag(shine, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *tiny_shine = lv_obj_create(eye_pupil);
    lv_obj_set_size(tiny_shine, 5, 5);
    lv_obj_align(tiny_shine, LV_ALIGN_TOP_LEFT, 19, 11);
    lv_obj_set_style_radius(tiny_shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(tiny_shine, lv_color_hex(0xB8FFF4), 0);
    lv_obj_set_style_border_width(tiny_shine, 0, 0);
    lv_obj_add_flag(tiny_shine, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_cheek_l = lv_obj_create(eye);
    lv_obj_set_size(eye_cheek_l, 34, 12);
    lv_obj_align(eye_cheek_l, LV_ALIGN_CENTER, -74, 46);
    lv_obj_set_style_radius(eye_cheek_l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_cheek_l, lv_color_hex(0xFF8FAB), 0);
    lv_obj_set_style_bg_opa(eye_cheek_l, LV_OPA_70, 0);
    lv_obj_set_style_border_width(eye_cheek_l, 0, 0);
    lv_obj_add_flag(eye_cheek_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye_cheek_l, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_cheek_r = lv_obj_create(eye);
    lv_obj_set_size(eye_cheek_r, 34, 12);
    lv_obj_align(eye_cheek_r, LV_ALIGN_CENTER, 74, 46);
    lv_obj_set_style_radius(eye_cheek_r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye_cheek_r, lv_color_hex(0xFF8FAB), 0);
    lv_obj_set_style_bg_opa(eye_cheek_r, LV_OPA_70, 0);
    lv_obj_set_style_border_width(eye_cheek_r, 0, 0);
    lv_obj_add_flag(eye_cheek_r, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye_cheek_r, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_spark_l = lv_label_create(eye);
    lv_label_set_text(eye_spark_l, "*");
    lv_obj_set_style_text_font(eye_spark_l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(eye_spark_l, lv_color_hex(0xFFF6A3), 0);
    lv_obj_align(eye_spark_l, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_add_flag(eye_spark_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye_spark_l, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_spark_r = lv_label_create(eye);
    lv_label_set_text(eye_spark_r, "+");
    lv_obj_set_style_text_font(eye_spark_r, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(eye_spark_r, lv_color_hex(0xB8FFF4), 0);
    lv_obj_align(eye_spark_r, LV_ALIGN_TOP_RIGHT, -24, 24);
    lv_obj_add_flag(eye_spark_r, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye_spark_r, LV_OBJ_FLAG_EVENT_BUBBLE);

    eye_mood_label = lv_label_create(eye);
    lv_label_set_text(eye_mood_label, "");
    lv_obj_set_style_text_font(eye_mood_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(eye_mood_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(eye_mood_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(eye_mood_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    sleep_label = lv_label_create(eye);
    lv_label_set_text(sleep_label, "Zzz");
    lv_obj_set_style_text_font(sleep_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sleep_label, lv_color_hex(0xB6C2CF), 0);
    lv_obj_align(sleep_label, LV_ALIGN_TOP_RIGHT, -18, 6);
    lv_obj_add_flag(sleep_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sleep_label, LV_OBJ_FLAG_EVENT_BUBBLE);
}
#endif

static lv_obj_t *create_flow_arc(lv_obj_t *parent, int size, int width, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 80);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, opa, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, opa, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_EVENT_BUBBLE);
    return arc;
}

static void create_report_metric(lv_obj_t *parent, int x, int y, report_metric_view_t *metric)
{
    metric->box = lv_obj_create(parent);
    lv_obj_set_size(metric->box, 94, 56);
    lv_obj_align(metric->box, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_bg_color(metric->box, lv_color_hex(0x101D32), 0);
    lv_obj_set_style_bg_opa(metric->box, LV_OPA_40, 0);
    lv_obj_set_style_border_color(metric->box, lv_color_hex(0x38527E), 0);
    lv_obj_set_style_border_opa(metric->box, LV_OPA_40, 0);
    lv_obj_set_style_border_width(metric->box, 1, 0);
    lv_obj_set_style_radius(metric->box, 8, 0);
    lv_obj_set_style_pad_all(metric->box, 5, 0);
    lv_obj_clear_flag(metric->box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(metric->box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(metric->box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(metric->box, report_metric_click_cb, LV_EVENT_CLICKED, NULL);

    metric->value = lv_label_create(metric->box);
    lv_obj_set_width(metric->value, 82);
    lv_obj_set_style_text_align(metric->value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(metric->value, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(metric->value, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(metric->value, LV_LABEL_LONG_DOT);
    lv_obj_align(metric->value, LV_ALIGN_TOP_MID, 0, 4);

    metric->name = lv_label_create(metric->box);
    lv_obj_set_width(metric->name, 82);
    lv_obj_set_style_text_align(metric->name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(metric->name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(metric->name, lv_color_hex(0x9EB4D1), 0);
    lv_label_set_long_mode(metric->name, LV_LABEL_LONG_DOT);
    lv_obj_align(metric->name, LV_ALIGN_BOTTOM_MID, 0, -3);
}

static void build_flow_report(lv_obj_t *parent)
{
    flow_report_title_label = lv_label_create(parent);
    lv_label_set_text(flow_report_title_label, "Uploaded");
    lv_obj_set_width(flow_report_title_label, 260);
    lv_obj_set_style_text_align(flow_report_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(flow_report_title_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(flow_report_title_label, lv_color_hex(0xF7FBFF), 0);
    lv_obj_align(flow_report_title_label, LV_ALIGN_CENTER, 0, -105);
    lv_obj_add_flag(flow_report_title_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_report_chip_label = lv_label_create(parent);
    lv_label_set_text(flow_report_chip_label, "--");
    lv_obj_set_width(flow_report_chip_label, 270);
    lv_obj_set_style_text_align(flow_report_chip_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(flow_report_chip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(flow_report_chip_label, lv_color_hex(0xA6C4E5), 0);
    lv_label_set_long_mode(flow_report_chip_label, LV_LABEL_LONG_DOT);
    lv_obj_align(flow_report_chip_label, LV_ALIGN_CENTER, 0, -78);
    lv_obj_add_flag(flow_report_chip_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    create_report_metric(parent, -58, -9, &flow_report_metrics[0]);
    create_report_metric(parent, 58, -9, &flow_report_metrics[1]);
    create_report_metric(parent, -58, 55, &flow_report_metrics[2]);
    create_report_metric(parent, 58, 55, &flow_report_metrics[3]);

    flow_report_tap_hitbox = lv_button_create(parent);
    lv_obj_set_size(flow_report_tap_hitbox, REPORT_TAP_HITBOX_W, REPORT_TAP_HITBOX_H);
    lv_obj_align(flow_report_tap_hitbox, LV_ALIGN_CENTER, 0, 118);
    lv_obj_set_style_bg_opa(flow_report_tap_hitbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(flow_report_tap_hitbox, 0, 0);
    lv_obj_set_style_pad_all(flow_report_tap_hitbox, 0, 0);
    lv_obj_clear_flag(flow_report_tap_hitbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(flow_report_tap_hitbox, report_eject_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(flow_report_tap_hitbox);

    flow_report_tap_label = lv_label_create(flow_report_tap_hitbox);
    lv_label_set_text(flow_report_tap_label, "Tap to eject");
    lv_obj_set_width(flow_report_tap_label, REPORT_TAP_HITBOX_W);
    lv_obj_set_style_text_align(flow_report_tap_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(flow_report_tap_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(flow_report_tap_label, lv_color_hex(0xA6C4E5), 0);
    lv_obj_center(flow_report_tap_label);
    lv_obj_add_flag(flow_report_tap_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    set_flow_report_visible(false);
}

static void set_flow_report_visible(bool visible)
{
    lv_obj_t *items[] = {
        flow_report_title_label,
        flow_report_chip_label,
        flow_report_metrics[0].box,
        flow_report_metrics[1].box,
        flow_report_metrics[2].box,
        flow_report_metrics[3].box,
        flow_report_tap_hitbox,
    };

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        if (items[i]) {
            set_obj_hidden(items[i], !visible);
        }
    }
}

static void update_flow_report(const test_flow_snapshot_t *flow)
{
    char chip[48] = "--";
    report_item_t items[REPORT_ITEM_MAX];
    int count;

    if (!flow) {
        return;
    }

    summary_value_after(flow->upload_summary, "Chip: ", chip, sizeof(chip));
    count = summary_report_items(flow->upload_summary, items, (int)(sizeof(items) / sizeof(items[0])));
    if (count <= 0) {
        count = 1;
        strncpy(items[0].name, "Report", sizeof(items[0].name) - 1);
        strncpy(items[0].value, "Done", sizeof(items[0].value) - 1);
    }
    if (flow_report_page * REPORT_PAGE_SIZE >= count) {
        flow_report_page = 0;
    }

    lv_label_set_text(flow_report_title_label, "Uploaded");
    lv_label_set_text(flow_report_chip_label, chip[0] ? chip : "--");

    for (int i = 0; i < REPORT_METRIC_COUNT; i++) {
        int idx = flow_report_page * REPORT_PAGE_SIZE + i;
        if (idx < count) {
            lv_label_set_text(flow_report_metrics[i].value, items[idx].value);
            lv_label_set_text(flow_report_metrics[i].name, items[idx].name);
            set_obj_hidden(flow_report_metrics[i].box, false);
        } else {
            set_obj_hidden(flow_report_metrics[i].box, true);
        }
    }
}

static void report_metric_click_cb(lv_event_t *e)
{
    (void)e;
    if (!flow_report_display_state(current_flow_state)) {
        return;
    }
    flow_report_page++;
    test_flow_snapshot_t flow;
    test_flow_get_snapshot(&flow);
    update_flow_report(&flow);
    lv_event_stop_bubbling(e);
}

static void report_eject_click_cb(lv_event_t *e)
{
    (void)e;
    if (current_flow_state == TEST_FLOW_UPLOAD_REVIEW &&
        test_flow_continue_after_upload_review()) {
        keep_awake_for(CARD_AWAKE_HOLD_MS);
        update_test_flow_ui();
        ESP_LOGI(TAG, "Report eject triggered by tap label");
    }
    lv_event_stop_bubbling(e);
}

static void build_kino_stage(lv_obj_t *parent)
{
    flow_halo = lv_obj_create(parent);
    lv_obj_set_size(flow_halo, 314, 314);
    lv_obj_align(flow_halo, LV_ALIGN_CENTER, 0, -2);
    lv_obj_set_style_radius(flow_halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(flow_halo, lv_color_hex(0x142847), 0);
    lv_obj_set_style_bg_opa(flow_halo, (lv_opa_t)115, 0);
    lv_obj_set_style_border_width(flow_halo, 1, 0);
    lv_obj_set_style_border_color(flow_halo, lv_color_hex(0x253A60), 0);
    lv_obj_set_style_shadow_width(flow_halo, 0, 0);
    lv_obj_set_style_shadow_color(flow_halo, lv_color_hex(0x6375EC), 0);
    lv_obj_set_style_shadow_opa(flow_halo, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(flow_halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(flow_halo, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_ring_bg = create_flow_arc(parent, 430, 6, lv_color_hex(0x263A5C), LV_OPA_70);
    lv_arc_set_angles(flow_ring_bg, 0, 360);

    flow_ring_main = create_flow_arc(parent, 430, 6, lv_color_hex(0x6375EC), LV_OPA_COVER);
    lv_arc_set_angles(flow_ring_main, 0, 360);

    for (int i = 0; i < FLOW_GLINT_DOT_COUNT; i++) {
        flow_glint_dots[i] = lv_obj_create(parent);
        lv_obj_set_size(flow_glint_dots[i], 13, 12);
        lv_obj_align(flow_glint_dots[i], LV_ALIGN_CENTER, 0, -FLOW_GLINT_RADIUS);
        lv_obj_set_style_radius(flow_glint_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(flow_glint_dots[i], lv_color_hex(0x6375EC), 0);
        lv_obj_set_style_bg_opa(flow_glint_dots[i], LV_OPA_80, 0);
        lv_obj_set_style_border_width(flow_glint_dots[i], 0, 0);
        lv_obj_clear_flag(flow_glint_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(flow_glint_dots[i], LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    flow_logo_label = lv_label_create(parent);
    lv_label_set_text(flow_logo_label, "KINO");
    lv_obj_set_style_text_font(flow_logo_label, &lv_font_montserrat_48, 0);
    flow_logo_last_color = lv_color_hex(0xEEF2FF);
    lv_obj_set_style_text_color(flow_logo_label, flow_logo_last_color, 0);
    lv_obj_set_style_text_letter_space(flow_logo_label, 6, 0);
    lv_obj_align(flow_logo_label, LV_ALIGN_CENTER, 3, -18);
    lv_obj_add_flag(flow_logo_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_logo_sweep_mask = lv_obj_create(parent);
    lv_obj_set_size(flow_logo_sweep_mask, 34, 116);
    lv_obj_set_style_bg_opa(flow_logo_sweep_mask, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(flow_logo_sweep_mask, 0, 0);
    lv_obj_set_style_pad_all(flow_logo_sweep_mask, 0, 0);
    lv_obj_set_style_transform_rotation(flow_logo_sweep_mask, 450, 0);
    lv_obj_set_style_transform_pivot_x(flow_logo_sweep_mask, 17, 0);
    lv_obj_set_style_transform_pivot_y(flow_logo_sweep_mask, 58, 0);
    lv_obj_set_style_transform_width(flow_logo_sweep_mask, 48, 0);
    lv_obj_set_style_transform_height(flow_logo_sweep_mask, 48, 0);
    lv_obj_clear_flag(flow_logo_sweep_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(flow_logo_sweep_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(flow_logo_sweep_mask, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_logo_sweep_label = lv_label_create(flow_logo_sweep_mask);
    lv_label_set_text(flow_logo_sweep_label, "KINO");
    lv_obj_set_style_text_font(flow_logo_sweep_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(flow_logo_sweep_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(flow_logo_sweep_label, 6, 0);
    lv_obj_add_flag(flow_logo_sweep_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_phase_label = lv_label_create(parent);
    lv_label_set_text(flow_phase_label, "Ready");
    lv_obj_set_width(flow_phase_label, 220);
    lv_obj_set_style_text_align(flow_phase_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(flow_phase_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(flow_phase_label, lv_color_hex(0xA6C4E5), 0);
    lv_label_set_long_mode(flow_phase_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_letter_space(flow_phase_label, 2, 0);
    lv_obj_align(flow_phase_label, LV_ALIGN_CENTER, 0, 42);
    lv_obj_add_flag(flow_phase_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_phase_arrow_label = lv_label_create(parent);
    lv_label_set_text(flow_phase_arrow_label, LV_SYMBOL_UP);
    lv_obj_set_style_text_font(flow_phase_arrow_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(flow_phase_arrow_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(flow_phase_arrow_label, LV_OPA_0, 0);
    lv_obj_align_to(flow_phase_arrow_label, flow_phase_label, LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_add_flag(flow_phase_arrow_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(flow_phase_arrow_label, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void update_flow_stage_visuals(test_flow_state_t state)
{
    bool animating = (state == TEST_FLOW_CARD_DETECTED ||
                      state == TEST_FLOW_PREP_HOMING ||
                      state == TEST_FLOW_PREP_OPENING ||
                      state == TEST_FLOW_CLOSING ||
                      state == TEST_FLOW_READING_NFC ||
                      state == TEST_FLOW_GETTING_CHIP ||
                      state == TEST_FLOW_MOCK_TESTING ||
                      state == TEST_FLOW_POSTING_BIOMARKERS ||
                      state == TEST_FLOW_SUCCESS_EJECTING ||
                      flow_logo_sweep_running);

    if (animating) {
        set_flow_glint_paused(true);
    } else {
        if (!is_dimmed && !s_in_diagnostic && !flow_stage_scroll_hidden) {
            set_flow_glint_paused(false);
        }
    }

    lv_color_t color = diag_long_press_feedback ? lv_color_hex(0xFF2EC4) :
                       is_dimmed ? lv_color_hex(0x5E6B78) : flow_state_color(state);
    lv_opa_t logo_opa = flow_report_display_state(state) ? LV_OPA_20 :
                         (!is_dimmed && state != TEST_FLOW_WAIT_CARD) ? LV_OPA_50 : LV_OPA_COVER;
    int speed = diag_long_press_feedback ? -10 :
                flow_retryable_error_state(state) ? 13 :
                flow_report_display_state(state) ? 3 :
                state == TEST_FLOW_WAIT_CARD ? 2 : 7;

    if (!flow_ring_main || !flow_glint_dots[0]) {
        return;
    }

    bool visual_changed = diag_long_press_feedback || last_visual_state != state || last_visual_dimmed != is_dimmed;
    if (visual_changed) {
        lv_opa_t ring_opa = is_dimmed ? LV_OPA_40 : LV_OPA_80;
        lv_obj_set_style_arc_color(flow_ring_main, color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(flow_ring_main, color, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(flow_ring_main, ring_opa, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(flow_ring_main, ring_opa, LV_PART_MAIN);

        int mid = FLOW_GLINT_DOT_COUNT / 2;
        for (int i = 0; i < FLOW_GLINT_DOT_COUNT; i++) {
            int distance = i > mid ? i - mid : mid - i;
            uint8_t highlight = (uint8_t)((mid - distance) * 210 / mid);
            lv_color_t dot_color = lv_color_mix(lv_color_hex(0xF8FCFF), color, highlight);
            lv_opa_t dot_opa = is_dimmed ? (lv_opa_t)(52 + (mid - distance) * 5) :
                                          (lv_opa_t)(145 + (mid - distance) * 18);
            lv_obj_set_style_bg_color(flow_glint_dots[i], dot_color, 0);
            lv_obj_set_style_bg_opa(flow_glint_dots[i], dot_opa, 0);
        }

        flow_logo_last_color = is_dimmed ? lv_color_hex(0x7B8794) : lv_color_hex(0xEEF2FF);
        lv_obj_set_style_text_color(flow_logo_label, flow_logo_last_color, 0);
        lv_obj_set_style_text_opa(flow_logo_label, logo_opa, 0);
        if ((is_dimmed && state == TEST_FLOW_WAIT_CARD) ||
            flow_report_display_state(state) ||
            state == TEST_FLOW_WAIT_REMOVE_CARD ||
            (state == TEST_FLOW_WAIT_CARD && flow_wait_card_armed)) {
            lv_label_set_text(flow_phase_label, "");
        } else {
            lv_label_set_text(flow_phase_label, flow_phase_text(state));
        }
        lv_obj_set_style_text_color(flow_phase_label, color, 0);
        lv_obj_set_style_text_opa(flow_phase_label, LV_OPA_COVER, 0);

        if (flow_report_display_state(state)) {
            lv_obj_set_style_text_color(flow_report_tap_label, color, 0);
        }

        last_visual_state = state;
        last_visual_dimmed = is_dimmed;
    }

    if (state == TEST_FLOW_WAIT_CARD && flow_wait_card_armed && !is_dimmed) {
        set_prompt_arrow(flow_status_arrow_label, flow_status_label, LV_SYMBOL_UP, true);
        if (flow_phase_arrow_label) {
            set_obj_hidden(flow_phase_arrow_label, true);
        }
        if (flow_status_label && !lv_obj_has_flag(flow_status_label, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_set_style_text_opa(flow_status_label, prompt_pulse_opa(), 0);
        }
    } else if (flow_report_display_state(state) && !is_dimmed) {
        if (flow_report_tap_label) {
            lv_obj_set_style_text_opa(flow_report_tap_label, prompt_pulse_opa(), 0);
        }
    } else if (state == TEST_FLOW_WAIT_REMOVE_CARD && !is_dimmed) {
        set_prompt_arrow(flow_status_arrow_label, flow_status_label, LV_SYMBOL_DOWN, true);
        if (flow_phase_arrow_label) {
            set_obj_hidden(flow_phase_arrow_label, true);
        }
        if (flow_status_label && !lv_obj_has_flag(flow_status_label, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_set_style_text_opa(flow_status_label, prompt_pulse_opa(), 0);
        }
    } else {
        if (flow_phase_arrow_label) {
            set_obj_hidden(flow_phase_arrow_label, true);
        }
        if (flow_status_arrow_label) {
            set_obj_hidden(flow_status_arrow_label, true);
        }
        if (flow_status_label) {
            lv_obj_set_style_text_opa(flow_status_label, LV_OPA_COVER, 0);
        }
    }

    int base_angle = (270 + flow_tick * speed * 2) % 360;
    if (base_angle < 0) {
        base_angle += 360;
    }
    if (diag_long_press_feedback) {
        return;
    }

    int mid = FLOW_GLINT_DOT_COUNT / 2;
    for (int i = 0; i < FLOW_GLINT_DOT_COUNT; i++) {
        int angle = (base_angle + (i - mid) * 3 + 360) % 360;
        int x = (FLOW_GLINT_RADIUS * lv_trigo_cos(angle)) >> LV_TRIGO_SHIFT;
        int y = (FLOW_GLINT_RADIUS * lv_trigo_sin(angle)) >> LV_TRIGO_SHIFT;
        lv_obj_align(flow_glint_dots[i], LV_ALIGN_CENTER, x, y);
    }
}

static void update_flow_logo_effect(test_flow_state_t state)
{
    if (!flow_logo_label || !flow_logo_sweep_mask || !flow_logo_sweep_label || flow_stage_scroll_hidden) {
        return;
    }

    bool rainbow_ready = state == TEST_FLOW_WAIT_CARD && !flow_wait_card_armed && !is_dimmed;

    if (rainbow_ready && flow_logo_sweep_running) {
        lv_area_t logo_area;
        lv_area_t parent_area;
        int32_t logo_w;
        int32_t logo_h;
        int32_t mask_w;
        int32_t travel;
        int elapsed = flow_tick - flow_logo_sweep_start_tick;
        int traversal = elapsed / LOGO_SWEEP_STEPS;
        int step = elapsed % LOGO_SWEEP_STEPS;
        int phase;
        int wave_angle;
        int32_t wave_amp;
        int32_t wave_y;

        if (traversal >= LOGO_SWEEP_TRAVERSALS) {
            stop_flow_logo_sweep();
            return;
        }

        phase = (traversal & 1) ? (LOGO_SWEEP_STEPS - step) : step;

        if (flow_logo_recolor_on && phase == flow_logo_sweep_phase) {
            return;
        }

        lv_obj_update_layout(flow_logo_label);
        lv_obj_get_coords(flow_logo_label, &logo_area);
        lv_obj_get_coords(lv_obj_get_parent(flow_logo_label), &parent_area);
        logo_w = lv_area_get_width(&logo_area);
        logo_h = lv_area_get_height(&logo_area);
        mask_w = lv_obj_get_width(flow_logo_sweep_mask);
        travel = logo_w + mask_w * 2;

        int32_t mask_x = logo_area.x1 - parent_area.x1 - mask_w + (travel * phase) / LOGO_SWEEP_STEPS;
        wave_angle = (flow_logo_sweep_wave_phase + elapsed * 22) % 360;
        wave_amp = logo_h / 5;
        wave_y = (wave_amp * lv_trigo_sin(wave_angle)) >> LV_TRIGO_SHIFT;
        int32_t mask_y = logo_area.y1 - parent_area.y1 - 28 + wave_y;
        lv_obj_set_style_text_color(flow_logo_sweep_label,
                                    lv_color_hsv_to_rgb((uint16_t)((elapsed * 37) % 360), 92, 100),
                                    0);
        lv_obj_set_pos(flow_logo_sweep_mask, mask_x, mask_y);
        lv_obj_set_pos(flow_logo_sweep_label,
                       (logo_area.x1 - parent_area.x1) - mask_x,
                       (logo_area.y1 - parent_area.y1) - mask_y);
        lv_obj_remove_flag(flow_logo_sweep_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(flow_logo_sweep_mask);
        flow_logo_sweep_phase = phase;
        flow_logo_recolor_on = true;
        return;
    }

    lv_color_t logo_color = is_dimmed ? lv_color_hex(0x7B8794) : lv_color_hex(0xEEF2FF);
    if (flow_logo_recolor_on) {
        stop_flow_logo_sweep();
    }

    if (lv_color_to_u32(logo_color) != lv_color_to_u32(flow_logo_last_color)) {
        lv_obj_set_style_text_color(flow_logo_label, logo_color, 0);
        flow_logo_last_color = logo_color;
    }
}

static void start_flow_logo_sweep(void)
{
    if (!flow_logo_sweep_mask || !flow_logo_sweep_label || is_dimmed || flow_wait_card_armed) {
        return;
    }

    flow_logo_sweep_start_tick = flow_tick;
    flow_logo_sweep_wave_phase = (flow_tick * 47 + 113) % 360;
    flow_logo_sweep_phase = -1;
    flow_logo_sweep_running = true;
    flow_logo_recolor_on = true;
    lv_obj_remove_flag(flow_logo_sweep_mask, LV_OBJ_FLAG_HIDDEN);
}

static void stop_flow_logo_sweep(void)
{
    if (flow_logo_sweep_mask) {
        lv_obj_add_flag(flow_logo_sweep_mask, LV_OBJ_FLAG_HIDDEN);
    }
    flow_logo_sweep_phase = -1;
    flow_logo_sweep_running = false;
    flow_logo_recolor_on = false;
}

static void build_main_flow(lv_obj_t *scr)
{
    main_page = lv_obj_create(scr);
    lv_obj_set_size(main_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_page, lv_color_hex(0x0B1C2E), 0);
    lv_obj_set_style_border_width(main_page, 0, 0);
    lv_obj_set_style_pad_all(main_page, 0, 0);
    lv_obj_clear_flag(main_page, LV_OBJ_FLAG_SCROLLABLE);

    main_tile = lv_obj_create(main_page);
    lv_obj_set_size(main_tile, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_tile, lv_color_hex(0x0B1C2E), 0);
    lv_obj_set_style_border_width(main_tile, 0, 0);
    lv_obj_set_style_pad_all(main_tile, 0, 0);
    lv_obj_clear_flag(main_tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(main_tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(main_tile, main_flow_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(main_tile, main_flow_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(main_tile, main_flow_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(main_tile, main_flow_press_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(main_tile, main_flow_click_cb, LV_EVENT_CLICKED, NULL);

    pass_tile = lv_obj_create(main_page);
    lv_obj_set_size(pass_tile, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(pass_tile, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(pass_tile, main_flow_press_cb, LV_EVENT_PRESSED, NULL);
    build_pass_tile(pass_tile);

    build_kino_stage(main_tile);

    flow_status_label = lv_label_create(main_tile);
    lv_label_set_text(flow_status_label, "Insert card");
    lv_obj_set_width(flow_status_label, 330);
    lv_obj_set_style_text_align(flow_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(flow_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(flow_status_label, lv_color_hex(0xA6C4E5), 0);
    lv_obj_align(flow_status_label, LV_ALIGN_BOTTOM_MID, 0, -104);
    lv_obj_add_flag(flow_status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(flow_status_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_status_arrow_label = lv_label_create(main_tile);
    lv_label_set_text(flow_status_arrow_label, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(flow_status_arrow_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(flow_status_arrow_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(flow_status_arrow_label, LV_OPA_0, 0);
    lv_obj_align_to(flow_status_arrow_label, flow_status_label, LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_add_flag(flow_status_arrow_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(flow_status_arrow_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    flow_hint_label = lv_label_create(main_tile);
    lv_label_set_text(flow_hint_label, "Waiting for reagent card");
    lv_obj_set_width(flow_hint_label, 330);
    lv_obj_set_style_text_align(flow_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(flow_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(flow_hint_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(flow_hint_label, lv_color_hex(0xA6C4E5), 0);
    lv_obj_align(flow_hint_label, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_add_flag(flow_hint_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(flow_hint_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    build_flow_report(main_tile);

    flow_timer = lv_timer_create(flow_timer_cb, FLOW_TIMER_PERIOD_MS, NULL);
}

static void main_flow_press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        cancel_diag_long_press();
        return;
    }

    if (code != LV_EVENT_PRESSED) {
        return;
    }

    if (is_dimmed) {
        restore_screen_now();
        cancel_diag_long_press();
        lv_event_stop_bubbling(e);
        lv_event_stop_processing(e);
        return;
    }

    if (!diag_long_press_allowed()) {
        return;
    }

    diag_long_press_tracking = true;
    diag_long_press_fired = false;
    diag_long_press_start_ms = lv_tick_get();
    if (!diag_long_press_timer) {
        diag_long_press_timer = lv_timer_create(diag_long_press_timer_cb, DIAG_LONG_PRESS_TICK_MS, NULL);
    }
    lv_timer_set_period(diag_long_press_timer, DIAG_LONG_PRESS_TICK_MS);
    lv_timer_resume(diag_long_press_timer);
    lv_timer_reset(diag_long_press_timer);
}

static void main_flow_click_cb(lv_event_t *e)
{
    (void)e;

    if (diag_long_press_fired) {
        diag_long_press_fired = false;
        return;
    }

    if (diag_long_press_tracking) {
        cancel_diag_long_press();
    }

    if (current_flow_state == TEST_FLOW_WAIT_CARD && !flow_wait_card_armed) {
        if ((uint32_t)(lv_tick_get() - last_dim_restore_ms) < WAKE_ARM_GUARD_MS) {
            return;
        }
        arm_wait_card_ui();
        return;
    }

    if (flow_report_display_state(current_flow_state)) {
        return;
    }

    if (!flow_retryable_error_state(current_flow_state)) {
        return;
    }

    if (test_flow_retry_after_error()) {
        keep_awake_for(CARD_AWAKE_HOLD_MS);
        update_test_flow_ui();
        ESP_LOGI(TAG, "Retry requested by main flow tap");
    }
}

static void exit_btn_cb(lv_event_t *e)
{
    (void)e;
    diag_exit_overlay_hide();
    show_main_flow();
}

static void diag_cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    diag_exit_overlay_hide();
}

static void diag_tileview_changed_cb(lv_event_t *e)
{
    update_active_page(diag_normalize_active_tile(lv_tileview_get_tile_active(lv_event_get_target(e))));
}

static void diag_enable_event_bubble_recursive(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        diag_enable_event_bubble_recursive(lv_obj_get_child(obj, i));
    }
}

static void diag_prepare_tile(lv_obj_t *tile)
{
    lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(tile, diag_pointer_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(tile, diag_pointer_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(tile, diag_pointer_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(tile, diag_pointer_event_cb, LV_EVENT_PRESS_LOST, NULL);
}

static lv_obj_t *diag_normalize_active_tile(lv_obj_t *active_tile)
{
    if (active_tile == t_wrap_l) {
        lv_obj_set_tile_id(tv, 6, 0, LV_ANIM_OFF);
        return t6;
    }

    if (active_tile == t_wrap_r) {
        lv_obj_set_tile_id(tv, 1, 0, LV_ANIM_OFF);
        return t1;
    }

    return active_tile;
}

static void diag_pointer_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (is_dimmed && code == LV_EVENT_PRESSED) {
        diag_touch_tracking = false;
        restore_screen_now();
        lv_event_stop_bubbling(e);
        lv_event_stop_processing(e);
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &diag_touch_start);
        diag_touch_tracking = true;
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        diag_touch_tracking = false;
        return;
    }

    if (!diag_touch_tracking) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    int dx = point.x - diag_touch_start.x;
    int dy = point.y - diag_touch_start.y;
    int abs_dx = dx >= 0 ? dx : -dx;
    int abs_dy = dy >= 0 ? dy : -dy;
    const int swipe_threshold = 35;

    if (code != LV_EVENT_RELEASED) {
        return;
    }

    diag_touch_tracking = false;

    if (abs_dy > abs_dx && dy <= -swipe_threshold) {
        diag_exit_overlay_show();
        return;
    }
}

static void diag_exit_overlay_hide(void)
{
    if (diag_exit_overlay) {
        lv_obj_add_flag(diag_exit_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void diag_exit_overlay_show(void)
{
    if (diag_exit_overlay) {
        lv_obj_remove_flag(diag_exit_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(diag_exit_overlay);
    }
}

static void build_diag_exit_overlay(lv_obj_t *parent)
{
    diag_exit_overlay = lv_obj_create(parent);
    lv_obj_set_size(diag_exit_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(diag_exit_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(diag_exit_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(diag_exit_overlay, 0, 0);
    lv_obj_set_style_pad_all(diag_exit_overlay, 0, 0);
    lv_obj_add_flag(diag_exit_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(diag_exit_overlay, LV_OBJ_FLAG_SCROLLABLE);

    diag_exit_panel = lv_obj_create(diag_exit_overlay);
    lv_obj_set_size(diag_exit_panel, 320, 200);
    lv_obj_center(diag_exit_panel);
    lv_obj_set_style_radius(diag_exit_panel, 16, 0);
    lv_obj_set_style_bg_color(diag_exit_panel, lv_color_hex(0x11181F), 0);
    lv_obj_set_style_border_width(diag_exit_panel, 0, 0);
    lv_obj_set_style_pad_all(diag_exit_panel, 16, 0);
    lv_obj_clear_flag(diag_exit_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(diag_exit_panel);
    lv_label_set_text(title, "Exit Diagnostic?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *btn_row = lv_obj_create(diag_exit_panel);
    lv_obj_set_size(btn_row, 280, 72);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 24, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    diag_exit_btn = lv_button_create(btn_row);
    lv_obj_set_size(diag_exit_btn, 124, 60);
    lv_obj_set_style_radius(diag_exit_btn, 29, 0);
    lv_obj_set_style_bg_color(diag_exit_btn, lv_color_hex(0xE74C3C), 0);
    lv_obj_add_event_cb(diag_exit_btn, exit_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *exit_label = lv_label_create(diag_exit_btn);
    lv_label_set_text(exit_label, "EXIT");
    lv_obj_set_style_text_font(exit_label, &lv_font_montserrat_18, 0);
    lv_obj_center(exit_label);

    diag_cancel_btn = lv_button_create(btn_row);
    lv_obj_set_size(diag_cancel_btn, 124, 60);
    lv_obj_set_style_radius(diag_cancel_btn, 29, 0);
    lv_obj_set_style_bg_color(diag_cancel_btn, lv_color_hex(0x2ECC71), 0);
    lv_obj_add_event_cb(diag_cancel_btn, diag_cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(diag_cancel_btn);
    lv_label_set_text(cancel_label, "CANCEL");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_18, 0);
    lv_obj_center(cancel_label);
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

    lv_obj_add_event_cb(tv, diag_tileview_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    t_wrap_l = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(t_wrap_l, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(t_wrap_l, LV_OBJ_FLAG_SCROLLABLE);

    t1 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    ui_sys_stats_init(t1);
    diag_prepare_tile(t1);
    diag_enable_event_bubble_recursive(t1);

    t2 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    ui_motor_init(t2);
    diag_prepare_tile(t2);
    diag_enable_event_bubble_recursive(t2);

    t3 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);
    ui_wifi_prov_init(t3);
    diag_prepare_tile(t3);
    diag_enable_event_bubble_recursive(t3);

    t4 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_HOR);
    ui_misc_init(t4);
    diag_prepare_tile(t4);
    diag_enable_event_bubble_recursive(t4);

    t5 = lv_tileview_add_tile(tv, 5, 0, LV_DIR_HOR);
    ui_standby_init(t5);
    diag_prepare_tile(t5);
    diag_enable_event_bubble_recursive(t5);

    t6 = lv_tileview_add_tile(tv, 6, 0, LV_DIR_HOR);
    ui_ble_init(t6);
    diag_prepare_tile(t6);
    diag_enable_event_bubble_recursive(t6);

    t_wrap_r = lv_tileview_add_tile(tv, 7, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(t_wrap_r, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(t_wrap_r, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(tv, diag_pointer_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(tv, diag_pointer_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(tv, diag_pointer_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(tv, diag_pointer_event_cb, LV_EVENT_PRESS_LOST, NULL);

    build_diag_exit_overlay(diagnostic_page);

    lv_obj_set_tile_id(tv, 1, 0, LV_ANIM_OFF);
    update_active_page(NULL);
}

static void update_active_page(lv_obj_t *active_tile)
{
    set_auto_dim(active_tile != t6);
    ui_sys_stats_set_active(active_tile == t1);
    ui_motor_set_active(active_tile == t2);
    ui_wifi_prov_set_active(active_tile == t3);
    ui_misc_set_active(active_tile == t4);
    ui_standby_set_active(active_tile == t5);
    ui_ble_set_active(active_tile == t6);
    ui_nfc_set_active(active_tile == t2);
}

static void show_main_flow(void)
{
    ESP_LOGI(TAG, "show main flow");
    s_in_diagnostic = false;
    flow_wait_card_armed = false;
    disarm_wait_card_ui();
    set_flow_stage_scroll_hidden(false);
    update_active_page(NULL);
    ui_wifi_prov_force_stop_now();
    lv_obj_add_flag(diagnostic_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(main_page);
    diag_exit_overlay_hide();
    pass_overlay_close();
    set_auto_dim(true);
    flow_set_active(true);
}

static void show_diagnostic(void)
{
    ESP_LOGI(TAG, "show diagnostic");
    s_in_diagnostic = true;
    flow_wait_card_armed = false;
    disarm_wait_card_ui();
    set_flow_stage_scroll_hidden(true);
    flow_set_active(false);
    lv_obj_add_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(diagnostic_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(diagnostic_page);
    lv_obj_set_tile_id(tv, 1, 0, LV_ANIM_OFF);
    diag_exit_overlay_hide();
    update_active_page(t1);
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);

    build_main_flow(scr);
    build_diagnostic(scr);
    test_flow_init();
    test_flow_start();
    last_flow_state = TEST_FLOW_PREP_HOMING;
    current_flow_state = TEST_FLOW_PREP_HOMING;
    keep_awake_until_ms = 0;

    if (!s_light_sleep_lock) {
        esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_sleep", &s_light_sleep_lock);
        // 初始持有锁，阻止运行期间进入休眠
        esp_pm_lock_acquire(s_light_sleep_lock);
        ESP_LOGI(TAG, "PM light sleep lock created and acquired");

        // 配置触摸屏中断引脚唤醒
        esp_sleep_enable_gpio_wakeup();
        gpio_wakeup_enable(BSP_LCD_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
        ESP_LOGI(TAG, "GPIO wakeup enabled on TOUCH_INT (GPIO %d)", BSP_LCD_TOUCH_INT);
    }

    if (!s_sys_worker_queue) {
        s_sys_worker_queue = xQueueCreate(8, sizeof(sys_worker_req_t));
        if (!s_sys_worker_queue) {
            ESP_LOGE(TAG, "sys worker queue create failed");
        } else {
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
            BaseType_t ok = xTaskCreatePinnedToCore(sys_worker_task, "sys_worker", 8192, NULL, 5, NULL, APP_WORKER_TASK_CORE);
#else
            BaseType_t ok = xTaskCreate(sys_worker_task, "sys_worker", 8192, NULL, 5, NULL);
#endif
            if (ok != pdPASS) {
                ESP_LOGE(TAG, "sys worker task create failed");
                vQueueDelete(s_sys_worker_queue);
                s_sys_worker_queue = NULL;
            }
        }
    }

    overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, overlay_event_cb, LV_EVENT_PRESSED, NULL);

    overlay_logo_label = lv_label_create(overlay);
    lv_label_set_text(overlay_logo_label, "KINO");
    lv_obj_set_style_text_font(overlay_logo_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(overlay_logo_label, lv_color_hex(0x536170), 0);
    lv_obj_set_style_text_letter_space(overlay_logo_label, 6, 0);
    lv_obj_set_style_text_opa(overlay_logo_label, LV_OPA_70, 0);
    lv_obj_align(overlay_logo_label, LV_ALIGN_CENTER, 3, -18);
    lv_obj_add_flag(overlay_logo_label, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(inactivity_timer_cb, 500, NULL);
    show_main_flow();
    sys_worker_send_req(SYS_WORKER_WIFI_AUTO_CONNECT);
}
