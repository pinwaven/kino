#ifndef TEST_FLOW_H
#define TEST_FLOW_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    TEST_FLOW_PREP_HOMING = 0,
    TEST_FLOW_PREP_OPENING,
    TEST_FLOW_WAIT_CARD,
    TEST_FLOW_CARD_DETECTED,
    TEST_FLOW_CLOSING,
    TEST_FLOW_READING_NFC,
    TEST_FLOW_NFC_READY,
    TEST_FLOW_ANALYZING_CARD,
    TEST_FLOW_GETTING_CHIP,
    TEST_FLOW_MOCK_TESTING,
    TEST_FLOW_POSTING_BIOMARKERS,
    TEST_FLOW_UPLOAD_REVIEW,
    TEST_FLOW_DONE,
    TEST_FLOW_SUCCESS_EJECTING,
    TEST_FLOW_WAIT_REMOVE_CARD,
    TEST_FLOW_MOTOR_ERROR,
    TEST_FLOW_NFC_ERROR,
    TEST_FLOW_API_ERROR,
    TEST_FLOW_CARD_DETECT_ERROR,
    TEST_FLOW_RECOVERY_CLOSING,
    TEST_FLOW_RECOVERY_OPENING,
} test_flow_state_t;

typedef struct {
    test_flow_state_t state;
    bool card_inserted;
    int adc1_value;
    int cd_value;
    char nfc_code[128];
    char last_error_message[128];
    char upload_summary[512];
    esp_err_t last_error;
} test_flow_snapshot_t;

bool test_flow_is_motor_mock(void);
void test_flow_reload_settings(void);
void test_flow_set_motor_mock(bool mock);

void test_flow_init(void);
void test_flow_start(void);
void test_flow_update(void);
bool test_flow_retry_after_error(void);
bool test_flow_continue_after_upload_review(void);
void test_flow_get_snapshot(test_flow_snapshot_t *out);
const char *test_flow_status_text(test_flow_state_t state);
const char *test_flow_hint_text(const test_flow_snapshot_t *snapshot);

#endif
