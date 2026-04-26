#include "WiFi.h"
#include <WiFi.h>

static wifi_hal_status_t current_status = WIFI_STATUS_DISCONNECTED;

void hal_wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.println("WiFi HAL Initialized");
}

void hal_wifi_connect(const char* ssid, const char* password) {
    if (!ssid || strlen(ssid) == 0) {
        Serial.println("WiFi Error: Empty SSID");
        current_status = WIFI_STATUS_ERROR;
        return;
    }

    Serial.print("WiFi: Connecting to ");
    Serial.println(ssid);
    
    current_status = WIFI_STATUS_CONNECTING;
    WiFi.begin(ssid, password);
}

wifi_hal_status_t hal_wifi_get_status() {
    wl_status_t status = WiFi.status();
    
    switch (status) {
        case WL_CONNECTED:
            current_status = WIFI_STATUS_CONNECTED;
            break;
        case WL_IDLE_STATUS:
        case WL_DISCONNECTED:
            current_status = WIFI_STATUS_DISCONNECTED;
            break;
        case WL_CONNECT_FAILED:
        case WL_CONNECTION_LOST:
            current_status = WIFI_STATUS_ERROR;
            break;
        default:
            current_status = WIFI_STATUS_CONNECTING;
            break;
    }
    
    return current_status;
}

String hal_wifi_get_ip() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}
