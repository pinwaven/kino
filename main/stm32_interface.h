#ifndef STM32_INTERFACE_H
#define STM32_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define STM32_ADDR          0x21
#define STM32_RESP_SEP      '|'

#define CMD_POLL            0x01
#define CMD_HI              0x02
#define CMD_GPIO_READ       0x06
#define CMD_I2C_WRITE       0x0A
#define CMD_I2C_READ        0x0B
#define CMD_GPIO_WRITE      0x07
#define CMD_ACTION_CANCEL   0x08
#define CMD_BMSINFO         0x0C
#define CMD_NFC_UUID        0x11
#define CMD_ACTION_HOMING   0x40
#define CMD_ACTION_MOVE_DUR 0x41
#define CMD_ACTION_MOVE_SS  0x42

typedef struct {
    int vbatt_mv;
    int vsys_mv;
    int ichg_ma;
    int vin_mv;
    int iin_ma;
    float tj_c;
    int psys_mw;
    int idchg_ma;
    uint8_t status_raw;
    uint8_t fault_raw;
    bool ac_ok; // status bit 1
} bms_info_extended_t;

typedef struct {
    bms_info_extended_t bms;
    char motor_action[16];
    char motor_status[16];
    bool is_connected;
    uint32_t last_update_tick;
} stm32_state_t;

esp_err_t stm32_interface_init(void);
void stm32_get_current_state(stm32_state_t *out_state);
esp_err_t stm32_update_bmsinfo(void);
esp_err_t stm32_update_motor_poll(void);
esp_err_t stm32_read_card_detect(bool *inserted, int *adc1_value, int *cd_value);
esp_err_t stm32_read_nfc_uuid(char *uuid, uint16_t uuid_len);
esp_err_t stm32_cmd_send_action(uint8_t cmd, const uint8_t *data, uint16_t len);
esp_err_t stm32_cmd_send_action_drain(uint8_t cmd, const uint8_t *data, uint16_t len, uint32_t drain_ms);
esp_err_t stm32_cmd_request(uint8_t cmd, const uint8_t *data, uint16_t len, char *resp, uint16_t resp_len);
esp_err_t stm32_cmd_request_timeout(uint8_t cmd, const uint8_t *data, uint16_t len, char *resp, uint16_t resp_len, uint32_t timeout_ms);

#endif // STM32_INTERFACE_H
