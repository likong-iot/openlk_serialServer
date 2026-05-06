#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin UART adapter. Used by serial_service; no other module should include
 * this header. Pins and port number are fixed at compile time via the
 * HAL_UART_* defaults below — override in `hal_uart.c` per board variant.
 */

typedef struct {
    int      port;           /* UART_NUM_x */
    int      tx_pin;
    int      rx_pin;
    int      rts_pin;        /* -1 if unused */
    int      cts_pin;        /* -1 if unused */
    uint32_t baud_rate;
    uint8_t  data_bits;      /* 5..8 */
    uint8_t  stop_bits;      /* esp-idf enum value */
    uint8_t  parity;         /* 0 none / 2 even / 3 odd  (esp-idf values) */
    bool     flow_ctrl;
    bool     rs485_half_duplex;
    size_t   rx_buf_size;
    size_t   tx_buf_size;
} hal_uart_config_t;

/* Install driver and apply config. Call hal_uart_deinit() before reconfiguring. */
esp_err_t hal_uart_init(const hal_uart_config_t *cfg);
esp_err_t hal_uart_deinit(int port);

esp_err_t hal_uart_write(int port, const uint8_t *data, size_t len, size_t *out_written);
/* Read up to `len` bytes, waiting at most `timeout_ms`. Returns bytes read in *out_read. */
esp_err_t hal_uart_read(int port, uint8_t *out, size_t len, uint32_t timeout_ms, size_t *out_read);
esp_err_t hal_uart_flush(int port);
esp_err_t hal_uart_available(int port, size_t *out_bytes);

#ifdef __cplusplus
}
#endif
