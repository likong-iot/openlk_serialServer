/*
 * Real UDP implementation.
 *
 *   - Binds a socket to CFG_KEY_UDP_LOCAL_PORT.
 *   - Datagrams received on that socket are forwarded to protocol_notify_rx;
 *     the sender address is cached as the "last peer".
 *   - protocol_send() first tries the configured remote (host:port); if
 *     unset, falls back to the last peer.
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

static const char *TAG = "udp";

#define RX_CHUNK 1500

typedef struct {
    int  local_port;
    char remote_host[64];
    int  remote_port;

    int               sock;
    struct sockaddr_in last_peer;   /* captured on recv */
    bool               has_last_peer;
    SemaphoreHandle_t  lock;
    TaskHandle_t       task;
    bool               stop;
} udp_state_t;

static void run(void *arg)
{
    protocol_handle_t *h  = arg;
    udp_state_t       *st = h->impl;

    uint8_t buf[RX_CHUNK];
    while (!st->stop) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int n = recvfrom(st->sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&peer, &plen);
        if (n <= 0) { if (!st->stop) vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        xSemaphoreTake(st->lock, portMAX_DELAY);
        st->last_peer     = peer;
        st->has_last_peer = true;
        xSemaphoreGive(st->lock);

        protocol_notify_rx(h, buf, (size_t)n);
    }
    st->task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t udp_start(protocol_handle_t *h)
{
    udp_state_t *st = h->impl;
    if (st->task) return ESP_OK;

    int32_t v;
    config_get_int(CFG_KEY_UDP_LOCAL_PORT,  &v, 9000); st->local_port  = (int)v;
    config_get_int(CFG_KEY_UDP_REMOTE_PORT, &v, 9001); st->remote_port = (int)v;
    config_get_str(CFG_KEY_UDP_REMOTE_HOST, st->remote_host, sizeof(st->remote_host), "");

    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return ESP_FAIL;

    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port   = htons(st->local_port),
    };
    if (bind(s, (struct sockaddr *)&local, sizeof(local)) != 0) {
        ESP_LOGE(TAG, "bind :%d failed: errno %d", st->local_port, errno);
        close(s);
        return ESP_FAIL;
    }

    /* Short recv timeout so stop flips without needing to send junk. */
    struct timeval tv = { .tv_sec = 1 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    st->sock = s;
    st->stop = false;
    BaseType_t ok = xTaskCreate(run, "udp_rx", 3072, h, 5, &st->task);
    if (ok != pdPASS) { close(s); st->sock = -1; return ESP_ERR_NO_MEM; }

    protocol_notify_state(h, PROTOCOL_STATE_CONNECTED);
    return ESP_OK;
}

static esp_err_t udp_stop(protocol_handle_t *h)
{
    udp_state_t *st = h->impl;
    st->stop = true;
    if (st->sock >= 0) { close(st->sock); st->sock = -1; }
    protocol_notify_state(h, PROTOCOL_STATE_STOPPED);
    return ESP_OK;
}

static esp_err_t udp_send(protocol_handle_t *h, const uint8_t *data, size_t len)
{
    udp_state_t *st = h->impl;
    if (st->sock < 0) return ESP_ERR_INVALID_STATE;

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;

    if (st->remote_host[0]) {
        /* Resolve once per send — simple, OK for low-rate use. */
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
        struct addrinfo *res = NULL;
        char port[8]; snprintf(port, sizeof(port), "%d", st->remote_port);
        if (getaddrinfo(st->remote_host, port, &hints, &res) != 0 || !res) {
            return ESP_FAIL;
        }
        memcpy(&dst, res->ai_addr, sizeof(dst));
        freeaddrinfo(res);
    } else {
        xSemaphoreTake(st->lock, portMAX_DELAY);
        bool ok = st->has_last_peer;
        if (ok) dst = st->last_peer;
        xSemaphoreGive(st->lock);
        if (!ok) return ESP_ERR_INVALID_STATE;
    }

    int n = sendto(st->sock, data, len, 0,
                   (struct sockaddr *)&dst, sizeof(dst));
    return n == (int)len ? ESP_OK : ESP_FAIL;
}

static void udp_destroy(protocol_handle_t *h)
{
    udp_state_t *st = h->impl;
    if (st) {
        udp_stop(h);
        if (st->lock) vSemaphoreDelete(st->lock);
        free(st);
    }
    free(h);
}

static const protocol_vtable_t VT = {
    .start = udp_start, .stop = udp_stop, .send = udp_send, .destroy = udp_destroy,
};

protocol_handle_t *protocol_udp_create(void)
{
    protocol_handle_t *h = calloc(1, sizeof(*h));
    udp_state_t       *s = calloc(1, sizeof(*s));
    if (!h || !s) { free(h); free(s); return NULL; }
    s->sock = -1;
    s->lock = xSemaphoreCreateMutex();
    if (!s->lock) { free(h); free(s); return NULL; }
    protocol_base_init(h, &VT, "udp", s);
    return h;
}
