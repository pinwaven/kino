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
    TEST_FLOW_VERIFYING_CARD,
    TEST_FLOW_MOCK_TESTING,
    TEST_FLOW_UPLOADING_RESULT,
    TEST_FLOW_DONE,
    TEST_FLOW_SUCCESS_EJECTING,
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
    char nfc_uuid[64];
    esp_err_t last_error;
} test_flow_snapshot_t;

void test_flow_init(void);
void test_flow_update(void);
void test_flow_get_snapshot(test_flow_snapshot_t *out);
const char *test_flow_status_text(test_flow_state_t state);
const char *test_flow_hint_text(const test_flow_snapshot_t *snapshot);

#endif
