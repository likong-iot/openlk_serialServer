#include "serial_service.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "config_service.h"
#include "hal_uart.h"

static const char *TAG = "serial_service";

/* Board defaults — match legacy SP501LW pinout (UART2 + RS-485 DIR on GPIO2). */
#ifndef SERIAL_SERVICE_UART_PORT
#define SERIAL_SERVICE_UART_PORT   2
#endif
#ifndef SERIAL_SERVICE_UART_TX
#define SERIAL_SERVICE_UART_TX     17
#endif
#ifndef SERIAL_SERVICE_UART_RX
#define SERIAL_SERVICE_UART_RX     16
#endif
#ifndef SERIAL_SERVICE_UART_RTS
#define SERIAL_SERVICE_UART_RTS    2
#endif
#ifndef SERIAL_SERVICE_UART_CTS
#define SERIAL_SERVICE_UART_CTS    (-1)
#endif
#ifndef SERIAL_SERVICE_RX_BUF
#define SERIAL_SERVICE_RX_BUF      4096
#endif
#ifndef SERIAL_SERVICE_TX_BUF
#define SERIAL_SERVICE_TX_BUF      2048
#endif

#define MAX_RX_CB  4

typedef struct {
    serial_rx_cb_t cb;
    void          *user;
} rx_slot_t;

typedef struct {
    bool               inited;
    bool               running;
    serial_config_t    cfg;
    serial_status_t    status;
    SemaphoreHandle_t  lock;
    TaskHandle_t       rx_task;
    rx_slot_t          cbs[MAX_RX_CB];
} state_t;

static state_t s;

static void load_config_from_nvs(serial_config_t *cfg)
{
    int32_t v;
    bool b;
    config_get_int(CFG_KEY_SER_BAUD,       &v, 115200); cfg->baud_rate = (uint32_t)v;
    config_get_int(CFG_KEY_SER_DATA_BITS,  &v, 8);      cfg->data_bits = (uint8_t)v;
    config_get_int(CFG_KEY_SER_STOP_BITS,  &v, SERIAL_STOP_BITS_1); cfg->stop_bits = (serial_stop_bits_t)v;
    config_get_int(CFG_KEY_SER_PARITY,     &v, SERIAL_PARITY_NONE); cfg->parity    = (serial_parity_t)v;
    config_get_bool(CFG_KEY_SER_FLOW_CTRL, &b, false);  cfg->flow_ctrl = b;
    config_get_bool(CFG_KEY_SER_RS485,     &b, false);  cfg->rs485_half_duplex = b;
    config_get_int(CFG_KEY_SER_FRAME_GAP,  &v, 0);      cfg->frame_gap_ms = (uint16_t)v;
}

static uint8_t parity_to_hal(serial_parity_t p)
{
    /* esp-idf uart_parity_t: NONE=0, EVEN=2, ODD=3 */
    switch (p) {
    case SERIAL_PARITY_EVEN: return 2;
    case SERIAL_PARITY_ODD:  return 3;
    default:                 return 0;
    }
}

static void dispatch_frame(const uint8_t *data, size_t len)
{
    s.status.rx_bytes += len;

    /* Snapshot so we don't hold the lock across user callbacks. */
    rx_slot_t snap[MAX_RX_CB];
    xSemaphoreTake(s.lock, portMAX_DELAY);
    memcpy(snap, s.cbs, sizeof(snap));
    xSemaphoreGive(s.lock);

    for (int i = 0; i < MAX_RX_CB; ++i) {
        if (snap[i].cb) snap[i].cb(data, len, snap[i].user);
    }
}

/* When the caller didn't pick an explicit frame_gap_ms, derive one of roughly
 * 3.5 character times (Modbus convention), floored at 3ms so slow-baud links
 * don't thrash, capped at 50ms so fast-baud links don't stall. */
static uint32_t resolve_gap_ms(const serial_config_t *cfg)
{
    if (cfg->frame_gap_ms) return cfg->frame_gap_ms;
    uint8_t bits_per_char = 1 + cfg->data_bits
        + (cfg->stop_bits == SERIAL_STOP_BITS_1 ? 1 : 2)
        + (cfg->parity   != SERIAL_PARITY_NONE  ? 1 : 0);
    uint32_t baud = cfg->baud_rate ? cfg->baud_rate : 9600;
    uint32_t ms = (bits_per_char * 1000U * 4U) / baud + 1U;
    if (ms < 3)  ms = 3;
    if (ms > 50) ms = 50;
    return ms;
}

/*
 * Frame-gap rx aggregator.
 *
 *   - Bytes are accumulated into a scratch buffer with a short UART read.
 *   - A frame is considered complete once the inter-byte silence exceeds
 *     `resolve_gap_ms(cfg)`, or the buffer fills.
 *   - Empty idle periods are cheap: the UART read returns 0 after its timeout.
 *
 * Without this aggregation a 20-byte Modbus reply can be split into 3+ rx
 * callbacks, breaking the upper-layer framing.
 */
static void rx_task_fn(void *arg)
{
    (void)arg;
    uint8_t *buf = malloc(SERIAL_SERVICE_RX_BUF);
    if (!buf) { ESP_LOGE(TAG, "rx buf alloc failed"); vTaskDelete(NULL); return; }

    size_t     off     = 0;
    TickType_t last_rx = xTaskGetTickCount();

    while (s.running) {
        uint32_t gap_ms = resolve_gap_ms(&s.cfg);

        size_t n = 0;
        esp_err_t err = hal_uart_read(SERIAL_SERVICE_UART_PORT,
                                      buf + off,
                                      SERIAL_SERVICE_RX_BUF - off,
                                      /*timeout_ms=*/5, &n);
        if (err != ESP_OK) {
            s.status.rx_errors++;
            s.status.last_error = err;
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if (n > 0) {
            off += n;
            last_rx = now;
            if (off >= SERIAL_SERVICE_RX_BUF) {
                dispatch_frame(buf, off);
                off = 0;
            }
        } else if (off > 0 && (now - last_rx) >= pdMS_TO_TICKS(gap_ms)) {
            dispatch_frame(buf, off);
            off = 0;
        }
    }

    if (off > 0) dispatch_frame(buf, off);
    free(buf);
    vTaskDelete(NULL);
}

esp_err_t serial_service_init(void)
{
    if (s.inited) return ESP_OK;
    s.lock = xSemaphoreCreateMutex();
    if (!s.lock) return ESP_ERR_NO_MEM;
    load_config_from_nvs(&s.cfg);
    s.inited = true;
    ESP_LOGI(TAG, "initialised @ %lu baud", (unsigned long)s.cfg.baud_rate);
    return ESP_OK;
}

esp_err_t serial_service_deinit(void)
{
    if (!s.inited) return ESP_OK;
    serial_service_stop();
    vSemaphoreDelete(s.lock);
    memset(&s, 0, sizeof(s));
    return ESP_OK;
}

esp_err_t serial_service_start(void)
{
    if (!s.inited)  return ESP_ERR_INVALID_STATE;
    if (s.running)  return ESP_OK;

    hal_uart_config_t hc = {
        .port        = SERIAL_SERVICE_UART_PORT,
        .tx_pin      = SERIAL_SERVICE_UART_TX,
        .rx_pin      = SERIAL_SERVICE_UART_RX,
        .rts_pin     = SERIAL_SERVICE_UART_RTS,
        .cts_pin     = SERIAL_SERVICE_UART_CTS,
        .baud_rate   = s.cfg.baud_rate,
        .data_bits   = s.cfg.data_bits,
        .stop_bits   = s.cfg.stop_bits,
        .parity      = parity_to_hal(s.cfg.parity),
        .flow_ctrl   = s.cfg.flow_ctrl,
        .rs485_half_duplex = s.cfg.rs485_half_duplex,
        .rx_buf_size = SERIAL_SERVICE_RX_BUF,
        .tx_buf_size = SERIAL_SERVICE_TX_BUF,
    };
    esp_err_t err = hal_uart_init(&hc);
    if (err != ESP_OK) { s.status.last_error = err; return err; }

    s.running = true;
    BaseType_t ok = xTaskCreate(rx_task_fn, "ser_rx", 4096, NULL, 10, &s.rx_task);
    if (ok != pdPASS) {
        s.running = false;
        hal_uart_deinit(SERIAL_SERVICE_UART_PORT);
        return ESP_ERR_NO_MEM;
    }
    s.status.running = true;
    return ESP_OK;
}

esp_err_t serial_service_stop(void)
{
    if (!s.running) return ESP_OK;
    s.running = false;
    /* Let task observe the flag and exit; read timeout bounds the wait. */
    vTaskDelay(pdMS_TO_TICKS(100));
    hal_uart_deinit(SERIAL_SERVICE_UART_PORT);
    s.status.running = false;
    return ESP_OK;
}

esp_err_t serial_service_configure(const serial_config_t *cfg)
{
    if (!s.inited || !cfg) return ESP_ERR_INVALID_ARG;

    /* Persist then hot-restart. */
    config_set_int (CFG_KEY_SER_BAUD,      (int32_t)cfg->baud_rate);
    config_set_int (CFG_KEY_SER_DATA_BITS, cfg->data_bits);
    config_set_int (CFG_KEY_SER_STOP_BITS, cfg->stop_bits);
    config_set_int (CFG_KEY_SER_PARITY,    cfg->parity);
    config_set_bool(CFG_KEY_SER_FLOW_CTRL, cfg->flow_ctrl);
    config_set_bool(CFG_KEY_SER_RS485,     cfg->rs485_half_duplex);
    config_set_int (CFG_KEY_SER_FRAME_GAP, cfg->frame_gap_ms);
    config_commit();

    bool was_running = s.running;
    if (was_running) serial_service_stop();
    s.cfg = *cfg;
    if (was_running) return serial_service_start();
    return ESP_OK;
}

esp_err_t serial_service_get_config(serial_config_t *out)
{
    if (!s.inited || !out) return ESP_ERR_INVALID_ARG;
    *out = s.cfg;
    return ESP_OK;
}

esp_err_t serial_service_send(const uint8_t *data, size_t len)
{
    if (!s.running) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    size_t n = 0;
    esp_err_t err = hal_uart_write(SERIAL_SERVICE_UART_PORT, data, len, &n);
    if (err == ESP_OK) s.status.tx_bytes += n;
    else               s.status.last_error = err;
    return err;
}

esp_err_t serial_service_register_rx_cb(serial_rx_cb_t cb, void *user_data)
{
    if (!s.inited || !cb) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NO_MEM;
    for (int i = 0; i < MAX_RX_CB; ++i) {
        if (!s.cbs[i].cb) {
            s.cbs[i].cb = cb; s.cbs[i].user = user_data;
            err = ESP_OK; break;
        }
    }
    xSemaphoreGive(s.lock);
    return err;
}

esp_err_t serial_service_unregister_rx_cb(serial_rx_cb_t cb)
{
    if (!s.inited || !cb) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (int i = 0; i < MAX_RX_CB; ++i) {
        if (s.cbs[i].cb == cb) { s.cbs[i].cb = NULL; s.cbs[i].user = NULL; }
    }
    xSemaphoreGive(s.lock);
    return ESP_OK;
}

esp_err_t serial_service_get_status(serial_status_t *out)
{
    if (!s.inited || !out) return ESP_ERR_INVALID_ARG;
    *out = s.status;
    return ESP_OK;
}
