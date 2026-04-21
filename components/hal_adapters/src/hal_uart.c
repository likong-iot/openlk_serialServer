#include "hal_uart.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "hal_uart";

esp_err_t hal_uart_init(const hal_uart_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    uart_config_t uc = {
        .baud_rate = cfg->baud_rate,
        .data_bits = cfg->data_bits - 5 + UART_DATA_5_BITS,  /* 5..8 maps directly */
        .parity    = cfg->parity,
        .stop_bits = cfg->stop_bits,
        .flow_ctrl = cfg->flow_ctrl ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(cfg->port, cfg->rx_buf_size, cfg->tx_buf_size, 0, NULL, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "driver_install: %s", esp_err_to_name(err)); return err; }

    err = uart_param_config(cfg->port, &uc);
    if (err != ESP_OK) return err;

    err = uart_set_pin(cfg->port, cfg->tx_pin, cfg->rx_pin, cfg->rts_pin, cfg->cts_pin);
    if (err != ESP_OK) return err;

    if (cfg->rs485_half_duplex) {
        err = uart_set_mode(cfg->port, UART_MODE_RS485_HALF_DUPLEX);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t hal_uart_deinit(int port)
{
    return uart_driver_delete(port);
}

esp_err_t hal_uart_write(int port, const uint8_t *data, size_t len, size_t *out_written)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    int n = uart_write_bytes(port, (const char *)data, len);
    if (n < 0) return ESP_FAIL;
    if (out_written) *out_written = (size_t)n;
    return ESP_OK;
}

esp_err_t hal_uart_read(int port, uint8_t *out, size_t len, uint32_t timeout_ms, size_t *out_read)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    /* Avoid busy-polling when timeout_ms is smaller than one RTOS tick.
     * pdMS_TO_TICKS(5) can become 0 on 100 Hz systems, which would spin. */
    TickType_t ticks = 0;
    if (timeout_ms > 0) {
        ticks = pdMS_TO_TICKS(timeout_ms);
        if (ticks == 0) ticks = 1;
    }
    int n = uart_read_bytes(port, out, len, ticks);
    if (n < 0) return ESP_FAIL;
    if (out_read) *out_read = (size_t)n;
    return ESP_OK;
}

esp_err_t hal_uart_flush(int port)
{
    return uart_flush_input(port);
}

esp_err_t hal_uart_available(int port, size_t *out_bytes)
{
    if (!out_bytes) return ESP_ERR_INVALID_ARG;
    return uart_get_buffered_data_len(port, out_bytes);
}
