#pragma once
#include <Arduino.h>

typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_ERROR
} wifi_hal_status_t;

#ifdef __cplusplus
extern "C" {
#endif

void hal_wifi_init();
void hal_wifi_connect(const char* ssid, const char* password);
wifi_hal_status_t hal_wifi_get_status();
String hal_wifi_get_ip();

#ifdef __cplusplus
}
#endif
