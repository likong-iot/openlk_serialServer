#include "web_internal.h"
#include "serial_service.h"
#include <string.h>
#include <strings.h>     /* strcasecmp */
#include <ctype.h>
#include <stdlib.h>

/*
 * Endpoints:
 *   GET  /api/serial       → current serial config
 *   POST /api/serial       → apply + persist
 *   POST /api/serial/send  → { fmt: "hex"|"text", data: "..." }
 */

static const char *parity_str(serial_parity_t p)
{
    switch (p) {
    case SERIAL_PARITY_EVEN: return "even";
    case SERIAL_PARITY_ODD:  return "odd";
    default:                 return "none";
    }
}

static serial_parity_t parity_from_str(const char *s)
{
    if (!s) return SERIAL_PARITY_NONE;
    if (!strcasecmp(s, "even")) return SERIAL_PARITY_EVEN;
    if (!strcasecmp(s, "odd"))  return SERIAL_PARITY_ODD;
    return SERIAL_PARITY_NONE;
}

static esp_err_t get_handler(httpd_req_t *req)
{
    serial_config_t c = {0};
    if (serial_service_get_config(&c) != ESP_OK) {
        return web_send_error(req, WEB_CODE_INTERNAL, "serial not ready");
    }

    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "baud",      c.baud_rate);
    cJSON_AddNumberToObject(d, "data_bits", c.data_bits);
    cJSON_AddNumberToObject(d, "stop_bits", c.stop_bits);
    cJSON_AddStringToObject(d, "parity",    parity_str(c.parity));
    cJSON_AddBoolToObject  (d, "flow_ctrl", c.flow_ctrl);
    cJSON_AddBoolToObject  (d, "rs485",     c.rs485_half_duplex);
    cJSON_AddNumberToObject(d, "frame_gap_ms", c.frame_gap_ms);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

static esp_err_t post_handler(httpd_req_t *req)
{
    char *body = web_read_body(req, 2048);
    if (!body) return web_send_error(req, WEB_CODE_BAD_PARAM, "body required");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid json");

    serial_config_t c = {0};
    serial_service_get_config(&c);

    const cJSON *baud = cJSON_GetObjectItem(root, "baud");
    const cJSON *db   = cJSON_GetObjectItem(root, "data_bits");
    const cJSON *sb   = cJSON_GetObjectItem(root, "stop_bits");
    const cJSON *par  = cJSON_GetObjectItem(root, "parity");
    const cJSON *fc   = cJSON_GetObjectItem(root, "flow_ctrl");
    const cJSON *rs   = cJSON_GetObjectItem(root, "rs485");
    const cJSON *fg   = cJSON_GetObjectItem(root, "frame_gap_ms");

    if (cJSON_IsNumber(baud)) c.baud_rate = (uint32_t)baud->valueint;
    if (cJSON_IsNumber(db))   c.data_bits = (uint8_t) db->valueint;
    if (cJSON_IsNumber(sb))   c.stop_bits = (serial_stop_bits_t)sb->valueint;
    if (cJSON_IsString(par))  c.parity    = parity_from_str(par->valuestring);
    if (cJSON_IsBool(fc))     c.flow_ctrl = cJSON_IsTrue(fc);
    if (cJSON_IsBool(rs))     c.rs485_half_duplex = cJSON_IsTrue(rs);
    if (cJSON_IsNumber(fg))   c.frame_gap_ms = (uint16_t)fg->valueint;

    cJSON_Delete(root);

    if (c.baud_rate < 1200 || c.baud_rate > 921600) {
        return web_send_error(req, WEB_CODE_BAD_PARAM, "baud out of range");
    }
    if (c.data_bits < 5 || c.data_bits > 8) {
        return web_send_error(req, WEB_CODE_BAD_PARAM, "data_bits 5..8");
    }

    esp_err_t err = serial_service_configure(&c);
    if (err != ESP_OK) return web_send_error(req, WEB_CODE_INTERNAL, esp_err_to_name(err));

    cJSON *d = cJSON_CreateObject();
    cJSON_AddBoolToObject(d, "applied", true);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

/* Decode "01 02 AF" into bytes. Returns number of bytes written to `out`,
 * or negative on parse error. Ignores whitespace; accepts optional 0x prefix. */
static int decode_hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    int nibble = -1;
    while (*s) {
        char c = *s++;
        if (isspace((unsigned char)c) || c == ',' || c == '-' || c == ':') continue;
        if (c == '0' && (*s == 'x' || *s == 'X')) { s++; continue; }

        int v;
        if      (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return -1;

        if (nibble < 0) { nibble = v; }
        else {
            if (n >= cap) return -1;
            out[n++] = (uint8_t)((nibble << 4) | v);
            nibble = -1;
        }
    }
    if (nibble >= 0) return -1;
    return (int)n;
}

static esp_err_t send_handler(httpd_req_t *req)
{
    char *body = web_read_body(req, 16 * 1024);
    if (!body) return web_send_error(req, WEB_CODE_BAD_PARAM, "body required");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid json");

    const cJSON *fmt  = cJSON_GetObjectItem(root, "fmt");
    const cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsString(data)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "data required");
    }

    bool is_hex = cJSON_IsString(fmt) && !strcasecmp(fmt->valuestring, "hex");
    size_t tx_len;
    uint8_t *tx;

    if (is_hex) {
        size_t cap = strlen(data->valuestring) / 2 + 1;
        tx = malloc(cap);
        int n = tx ? decode_hex(data->valuestring, tx, cap) : -1;
        if (n < 0) {
            free(tx); cJSON_Delete(root);
            return web_send_error(req, WEB_CODE_BAD_PARAM, "bad hex");
        }
        tx_len = (size_t)n;
    } else {
        tx_len = strlen(data->valuestring);
        tx = (uint8_t *)malloc(tx_len);
        if (tx) memcpy(tx, data->valuestring, tx_len);
    }
    cJSON_Delete(root);

    if (!tx) return web_send_error(req, WEB_CODE_INTERNAL, "alloc");

    esp_err_t err = serial_service_send(tx, tx_len);
    free(tx);
    if (err != ESP_OK) return web_send_error(req, WEB_CODE_BAD_STATE, esp_err_to_name(err));

    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "sent", tx_len);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

esp_err_t web_register_serial_api(httpd_handle_t server)
{
    static const httpd_uri_t g  = { .uri = "/api/serial",      .method = HTTP_GET,  .handler = get_handler  };
    static const httpd_uri_t p  = { .uri = "/api/serial",      .method = HTTP_POST, .handler = post_handler };
    static const httpd_uri_t s  = { .uri = "/api/serial/send", .method = HTTP_POST, .handler = send_handler };
    esp_err_t err = httpd_register_uri_handler(server, &g);
    if (err != ESP_OK) return err;
    err = httpd_register_uri_handler(server, &p);
    if (err != ESP_OK) return err;
    err = httpd_register_uri_handler(server, &s);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
