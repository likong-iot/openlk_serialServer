/*
 * HTTP client — outbound only (esp_http_client).
 *
 * protocol_send() → performs an HTTP request (GET or POST per config) to
 * CFG_KEY_HTTP_URL with `data` as the body.  The response body is delivered
 * back up via protocol_notify_rx.  No OTA, no retry policy.
 *
 * The request runs on the caller's task — callers rate-limit themselves.
 */

#include "protocol_service_internal.h"
#include "config_service.h"

#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_http_client.h"

static const char *TAG = "http";

typedef struct {
    char url[128];
    char method[8];
    int  timeout_ms;
    bool running;
} http_state_t;

typedef struct {
    protocol_handle_t *h;
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} rx_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *ev)
{
    rx_ctx_t *ctx = ev->user_data;
    if (ev->event_id != HTTP_EVENT_ON_DATA || !ev->data || ev->data_len <= 0) return ESP_OK;

    /* Grow-once buffer; protocol_notify_rx is called once at ON_FINISH. */
    size_t need = ctx->len + ev->data_len;
    if (need > ctx->cap) {
        size_t newcap = ctx->cap ? ctx->cap * 2 : 256;
        while (newcap < need) newcap *= 2;
        uint8_t *nb = realloc(ctx->buf, newcap);
        if (!nb) return ESP_ERR_NO_MEM;
        ctx->buf = nb; ctx->cap = newcap;
    }
    memcpy(ctx->buf + ctx->len, ev->data, ev->data_len);
    ctx->len += ev->data_len;
    return ESP_OK;
}

static esp_err_t http_start(protocol_handle_t *h)
{
    http_state_t *st = h->impl;

    config_get_str(CFG_KEY_HTTP_URL,        st->url,    sizeof(st->url),    "");
    config_get_str(CFG_KEY_HTTP_METHOD,     st->method, sizeof(st->method), "POST");
    int32_t v; config_get_int(CFG_KEY_HTTP_TIMEOUT_MS, &v, 5000);
    st->timeout_ms = (int)v;

    if (!st->url[0]) return ESP_ERR_INVALID_STATE;

    st->running = true;
    protocol_notify_state(h, PROTOCOL_STATE_CONNECTED);  /* logically "ready" */
    return ESP_OK;
}

static esp_err_t http_stop(protocol_handle_t *h)
{
    http_state_t *st = h->impl;
    st->running = false;
    protocol_notify_state(h, PROTOCOL_STATE_STOPPED);
    return ESP_OK;
}

static esp_err_t http_send(protocol_handle_t *h, const uint8_t *data, size_t len)
{
    http_state_t *st = h->impl;
    if (!st->running) return ESP_ERR_INVALID_STATE;

    rx_ctx_t ctx = { .h = h };

    esp_http_client_config_t cfg = {
        .url           = st->url,
        .method        = strcasecmp(st->method, "GET") == 0 ? HTTP_METHOD_GET : HTTP_METHOD_POST,
        .timeout_ms    = st->timeout_ms,
        .event_handler = on_http_event,
        .user_data     = &ctx,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;

    esp_err_t err = ESP_OK;
    if (cfg.method == HTTP_METHOD_POST) {
        esp_http_client_set_header(c, "Content-Type", "application/octet-stream");
        esp_http_client_set_post_field(c, (const char *)data, (int)len);
    }

    err = esp_http_client_perform(c);
    int status = err == ESP_OK ? esp_http_client_get_status_code(c) : 0;
    esp_http_client_cleanup(c);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "perform: %s", esp_err_to_name(err));
        free(ctx.buf);
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "HTTP %d", status);
        free(ctx.buf);
        return ESP_FAIL;
    }
    if (ctx.buf && ctx.len) protocol_notify_rx(h, ctx.buf, ctx.len);
    free(ctx.buf);
    return ESP_OK;
}

static void http_destroy(protocol_handle_t *h)
{
    http_state_t *st = h->impl;
    if (st) free(st);
    free(h);
}

static const protocol_vtable_t VT = {
    .start = http_start, .stop = http_stop, .send = http_send, .destroy = http_destroy,
};

protocol_handle_t *protocol_http_create(void)
{
    protocol_handle_t *h = calloc(1, sizeof(*h));
    http_state_t      *s = calloc(1, sizeof(*s));
    if (!h || !s) { free(h); free(s); return NULL; }
    protocol_base_init(h, &VT, "http", s);
    return h;
}
