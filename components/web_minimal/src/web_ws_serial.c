#include "web_internal.h"
#include "serial_service.h"
#include "auth_service.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web_ws_serial";

#define MAX_WS_CLIENTS 4

static httpd_handle_t    s_server;
static int               s_fds[MAX_WS_CLIENTS];
static SemaphoreHandle_t s_lock;

static void add_client(int fd)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_fds[i] == 0) { s_fds[i] = fd; break; }
    }
    xSemaphoreGive(s_lock);
}

static void remove_client(int fd)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_fds[i] == fd) { s_fds[i] = 0; break; }
    }
    xSemaphoreGive(s_lock);
}

/* Build a JSON frame: {"dir":"rx","ts":N,"fmt":"hex","data":"AA BB"} */
static char *make_rx_frame(const uint8_t *data, size_t len, size_t *out_len)
{
    /* Each byte as "XX " → 3 chars. +overhead */
    size_t cap = 64 + len * 3;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    int64_t ts = esp_timer_get_time() / 1000;  /* microseconds → ms */
    int n = snprintf(buf, cap,
                     "{\"dir\":\"rx\",\"ts\":%lld,\"fmt\":\"hex\",\"data\":\"",
                     (long long)ts);
    for (size_t i = 0; i < len && n + 4 < (int)cap; ++i) {
        n += snprintf(buf + n, cap - n, i ? " %02X" : "%02X", data[i]);
    }
    n += snprintf(buf + n, cap - n, "\"}");
    if (out_len) *out_len = (size_t)n;
    return buf;
}

static void broadcast_frame(const char *frame, size_t len)
{
    if (!s_server) return;
    httpd_ws_frame_t ws = {
        .final   = true,
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)frame,
        .len     = len,
    };
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        int fd = s_fds[i];
        if (fd <= 0) continue;
        esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &ws);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ws_send fd=%d: %s", fd, esp_err_to_name(err));
            s_fds[i] = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

/* Called from the serial rx task. Must be quick. */
static void on_serial_rx(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    size_t fl = 0;
    char *f = make_rx_frame(data, len, &fl);
    if (!f) return;
    broadcast_frame(f, fl);
    free(f);
}

void web_ws_serial_push_rx(const uint8_t *data, size_t len)
{
    on_serial_rx(data, len, NULL);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Handshake phase. Browsers can't set Authorization on WS, so the
         * page passes ?token=... in the URL. Validate before completing. */
        size_t qlen = httpd_req_get_url_query_len(req);
        bool ok = false;
        if (qlen > 0 && qlen < 256) {
            char q[256];
            if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
                char tok[AUTH_TOKEN_BUF];
                if (httpd_query_key_value(q, "token", tok, sizeof(tok)) == ESP_OK) {
                    ok = auth_service_session_valid(tok);
                }
            }
        }
        if (!ok) {
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_send(req, NULL, 0);
            return ESP_FAIL;
        }
        s_server = req->handle;
        add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t ws = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &ws, 0);
    if (err != ESP_OK) return err;

    if (ws.type == HTTPD_WS_TYPE_CLOSE) {
        remove_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    if (ws.len == 0) return ESP_OK;
    ws.payload = malloc(ws.len + 1);
    if (!ws.payload) return ESP_ERR_NO_MEM;
    err = httpd_ws_recv_frame(req, &ws, ws.len);
    if (err != ESP_OK) { free(ws.payload); return err; }
    ws.payload[ws.len] = 0;

    /* Client → serial: simple text passthrough (optional; real parsing uses REST API). */
    /* Ignored here to keep the debug contract one-way; REST /api/serial/send covers TX. */
    free(ws.payload);
    return ESP_OK;
}

esp_err_t web_register_serial_ws(httpd_handle_t server)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    static const httpd_uri_t u = {
        .uri         = "/ws/serial",
        .method      = HTTP_GET,
        .handler     = ws_handler,
        .user_ctx    = NULL,
        .is_websocket = true,
    };
    esp_err_t err = httpd_register_uri_handler(server, &u);
    if (err != ESP_OK) return err;

    /* Bridge serial rx → ws broadcast. */
    err = serial_service_register_rx_cb(on_serial_rx, NULL);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
