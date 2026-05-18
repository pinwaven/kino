#include "poct_api.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FROS_BASE_URL   "https://fros-api.gyyyhospital.com"
#define SBEDGE_BASE_URL "https://supabase.virtualhealth.cn/functions/v1"
#define HTTP_TIMEOUT_MS 8000

static const char *TAG = "POCT_API";

static esp_err_t post_json(const char *url, const char *body)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "POST %s -> err=%s status=%d len=%d", url, esp_err_to_name(err), status, content_len);

    esp_http_client_cleanup(client);

    if (err != ESP_OK) return err;
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

static void current_time_string(char *out, size_t out_len)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (tm_now.tm_year < 120) {
        snprintf(out, out_len, "2026-05-18 00:00:00");
        return;
    }

    strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &tm_now);
}

esp_err_t poct_api_verify_card(const char *code)
{
    char body[192];
    char url[160];

    snprintf(url, sizeof(url), FROS_BASE_URL "/api/service/poct/device/queryByCode");
    snprintf(body, sizeof(body), "{\"code\":\"%s\"}", code);
    esp_err_t err = post_json(url, body);
    if (err != ESP_OK) return err;

    snprintf(url, sizeof(url), SBEDGE_BASE_URL "/business/poct/card/getInfo");
    snprintf(body, sizeof(body), "{\"cardBatchCode\":\"\",\"cardCode\":\"%s\"}", code);
    err = post_json(url, body);
    if (err != ESP_OK) return err;

    snprintf(url, sizeof(url), SBEDGE_BASE_URL "/business/poct/card/updateStatus");
    snprintf(body, sizeof(body), "{\"cardCode\":\"%s\",\"status\":\"checking\"}", code);
    return post_json(url, body);
}

esp_err_t poct_api_upload_mock_result(const char *code)
{
    char date[32];
    char body[768];
    char url[160];

    current_time_string(date, sizeof(date));

    snprintf(url, sizeof(url), FROS_BASE_URL "/api/service/poct/device/uploadCheckData");
    snprintf(body, sizeof(body),
             "{\"code\":\"%s\",\"status\":\"completed\",\"date\":\"%s\",\"result\":["
             "{\"name\":\"Mock CRP\",\"result\":\"5.2\",\"radioValue\":\"0.82\","
             "\"refer\":\"0-10 mg/L\",\"t1Value\":\"12345\",\"cValue\":\"15055\","
             "\"t1ValueName\":\"T1\",\"t1ValueStr\":\"T1=12345\"}"
             "]}",
             code, date);
    esp_err_t err = post_json(url, body);
    if (err != ESP_OK) return err;

    snprintf(url, sizeof(url), SBEDGE_BASE_URL "/business/poct/card/updateStatus");
    snprintf(body, sizeof(body), "{\"cardCode\":\"%s\",\"status\":\"success\"}", code);
    return post_json(url, body);
}
