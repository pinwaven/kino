#include "stm32_interface.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "crc.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>

#define UART_PORT_NUM      UART_NUM_1
#define UART_BAUD_RATE     230400
#define UART_TX_PIN        GPIO_NUM_43
#define UART_RX_PIN        GPIO_NUM_44
#define BUF_SIZE           1024
#define CARD_DETECT_INSERTED_LEVEL 1

static stm32_state_t g_state;
static SemaphoreHandle_t g_mutex;
static SemaphoreHandle_t g_uart_mutex;

// 内部函数：发送原始命令
static esp_err_t send_raw(uint8_t cmd, const uint8_t *data, uint16_t len) {
    uint8_t *full_frame = malloc(len + 3);
    if (!full_frame) return ESP_ERR_NO_MEM;
    full_frame[0] = STM32_ADDR;
    full_frame[1] = cmd;
    if (len > 0 && data) memcpy(&full_frame[2], data, len);
    full_frame[len + 2] = cal_crc8(full_frame, len + 2);
    int written = uart_write_bytes(UART_PORT_NUM, (const char *)full_frame, len + 3);
    free(full_frame);
    return (written == len + 3) ? ESP_OK : ESP_FAIL;
}

// 内部函数：读取响应
static esp_err_t read_resp(char *buffer, uint16_t max_len, uint32_t timeout_ms) {
    uint8_t temp[128];
    int len = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (len < sizeof(temp)) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) break;

        TickType_t wait_ticks = len == 0 ? (deadline - now) : pdMS_TO_TICKS(20);
        int read_len = uart_read_bytes(UART_PORT_NUM, &temp[len], 1, wait_ticks);
        if (read_len == 1) {
            len++;
            continue;
        }

        if (len > 0) break;
    }

    if (len < 3) return ESP_ERR_TIMEOUT;
    if (temp[0] != STM32_ADDR || temp[1] != STM32_RESP_SEP) return ESP_ERR_INVALID_RESPONSE;
    if (temp[len-1] != cal_crc8(temp, len - 1)) return ESP_ERR_INVALID_CRC;
    
    int payload_len = len - 3;
    if (payload_len >= max_len) payload_len = max_len - 1;
    memcpy(buffer, &temp[2], payload_len);
    buffer[payload_len] = '\0';
    return ESP_OK;
}

static esp_err_t request_raw(uint8_t cmd, const uint8_t *data, uint16_t len, char *resp, uint16_t resp_len, uint32_t timeout_ms) {
    esp_err_t err;

    if (g_uart_mutex && xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(timeout_ms + 200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uart_flush_input(UART_PORT_NUM);
    err = send_raw(cmd, data, len);
    if (err == ESP_OK && resp && resp_len > 0) {
        err = read_resp(resp, resp_len, timeout_ms);
    }

    if (g_uart_mutex) {
        xSemaphoreGive(g_uart_mutex);
    }

    return err;
}

static void drain_resp(uint32_t drain_ms) {
    uint8_t temp[64];
    bool got_data = false;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(drain_ms);

    while (xTaskGetTickCount() < deadline) {
        TickType_t wait_ticks = got_data ? pdMS_TO_TICKS(20) : (deadline - xTaskGetTickCount());
        int read_len = uart_read_bytes(UART_PORT_NUM, temp, sizeof(temp), wait_ticks);
        if (read_len > 0) {
            got_data = true;
            continue;
        }

        if (got_data) {
            break;
        }
    }

    uart_flush_input(UART_PORT_NUM);
}

static void parse_bms_response(const char *buf) {
    if (strncmp(buf, "bms:", 4) != 0) return;

    int v_tj_c_raw;
    int v_status, v_fault;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_state.is_connected = true;
    g_state.last_update_tick = xTaskGetTickCount();

    int n = sscanf(buf + 4, "%d,%d,%d,%d,%d,%d,%d,%d,0x%x,0x%x",
                   &g_state.bms.vbatt_mv, &g_state.bms.vsys_mv,
                   &g_state.bms.ichg_ma, &g_state.bms.vin_mv,
                   &g_state.bms.iin_ma, &v_tj_c_raw,
                   &g_state.bms.psys_mw, &g_state.bms.idchg_ma,
                   &v_status, &v_fault);
    if (n >= 10) {
        g_state.bms.tj_c = v_tj_c_raw / 10.0f;
        g_state.bms.status_raw = (uint8_t)v_status;
        g_state.bms.fault_raw = (uint8_t)v_fault;
        g_state.bms.ac_ok = (g_state.bms.status_raw & 0x02) != 0;
    }

    xSemaphoreGive(g_mutex);
}

static void parse_motor_poll_response(const char *buf) {
    char *p = strchr(buf, ':');
    if (!p) return;

    char *comma = strchr(p + 1, ',');
    if (!comma) return;

    int action_len = comma - (p + 1);
    if (action_len > 15) action_len = 15;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_state.is_connected = true;
    g_state.last_update_tick = xTaskGetTickCount();

    strncpy(g_state.motor_action, p + 1, action_len);
    g_state.motor_action[action_len] = '\0';

    strncpy(g_state.motor_status, comma + 1, 15);
    g_state.motor_status[15] = '\0';

    xSemaphoreGive(g_mutex);
}

esp_err_t stm32_interface_init(void) {
    g_mutex = xSemaphoreCreateMutex();
    g_uart_mutex = xSemaphoreCreateMutex();
    memset(&g_state, 0, sizeof(g_state));

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_XTAL,
    };
    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    return ESP_OK;
}

void stm32_get_current_state(stm32_state_t *out_state) {
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(out_state, &g_state, sizeof(stm32_state_t));
        xSemaphoreGive(g_mutex);
    }
}

esp_err_t stm32_update_bmsinfo(void) {
    char buf[128];
    esp_err_t err = request_raw(CMD_BMSINFO, NULL, 0, buf, sizeof(buf), 150);
    if (err == ESP_OK) {
        parse_bms_response(buf);
    }
    return err;
}

esp_err_t stm32_update_motor_poll(void) {
    char buf[128];
    esp_err_t err = request_raw(CMD_POLL, NULL, 0, buf, sizeof(buf), 150);
    if (err == ESP_OK) {
        parse_motor_poll_response(buf);
    }
    return err;
}

esp_err_t stm32_read_card_detect(bool *inserted, int *adc1_value, int *cd_value) {
    char buf[64];
    int adc1 = -1;
    int cd = -1;
    esp_err_t err = request_raw(CMD_GPIO_READ, NULL, 0, buf, sizeof(buf), 150);
    if (err != ESP_OK) {
        return err;
    }

    char *adc_pos = strstr(buf, "adc1:");
    char *cd_pos = strstr(buf, "cd:");
    if (!cd_pos) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (adc_pos) {
        sscanf(adc_pos, "adc1:%d", &adc1);
    }
    if (sscanf(cd_pos, "cd:%d", &cd) != 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (inserted) *inserted = (cd == CARD_DETECT_INSERTED_LEVEL);
    if (adc1_value) *adc1_value = adc1;
    if (cd_value) *cd_value = cd;
    return ESP_OK;
}

esp_err_t stm32_read_nfc_uuid(char *uuid, uint16_t uuid_len) {
    char buf[96];
    esp_err_t err = request_raw(CMD_NFC_UUID, NULL, 0, buf, sizeof(buf), 1200);
    if (err != ESP_OK) {
        return err;
    }

    if (strncmp(buf, "nfc_uuid:", 9) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *payload = buf + 9;
    if (strncmp(payload, "err:", 4) == 0) {
        return ESP_FAIL;
    }

    if (uuid && uuid_len > 0) {
        strncpy(uuid, payload, uuid_len - 1);
        uuid[uuid_len - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t stm32_cmd_send_action(uint8_t cmd, const uint8_t *data, uint16_t len) {
    esp_err_t err;

    if (g_uart_mutex && xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uart_flush_input(UART_PORT_NUM);
    err = send_raw(cmd, data, len);

    if (g_uart_mutex) {
        xSemaphoreGive(g_uart_mutex);
    }

    return err;
}

esp_err_t stm32_cmd_send_action_drain(uint8_t cmd, const uint8_t *data, uint16_t len, uint32_t drain_ms) {
    esp_err_t err;

    if (g_uart_mutex && xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(drain_ms + 200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uart_flush_input(UART_PORT_NUM);
    err = send_raw(cmd, data, len);
    if (err == ESP_OK && drain_ms > 0) {
        drain_resp(drain_ms);
    }

    if (g_uart_mutex) {
        xSemaphoreGive(g_uart_mutex);
    }

    return err;
}

esp_err_t stm32_cmd_request(uint8_t cmd, const uint8_t *data, uint16_t len, char *resp, uint16_t resp_len) {
    return request_raw(cmd, data, len, resp, resp_len, 100);
}

esp_err_t stm32_cmd_request_timeout(uint8_t cmd, const uint8_t *data, uint16_t len, char *resp, uint16_t resp_len, uint32_t timeout_ms) {
    return request_raw(cmd, data, len, resp, resp_len, timeout_ms);
}
