#ifndef BLE_CONTROL_H
#define BLE_CONTROL_H

#include <stdbool.h>
#include "esp_err.h"

#define BLE_CONTROL_DEVICE_NAME "KINO_CTRL"

#ifndef BLE_CONTROL_ENABLED
#define BLE_CONTROL_ENABLED 0
#endif

typedef struct {
    bool initialized;
    bool active;
    bool advertising;
    bool connected;
    bool notify_enabled;
    bool phy_2m;
    char last_command[32];
    char last_result[96];
    char phy_status[16];
} ble_control_state_t;

esp_err_t ble_control_set_active(bool active);
void ble_control_get_state(ble_control_state_t *out_state);
void ble_control_log_heap(const char *tag);
bool ble_control_is_enabled(void);

#endif // BLE_CONTROL_H
