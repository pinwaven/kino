#include "test_flow.h"
#include "poct_api.h"
#include "stm32_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define MOCK_MOTOR_ACTION_MS 2500
#define MOCK_ANALYZE_MS 4000

static test_flow_snapshot_t s_flow;
static char s_hint[96];
static TickType_t s_state_started;

static bool elapsed_ms(uint32_t ms)
{
    return (xTaskGetTickCount() - s_state_started) >= pdMS_TO_TICKS(ms);
}

static void set_state(test_flow_state_t state)
{
    if (s_flow.state == state) return;
    s_flow.state = state;
    s_state_started = xTaskGetTickCount();
}

static void start_recovery(void)
{
    set_state(TEST_FLOW_RECOVERY_CLOSING);
}

static void reset_for_wait_card(void)
{
    s_flow.card_inserted = false;
    s_flow.adc1_value = -1;
    s_flow.cd_value = -1;
    s_flow.nfc_uuid[0] = '\0';
    s_flow.last_error = ESP_OK;
    set_state(TEST_FLOW_WAIT_CARD);
}

void test_flow_init(void)
{
    memset(&s_flow, 0, sizeof(s_flow));
    s_flow.state = TEST_FLOW_PREP_HOMING;
    s_state_started = xTaskGetTickCount();
    s_flow.adc1_value = -1;
    s_flow.cd_value = -1;
    s_flow.last_error = ESP_OK;
}

void test_flow_update(void)
{
    if (s_flow.state == TEST_FLOW_PREP_HOMING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            set_state(TEST_FLOW_PREP_OPENING);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_PREP_OPENING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            set_state(TEST_FLOW_WAIT_CARD);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_CLOSING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            set_state(TEST_FLOW_READING_NFC);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_READING_NFC) {
        s_flow.last_error = stm32_read_nfc_uuid(s_flow.nfc_uuid, sizeof(s_flow.nfc_uuid));
        if (s_flow.last_error == ESP_OK) {
            set_state(TEST_FLOW_NFC_READY);
        } else {
            set_state(TEST_FLOW_NFC_ERROR);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_NFC_READY) {
        set_state(TEST_FLOW_ANALYZING_CARD);
        return;
    }

    if (s_flow.state == TEST_FLOW_ANALYZING_CARD) {
        if (elapsed_ms(MOCK_ANALYZE_MS)) {
            set_state(TEST_FLOW_VERIFYING_CARD);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_VERIFYING_CARD) {
        s_flow.last_error = poct_api_verify_card(s_flow.nfc_uuid);
        set_state(s_flow.last_error == ESP_OK ? TEST_FLOW_MOCK_TESTING : TEST_FLOW_API_ERROR);
        return;
    }

    if (s_flow.state == TEST_FLOW_MOCK_TESTING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            set_state(TEST_FLOW_UPLOADING_RESULT);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_UPLOADING_RESULT) {
        s_flow.last_error = poct_api_upload_mock_result(s_flow.nfc_uuid);
        set_state(s_flow.last_error == ESP_OK ? TEST_FLOW_DONE : TEST_FLOW_API_ERROR);
        return;
    }

    if (s_flow.state == TEST_FLOW_DONE) {
        if (elapsed_ms(1500)) {
            set_state(TEST_FLOW_SUCCESS_EJECTING);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_SUCCESS_EJECTING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            reset_for_wait_card();
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_NFC_ERROR ||
        s_flow.state == TEST_FLOW_API_ERROR ||
        s_flow.state == TEST_FLOW_CARD_DETECT_ERROR) {
        start_recovery();
        return;
    }

    if (s_flow.state == TEST_FLOW_RECOVERY_CLOSING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            set_state(TEST_FLOW_RECOVERY_OPENING);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_RECOVERY_OPENING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            reset_for_wait_card();
        }
        return;
    }

    bool inserted = false;
    int adc1 = -1;
    int cd = -1;
    esp_err_t err = stm32_read_card_detect(&inserted, &adc1, &cd);

    s_flow.last_error = err;
    if (err != ESP_OK) {
        set_state(TEST_FLOW_CARD_DETECT_ERROR);
        s_flow.card_inserted = false;
        return;
    }

    s_flow.card_inserted = inserted;
    s_flow.adc1_value = adc1;
    s_flow.cd_value = cd;
    if (inserted) {
        set_state(s_flow.state == TEST_FLOW_CARD_DETECTED ? TEST_FLOW_CLOSING : TEST_FLOW_CARD_DETECTED);
    } else {
        set_state(TEST_FLOW_WAIT_CARD);
    }
}

void test_flow_get_snapshot(test_flow_snapshot_t *out)
{
    if (!out) return;
    memcpy(out, &s_flow, sizeof(s_flow));
}

const char *test_flow_status_text(test_flow_state_t state)
{
    switch (state) {
    case TEST_FLOW_PREP_HOMING:
        return "Preparing";
    case TEST_FLOW_PREP_OPENING:
        return "Opening tray";
    case TEST_FLOW_CARD_DETECTED:
        return "Card detected";
    case TEST_FLOW_CLOSING:
        return "Closing tray";
    case TEST_FLOW_READING_NFC:
        return "Reading NFC";
    case TEST_FLOW_NFC_READY:
        return "NFC ready";
    case TEST_FLOW_ANALYZING_CARD:
        return "Analyzing";
    case TEST_FLOW_VERIFYING_CARD:
        return "Verifying";
    case TEST_FLOW_MOCK_TESTING:
        return "Testing";
    case TEST_FLOW_UPLOADING_RESULT:
        return "Uploading";
    case TEST_FLOW_DONE:
        return "Done";
    case TEST_FLOW_SUCCESS_EJECTING:
        return "Ejecting card";
    case TEST_FLOW_NFC_ERROR:
        return "NFC error";
    case TEST_FLOW_API_ERROR:
        return "API error";
    case TEST_FLOW_CARD_DETECT_ERROR:
        return "Detect error";
    case TEST_FLOW_RECOVERY_CLOSING:
        return "Recovering";
    case TEST_FLOW_RECOVERY_OPENING:
        return "Ejecting card";
    case TEST_FLOW_WAIT_CARD:
    default:
        return "Insert card";
    }
}

const char *test_flow_hint_text(const test_flow_snapshot_t *snapshot)
{
    if (!snapshot) return "";

    if (snapshot->state == TEST_FLOW_PREP_HOMING) {
        return "Homing tray (mock 2.5s)";
    }

    if (snapshot->state == TEST_FLOW_PREP_OPENING) {
        return "Opening tray (mock 2.5s)";
    }

    if (snapshot->state == TEST_FLOW_CARD_DETECT_ERROR) {
        snprintf(s_hint, sizeof(s_hint), "CardDetect failed: %s", esp_err_to_name(snapshot->last_error));
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_RECOVERY_CLOSING) {
        return "Closing tray before eject (mock 2.5s)";
    }

    if (snapshot->state == TEST_FLOW_RECOVERY_OPENING) {
        return "Opening tray, remove card (mock 2.5s)";
    }

    if (snapshot->state == TEST_FLOW_CARD_DETECTED) {
        snprintf(s_hint, sizeof(s_hint), "CardDetect cd:%d adc1:%d", snapshot->cd_value, snapshot->adc1_value);
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_CLOSING) {
        return "Closing tray (mock 2.5s)";
    }

    if (snapshot->state == TEST_FLOW_READING_NFC) {
        return "Reading card UUID";
    }

    if (snapshot->state == TEST_FLOW_NFC_READY) {
        snprintf(s_hint, sizeof(s_hint), "UUID: %s", snapshot->nfc_uuid);
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_ANALYZING_CARD) {
        return "Analyzing card data (mock 4s)";
    }

    if (snapshot->state == TEST_FLOW_VERIFYING_CARD) {
        return "Calling real API: verify card";
    }

    if (snapshot->state == TEST_FLOW_MOCK_TESTING) {
        return "Mock testing in progress";
    }

    if (snapshot->state == TEST_FLOW_UPLOADING_RESULT) {
        return "Uploading mock result to real API";
    }

    if (snapshot->state == TEST_FLOW_DONE) {
        return "Mock result uploaded";
    }

    if (snapshot->state == TEST_FLOW_SUCCESS_EJECTING) {
        return "Opening tray, remove card (mock 2.5s)";
    }

    if (snapshot->state == TEST_FLOW_NFC_ERROR) {
        snprintf(s_hint, sizeof(s_hint), "NFC failed: %s", esp_err_to_name(snapshot->last_error));
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_API_ERROR) {
        snprintf(s_hint, sizeof(s_hint), "API failed: %s", esp_err_to_name(snapshot->last_error));
        return s_hint;
    }

    snprintf(s_hint, sizeof(s_hint), "Waiting... cd:%d adc1:%d", snapshot->cd_value, snapshot->adc1_value);
    return s_hint;
}
