#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Web Minimal — scope is strictly the features required by the demo firmware:
 * base distribution firmware:
 *
 *   1. Network config  (GET/POST /api/network)
 *   2. Bridge  config  (GET/POST /api/bridge)
 *   3. Serial  config  (GET/POST /api/serial)
 *   4. Serial  debug   (POST /api/serial/send + WS /ws/serial)
 *
 * All responses share { "code": int, "msg": str, "data": object }.
 * See docs/API.md for the full contract.
 */

typedef struct {
    uint16_t port;                    /* default 80 */
    uint16_t max_sockets;             /* http sockets, default 7 */
} web_minimal_config_t;

esp_err_t web_minimal_start(const web_minimal_config_t *cfg);
esp_err_t web_minimal_stop(void);
bool      web_minimal_is_running(void);

#ifdef __cplusplus
}
#endif
