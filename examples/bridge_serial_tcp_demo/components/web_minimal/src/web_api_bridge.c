#include "web_internal.h"
#include "config_service.h"
#include <string.h>
#include <stdlib.h>

#define BRIDGE_HOST_MAX_LEN 63
#define BRIDGE_PORT_MIN     1
#define BRIDGE_PORT_MAX     65535
#define BRIDGE_RECONN_MIN   200
#define BRIDGE_RECONN_MAX   60000

static esp_err_t get_handler(httpd_req_t *req)
{
    char host[64] = {0};
    int32_t port = 0;
    int32_t reconn_ms = 0;

    config_get_str(CFG_KEY_TCP_HOST, host, sizeof(host), "");
    config_get_int(CFG_KEY_TCP_PORT, &port, 8080);
    config_get_int(CFG_KEY_TCP_RECONN_MS, &reconn_ms, 2000);

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "host", host);
    cJSON_AddNumberToObject(d, "port", port);
    cJSON_AddNumberToObject(d, "reconnect_ms", reconn_ms);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

static esp_err_t post_handler(httpd_req_t *req)
{
    char *body = web_read_body(req, 2048);
    if (!body) return web_send_error(req, WEB_CODE_BAD_PARAM, "body required");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid json");

    const cJSON *host = cJSON_GetObjectItem(root, "host");
    const cJSON *port = cJSON_GetObjectItem(root, "port");
    const cJSON *reconnect_ms = cJSON_GetObjectItem(root, "reconnect_ms");
    if (!cJSON_IsNumber(reconnect_ms)) {
        reconnect_ms = cJSON_GetObjectItem(root, "reconn_ms");
    }

    if (!cJSON_IsString(host) || !host->valuestring[0]) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "host required");
    }
    if (strlen(host->valuestring) > BRIDGE_HOST_MAX_LEN) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "host too long");
    }
    if (!cJSON_IsNumber(port) || port->valueint < BRIDGE_PORT_MIN || port->valueint > BRIDGE_PORT_MAX) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "port 1..65535");
    }
    if (!cJSON_IsNumber(reconnect_ms) ||
        reconnect_ms->valueint < BRIDGE_RECONN_MIN ||
        reconnect_ms->valueint > BRIDGE_RECONN_MAX) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "reconnect_ms 200..60000");
    }

    esp_err_t err = config_set_str(CFG_KEY_TCP_HOST, host->valuestring);
    if (err == ESP_OK) err = config_set_int(CFG_KEY_TCP_PORT, port->valueint);
    if (err == ESP_OK) err = config_set_int(CFG_KEY_TCP_RECONN_MS, reconnect_ms->valueint);
    cJSON_Delete(root);

    if (err != ESP_OK) return web_send_error(req, WEB_CODE_INTERNAL, esp_err_to_name(err));
    err = config_commit();
    if (err != ESP_OK) return web_send_error(req, WEB_CODE_INTERNAL, esp_err_to_name(err));

    cJSON *d = cJSON_CreateObject();
    cJSON_AddBoolToObject(d, "applied", true);
    cJSON_AddBoolToObject(d, "reboot_required", true);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

esp_err_t web_register_bridge_api(httpd_handle_t server)
{
    static const httpd_uri_t g = { .uri = "/api/bridge", .method = HTTP_GET, .handler = get_handler };
    static const httpd_uri_t p = { .uri = "/api/bridge", .method = HTTP_POST, .handler = post_handler };

    esp_err_t err = httpd_register_uri_handler(server, &g);
    if (err != ESP_OK) return err;
    err = httpd_register_uri_handler(server, &p);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
