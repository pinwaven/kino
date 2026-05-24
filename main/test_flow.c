#include "test_flow.h"
#include "nano_api.h"
#include "stm32_interface.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include <string.h>
#include <stdio.h>

#define MOCK_MOTOR_ACTION_MS 2500
#define MOCK_ANALYZE_MS 4000
#define FLOW_TASK_PERIOD_MS 100
#define FLOW_TASK_STACK_BYTES 12288
#define MOTOR_ACTION_DRAIN_MS 180
#define MOTOR_POLL_INTERVAL_MS 250
#define MOTOR_OPEN_TIMEOUT_MS 8000
#define MOTOR_CLOSE_TIMEOUT_MS 10000
#define MOTOR_HOMING_TIMEOUT_MS 35000
#define WAIT_CARD_POLL_SETTLE_MS 500
#define CARD_DETECT_MAX_FAILS 3
#define CARD_INSERT_STABLE_POLLS 5

static const char *TAG = "TEST_FLOW";

static test_flow_snapshot_t s_flow;
static char s_hint[512];
static TickType_t s_state_started;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_task_started;
static bool s_wdt_registered;
static bool s_motor_mock = true;
static bool s_wait_card_enabled = true;
static bool s_motor_action_started;
static bool s_boot_homing_done;
static TickType_t s_last_motor_poll_tick;
static TickType_t s_wait_card_enabled_tick;
static uint8_t s_card_detect_fail_count;
static uint8_t s_consecutive_card_count;

typedef enum {
    MOTOR_OP_HOMING,
    MOTOR_OP_OPEN,
    MOTOR_OP_CLOSE,
} motor_op_t;

static void set_last_error_message_locked(const char *message);

bool test_flow_uses_large_error_layout(const char *message)
{
    return message &&
           (strcmp(message, "Chip already used") == 0 ||
            strcmp(message, "Chip not registered") == 0 ||
            strcmp(message, "Chip not configured") == 0 ||
            strcmp(message, "Unknown Nano panel") == 0 ||
            strcmp(message, "Nano chip has no user_id") == 0);
}

static void load_settings(void)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 1;
        if (nvs_get_u8(nvs, "motor_mock", &val) == ESP_OK) {
            s_motor_mock = (val != 0);
        }
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Settings loaded: motor_mock=%d", s_motor_mock);
}

static void save_settings(void)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "motor_mock", s_motor_mock ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Settings saved: motor_mock=%d", s_motor_mock);
}

bool test_flow_is_motor_mock(void)
{
    return s_motor_mock;
}

void test_flow_set_motor_mock(bool mock)
{
    if (s_motor_mock != mock) {
        s_motor_mock = mock;
        save_settings();
    }
}

static void log_stack_watermark(const char *stage)
{
    if (!s_task) {
        return;
    }

    UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(s_task);
    ESP_LOGI(TAG, "%s stack watermark=%u bytes", stage, (unsigned)(watermark_words * sizeof(StackType_t)));
}

static bool elapsed_ms(uint32_t ms)
{
    return (xTaskGetTickCount() - s_state_started) >= pdMS_TO_TICKS(ms);
}

static void lock_flow(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock_flow(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

void test_flow_reload_settings(void)
{
    lock_flow();
    load_settings();
    unlock_flow();
}

static void set_state_locked(test_flow_state_t state)
{
    if (s_flow.state == state) return;
    s_flow.state = state;
    s_state_started = xTaskGetTickCount();
    s_motor_action_started = false;
    s_last_motor_poll_tick = 0;
    ESP_LOGI(TAG, "State -> %d (%s)", (int)state, test_flow_status_text(state));
}

static bool text_has(const char *text, const char *needle)
{
    return text && needle && strstr(text, needle) != NULL;
}

static bool motor_status_is_terminal(const stm32_state_t *state)
{
    const char *a = state->motor_action;
    const char *s = state->motor_status;
    return text_has(a, "idle") || text_has(a, "none") ||
           text_has(s, "idle") || text_has(s, "done") ||
           text_has(s, "finish") || text_has(s, "stop") ||
           text_has(s, "cancel") || text_has(s, "error") ||
           text_has(s, "timeout");
}

static bool motor_status_is_fault(const stm32_state_t *state)
{
    return text_has(state->motor_status, "error") ||
           text_has(state->motor_status, "timeout") ||
           text_has(state->motor_status, "cancel");
}

static esp_err_t start_homing(void)
{
    uint32_t timeout = 30000;
    return stm32_cmd_send_action_drain(CMD_ACTION_HOMING, (uint8_t *)&timeout, 4, MOTOR_ACTION_DRAIN_MS);
}

static esp_err_t start_open(void)
{
    uint8_t payload[8];
    int32_t speed = 15000;
    uint32_t duration = 3250;
    memcpy(&payload[0], &speed, 4);
    memcpy(&payload[4], &duration, 4);
    return stm32_cmd_send_action_drain(CMD_ACTION_MOVE_DUR, payload, 8, MOTOR_ACTION_DRAIN_MS);
}

static esp_err_t start_close(void)
{
    uint8_t payload[9];
    int32_t speed = -12000;
    uint32_t timeout = 8000;
    uint8_t sensor = 0x00;
    memcpy(&payload[0], &speed, 4);
    memcpy(&payload[4], &timeout, 4);
    payload[8] = sensor;
    return stm32_cmd_send_action_drain(CMD_ACTION_MOVE_SS, payload, 9, MOTOR_ACTION_DRAIN_MS);
}

static const char *motor_op_text(motor_op_t op)
{
    switch (op) {
    case MOTOR_OP_HOMING: return "homing";
    case MOTOR_OP_OPEN: return "open";
    case MOTOR_OP_CLOSE: return "close";
    default: return "unknown";
    }
}

static esp_err_t start_motor_op(motor_op_t op)
{
    switch (op) {
    case MOTOR_OP_HOMING: return start_homing();
    case MOTOR_OP_OPEN: return start_open();
    case MOTOR_OP_CLOSE: return start_close();
    default: return ESP_ERR_INVALID_ARG;
    }
}

static void set_motor_error_locked(const char *message, esp_err_t err)
{
    s_flow.last_error = err;
    set_last_error_message_locked(message);
    set_state_locked(TEST_FLOW_MOTOR_ERROR);
}

static bool run_motor_action_locked(motor_op_t op, uint32_t timeout_ms)
{
    if (s_motor_mock) {
        return elapsed_ms(MOCK_MOTOR_ACTION_MS);
    }

    TickType_t now = xTaskGetTickCount();

    if (!s_motor_action_started) {
        unlock_flow();
        esp_err_t err = start_motor_op(op);
        lock_flow();
        if (err != ESP_OK) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Motor %s send failed: %s", motor_op_text(op), esp_err_to_name(err));
            set_motor_error_locked(msg, err);
            return false;
        }

        s_motor_action_started = true;
        s_last_motor_poll_tick = 0;
        ESP_LOGI(TAG, "Motor %s started", motor_op_text(op));
        return false;
    }

    if (timeout_ms > 0 && elapsed_ms(timeout_ms)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Motor %s timeout", motor_op_text(op));
        set_motor_error_locked(msg, ESP_ERR_TIMEOUT);
        return false;
    }

    if (s_last_motor_poll_tick != 0 &&
        (now - s_last_motor_poll_tick) < pdMS_TO_TICKS(MOTOR_POLL_INTERVAL_MS)) {
        return false;
    }
    s_last_motor_poll_tick = now;

    unlock_flow();
    esp_err_t err = stm32_update_motor_poll();
    stm32_state_t state;
    stm32_get_current_state(&state);
    lock_flow();

    if (err != ESP_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Motor %s poll failed: %s", motor_op_text(op), esp_err_to_name(err));
        set_motor_error_locked(msg, err);
        return false;
    }

    ESP_LOGI(TAG, "Motor %s poll action=%s status=%s",
             motor_op_text(op), state.motor_action, state.motor_status);

    if (motor_status_is_fault(&state)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Motor %s fault: %s", motor_op_text(op), state.motor_status);
        set_motor_error_locked(msg, ESP_FAIL);
        return false;
    }

    return motor_status_is_terminal(&state);
}

static bool is_retryable_error_state(test_flow_state_t state)
{
    return state == TEST_FLOW_NFC_ERROR ||
           state == TEST_FLOW_API_ERROR ||
           state == TEST_FLOW_CARD_DETECT_ERROR ||
           state == TEST_FLOW_MOTOR_ERROR;
}

static void reset_for_wait_card_locked(void)
{
    s_flow.card_inserted = false;
    s_flow.adc1_value = -1;
    s_flow.cd_value = -1;
    s_flow.nfc_code[0] = '\0';
    s_flow.last_error_message[0] = '\0';
    s_flow.upload_summary[0] = '\0';
    s_flow.last_error = ESP_OK;
    s_card_detect_fail_count = 0;
    set_state_locked(TEST_FLOW_WAIT_CARD);
}

static void set_last_error_message_locked(const char *message)
{
    if (!message || message[0] == '\0') {
        s_flow.last_error_message[0] = '\0';
        return;
    }

    strncpy(s_flow.last_error_message, message, sizeof(s_flow.last_error_message) - 1);
    s_flow.last_error_message[sizeof(s_flow.last_error_message) - 1] = '\0';
}

static void set_upload_summary_locked(const char *summary)
{
    if (!summary || summary[0] == '\0') {
        s_flow.upload_summary[0] = '\0';
        return;
    }

    strncpy(s_flow.upload_summary, summary, sizeof(s_flow.upload_summary) - 1);
    s_flow.upload_summary[sizeof(s_flow.upload_summary) - 1] = '\0';
}

static bool poll_card_removed_locked(void)
{
    bool inserted = false;
    int adc1 = -1;
    int cd = -1;

    unlock_flow();
    esp_err_t err = stm32_read_card_detect(&inserted, &adc1, &cd);
    lock_flow();

    s_flow.last_error = err;
    if (err != ESP_OK) {
        set_state_locked(TEST_FLOW_CARD_DETECT_ERROR);
        s_flow.card_inserted = true;
        return false;
    }

    s_flow.card_inserted = inserted;
    s_flow.adc1_value = adc1;
    s_flow.cd_value = cd;
    if (!inserted) {
        reset_for_wait_card_locked();
        return true;
    }
    return false;
}

static void test_flow_step_locked(void)
{
    if (s_flow.state == TEST_FLOW_PREP_HOMING) {
        motor_op_t prep_op = s_boot_homing_done ? MOTOR_OP_CLOSE : MOTOR_OP_HOMING;
        uint32_t prep_timeout = s_boot_homing_done ? MOTOR_CLOSE_TIMEOUT_MS : MOTOR_HOMING_TIMEOUT_MS;
        if (run_motor_action_locked(prep_op, prep_timeout)) {
            s_boot_homing_done = true;
            set_state_locked(TEST_FLOW_PREP_OPENING);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_PREP_OPENING) {
        if (run_motor_action_locked(MOTOR_OP_OPEN, MOTOR_OPEN_TIMEOUT_MS)) {
            set_state_locked(TEST_FLOW_WAIT_CARD);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_CLOSING) {
        if (run_motor_action_locked(MOTOR_OP_CLOSE, MOTOR_CLOSE_TIMEOUT_MS)) {
            set_state_locked(TEST_FLOW_READING_NFC);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_READING_NFC) {
        unlock_flow();
        s_flow.last_error = stm32_read_nfc_first_record(s_flow.nfc_code, sizeof(s_flow.nfc_code));
        lock_flow();
        if (s_flow.last_error == ESP_OK) {
            set_state_locked(TEST_FLOW_NFC_READY);
        } else {
            set_state_locked(TEST_FLOW_NFC_ERROR);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_NFC_READY) {
        set_state_locked(TEST_FLOW_GETTING_CHIP);
        return;
    }

    if (s_flow.state == TEST_FLOW_ANALYZING_CARD) {
        if (elapsed_ms(MOCK_ANALYZE_MS)) {
            set_state_locked(TEST_FLOW_GETTING_CHIP);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_GETTING_CHIP) {
        log_stack_watermark("before verify");
        if (s_task && s_wdt_registered) {
            esp_task_wdt_delete(NULL);
        }
        unlock_flow();
        s_flow.last_error = nano_api_get_chip(s_flow.nfc_code);
        lock_flow();
        if (s_task && s_wdt_registered) {
            esp_task_wdt_add(NULL);
        }
        log_stack_watermark("after verify");
        set_last_error_message_locked(nano_api_last_error_message());
        set_state_locked(s_flow.last_error == ESP_OK ? TEST_FLOW_MOCK_TESTING : TEST_FLOW_API_ERROR);
        return;
    }

    if (s_flow.state == TEST_FLOW_MOCK_TESTING) {
        if (elapsed_ms(MOCK_MOTOR_ACTION_MS)) {
            set_state_locked(TEST_FLOW_POSTING_BIOMARKERS);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_POSTING_BIOMARKERS) {
        log_stack_watermark("before upload");
        if (s_task && s_wdt_registered) {
            esp_task_wdt_delete(NULL);
        }
        unlock_flow();
        s_flow.last_error = nano_api_post_mock_biomarkers();
        if (s_flow.last_error == ESP_OK) {
            esp_err_t final_err = nano_api_post_kino_result();
            if (final_err != ESP_OK) {
                ESP_LOGW(TAG, "Nano kino-result finalisation failed but mock flow is complete: %s", nano_api_last_error_message());
            }
        }
        lock_flow();
        if (s_task && s_wdt_registered) {
            esp_task_wdt_add(NULL);
        }
        log_stack_watermark("after upload");
        set_last_error_message_locked(nano_api_last_error_message());
        set_upload_summary_locked(nano_api_last_upload_summary());
        set_state_locked(s_flow.last_error == ESP_OK ? TEST_FLOW_UPLOAD_REVIEW : TEST_FLOW_API_ERROR);
        return;
    }

    if (s_flow.state == TEST_FLOW_UPLOAD_REVIEW) {
        return;
    }

    if (s_flow.state == TEST_FLOW_SUCCESS_EJECTING) {
        if (run_motor_action_locked(MOTOR_OP_OPEN, MOTOR_OPEN_TIMEOUT_MS)) {
            set_state_locked(TEST_FLOW_WAIT_REMOVE_CARD);
        }
        return;
    }

    if (is_retryable_error_state(s_flow.state)) {
        return;
    }

    if (s_flow.state == TEST_FLOW_RECOVERY_CLOSING) {
        if (run_motor_action_locked(MOTOR_OP_CLOSE, MOTOR_CLOSE_TIMEOUT_MS)) {
            set_state_locked(TEST_FLOW_RECOVERY_OPENING);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_RECOVERY_OPENING) {
        if (run_motor_action_locked(MOTOR_OP_OPEN, MOTOR_OPEN_TIMEOUT_MS)) {
            set_state_locked(TEST_FLOW_WAIT_REMOVE_CARD);
        }
        return;
    }

    if (s_flow.state == TEST_FLOW_WAIT_REMOVE_CARD) {
        poll_card_removed_locked();
        return;
    }

    if (!s_wait_card_enabled) {
        if (s_flow.state != TEST_FLOW_WAIT_CARD) {
            set_state_locked(TEST_FLOW_WAIT_CARD);
        }
        s_flow.card_inserted = false;
        s_flow.adc1_value = -1;
        s_flow.cd_value = -1;
        return;
    }

    if (s_flow.state == TEST_FLOW_WAIT_CARD &&
        s_wait_card_enabled_tick != 0 &&
        (xTaskGetTickCount() - s_wait_card_enabled_tick) < pdMS_TO_TICKS(WAIT_CARD_POLL_SETTLE_MS)) {
        s_flow.card_inserted = false;
        s_flow.adc1_value = -1;
        s_flow.cd_value = -1;
        return;
    }

    bool inserted = false;
    int adc1 = -1;
    int cd = -1;

    unlock_flow();
    esp_err_t err = stm32_read_card_detect(&inserted, &adc1, &cd);
    lock_flow();

    s_flow.last_error = err;
    if (err != ESP_OK) {
        s_card_detect_fail_count++;
        s_flow.card_inserted = false;
        s_flow.adc1_value = -1;
        s_flow.cd_value = -1;
        ESP_LOGW(TAG, "card detect read failed %u/%u: %s",
                 (unsigned)s_card_detect_fail_count,
                 (unsigned)CARD_DETECT_MAX_FAILS,
                 esp_err_to_name(err));
        if (s_card_detect_fail_count >= CARD_DETECT_MAX_FAILS) {
            set_state_locked(TEST_FLOW_CARD_DETECT_ERROR);
        }
        return;
    }

    s_card_detect_fail_count = 0;
    s_flow.card_inserted = inserted;
    s_flow.adc1_value = adc1;
    s_flow.cd_value = cd;

    if (inserted) {
        s_consecutive_card_count++;
        if (s_consecutive_card_count >= CARD_INSERT_STABLE_POLLS) {
            set_state_locked(TEST_FLOW_CLOSING);
        } else {
            set_state_locked(TEST_FLOW_CARD_DETECTED);
        }
    } else {
        s_consecutive_card_count = 0;
        set_state_locked(TEST_FLOW_WAIT_CARD);
    }
}

static void test_flow_task(void *arg)
{
    (void)arg;

    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    s_wdt_registered = (wdt_err == ESP_OK);
    if (!s_wdt_registered) {
        ESP_LOGW(TAG, "failed to subscribe test flow task to WDT: %s", esp_err_to_name(wdt_err));
    }

    while (1) {
        if (s_wdt_registered) {
            esp_task_wdt_reset();
        }

        lock_flow();
        test_flow_step_locked();
        unlock_flow();
        vTaskDelay(pdMS_TO_TICKS(FLOW_TASK_PERIOD_MS));
    }
}

void test_flow_init(void)
{
    load_settings();
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }

    lock_flow();
    memset(&s_flow, 0, sizeof(s_flow));
    s_consecutive_card_count = 0;
    s_flow.state = TEST_FLOW_PREP_HOMING;
    s_state_started = xTaskGetTickCount();
    s_flow.adc1_value = -1;
    s_flow.cd_value = -1;
    s_flow.last_error = ESP_OK;
    unlock_flow();
}

void test_flow_start(void)
{
    if (s_task_started) return;
    s_task_started = true;
    xTaskCreate(test_flow_task, "test_flow", FLOW_TASK_STACK_BYTES, NULL, 5, &s_task);
}

void test_flow_update(void)
{
    /* Legacy entry point kept for compatibility; the flow now runs in its own task. */
}

void test_flow_set_wait_card_enabled(bool enabled)
{
    lock_flow();
    s_wait_card_enabled = enabled;
    s_card_detect_fail_count = 0;
    s_wait_card_enabled_tick = enabled ? xTaskGetTickCount() : 0;
    if (!enabled && s_flow.state == TEST_FLOW_WAIT_CARD) {
        s_flow.card_inserted = false;
        s_flow.adc1_value = -1;
        s_flow.cd_value = -1;
    }
    unlock_flow();
    ESP_LOGI(TAG, "wait-card trigger %s", enabled ? "enabled" : "disabled");
}

bool test_flow_retry_after_error(void)
{
    bool accepted = false;

    lock_flow();
    if (is_retryable_error_state(s_flow.state)) {
        set_state_locked(TEST_FLOW_RECOVERY_OPENING);
        accepted = true;
    }
    unlock_flow();

    if (accepted) {
        ESP_LOGI(TAG, "eject/retry requested after error");
    }
    return accepted;
}

bool test_flow_continue_after_upload_review(void)
{
    bool accepted = false;

    lock_flow();
    if (s_flow.state == TEST_FLOW_UPLOAD_REVIEW) {
        set_state_locked(TEST_FLOW_SUCCESS_EJECTING);
        accepted = true;
    }
    unlock_flow();

    if (accepted) {
        ESP_LOGI(TAG, "upload review acknowledged");
    }
    return accepted;
}

void test_flow_get_snapshot(test_flow_snapshot_t *out)
{
    if (!out) return;
    lock_flow();
    memcpy(out, &s_flow, sizeof(s_flow));
    unlock_flow();
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
    case TEST_FLOW_GETTING_CHIP:
        return "Verifying";
    case TEST_FLOW_MOCK_TESTING:
        return "Testing";
    case TEST_FLOW_POSTING_BIOMARKERS:
        return "Uploading";
    case TEST_FLOW_UPLOAD_REVIEW:
        return "Upload result";
    case TEST_FLOW_DONE:
        return "Done";
    case TEST_FLOW_SUCCESS_EJECTING:
        return "Ejecting card";
    case TEST_FLOW_WAIT_REMOVE_CARD:
        return "Remove card";
    case TEST_FLOW_MOTOR_ERROR:
        return "Motor error";
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
        return "Insert Card";
    }
}

const char *test_flow_hint_text(const test_flow_snapshot_t *snapshot)
{
    if (!snapshot) return "";

    bool mock = s_motor_mock;

    if (snapshot->state == TEST_FLOW_PREP_HOMING) {
        return mock ? "Homing tray (mock 2.5s)" : "Homing tray (Hardware)";
    }

    if (snapshot->state == TEST_FLOW_PREP_OPENING) {
        return mock ? "Opening tray (mock 2.5s)" : "Opening tray (Hardware)";
    }

    if (snapshot->state == TEST_FLOW_CARD_DETECT_ERROR) {
        snprintf(s_hint, sizeof(s_hint), "CardDetect failed: %s. Tap to eject", esp_err_to_name(snapshot->last_error));
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_MOTOR_ERROR) {
        if (snapshot->last_error_message[0] != '\0') {
            snprintf(s_hint, sizeof(s_hint), "%s. Tap to eject", snapshot->last_error_message);
        } else {
            snprintf(s_hint, sizeof(s_hint), "Motor failed: %s. Tap to eject", esp_err_to_name(snapshot->last_error));
        }
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_RECOVERY_CLOSING) {
        return mock ? "Closing tray before eject (mock 2.5s)" : "Closing tray (Hardware)";
    }

    if (snapshot->state == TEST_FLOW_RECOVERY_OPENING) {
        return mock ? "Opening tray, remove card (mock 2.5s)" : "Opening tray (Hardware)";
    }

    if (snapshot->state == TEST_FLOW_CARD_DETECTED) {
        snprintf(s_hint, sizeof(s_hint), "CardDetect cd:%d adc1:%d", snapshot->cd_value, snapshot->adc1_value);
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_CLOSING) {
        return mock ? "Closing tray (mock 2.5s)" : "Closing tray (Hardware)";
    }

    if (snapshot->state == TEST_FLOW_READING_NFC) {
        return "Reading NFC NDEF";
    }

    if (snapshot->state == TEST_FLOW_NFC_READY) {
        snprintf(s_hint, sizeof(s_hint), "NFC code: %s", snapshot->nfc_code);
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_ANALYZING_CARD) {
        return "Analyzing card data (mock 4s)";
    }

    if (snapshot->state == TEST_FLOW_GETTING_CHIP) {
        return "Nano API: get chip";
    }

    if (snapshot->state == TEST_FLOW_MOCK_TESTING) {
        return "Mock testing in progress";
    }

    if (snapshot->state == TEST_FLOW_POSTING_BIOMARKERS) {
        return "Nano API: post biomarkers";
    }

    if (snapshot->state == TEST_FLOW_UPLOAD_REVIEW) {
        if (snapshot->upload_summary[0] != '\0') {
            snprintf(s_hint, sizeof(s_hint), "%.430s%s\nTap to eject", snapshot->upload_summary,
                     strlen(snapshot->upload_summary) > 430 ? "..." : "");
            return s_hint;
        }
        return "Upload finished. Tap to eject";
    }

    if (snapshot->state == TEST_FLOW_DONE) {
        return "Upload acknowledged";
    }

    if (snapshot->state == TEST_FLOW_SUCCESS_EJECTING) {
        return mock ? "Opening tray, remove card (mock 2.5s)" : "Opening tray (Hardware)";
    }

    if (snapshot->state == TEST_FLOW_WAIT_REMOVE_CARD) {
        snprintf(s_hint, sizeof(s_hint), "Please remove card cd:%d adc1:%d", snapshot->cd_value, snapshot->adc1_value);
        return s_hint;
    }

    if (snapshot->state == TEST_FLOW_NFC_ERROR) {
        return "NFC error\nTap to eject";
    }

    if (snapshot->state == TEST_FLOW_API_ERROR) {
        if (snapshot->last_error_message[0] != '\0') {
            const char *msg = snapshot->last_error_message;
            if (test_flow_uses_large_error_layout(msg)) {
                snprintf(s_hint, sizeof(s_hint), "%s\nTap to eject", msg);
            } else {
                snprintf(s_hint, sizeof(s_hint), "%s. Tap to eject", msg);
            }
        } else {
            snprintf(s_hint, sizeof(s_hint), "API failed: %s. Tap to eject", esp_err_to_name(snapshot->last_error));
        }
        return s_hint;
    }

    snprintf(s_hint, sizeof(s_hint), "Waiting... cd:%d adc1:%d", snapshot->cd_value, snapshot->adc1_value);
    return s_hint;
}
