#include "wifi_prov.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "dhcpserver/dhcpserver.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "esp_task_wdt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define TAG "WIFI_PROV"

static volatile wifi_prov_state_t s_state = WIFI_PROV_STATE_IDLE;
static char s_target_ssid[33] = {0};
static char s_got_ip[16] = {0};
static char s_pending_ssid[33] = {0};
static char s_pending_pass[65] = {0};

static httpd_handle_t s_httpd = NULL;
static TaskHandle_t   s_dns_task_handle = NULL;
static volatile bool  s_dns_running = false;
static SemaphoreHandle_t s_dns_done_sem = NULL;

static esp_netif_t *s_ap_netif  = NULL;
static esp_netif_t *s_sta_netif = NULL;
static bool s_global_inited = false;
static bool s_wifi_started  = false;
static bool s_handlers_registered = false;
static bool s_portal_running = false;
static bool s_sta_connected = false;
static bool s_sta_reconnect_pending = false;
static volatile bool s_disconnecting_for_reprovision = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);

static void configure_ap_captive_portal_dhcp(void)
{
    if (!s_ap_netif) {
        return;
    }

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ipaddr_addr(WIFI_PROV_AP_IP);

    uint8_t dhcps_offer_dns = OFFER_DNS;
    static char captiveportal_uri[] = "http://" WIFI_PROV_AP_IP "/";

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                                                         ESP_NETIF_DOMAIN_NAME_SERVER,
                                                         &dhcps_offer_dns,
                                                         sizeof(dhcps_offer_dns)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                                                         ESP_NETIF_CAPTIVEPORTAL_URI,
                                                         captiveportal_uri,
                                                         strlen(captiveportal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(s_ap_netif));
}

/* ---- helpers ------------------------------------------------------------ */

static void url_decode(char *dst, const char *src, size_t dst_len)
{
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dst_len; i++) {
        if (src[i] == '+') {
            dst[di++] = ' ';
        } else if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

static bool form_field(const char *body, const char *key, char *out, size_t out_len)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *p = strstr(body, needle);
    if (!p) return false;
    p += strlen(needle);
    const char *end = strchr(p, '&');
    size_t raw_len = end ? (size_t)(end - p) : strlen(p);
    char tmp[256] = {0};
    if (raw_len >= sizeof(tmp)) raw_len = sizeof(tmp) - 1;
    memcpy(tmp, p, raw_len);
    url_decode(out, tmp, out_len);
    return true;
}

static esp_err_t save_sta_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_sta", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "pass", pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved WiFi credentials for SSID=%s", ssid);
    } else {
        ESP_LOGW(TAG, "save WiFi credentials failed: %s", esp_err_to_name(err));
    }
    return err;
}

static bool load_sta_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_sta", NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;

    size_t len = ssid_len;
    err = nvs_get_str(nvs, "ssid", ssid, &len);
    if (err != ESP_OK || ssid[0] == '\0') {
        nvs_close(nvs);
        return false;
    }

    len = pass_len;
    err = nvs_get_str(nvs, "pass", pass, &len);
    if (err != ESP_OK) {
        pass[0] = '\0';
    }
    nvs_close(nvs);
    return true;
}

static esp_err_t ensure_global_init(void)
{
    if (s_global_inited) return ESP_OK;

    esp_err_t r = esp_netif_init();
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "netif init: %s", esp_err_to_name(r));
        return r;
    }

    r = esp_event_loop_create_default();
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create: %s", esp_err_to_name(r));
        return r;
    }

    s_global_inited = true;
    return ESP_OK;
}

static void ensure_netifs(void)
{
    if (!s_ap_netif)  s_ap_netif  = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();
}

static esp_err_t ensure_wifi_driver(void)
{
    if (s_wifi_started) return ESP_OK;

    ESP_LOGI(TAG, "heap before wifi init: internal=%u dma=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    init_cfg.static_rx_buf_num = 4;
    init_cfg.dynamic_rx_buf_num = 8;
    init_cfg.dynamic_tx_buf_num = 8;
    init_cfg.cache_tx_buf_num = 0;

    esp_err_t ret = esp_wifi_init(&init_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGE(TAG, "wifi init: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "heap after wifi init: internal=%u dma=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi start: %s", esp_err_to_name(ret));
        return ret;
    }

    s_wifi_started = true;
    return ESP_OK;
}

static void log_wifi_mode(const char *tag)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        ESP_LOGI(TAG, "%s wifi mode=%d", tag ? tag : "wifi", (int)mode);
    }
}

static void register_wifi_handlers(void)
{
    if (s_handlers_registered) return;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_event_handler, NULL);
    s_handlers_registered = true;
}

/* ---- HTTP handlers ------------------------------------------------------ */

static const char HTML_INDEX[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WiFi Setup</title>"
    "<style>"
    "body{background:#111;color:#fff;font-family:sans-serif;max-width:420px;"
         "margin:0 auto;padding:28px 16px;text-align:center}"
    "h2{color:#2196F3;font-size:22px;margin:0 0 24px}"
    "p{margin:10px 0}"
    "input{display:block;width:100%;padding:12px;margin:8px 0;"
          "background:#1e1e1e;border:1px solid #444;border-radius:8px;"
          "color:#fff;font-size:16px;box-sizing:border-box}"
    ".btn{background:#2196F3;color:#fff;border:none;padding:14px;"
          "border-radius:8px;font-size:17px;cursor:pointer;width:100%;margin-top:18px}"
    ".btn:active{background:#1565C0}"
    ".note{color:#888;font-size:13px;margin-top:20px}"
    "</style></head><body>"
    "<h2>&#x1F4F6; WiFi Setup</h2>"
    "<form method='post' action='/connect'>"
    "<p><input name='ssid' type='text' placeholder='WiFi SSID' autocomplete='off'></p>"
    "<p><input name='password' type='password' placeholder='Password (leave blank if open)'></p>"
    "<button class='btn' type='submit'>Connect</button>"
    "</form>"
    "<p class='note'>After submitting, check the device screen for connection status.</p>"
    "</body></html>";

static esp_err_t handler_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, HTML_INDEX, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_connect(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total >= 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
        return ESP_FAIL;
    }
    char body[256] = {0};
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, body + received, total - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return ESP_FAIL;
        received += r;
    }

    char ssid[33] = {0}, pass[65] = {0};
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "password", pass, sizeof(pass));

    if (!ssid[0]) {
        const char *err =
            "<html><head><meta charset='utf-8'></head>"
            "<body style='background:#111;color:#fff;text-align:center;padding:24px;font-family:sans-serif'>"
            "<h2 style='color:#F44336'>&#x274C; Please enter a WiFi SSID</h2>"
            "<a href='/' style='color:#2196F3;font-size:16px'>Go back</a>"
            "</body></html>";
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_send(req, err, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (s_sta_connected) {
        s_disconnecting_for_reprovision = true;
        esp_wifi_disconnect();
    }

    strncpy(s_target_ssid, ssid, sizeof(s_target_ssid) - 1);
    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    strncpy(s_pending_pass, pass, sizeof(s_pending_pass) - 1);
    s_state = WIFI_PROV_STATE_CONNECTING;

    /* Respond before connecting so the page loads */
    char html[768];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Connecting</title>"
        "<style>body{background:#111;color:#fff;font-family:sans-serif;"
        "text-align:center;padding:28px;max-width:420px;margin:0 auto}</style>"
        "</head><body>"
        "<h2 style='color:#FF9800'>&#x23F3; Connecting...</h2>"
        "<p>Connecting to: <b style='color:#2196F3'>%s</b></p>"
        "<p style='color:#888;font-size:14px'>Check the device screen for the connection result.</p>"
        "</body></html>",
        ssid);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);

    /* Switch to APSTA and connect */
    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();

    return ESP_OK;
}

/* Captive portal: redirect everything to the provisioning page */
static esp_err_t handler_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t start_httpd(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 12;

    esp_err_t ret = httpd_start(&s_httpd, &cfg);
    if (ret != ESP_OK) return ret;

    static const httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = handler_index};
    static const httpd_uri_t uri_post = {
        .uri = "/connect", .method = HTTP_POST, .handler = handler_connect};
    /* Android captive portal probes */
    static const httpd_uri_t uri_gen204 = {
        .uri = "/generate_204", .method = HTTP_GET, .handler = handler_redirect};
    static const httpd_uri_t uri_gen204b = {
        .uri = "/gen_204", .method = HTTP_GET, .handler = handler_redirect};
    /* Apple captive portal probe */
    static const httpd_uri_t uri_hotspot = {
        .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handler_redirect};
    /* Windows NCSI */
    static const httpd_uri_t uri_ncsi = {
        .uri = "/ncsi.txt", .method = HTTP_GET, .handler = handler_redirect};
    static const httpd_uri_t uri_connecttest = {
        .uri = "/connecttest.txt", .method = HTTP_GET, .handler = handler_redirect};
    static const httpd_uri_t uri_success = {
        .uri = "/success.txt", .method = HTTP_GET, .handler = handler_redirect};
    static const httpd_uri_t uri_canonical = {
        .uri = "/canonical.html", .method = HTTP_GET, .handler = handler_redirect};
    static const httpd_uri_t uri_redirect = {
        .uri = "/redirect", .method = HTTP_GET, .handler = handler_redirect};

    httpd_register_uri_handler(s_httpd, &uri_index);
    httpd_register_uri_handler(s_httpd, &uri_post);
    httpd_register_uri_handler(s_httpd, &uri_gen204);
    httpd_register_uri_handler(s_httpd, &uri_gen204b);
    httpd_register_uri_handler(s_httpd, &uri_hotspot);
    httpd_register_uri_handler(s_httpd, &uri_ncsi);
    httpd_register_uri_handler(s_httpd, &uri_connecttest);
    httpd_register_uri_handler(s_httpd, &uri_success);
    httpd_register_uri_handler(s_httpd, &uri_canonical);
    httpd_register_uri_handler(s_httpd, &uri_redirect);
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, handler_404);

    return ESP_OK;
}

/* ---- DNS responder ------------------------------------------------------ */

static void dns_task(void *arg)
{
    static const uint8_t AP_IP_BYTES[4] = {192, 168, 4, 1};

    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    bool wdt_registered = (wdt_err == ESP_OK);
    if (!wdt_registered) {
        ESP_LOGW(TAG, "failed to subscribe DNS task to WDT: %s", esp_err_to_name(wdt_err));
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        goto done;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port   = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed: errno %d", errno);
        close(sock);
        goto done;
    }

    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "DNS captive portal running on :53");

    uint8_t buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    while (s_dns_running) {
        if (wdt_registered) {
            esp_task_wdt_reset();
        }

        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&from, &from_len);
        if (len < 12) continue; /* timeout or too short */

        /* Build response header (copy transaction ID + question count) */
        uint8_t resp[512];
        memcpy(resp, buf, 12);
        resp[2]  = 0x81; /* QR=1, Opcode=0, AA=0, TC=0, RD=1 */
        resp[3]  = 0x80; /* RA=1, RCODE=0 */
        resp[6]  = 0x00; /* ANCOUNT high */
        resp[7]  = 0x01; /* ANCOUNT low = 1 */
        resp[8]  = resp[9] = resp[10] = resp[11] = 0;

        /* Walk QNAME to find its end */
        int pos = 12;
        while (pos < len) {
            uint8_t l = buf[pos];
            if (l == 0)           { pos++; break; }
            if ((l & 0xC0) == 0xC0) { pos += 2; break; }
            pos += l + 1;
        }
        int q_len = (pos - 12) + 4; /* QNAME + QTYPE(2) + QCLASS(2) */
        if (12 + q_len > len) continue;

        memcpy(resp + 12, buf + 12, q_len);
        int rp = 12 + q_len;

        /* Answer: name ptr 0xC00C, A record, IN class, TTL 60, IP 192.168.4.1 */
        resp[rp++] = 0xC0; resp[rp++] = 0x0C;
        resp[rp++] = 0x00; resp[rp++] = 0x01;
        resp[rp++] = 0x00; resp[rp++] = 0x01;
        resp[rp++] = 0x00; resp[rp++] = 0x00; resp[rp++] = 0x00; resp[rp++] = 0x3C;
        resp[rp++] = 0x00; resp[rp++] = 0x04;
        resp[rp++] = AP_IP_BYTES[0]; resp[rp++] = AP_IP_BYTES[1];
        resp[rp++] = AP_IP_BYTES[2]; resp[rp++] = AP_IP_BYTES[3];

        sendto(sock, resp, rp, 0, (struct sockaddr *)&from, from_len);
    }

    close(sock);
done:
    ESP_LOGI(TAG, "DNS task exiting");
    if (wdt_registered) {
        esp_task_wdt_delete(NULL);
    }
    if (s_dns_done_sem) xSemaphoreGive(s_dns_done_sem);
    vTaskDelete(NULL);
}

/* ---- WiFi event handler ------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (s_state == WIFI_PROV_STATE_IDLE) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "STA disconnected, reason=%d", d->reason);
        bool was_connected = s_sta_connected;
        s_sta_connected = false;
        memset(s_got_ip, 0, sizeof(s_got_ip));
        if (s_disconnecting_for_reprovision) {
            s_disconnecting_for_reprovision = false;
            ESP_LOGI(TAG, "ignored explicit disconnect for reprovisioning");
            return;
        }
        if (s_sta_reconnect_pending) {
            s_sta_reconnect_pending = false;
            s_state = WIFI_PROV_STATE_CONNECTING;
            ESP_LOGI(TAG, "reconnecting STA");
            esp_wifi_connect();
        } else if (s_state == WIFI_PROV_STATE_CONNECTING && s_pending_ssid[0]) {
            s_state = WIFI_PROV_STATE_FAILED;
            /* Revert to AP-only so the HTTP server stays reachable */
            if (s_portal_running) esp_wifi_set_mode(WIFI_MODE_AP);
        } else if (s_state == WIFI_PROV_STATE_CONNECTING) {
            ESP_LOGI(TAG, "retrying STA connection");
            esp_wifi_connect();
        } else if (was_connected || s_state == WIFI_PROV_STATE_CONNECTED) {
            s_state = WIFI_PROV_STATE_CONNECTING;
            ESP_LOGI(TAG, "reconnecting STA after disconnect");
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        snprintf(s_got_ip, sizeof(s_got_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "Connected, IP: %s", s_got_ip);
        s_sta_connected = true;
        if (s_pending_ssid[0]) {
            save_sta_credentials(s_pending_ssid, s_pending_pass);
            memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
            memset(s_pending_pass, 0, sizeof(s_pending_pass));
        }
        s_state = WIFI_PROV_STATE_CONNECTED;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        if (s_sta_connected || s_state == WIFI_PROV_STATE_CONNECTED) {
            ESP_LOGW(TAG, "STA lost IP, reconnecting to refresh DHCP lease");
            s_sta_connected = false;
            memset(s_got_ip, 0, sizeof(s_got_ip));
            s_state = WIFI_PROV_STATE_CONNECTING;
            s_sta_reconnect_pending = true;
            esp_wifi_disconnect();
        }
    }
}

/* ---- public API --------------------------------------------------------- */

static bool s_starting = false;
static bool s_stopping = false;

esp_err_t wifi_prov_start(void)
{
    if (s_portal_running || s_starting) {
        ESP_LOGW(TAG, "already running or starting (state=%d)", s_state);
        return ESP_OK;
    }
    s_starting = true;

    esp_err_t r = ensure_global_init();
    if (r != ESP_OK) {
        s_starting = false;
        return r;
    }

    ensure_netifs();
    register_wifi_handlers();

    esp_err_t ret = ensure_wifi_driver();
    if (ret != ESP_OK) {
        s_starting = false;
        return ret;
    }

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = WIFI_PROV_AP_SSID,
            .password       = WIFI_PROV_AP_PASS,
            .ssid_len       = sizeof(WIFI_PROV_AP_SSID) - 1,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .max_connection = 4,
            .channel        = 6,
        },
    };

    if (!s_sta_connected) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
    }

    ESP_LOGI(TAG, "starting provisioning AP: ssid=%s sta_connected=%d", WIFI_PROV_AP_SSID, s_sta_connected);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(s_sta_connected ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    log_wifi_mode("after set_mode");
    configure_ap_captive_portal_dhcp();

    /* DNS semaphore */
    if (!s_dns_done_sem) s_dns_done_sem = xSemaphoreCreateBinary();

    /* Start DNS captive portal (reduce stack if possible, 4096 is safe but generous) */
    s_dns_running = true;
    xTaskCreate(dns_task, "wifi_prov_dns", 3072, NULL, 5, &s_dns_task_handle);

    /* Start HTTP server */
    ret = start_httpd();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd start: %s", esp_err_to_name(ret));
        s_starting = false;
        return ret;
    }

    s_state = WIFI_PROV_STATE_AP_ACTIVE;
    s_portal_running = true;
    s_starting = false;
    ESP_LOGI(TAG, "AP ready: SSID=%s IP=%s", WIFI_PROV_AP_SSID, WIFI_PROV_AP_IP);
    return ESP_OK;
}

esp_err_t wifi_prov_stop(void)
{
    if ((!s_portal_running && s_state != WIFI_PROV_STATE_FAILED) || s_stopping) return ESP_OK;
    s_stopping = true;

    /* Stop HTTP server */
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    /* Stop DNS task */
    s_dns_running = false;
    if (s_dns_task_handle && s_dns_done_sem) {
        xSemaphoreTake(s_dns_done_sem, pdMS_TO_TICKS(1000));
        s_dns_task_handle = NULL;
    }

    s_portal_running = false;
    memset(s_target_ssid, 0, sizeof(s_target_ssid));

    if (s_sta_connected) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        s_state = WIFI_PROV_STATE_CONNECTED;
    } else if (s_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_started = false;
        if (s_handlers_registered) {
            esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
            esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler);
            esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_event_handler);
            s_handlers_registered = false;
        }
        s_state = WIFI_PROV_STATE_IDLE;
        memset(s_got_ip, 0, sizeof(s_got_ip));
    }

    s_stopping = false;
    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}

esp_err_t wifi_prov_auto_connect_saved(void)
{
    if (s_state != WIFI_PROV_STATE_IDLE || s_starting || s_wifi_started) {
        return ESP_OK;
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    if (!load_sta_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "no saved WiFi credentials");
        return ESP_ERR_NOT_FOUND;
    }

    s_starting = true;
    esp_err_t ret = ensure_global_init();
    if (ret != ESP_OK) {
        s_starting = false;
        return ret;
    }

    ensure_netifs();
    register_wifi_handlers();

    ret = ensure_wifi_driver();
    if (ret != ESP_OK) {
        s_starting = false;
        return ret;
    }

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);
    strncpy(s_target_ssid, ssid, sizeof(s_target_ssid) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    ret = esp_wifi_connect();
    if (ret == ESP_OK) {
        s_state = WIFI_PROV_STATE_CONNECTING;
        ESP_LOGI(TAG, "auto connecting to saved SSID=%s", ssid);
    } else {
        ESP_LOGW(TAG, "auto connect failed: %s", esp_err_to_name(ret));
        s_state = WIFI_PROV_STATE_FAILED;
    }

    s_starting = false;
    return ret;
}


void wifi_prov_get_status(wifi_prov_status_t *out)
{
    out->state = s_state;
    strncpy(out->target_ssid, s_target_ssid, sizeof(out->target_ssid) - 1);
    out->target_ssid[sizeof(out->target_ssid) - 1] = '\0';
    strncpy(out->got_ip, s_got_ip, sizeof(out->got_ip) - 1);
    out->got_ip[sizeof(out->got_ip) - 1] = '\0';
}
