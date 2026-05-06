#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Serial Service — unified façade over the UART peripheral.
 *
 * Design rules:
 *   - Upper layers must not touch `driver/uart.h` directly.
 *   - Receive is delivered via a user-registered callback, never polled.
 *   - Parameters are loaded from config_service on init; callers may
 *     override at runtime via serial_service_configure().
 */

typedef enum {
    SERIAL_PARITY_NONE = 0,
    SERIAL_PARITY_EVEN = 1,
    SERIAL_PARITY_ODD  = 2,
} serial_parity_t;

typedef enum {
    SERIAL_STOP_BITS_1   = 1,
    SERIAL_STOP_BITS_1_5 = 2,   /* esp-idf enum value, kept here for clarity */
    SERIAL_STOP_BITS_2   = 3,
} serial_stop_bits_t;

typedef struct {
    uint32_t           baud_rate;     /* 1200..921600 */
    uint8_t            data_bits;     /* 5..8 */
    serial_stop_bits_t stop_bits;
    serial_parity_t    parity;
    bool               flow_ctrl;     /* RTS/CTS */
    bool               rs485_half_duplex;
    uint16_t           frame_gap_ms;  /* silent time considered frame end, 0 = auto */
} serial_config_t;

typedef struct {
    bool     running;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t rx_errors;
    int32_t  last_error;        /* mirrors the latest non-OK esp_err_t */
} serial_status_t;

/* Callback invoked from an internal rx task. Contract:
 *   - `data` is valid only for the duration of the call.
 *   - Callback must be short; long work must be posted to a queue.
 */
typedef void (*serial_rx_cb_t)(const uint8_t *data, size_t len, void *user_data);

/* Lifecycle */
esp_err_t serial_service_init(void);
esp_err_t serial_service_deinit(void);
esp_err_t serial_service_start(void);
esp_err_t serial_service_stop(void);

/* Configuration */
esp_err_t serial_service_configure(const serial_config_t *cfg);  /* hot-reconfigure */
esp_err_t serial_service_get_config(serial_config_t *out);

/* IO */
esp_err_t serial_service_send(const uint8_t *data, size_t len);
esp_err_t serial_service_register_rx_cb(serial_rx_cb_t cb, void *user_data);
esp_err_t serial_service_unregister_rx_cb(serial_rx_cb_t cb);

/* Introspection */
esp_err_t serial_service_get_status(serial_status_t *out);

#ifdef __cplusplus
}
#endif
