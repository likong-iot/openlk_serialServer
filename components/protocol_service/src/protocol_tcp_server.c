/*
 * Real TCP server — listens on a configurable port, accepts up to N clients,
 * forwards any received bytes upstream via protocol_notify_rx, and broadcasts
 * protocol_send() to every connected client.
 *
 * No serial bridging, no framing — bridging a peer to the UART (business)
 * belongs in a separate component that consumes this one.
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

static const char *TAG = "tcp_srv";

#define RX_CHUNK     1024
#define MAX_CLIENTS  8

typedef struct {
    int port;
    int max_clients;

    int          listen_sock;
    int          clients[MAX_CLIENTS];
    TaskHandle_t task;
    SemaphoreHandle_t lock;
    bool         stop;
} tcps_state_t;

static void add_client(tcps_state_t *st, int fd)
{
    xSemaphoreTake(st->lock, portMAX_DELAY);
    for (int i = 0; i < st->max_clients; ++i) {
        if (st->clients[i] < 0) { st->clients[i] = fd; break; }
    }
    xSemaphoreGive(st->lock);
}

static void drop_client(tcps_state_t *st, int fd)
{
    xSemaphoreTake(st->lock, portMAX_DELAY);
    for (int i = 0; i < st->max_clients; ++i) {
        if (st->clients[i] == fd) { st->clients[i] = -1; break; }
    }
    xSemaphoreGive(st->lock);
    close(fd);
}

typedef struct {
    protocol_handle_t *h;
    int                fd;
} client_arg_t;

static void client_task(void *arg)
{
    client_arg_t *ca = arg;
    protocol_handle_t *h = ca->h;
    tcps_state_t *st = h->impl;
    int fd = ca->fd;
    free(ca);

    uint8_t buf[RX_CHUNK];
    while (!st->stop) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        protocol_notify_rx(h, buf, (size_t)n);
    }
    drop_client(st, fd);
    vTaskDelete(NULL);
}

static void accept_task(void *arg)
{
    protocol_handle_t *h  = arg;
    tcps_state_t      *st = h->impl;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { protocol_notify_state(h, PROTOCOL_STATE_ERROR); vTaskDelete(NULL); return; }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port   = htons(st->port),
    };
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(s, st->max_clients) != 0) {
        ESP_LOGE(TAG, "bind/listen %d failed: errno %d", st->port, errno);
        close(s);
        protocol_notify_state(h, PROTOCOL_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    st->listen_sock = s;
    protocol_notify_state(h, PROTOCOL_STATE_CONNECTED);
    ESP_LOGI(TAG, "listening on :%d", st->port);

    while (!st->stop) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(s, (struct sockaddr *)&ca, &cl);
        if (c < 0) { if (!st->stop) vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        if (st->max_clients <= 0) { close(c); continue; }
        add_client(st, c);
        client_arg_t *carg = malloc(sizeof(*carg));
        carg->h = h; carg->fd = c;
        xTaskCreate(client_task, "tcps_rx", 3072, carg, 4, NULL);
    }

    /* Cleanup. */
    xSemaphoreTake(st->lock, portMAX_DELAY);
    for (int i = 0; i < st->max_clients; ++i) {
        if (st->clients[i] >= 0) { close(st->clients[i]); st->clients[i] = -1; }
    }
    close(st->listen_sock);
    st->listen_sock = -1;
    xSemaphoreGive(st->lock);

    protocol_notify_state(h, PROTOCOL_STATE_STOPPED);
    st->task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t tcps_start(protocol_handle_t *h)
{
    tcps_state_t *st = h->impl;
    if (st->task) return ESP_OK;

    int32_t v;
    config_get_int(CFG_KEY_TCPS_PORT,        &v, 8080); st->port        = (int)v;
    config_get_int(CFG_KEY_TCPS_MAX_CLIENTS, &v, 4);    st->max_clients = (int)v;
    if (st->max_clients > MAX_CLIENTS) st->max_clients = MAX_CLIENTS;

    for (int i = 0; i < MAX_CLIENTS; ++i) st->clients[i] = -1;
    st->stop = false;
    protocol_notify_state(h, PROTOCOL_STATE_STARTING);
    BaseType_t ok = xTaskCreate(accept_task, "tcps_acc", 3072, h, 5, &st->task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t tcps_stop(protocol_handle_t *h)
{
    tcps_state_t *st = h->impl;
    st->stop = true;
    /* Unblock accept by closing the listen sock. */
    if (st->listen_sock >= 0) shutdown(st->listen_sock, SHUT_RDWR);
    return ESP_OK;
}

static esp_err_t tcps_send(protocol_handle_t *h, const uint8_t *data, size_t len)
{
    tcps_state_t *st = h->impl;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    xSemaphoreTake(st->lock, portMAX_DELAY);
    for (int i = 0; i < st->max_clients; ++i) {
        if (st->clients[i] < 0) continue;
        int n = send(st->clients[i], data, len, 0);
        if (n == (int)len) err = ESP_OK;
    }
    xSemaphoreGive(st->lock);
    return err;
}

static void tcps_destroy(protocol_handle_t *h)
{
    tcps_state_t *st = h->impl;
    if (st) {
        tcps_stop(h);
        if (st->lock) vSemaphoreDelete(st->lock);
        free(st);
    }
    free(h);
}

static const protocol_vtable_t VT = {
    .start = tcps_start, .stop = tcps_stop, .send = tcps_send, .destroy = tcps_destroy,
};

protocol_handle_t *protocol_tcp_server_create(void)
{
    protocol_handle_t *h = calloc(1, sizeof(*h));
    tcps_state_t      *s = calloc(1, sizeof(*s));
    if (!h || !s) { free(h); free(s); return NULL; }
    s->listen_sock = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) s->clients[i] = -1;
    s->lock = xSemaphoreCreateMutex();
    if (!s->lock) { free(h); free(s); return NULL; }
    protocol_base_init(h, &VT, "tcp_server", s);
    return h;
}
