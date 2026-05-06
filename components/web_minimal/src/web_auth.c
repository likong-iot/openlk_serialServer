#include "web_internal.h"
#include "auth_service.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "web_auth";

static int extract_bearer(httpd_req_t *req, char *out, size_t out_sz)
{
    size_t n = httpd_req_get_hdr_value_len(req, "Authorization");
    if (n == 0 || n + 1 > 128) return -1;
    char hdr[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK)
        return -1;
    static const char prefix[] = "Bearer ";
    if (strncmp(hdr, prefix, sizeof(prefix) - 1) != 0) return -1;
    const char *tok = hdr + sizeof(prefix) - 1;
    while (*tok == ' ') ++tok;
    if (strlen(tok) >= out_sz) return -1;
    strcpy(out, tok);
    return 0;
}

esp_err_t web_auth_require(httpd_req_t *req)
{
    char token[64];
    if (extract_bearer(req, token, sizeof(token)) != 0 ||
        !auth_service_session_valid(token)) {
        ESP_LOGD(TAG, "deny %s", req->uri);
        web_send_error(req, WEB_CODE_UNAUTHORIZED, "unauthorized");
        return ESP_FAIL;
    }
    return ESP_OK;
}
