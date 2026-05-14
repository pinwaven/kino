#include "ui_app.h"
#include "wifi_prov.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define TAG "UI_WIFI_PROV"
#define QR_SIZE 200

/* QR code WiFi URI: phone camera scans this and auto-connects to AP */
#define QR_DATA "WIFI:T:WPA;S:" WIFI_PROV_AP_SSID ";P:" WIFI_PROV_AP_PASS ";;"

static lv_obj_t *qr_cont;      /* white-bg container so QR has quiet zone */
static lv_obj_t *qr_obj;
static lv_obj_t *ssid_label;
static lv_obj_t *status_label;
static lv_timer_t *update_timer;

static wifi_prov_state_t last_state = WIFI_PROV_STATE_IDLE - 1; /* force first draw */
static bool page_active = false;

/* ---- prov start/stop tasks (must not block LVGL task) ------------------- */

static void prov_start_task(void *arg)
{
    esp_err_t err = wifi_prov_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prov start failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

static void prov_stop_task(void *arg)
{
    wifi_prov_stop();
    vTaskDelete(NULL);
}

/* ---- UI update ---------------------------------------------------------- */

static void show_qr(bool visible)
{
    if (visible) {
        lv_obj_remove_flag(qr_cont,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ssid_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(qr_cont,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ssid_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_prov_status_t st;
    wifi_prov_get_status(&st);

    if (st.state == last_state) return;
    last_state = st.state;

    /* reset text colour first */
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xCCCCCC), 0);

    switch (st.state) {
    case WIFI_PROV_STATE_IDLE:
        show_qr(false);
        lv_label_set_text(status_label, "Starting hotspot...");
        break;

    case WIFI_PROV_STATE_AP_ACTIVE:
        show_qr(true);
        lv_label_set_text(status_label, "Scan QR to provision WiFi");
        break;

    case WIFI_PROV_STATE_CONNECTING:
        show_qr(false);
        lv_label_set_text_fmt(status_label, "Connecting to %s...", st.target_ssid);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF9800), 0);
        break;

    case WIFI_PROV_STATE_CONNECTED:
        show_qr(false);
        lv_label_set_text_fmt(status_label, "Connected!\nIP: %s", st.got_ip);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x2ECC71), 0);
        break;

    case WIFI_PROV_STATE_FAILED:
        show_qr(true);
        lv_label_set_text(status_label, "Failed. Scan again to retry.");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xE74C3C), 0);
        break;
    }
}

/* ---- public API --------------------------------------------------------- */

void ui_wifi_prov_set_active(bool active)
{
    page_active = active;
    if (!update_timer) return;

    if (active) {
        last_state = (wifi_prov_state_t)(WIFI_PROV_STATE_IDLE - 1); /* force redraw */
        lv_timer_resume(update_timer);
        lv_timer_ready(update_timer);
        xTaskCreate(prov_start_task, "prov_start", 4096, NULL, 5, NULL);
    } else {
        lv_timer_pause(update_timer);
        xTaskCreate(prov_stop_task, "prov_stop", 4096, NULL, 5, NULL);
    }
}

void ui_wifi_prov_init(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x000000), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x2196F3), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    /* QR code container (white background = quiet zone) */
    qr_cont = lv_obj_create(tile);
    lv_obj_set_size(qr_cont, QR_SIZE + 20, QR_SIZE + 20);
    lv_obj_set_style_bg_color(qr_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(qr_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_cont, 0, 0);
    lv_obj_set_style_pad_all(qr_cont, 10, 0);
    lv_obj_set_style_radius(qr_cont, 8, 0);
    lv_obj_clear_flag(qr_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(qr_cont, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(qr_cont, LV_OBJ_FLAG_HIDDEN);

    /* QR code widget inside container */
    qr_obj = lv_qrcode_create(qr_cont);
    lv_qrcode_set_size(qr_obj, QR_SIZE);
    lv_qrcode_set_dark_color(qr_obj, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr_obj, lv_color_hex(0xFFFFFF));
    lv_qrcode_set_quiet_zone(qr_obj, false); /* container is our quiet zone */
    lv_qrcode_set_data(qr_obj, QR_DATA);
    lv_obj_align(qr_obj, LV_ALIGN_CENTER, 0, 0);

    /* SSID / password hint below QR container */
    ssid_label = lv_label_create(tile);
    lv_label_set_text(ssid_label,
        WIFI_PROV_AP_SSID "  /  " WIFI_PROV_AP_PASS);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0x888888), 0);
    lv_obj_align(ssid_label, LV_ALIGN_CENTER, 0, 130);
    lv_obj_add_flag(ssid_label, LV_OBJ_FLAG_HIDDEN);

    /* Status label at bottom */
    status_label = lv_label_create(tile);
    lv_label_set_text(status_label, "...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(status_label, 260);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -28);

    update_timer = lv_timer_create(update_timer_cb, 500, NULL);
    lv_timer_pause(update_timer);
}
