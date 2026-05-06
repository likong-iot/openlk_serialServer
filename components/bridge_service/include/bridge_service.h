#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "protocol_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bridge Service — wires `serial_service` ↔ a `protocol_handle_t`.
 *
 * This is the "work mode" of a serial server:
 *   serial RX  → protocol_send  (uplink)
 *   protocol RX → serial_service_send (downlink)
 *
 * The bridge owns at most one protocol instance at a time. Switching modes
 * tears down the old protocol cleanly before starting the new one.
 *
 * Architecture rule honoured: this component depends on
 * `serial_service` + `protocol_service` + `config_service`. It does NOT
 * touch UART drivers, sockets, MQTT clients, or NVS directly.
 */

typedef enum {
    BRIDGE_MODE_OFF = 0,
    BRIDGE_MODE_TCP_CLIENT,
    BRIDGE_MODE_TCP_SERVER,
    BRIDGE_MODE_UDP,
    BRIDGE_MODE_MQTT,
    BRIDGE_MODE_HTTP,
    BRIDGE_MODE__COUNT
} bridge_mode_t;

typedef struct {
    bridge_mode_t    mode;
    protocol_state_t proto_state;
    uint64_t         tx_bytes;          /* serial → protocol */
    uint64_t         rx_bytes;          /* protocol → serial */
    uint32_t         tx_packets;
    uint32_t         rx_packets;
    int32_t          last_error;        /* esp_err_t of last send */
    uint64_t         started_ms;        /* boot-relative timestamp */
} bridge_status_t;

/* Lifecycle. init() reads the saved mode but does NOT start the protocol —
 * call start() once net_service / serial_service are up. */
esp_err_t bridge_service_init(void);
esp_err_t bridge_service_start(void);
esp_err_t bridge_service_stop(void);

/* Persist the new mode and immediately restart the bridge. The protocol
 * impl re-reads its config_service keys on start, so a Web POST that wrote
 * fresh params before calling apply_mode picks them up automatically. */
esp_err_t bridge_service_apply_mode(bridge_mode_t mode);

esp_err_t bridge_service_get_status(bridge_status_t *out);

const char    *bridge_mode_str (bridge_mode_t m);
bridge_mode_t  bridge_mode_from(const char *s);   /* returns BRIDGE_MODE_OFF on miss */

#ifdef __cplusplus
}
#endif
