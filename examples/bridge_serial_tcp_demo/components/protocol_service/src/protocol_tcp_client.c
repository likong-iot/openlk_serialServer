/*
 * Real TCP client implementation — persistent connection with exponential
 * reconnect. Intentionally minimal; serves as a reference for how to replace
 * a protocol stub with real IO.
 *
 * Reads configuration via config_service on start(); runtime changes require
 * a stop() / start() cycle. Business policy (how/when to send) belongs in a
 * caller module, not here.
 */

#include "protocol_service_internal.h"
#include "config_service.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "tcp_cli";

#define RX_CHUNK    1024
#define RECONN_MAX  60000

typedef struct {
    char     host[64];
    int      port;
    int      reconn_ms;

    int              sock;
    TaskHandle_t     task;
    SemaphoreHandle_t lock;   /* guards sock for concurrent send/close */
    bool             stop;
} tcp_state_t;

static int connect_once(const char *host, int port)
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        ESP_LOGW(TAG, "getaddrinfo(%s) failed", host);
        return -1;
    }
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) { freeaddrinfo(res); return -1; }

    /* 5-second connect timeout via setsockopt(SO_SNDTIMEO). */
    struct timeval tv = { .tv_sec = 5 };
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGW(TAG, "connect %s:%d failed: errno %d", host, port, errno);
        close(s); freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    /* Once connected, blocking reads are fine — rx task parks here. */
    tv.tv_sec = 0;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int keep = 1;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &keep, sizeof(keep));
    return s;
}

static void run(void *arg)
{
    protocol_handle_t *h = arg;
    tcp_state_t *st = h->impl;

    int backoff = st->reconn_ms;

    while (!st->stop) {
        protocol_notify_state(h, PROTOCOL_STATE_STARTING);
        int s = connect_once(st->host, st->port);
        if (s < 0) {
            protocol_notify_state(h, PROTOCOL_STATE_DISCONNECTED);
            vTaskDelay(pdMS_TO_TICKS(backoff));
            backoff = (backoff * 2 > RECONN_MAX) ? RECONN_MAX : backoff * 2;
            continue;
        }

        xSemaphoreTake(st->lock, portMAX_DELAY);
        st->sock = s;
        xSemaphoreGive(st->lock);

        protocol_notify_state(h, PROTOCOL_STATE_CONNECTED);
        backoff = st->reconn_ms;

        uint8_t buf[RX_CHUNK];
        while (!st->stop) {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            protocol_notify_rx(h, buf, (size_t)n);
        }

        xSemaphoreTake(st->lock, portMAX_DELAY);
        st->sock = -1;
        close(s);
        xSemaphoreGive(st->lock);

        protocol_notify_state(h, PROTOCOL_STATE_DISCONNECTED);
    }

    protocol_notify_state(h, PROTOCOL_STATE_STOPPED);
    st->task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t tcp_start(protocol_handle_t *h)
{
    tcp_state_t *st = h->impl;
    if (st->task) return ESP_OK;

    config_get_str(CFG_KEY_TCP_HOST,      st->host, sizeof(st->host), "");
    int32_t v;
    config_get_int(CFG_KEY_TCP_PORT,      &v, 8080);  st->port      = (int)v;
    config_get_int(CFG_KEY_TCP_RECONN_MS, &v, 2000);  st->reconn_ms = (int)v;

    if (!st->host[0]) {
        ESP_LOGW(TAG, "no host configured");
        return ESP_ERR_INVALID_STATE;
    }

    st->stop = false;
    BaseType_t ok = xTaskCreate(run, "tcp_cli", 4096, h, 5, &st->task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t tcp_stop(protocol_handle_t *h)
{
    tcp_state_t *st = h->impl;
    st->stop = true;
    xSemaphoreTake(st->lock, portMAX_DELAY);
    if (st->sock >= 0) { shutdown(st->sock, SHUT_RDWR); close(st->sock); st->sock = -1; }
    xSemaphoreGive(st->lock);
    /* Task observes st->stop and exits on its own. */
    return ESP_OK;
}

static esp_err_t tcp_send(protocol_handle_t *h, const uint8_t *data, size_t len)
{
    tcp_state_t *st = h->impl;
    esp_err_t err = ESP_FAIL;
    xSemaphoreTake(st->lock, portMAX_DELAY);
    if (st->sock >= 0) {
        int n = send(st->sock, data, len, 0);
        err = (n == (int)len) ? ESP_OK : ESP_FAIL;
    } else {
        err = ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(st->lock);
    return err;
}

static void tcp_destroy(protocol_handle_t *h)
{
    tcp_state_t *st = h->impl;
    if (st) {
        tcp_stop(h);
        if (st->lock) vSemaphoreDelete(st->lock);
        free(st);
    }
    free(h);
}

static const protocol_vtable_t VT = {
    .start = tcp_start, .stop = tcp_stop, .send = tcp_send, .destroy = tcp_destroy,
};

protocol_handle_t *protocol_tcp_client_create(void)
{
    protocol_handle_t *h = calloc(1, sizeof(*h));
    tcp_state_t       *s = calloc(1, sizeof(*s));
    if (!h || !s) { free(h); free(s); return NULL; }
    s->sock = -1;
    s->lock = xSemaphoreCreateMutex();
    if (!s->lock) { free(h); free(s); return NULL; }
    protocol_base_init(h, &VT, "tcp_client", s);
    return h;
}
