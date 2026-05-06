#pragma once

/* Shared internals for the web_minimal component. Private — DO NOT export. */

#include "esp_http_server.h"
#include "cJSON.h"

/* Unified error codes returned in the `code` field of the JSON envelope.
 * Keep stable — the frontend switches on these values. */
enum {
    WEB_CODE_OK            = 0,
    WEB_CODE_BAD_PARAM     = 400,
    WEB_CODE_NOT_FOUND     = 404,
    WEB_CODE_BAD_STATE     = 409,
    WEB_CODE_INTERNAL      = 500,
    WEB_CODE_NOT_SUPPORTED = 501,
};

/* Read the full request body into a new NUL-terminated buffer (caller frees).
 * Returns NULL on error. `limit` is a hard max — oversized bodies are rejected. */
char *web_read_body(httpd_req_t *req, size_t limit);

/* Send a JSON envelope: {"code":N,"msg":"...","data":<obj|null>}
 * Takes ownership of `data` (may be NULL). */
esp_err_t web_send_json(httpd_req_t *req, int code, const char *msg, cJSON *data);

/* Convenience — use when returning an error with no data payload. */
esp_err_t web_send_error(httpd_req_t *req, int code, const char *msg);

/* URI handler registration (each file exports one). */
esp_err_t web_register_network_api(httpd_handle_t server);
esp_err_t web_register_serial_api(httpd_handle_t server);
esp_err_t web_register_serial_ws(httpd_handle_t server);
esp_err_t web_register_static(httpd_handle_t server);
esp_err_t web_register_system_api(httpd_handle_t server);
esp_err_t web_register_scan_api(httpd_handle_t server);
esp_err_t web_register_bridge_api(httpd_handle_t server);

/* WS push from other threads (serial rx). No-op if no clients connected. */
void web_ws_serial_push_rx(const uint8_t *data, size_t len);
