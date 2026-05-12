#include "ble_control.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "stm32_interface.h"

#define BLE_CMD_QUEUE_LEN 4
#define BLE_CMD_MAX_LEN 31
#define MOTOR_ACTION_DRAIN_MS 180

static const char *TAG = "BLE_CONTROL";
static const ble_uuid16_t g_service_uuid = BLE_UUID16_INIT(0xFFE0);
static const ble_uuid16_t g_cmd_uuid = BLE_UUID16_INIT(0xFFE1);
static const ble_uuid16_t g_status_uuid = BLE_UUID16_INIT(0xFFE2);

typedef struct {
    char text[BLE_CMD_MAX_LEN + 1];
} ble_cmd_msg_t;

static SemaphoreHandle_t g_state_mutex;
static QueueHandle_t g_cmd_queue;
static bool g_host_synced;
static uint8_t g_own_addr_type;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_status_val_handle;
static ble_control_state_t g_state = {
    .last_result = "BLE: off",
};

static int gap_event_cb(struct ble_gap_event *event, void *arg);
static int status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);
static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static void start_advertising(void);
static void str_to_lower_trim(char *s);

static const struct ble_gatt_svc_def g_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &g_cmd_uuid.u,
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &g_status_uuid.u,
                .access_cb = status_access_cb,
                .val_handle = &g_status_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {0}
        },
    },
    {0}
};

static void set_state_result(const char *cmd, const char *result) {
    if (g_state_mutex && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (cmd) {
            strlcpy(g_state.last_command, cmd, sizeof(g_state.last_command));
        }
        if (result) {
            strlcpy(g_state.last_result, result, sizeof(g_state.last_result));
        }
        xSemaphoreGive(g_state_mutex);
    }
}

static void set_flag_state(bool active, bool advertising, bool connected, bool notify_enabled) {
    if (g_state_mutex && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_state.active = active;
        g_state.advertising = advertising;
        g_state.connected = connected;
        g_state.notify_enabled = notify_enabled;
        xSemaphoreGive(g_state_mutex);
    }
}

static bool get_active(void) {
    bool active = false;
    if (g_state_mutex && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        active = g_state.active;
        xSemaphoreGive(g_state_mutex);
    }
    return active;
}

static bool get_notify_enabled(void) {
    bool notify_enabled = false;
    if (g_state_mutex && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        notify_enabled = g_state.notify_enabled;
        xSemaphoreGive(g_state_mutex);
    }
    return notify_enabled;
}

static bool get_initialized(void) {
    bool initialized = false;
    if (g_state_mutex && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        initialized = g_state.initialized;
        xSemaphoreGive(g_state_mutex);
    }
    return initialized;
}

static void build_status(char *out, size_t out_len) {
    ble_control_state_t state;
    stm32_state_t stm32;

    ble_control_get_state(&state);
    stm32_get_current_state(&stm32);
    snprintf(out, out_len,
             "BLE:%s%s\nCMD:%s\n%s\nACT:%s\nSTS:%s",
             state.connected ? "connected" : (state.advertising ? "advertising" : "off"),
             state.active ? "" : " inactive",
             state.last_command[0] ? state.last_command : "--",
             state.last_result[0] ? state.last_result : "--",
             stm32.motor_action[0] ? stm32.motor_action : "--",
             stm32.motor_status[0] ? stm32.motor_status : "--");
}

static void normalize_command(char *cmd) {
    str_to_lower_trim(cmd);

    if (strcmp(cmd, "1") == 0 || strcmp(cmd, "01") == 0 || strcmp(cmd, "0x01") == 0) {
        strlcpy(cmd, "open", BLE_CMD_MAX_LEN + 1);
    } else if (strcmp(cmd, "2") == 0 || strcmp(cmd, "02") == 0 || strcmp(cmd, "0x02") == 0) {
        strlcpy(cmd, "close", BLE_CMD_MAX_LEN + 1);
    } else if (strcmp(cmd, "3") == 0 || strcmp(cmd, "03") == 0 || strcmp(cmd, "0x03") == 0) {
        strlcpy(cmd, "homing", BLE_CMD_MAX_LEN + 1);
    } else if (strcmp(cmd, "4") == 0 || strcmp(cmd, "04") == 0 || strcmp(cmd, "0x04") == 0) {
        strlcpy(cmd, "stop", BLE_CMD_MAX_LEN + 1);
    } else if (strcmp(cmd, "5") == 0 || strcmp(cmd, "05") == 0 || strcmp(cmd, "0x05") == 0) {
        strlcpy(cmd, "nfc", BLE_CMD_MAX_LEN + 1);
    } else if (strcmp(cmd, "6") == 0 || strcmp(cmd, "06") == 0 || strcmp(cmd, "0x06") == 0) {
        strlcpy(cmd, "status", BLE_CMD_MAX_LEN + 1);
    }
}

static void notify_status(void) {
    char status[192];
    ble_control_state_t state;

    ble_control_get_state(&state);
    if (!state.notify_enabled || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    build_status(status, sizeof(status));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(status, strlen(status));
    if (!om) {
        return;
    }
    ble_gatts_notify_custom(g_conn_handle, g_status_val_handle, om);
}

static void str_to_lower_trim(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    for (char *p = s; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

static esp_err_t send_motor_command(const char *cmd) {
    if (strcmp(cmd, "open") == 0) {
        uint8_t payload[8];
        int32_t speed = 15000;
        uint32_t duration = 3250;
        memcpy(&payload[0], &speed, 4);
        memcpy(&payload[4], &duration, 4);
        return stm32_cmd_send_action_drain(CMD_ACTION_MOVE_DUR, payload, sizeof(payload), MOTOR_ACTION_DRAIN_MS);
    }
    if (strcmp(cmd, "close") == 0) {
        uint8_t payload[9];
        int32_t speed = -12000;
        uint32_t timeout = 8000;
        memcpy(&payload[0], &speed, 4);
        memcpy(&payload[4], &timeout, 4);
        payload[8] = 0x00;
        return stm32_cmd_send_action_drain(CMD_ACTION_MOVE_SS, payload, sizeof(payload), MOTOR_ACTION_DRAIN_MS);
    }
    if (strcmp(cmd, "homing") == 0 || strcmp(cmd, "home") == 0) {
        uint32_t timeout = 30000;
        return stm32_cmd_send_action_drain(CMD_ACTION_HOMING, (uint8_t *)&timeout, sizeof(timeout), MOTOR_ACTION_DRAIN_MS);
    }
    if (strcmp(cmd, "stop") == 0) {
        return stm32_cmd_send_action_drain(CMD_ACTION_CANCEL, NULL, 0, MOTOR_ACTION_DRAIN_MS);
    }

    return ESP_ERR_NOT_SUPPORTED;
}

static void cmd_worker_task(void *arg) {
    (void)arg;
    ble_cmd_msg_t msg;

    while (true) {
        if (xQueueReceive(g_cmd_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char cmd[BLE_CMD_MAX_LEN + 1];
        strlcpy(cmd, msg.text, sizeof(cmd));
        normalize_command(cmd);

        ESP_LOGI(TAG, "command: %s", cmd);

        if (!get_active()) {
            set_state_result(cmd, "ERR: BLE page inactive");
            notify_status();
            continue;
        }

        if (strcmp(cmd, "help") == 0) {
            set_state_result(cmd, "CMDS: open close homing stop nfc status; hex 01-06");
            notify_status();
            continue;
        }

        if (strcmp(cmd, "status") == 0) {
            set_state_result(cmd, "OK: status");
            notify_status();
            continue;
        }

        if (strcmp(cmd, "nfc") == 0) {
            char resp[96];
            esp_err_t err = stm32_cmd_request_timeout(CMD_NFC_UUID, NULL, 0, resp, sizeof(resp), 1200);
            if (err == ESP_OK) {
                set_state_result(cmd, resp);
            } else {
                char err_text[48];
                snprintf(err_text, sizeof(err_text), "ERR: %s", esp_err_to_name(err));
                set_state_result(cmd, err_text);
            }
            notify_status();
            continue;
        }

        esp_err_t err = send_motor_command(cmd);
        if (err == ESP_OK) {
            set_state_result(cmd, "OK");
            ESP_LOGI(TAG, "command %s done", cmd);
        } else if (err == ESP_ERR_NOT_SUPPORTED) {
            set_state_result(cmd, "ERR: unknown command");
            ESP_LOGW(TAG, "unknown command: %s", cmd);
        } else {
            char err_text[48];
            snprintf(err_text, sizeof(err_text), "ERR: %s", esp_err_to_name(err));
            set_state_result(cmd, err_text);
            ESP_LOGW(TAG, "command %s failed: %s", cmd, esp_err_to_name(err));
        }
        notify_status();
    }
}

static void host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE address infer failed: %d", rc);
        set_state_result(NULL, "BLE: address error");
        return;
    }

    g_host_synced = true;
    if (get_active()) {
        start_advertising();
    }
}

static int status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    char status[192];
    build_status(status, sizeof(status));
    return os_mbuf_append(ctxt->om, status, strlen(status)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    ble_cmd_msg_t msg = {0};
    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, msg.text, BLE_CMD_MAX_LEN, &copied);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    msg.text[copied] = '\0';

    if (copied == 1 && (uint8_t)msg.text[0] >= 1 && (uint8_t)msg.text[0] <= 6) {
        snprintf(msg.text, sizeof(msg.text), "%u", (unsigned)(uint8_t)msg.text[0]);
    }

    ESP_LOGI(TAG, "write FFE1: %s", msg.text[0] ? msg.text : "<empty>");
    set_state_result(msg.text[0] ? msg.text : "--", "RX: queued");

    if (xQueueSend(g_cmd_queue, &msg, 0) != pdTRUE) {
        set_state_result(msg.text[0] ? msg.text : "--", "ERR: command queue full");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    notify_status();
    return 0;
}

static void start_advertising(void) {
    struct ble_gap_adv_params params = {0};
    struct ble_hs_adv_fields fields = {0};
    int rc;

    if (!g_host_synced || !get_active()) {
        return;
    }

    ble_gap_adv_stop();

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (const uint8_t *)BLE_CONTROL_DEVICE_NAME;
    fields.name_len = strlen(BLE_CONTROL_DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields failed: %d", rc);
        set_state_result(NULL, "BLE: adv fields failed");
        return;
    }

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(250);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(260);

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc == 0) {
        set_flag_state(true, true, false, get_notify_enabled());
        set_state_result(NULL, "BLE: advertising");
        ESP_LOGI(TAG, "advertising started as %s", BLE_CONTROL_DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
        set_state_result(NULL, "BLE: adv start failed");
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            set_flag_state(true, false, true, get_notify_enabled());
            set_state_result(NULL, "BLE: connected");
            ESP_LOGI(TAG, "connected");
            notify_status();
        } else if (get_active()) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        set_flag_state(get_active(), false, false, false);
        set_state_result(NULL, "BLE: disconnected");
        ESP_LOGI(TAG, "disconnected");
        if (get_active()) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        set_flag_state(get_active(), false, false, get_notify_enabled());
        if (get_active()) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == g_status_val_handle) {
            bool notify = event->subscribe.cur_notify != 0;
            ble_control_state_t state;
            ble_control_get_state(&state);
            set_flag_state(state.active, state.advertising, state.connected, notify);
            ESP_LOGI(TAG, "notify %s", notify ? "enabled" : "disabled");
            if (notify) {
                notify_status();
            }
        }
        break;

    default:
        break;
    }

    return 0;
}

static esp_err_t ble_control_init_once(void) {
    if (get_initialized()) {
        return ESP_OK;
    }

    if (!g_state_mutex) {
        g_state_mutex = xSemaphoreCreateMutex();
        if (!g_state_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!g_cmd_queue) {
        g_cmd_queue = xQueueCreate(BLE_CMD_QUEUE_LEN, sizeof(ble_cmd_msg_t));
        if (!g_cmd_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(BLE_CONTROL_DEVICE_NAME);

    int rc = ble_gatts_count_cfg(g_gatt_svcs);
    if (rc != 0) {
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(g_gatt_svcs);
    if (rc != 0) {
        return ESP_FAIL;
    }

    BaseType_t task_ok = xTaskCreate(cmd_worker_task, "ble_cmd_worker", 4096, NULL, 5, NULL);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    nimble_port_freertos_init(host_task);

    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_state.initialized = true;
        xSemaphoreGive(g_state_mutex);
    }
    set_state_result(NULL, "BLE: ready");
    return ESP_OK;
}

esp_err_t ble_control_set_active(bool active) {
    if (!active) {
        if (!get_initialized()) {
            set_flag_state(false, false, false, false);
            set_state_result(NULL, "BLE: off");
            return ESP_OK;
        }

        if (g_host_synced) {
            ble_gap_adv_stop();
            if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            }
        }
        set_flag_state(false, false, false, false);
        set_state_result(NULL, "BLE: off");
        return ESP_OK;
    }

    esp_err_t err = ble_control_init_once();
    if (err != ESP_OK) {
        set_state_result(NULL, "BLE: init failed");
        return err;
    }

    ble_control_state_t state;
    ble_control_get_state(&state);
    set_flag_state(true, state.advertising, state.connected, state.notify_enabled);
    if (g_host_synced && g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        start_advertising();
    }
    return ESP_OK;
}

void ble_control_get_state(ble_control_state_t *out_state) {
    if (!out_state) {
        return;
    }

    memset(out_state, 0, sizeof(*out_state));
    if (g_state_mutex && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(out_state, &g_state, sizeof(*out_state));
        xSemaphoreGive(g_state_mutex);
    } else {
        strlcpy(out_state->last_result, "BLE: unavailable", sizeof(out_state->last_result));
    }
}
