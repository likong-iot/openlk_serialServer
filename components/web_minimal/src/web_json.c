#include "web_internal.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "web_json";

#define DEFAULT_BODY_LIMIT (8 * 1024)

char *web_read_body(httpd_req_t *req, size_t limit)
{
    size_t total = req->content_len;
    if (total == 0 || total > (limit ? limit : DEFAULT_BODY_LIMIT)) {
        return NULL;
    }

    char *buf = malloc(total + 1);
    if (!buf) return NULL;

    size_t off = 0;
    while (off < total) {
        int n = httpd_req_recv(req, buf + off, total - off);
        if (n <= 0) { free(buf); return NULL; }
        off += (size_t)n;
    }
    buf[total] = '\0';
    return buf;
}

esp_err_t web_send_json(httpd_req_t *req, int code, const char *msg, cJSON *data)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) { if (data) cJSON_Delete(data); return ESP_ERR_NO_MEM; }

    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg",  msg ? msg : (code == 0 ? "ok" : "error"));
    if (data) cJSON_AddItemToObject(root, "data", data);
    else      cJSON_AddNullToObject(root, "data");

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr (req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    if (err != ESP_OK) ESP_LOGW(TAG, "send: %s", esp_err_to_name(err));
    return err;
}

esp_err_t web_send_error(httpd_req_t *req, int code, const char *msg)
{
    return web_send_json(req, code, msg, NULL);
}
