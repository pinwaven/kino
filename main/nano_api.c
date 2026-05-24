#include "nano_api.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef NANO_BASE_URL
#define NANO_BASE_URL "https://nano.fros.cc"
#endif

#ifndef NANO_API_TOKEN
#define NANO_API_TOKEN "tokenData-gh9bc7917115bid72c68c8c4693g"
#endif

#ifndef NANO_DEVICE_ID
#define NANO_DEVICE_ID "KNA1-0001"
#endif

#define HTTP_TIMEOUT_MS 15000
#define HTTP_SLOW_TIMEOUT_MS 60000
#define HTTP_BUFFER_RX 1024
#define HTTP_BUFFER_TX 1024
#define HTTP_LOG_BODY_BYTES 2048
#define NANO_MOCK_VALUE "5.2"

static const char *TAG = "NANO_API";
static char s_last_error_message[128];
static char s_chip_id[128];
static char s_openid[96];
static char s_biomarker_key[32] = "hsCRP";
static char s_kino_result_status[96];
static EXT_RAM_BSS_ATTR char s_biomarkers_resp[HTTP_LOG_BODY_BYTES];
static EXT_RAM_BSS_ATTR char s_http_resp_body[HTTP_LOG_BODY_BYTES];
static EXT_RAM_BSS_ATTR char s_api_resp[HTTP_LOG_BODY_BYTES];
static EXT_RAM_BSS_ATTR char s_post_body[HTTP_LOG_BODY_BYTES + 512];
static EXT_RAM_BSS_ATTR char s_last_upload_summary[512];
static double s_bio_age;
static bool s_have_bio_age;
static double s_chrono_age;
static double s_age_diff;
static bool s_have_chrono_age;
static bool s_have_age_diff;

typedef struct {
    char *buf;
    int len;
    int cap;
    bool truncated;
} http_log_body_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }

    http_log_body_t *body = (http_log_body_t *)evt->user_data;
    if (!body->buf || body->cap <= 0) {
        return ESP_OK;
    }

    int available = body->cap - body->len - 1;
    if (available <= 0) {
        body->truncated = true;
        return ESP_OK;
    }

    int copy_len = evt->data_len;
    if (copy_len > available) {
        copy_len = available;
        body->truncated = true;
    }

    memcpy(body->buf + body->len, evt->data, copy_len);
    body->len += copy_len;
    body->buf[body->len] = '\0';
    return ESP_OK;
}

static void set_last_error_message(const char *message)
{
    if (!message || message[0] == '\0') {
        s_last_error_message[0] = '\0';
        return;
    }

    strncpy(s_last_error_message, message, sizeof(s_last_error_message) - 1);
    s_last_error_message[sizeof(s_last_error_message) - 1] = '\0';
}

const char *nano_api_last_error_message(void)
{
    return s_last_error_message;
}

const char *nano_api_last_upload_summary(void)
{
    return s_last_upload_summary;
}

static void update_upload_report_summary(void)
{
    char bio_age[24] = "--";
    char chrono_age[24] = "--";
    char age_delta[32] = "";

    if (s_have_bio_age) {
        snprintf(bio_age, sizeof(bio_age), "%.1f", s_bio_age);
    }
    if (s_have_chrono_age) {
        snprintf(chrono_age, sizeof(chrono_age), "%.1f", s_chrono_age);
    }
    if (s_have_age_diff) {
        snprintf(age_delta, sizeof(age_delta), "\nAge delta: %+.1f", s_age_diff);
    }

    snprintf(s_last_upload_summary, sizeof(s_last_upload_summary),
             "Report uploaded\nChip: %.32s\n"
             "GDF15: 573\nIL6: 0.5\nhsCRP: 0.31\nGA: 15.2\nCystatinC: 0.78\nCD38: 1.2\n"
             "BioAge: %s\nChronoAge: %s%s\nKino result: %s\nTap to eject",
             s_chip_id[0] ? s_chip_id : "--",
             bio_age,
             chrono_age,
             age_delta,
             s_kino_result_status[0] ? s_kino_result_status : "pending");
}

static void log_http_heap(const char *stage, const char *url)
{
    ESP_LOGI(TAG,
             "%s %s internal=%u dma=%u psram=%u",
             stage,
             url,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void log_http_request_debug(const char *method, const char *url, const char *body)
{
    ESP_LOGI(TAG, "%s %s", method, url);
    if (body && body[0] != '\0') {
        ESP_LOGI(TAG, "%s body: %s", method, body);
    }
}

static esp_err_t http_request(const char *method,
                              const char *url,
                              const char *body,
                              uint32_t timeout_ms,
                              char *resp_out,
                              size_t resp_out_len)
{
    http_log_body_t log_body = {
        .buf = s_http_resp_body,
        .len = 0,
        .cap = sizeof(s_http_resp_body),
        .truncated = false,
    };
    s_http_resp_body[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = strcmp(method, "GET") == 0 ? HTTP_METHOD_GET : HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
        .buffer_size = HTTP_BUFFER_RX,
        .buffer_size_tx = HTTP_BUFFER_TX,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &log_body,
    };

    log_http_heap("http begin", url);
    log_http_request_debug(method, url, body);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        set_last_error_message("HTTP client init failed");
        return ESP_ERR_NO_MEM;
    }

    if (NANO_API_TOKEN[0] != '\0') {
        esp_http_client_set_header(client, "Authorization", "Bearer " NANO_API_TOKEN);
    }
    if (body && body[0] != '\0') {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "%s %s -> err=%s status=%d len=%d", method, url, esp_err_to_name(err), status, content_len);
    ESP_LOGI(TAG, "%s result%s: %s", method, log_body.truncated ? " (truncated)" : "", log_body.len > 0 ? log_body.buf : "<empty>");

    esp_http_client_cleanup(client);
    log_http_heap("http end", url);

    if (resp_out && resp_out_len > 0) {
        strncpy(resp_out, log_body.buf, resp_out_len - 1);
        resp_out[resp_out_len - 1] = '\0';
    }

    if (err != ESP_OK) {
        set_last_error_message("Network request failed");
        return err;
    }
    if (status < 200 || status >= 300) {
        set_last_error_message("Nano API HTTP error");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void json_copy_string(cJSON *object, const char *name, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';

    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || !item->valuestring) return;

    strncpy(out, item->valuestring, out_len - 1);
    out[out_len - 1] = '\0';
}

static double json_number(cJSON *object, const char *name, double fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static void url_encode(const char *src, char *dst, size_t dst_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;
    for (size_t i = 0; src && src[i] != '\0' && out + 1 < dst_len; i++) {
        unsigned char c = (unsigned char)src[i];
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            dst[out++] = (char)c;
        } else if (out + 3 < dst_len) {
            dst[out++] = '%';
            dst[out++] = hex[c >> 4];
            dst[out++] = hex[c & 0x0F];
        } else {
            break;
        }
    }
    if (dst_len > 0) dst[out] = '\0';
}

static void json_escape(const char *src, char *dst, size_t dst_len)
{
    size_t out = 0;
    for (size_t i = 0; src && src[i] != '\0' && out + 1 < dst_len; i++) {
        char c = src[i];
        if ((c == '\\' || c == '"') && out + 2 < dst_len) {
            dst[out++] = '\\';
            dst[out++] = c;
        } else if ((unsigned char)c >= 0x20) {
            dst[out++] = c;
        }
    }
    if (dst_len > 0) dst[out] = '\0';
}

static esp_err_t parse_nano_chip(const char *chip_id, const char *resp)
{
    cJSON *root = cJSON_Parse(resp);
    if (!root) {
        set_last_error_message("Invalid Nano chip response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t err = ESP_OK;
    cJSON *found = cJSON_GetObjectItemCaseSensitive(root, "found");
    cJSON *used = cJSON_GetObjectItemCaseSensitive(root, "used");
    cJSON *chip_config = cJSON_GetObjectItemCaseSensitive(root, "chip_config");
    cJSON *biomarker_keys = cJSON_GetObjectItemCaseSensitive(root, "biomarker_keys");

    if (!cJSON_IsTrue(found)) {
        set_last_error_message("Chip not registered");
        err = ESP_FAIL;
    } else if (cJSON_IsTrue(used)) {
        set_last_error_message("Chip already used");
        err = ESP_FAIL;
    } else if (!cJSON_IsObject(chip_config)) {
        set_last_error_message("Chip not configured");
        err = ESP_FAIL;
    } else if (!cJSON_IsArray(biomarker_keys) || cJSON_GetArraySize(biomarker_keys) <= 0) {
        set_last_error_message("Unknown Nano panel");
        err = ESP_FAIL;
    }

    if (err == ESP_OK) {
        strncpy(s_chip_id, chip_id, sizeof(s_chip_id) - 1);
        s_chip_id[sizeof(s_chip_id) - 1] = '\0';
        json_copy_string(root, "user_id", s_openid, sizeof(s_openid));
        cJSON *first_key = cJSON_GetArrayItem(biomarker_keys, 0);
        if (cJSON_IsString(first_key) && first_key->valuestring) {
            strncpy(s_biomarker_key, first_key->valuestring, sizeof(s_biomarker_key) - 1);
            s_biomarker_key[sizeof(s_biomarker_key) - 1] = '\0';
        }
        if (s_openid[0] == '\0') {
            set_last_error_message("Nano chip has no user_id");
            err = ESP_FAIL;
        }
    }

    cJSON_Delete(root);
    return err;
}

static esp_err_t parse_nano_biomarkers(const char *resp)
{
    cJSON *root = cJSON_Parse(resp);
    if (!root) {
        set_last_error_message("Invalid Nano biomarkers response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t err = ESP_OK;
    cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
    cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "bioage_profile");
    if (!cJSON_IsTrue(success)) {
        set_last_error_message("Nano biomarkers failed");
        err = ESP_FAIL;
    } else if (cJSON_IsObject(profile)) {
        s_bio_age = json_number(profile, "BioAge", 0.0);
        s_have_bio_age = s_bio_age > 0.0;
        s_chrono_age = json_number(profile, "ChronoAge", 0.0);
        s_age_diff = json_number(profile, "AgeDifference", 0.0);
        s_have_chrono_age = s_chrono_age > 0.0;
        s_have_age_diff = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(profile, "AgeDifference"));
    }

    cJSON_Delete(root);
    return err;
}

esp_err_t nano_api_get_chip(const char *chip_id)
{
    char encoded[192];
    char url[320];

    set_last_error_message(NULL);
    s_openid[0] = '\0';
    s_biomarkers_resp[0] = '\0';
    s_kino_result_status[0] = '\0';
    s_have_bio_age = false;
    s_chrono_age = 0.0;
    s_age_diff = 0.0;
    s_have_chrono_age = false;
    s_have_age_diff = false;
    strncpy(s_biomarker_key, "hsCRP", sizeof(s_biomarker_key) - 1);
    s_biomarker_key[sizeof(s_biomarker_key) - 1] = '\0';

    if (!chip_id || chip_id[0] == '\0') {
        set_last_error_message("Invalid chip code");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "nano chip_id=%s", chip_id);
    url_encode(chip_id, encoded, sizeof(encoded));
    snprintf(url, sizeof(url), NANO_BASE_URL "/api/kino-chip?chip_id=%s", encoded);

    esp_err_t err = http_request("GET", url, NULL, HTTP_TIMEOUT_MS, s_api_resp, sizeof(s_api_resp));
    if (err != ESP_OK) return err;

    return parse_nano_chip(chip_id, s_api_resp);
}

esp_err_t nano_api_post_mock_biomarkers(void)
{
    char openid[160];
    char key[64];
    char device[64];
    char url[256];

    set_last_error_message(NULL);
    json_escape(s_openid, openid, sizeof(openid));
    json_escape(s_biomarker_key, key, sizeof(key));
    json_escape(NANO_DEVICE_ID, device, sizeof(device));

    snprintf(url, sizeof(url), NANO_BASE_URL "/api/biomarkers");
    snprintf(s_post_body, sizeof(s_post_body),
             "{\"openid\":\"%s\",\"test_type\":\"kino_chip\",\"test_data\":{\"%s\":%s},\"kino_device_id\":\"%s\"}",
             openid, key, NANO_MOCK_VALUE, device);

    s_last_upload_summary[0] = '\0';
    esp_err_t err = http_request("POST", url, s_post_body, HTTP_SLOW_TIMEOUT_MS, s_biomarkers_resp, sizeof(s_biomarkers_resp));
    if (err != ESP_OK) return err;

    err = parse_nano_biomarkers(s_biomarkers_resp);
    if (err != ESP_OK) return err;

    strncpy(s_kino_result_status, "pending", sizeof(s_kino_result_status) - 1);
    s_kino_result_status[sizeof(s_kino_result_status) - 1] = '\0';
    update_upload_report_summary();

    return ESP_OK;
}

esp_err_t nano_api_post_kino_result(void)
{
    char url[256];
    char chip[192];
    char kino_resp[256];

    set_last_error_message(NULL);
    json_escape(s_chip_id, chip, sizeof(chip));
    snprintf(url, sizeof(url), NANO_BASE_URL "/api/kino-result");
    snprintf(s_post_body, sizeof(s_post_body),
             "{\"chip_id\":\"%s\",\"data\":%s,\"bio_age\":%.2f}",
             chip,
             s_biomarkers_resp[0] ? s_biomarkers_resp : "{}",
             s_have_bio_age ? s_bio_age : 0.0);

    esp_err_t err = http_request("POST", url, s_post_body, HTTP_TIMEOUT_MS, kino_resp, sizeof(kino_resp));

    if (err == ESP_OK) {
        strncpy(s_kino_result_status, "done", sizeof(s_kino_result_status) - 1);
    } else {
        strncpy(s_kino_result_status, "failed", sizeof(s_kino_result_status) - 1);
    }
    s_kino_result_status[sizeof(s_kino_result_status) - 1] = '\0';
    update_upload_report_summary();
    return err;
}

static void *cjson_psram_malloc(size_t size) {
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static void cjson_psram_free(void *ptr) {
    heap_caps_free(ptr);
}

void init_cjson_psram_hooks(void) {
    cJSON_Hooks hooks = {
        .malloc_fn = cjson_psram_malloc,
        .free_fn = cjson_psram_free
    };
    cJSON_InitHooks(&hooks);
    ESP_LOGI(TAG, "cJSON memory hooks configured to use PSRAM");
}
