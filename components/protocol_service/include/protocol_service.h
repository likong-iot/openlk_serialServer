#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Protocol Service — a single abstraction for every outbound network
 * protocol module (MQTT, TCP client/server, HTTP, UDP, …).
 *
 * Goal: lifecycle, send, and status are identical across protocols; only the
 * factory call differs. This keeps the upper layer free of protocol-specific
 * switches.
 *
 * Stub-friendly: a concrete implementation may return empty data or simply
 * record calls; it MUST still expose every operation below without crashing.
 */

typedef struct protocol_handle_s protocol_handle_t;

typedef enum {
    PROTOCOL_STATE_STOPPED = 0,
    PROTOCOL_STATE_STARTING,
    PROTOCOL_STATE_CONNECTED,
    PROTOCOL_STATE_DISCONNECTED,
    PROTOCOL_STATE_ERROR,
} protocol_state_t;

typedef struct {
    const char       *name;          /* e.g. "mqtt", "tcp_client" */
    protocol_state_t  state;
    uint32_t          tx_packets;
    uint32_t          rx_packets;
    int32_t           last_error;
} protocol_status_t;

typedef void (*protocol_rx_cb_t)(protocol_handle_t *h,
                                 const uint8_t *data, size_t len,
                                 void *user_data);

typedef void (*protocol_event_cb_t)(protocol_handle_t *h,
                                    protocol_state_t new_state,
                                    void *user_data);

/* Lifecycle (vtable-dispatched). */
esp_err_t protocol_start(protocol_handle_t *h);
esp_err_t protocol_stop(protocol_handle_t *h);
esp_err_t protocol_send(protocol_handle_t *h, const uint8_t *data, size_t len);
esp_err_t protocol_get_status(protocol_handle_t *h, protocol_status_t *out);
void      protocol_destroy(protocol_handle_t *h);

esp_err_t protocol_register_rx_cb(protocol_handle_t *h,
                                  protocol_rx_cb_t cb, void *user_data);
esp_err_t protocol_register_event_cb(protocol_handle_t *h,
                                     protocol_event_cb_t cb, void *user_data);

/* Factories — stub implementations live under src/. Each reads its own
 * parameters from config_service; none takes raw params here. */
protocol_handle_t *protocol_mqtt_create(void);
protocol_handle_t *protocol_tcp_client_create(void);
protocol_handle_t *protocol_tcp_server_create(void);
protocol_handle_t *protocol_http_create(void);
protocol_handle_t *protocol_udp_create(void);

#ifdef __cplusplus
}
#endif
