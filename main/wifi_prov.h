#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include "esp_err.h"
#include <stdbool.h>

#define WIFI_PROV_AP_SSID  "Kino-WiFi"
#define WIFI_PROV_AP_PASS  "12345678"
#define WIFI_PROV_AP_IP    "192.168.4.1"

typedef enum {
    WIFI_PROV_STATE_IDLE,
    WIFI_PROV_STATE_AP_ACTIVE,
    WIFI_PROV_STATE_CONNECTING,
    WIFI_PROV_STATE_CONNECTED,
    WIFI_PROV_STATE_FAILED,
} wifi_prov_state_t;

typedef struct {
    wifi_prov_state_t state;
    char target_ssid[33];
    char got_ip[16];
    int8_t rssi;
    bool rssi_valid;
} wifi_prov_status_t;

esp_err_t wifi_prov_start(void);
esp_err_t wifi_prov_stop(void);
esp_err_t wifi_prov_auto_connect_saved(void);
esp_err_t wifi_prov_pause_radio(void);
esp_err_t wifi_prov_resume_radio(void);
void wifi_prov_get_status(wifi_prov_status_t *out);

#endif // WIFI_PROV_H
