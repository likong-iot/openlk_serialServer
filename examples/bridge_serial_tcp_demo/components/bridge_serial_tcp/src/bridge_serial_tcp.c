#include "bridge_serial_tcp.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "serial_service.h"
#include "protocol_service.h"

#define BRIDGE_QUEUE_LEN   16
#define BRIDGE_FRAME_MAX   512
#define BRIDGE_TX_TASK_STK 4096
#define BRIDGE_TX_TASK_PRIO 8

typedef struct {
    size_t  len;
    uint8_t buf[BRIDGE_FRAME_MAX];
} frame_t;

static const char *TAG = "bridge_s2t";

static QueueHandle_t      s_q;
static TaskHandle_t       s_task;
static protocol_handle_t *s_tcp;
static volatile bool      s_running;
static uint32_t           s_drop_cnt;

static void on_serial_rx(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    if (!s_running || !s_q || !data || len == 0) return;

    frame_t f = {0};
    f.len = len > sizeof(f.buf) ? sizeof(f.buf) : len;
    memcpy(f.buf, data, f.len);

    /* Keep callback non-blocking. */
    if (xQueueSend(s_q, &f, 0) != pdTRUE) {
        s_drop_cnt++;
    }
}

static void on_tcp_rx(protocol_handle_t *h, const uint8_t *data, size_t len, void *user)
{
    (void)h;
    (void)user;
    if (!s_running || !data || len == 0) return;

    esp_err_t err = serial_service_send(data, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "serial_service_send: %s", esp_err_to_name(err));
    }
}

static void on_tcp_state(protocol_handle_t *h, protocol_state_t st, void *user)
{
    (void)h;
    (void)user;
    ESP_LOGI(TAG, "tcp state=%d", (int)st);
}

static void tx_task(void *arg)
{
    (void)arg;

    frame_t f;
    for (;;) {
        if (xQueueReceive(s_q, &f, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (s_running) {
                esp_err_t err = protocol_send(s_tcp, f.buf, f.len);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "protocol_send: %s", esp_err_to_name(err));
                }
            }
            continue;
        }
        if (!s_running) break;
    }

    if (s_drop_cnt) {
        ESP_LOGW(TAG, "dropped frames=%lu", (unsigned long)s_drop_cnt);
        s_drop_cnt = 0;
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_worker(void)
{
    BaseType_t ok = xTaskCreate(tx_task, "bridge_tx", BRIDGE_TX_TASK_STK, NULL,
                                BRIDGE_TX_TASK_PRIO, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void stop_worker(void)
{
    if (!s_task) return;

    /* Wait for self-exit to avoid deleting queue while task is waiting on it. */
    for (int i = 0; i < 50 && s_task; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_task) {
        ESP_LOGW(TAG, "tx task stop timeout, force delete");
        vTaskDelete(s_task);
        s_task = NULL;
    }
}

esp_err_t bridge_serial_tcp_start(void)
{
    if (s_running) return ESP_OK;

    s_tcp = protocol_tcp_client_create();
    if (!s_tcp) return ESP_ERR_NO_MEM;

    s_q = xQueueCreate(BRIDGE_QUEUE_LEN, sizeof(frame_t));
    if (!s_q) {
        protocol_destroy(s_tcp);
        s_tcp = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = protocol_register_rx_cb(s_tcp, on_tcp_rx, NULL);
    if (err != ESP_OK) goto fail;

    err = protocol_register_event_cb(s_tcp, on_tcp_state, NULL);
    if (err != ESP_OK) goto fail;

    err = serial_service_register_rx_cb(on_serial_rx, NULL);
    if (err != ESP_OK) goto fail;

    err = protocol_start(s_tcp);
    if (err != ESP_OK) goto fail_unreg_serial;

    s_running = true;
    err = start_worker();
    if (err != ESP_OK) goto fail_stop_proto;

    ESP_LOGI(TAG, "bridge started");
    return ESP_OK;

fail_stop_proto:
    s_running = false;
    protocol_stop(s_tcp);
fail_unreg_serial:
    serial_service_unregister_rx_cb(on_serial_rx);
fail:
    protocol_destroy(s_tcp);
    s_tcp = NULL;
    if (s_q) {
        vQueueDelete(s_q);
        s_q = NULL;
    }
    return err;
}

esp_err_t bridge_serial_tcp_stop(void)
{
    if (!s_running) return ESP_OK;

    s_running = false;

    serial_service_unregister_rx_cb(on_serial_rx);
    stop_worker();

    protocol_stop(s_tcp);
    protocol_destroy(s_tcp);
    s_tcp = NULL;

    if (s_q) {
        vQueueDelete(s_q);
        s_q = NULL;
    }

    ESP_LOGI(TAG, "bridge stopped");
    return ESP_OK;
}
