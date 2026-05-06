#include "web_internal.h"
#include "auth_service.h"

#include <string.h>

/*
 * /api/auth/login          — POST { user, password }            → { token, must_change }
 * /api/auth/logout         — POST (Authorization: Bearer ...)
 * /api/auth/me             — GET  (Authorization: Bearer ...)   → { user, must_change }
 * /api/auth/change_password— POST { old, new } (Authorization)  → ok
 *
 * Login + logout are intentionally exempt from web_auth_require().
 */

static esp_err_t login_handler(httpd_req_t *req)
{
    char *body = web_read_body(req, 1024);
    if (!body) return web_send_error(req, WEB_CODE_BAD_PARAM, "body required");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid json");

    const cJSON *u = cJSON_GetObjectItem(root, "user");
    const cJSON *p = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(u) || !cJSON_IsString(p)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "user/password required");
    }

    char token[AUTH_TOKEN_BUF];
    bool mustchg = false;
    esp_err_t err = auth_service_login(u->valuestring, p->valuestring, token, &mustchg);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return web_send_error(req, WEB_CODE_UNAUTHORIZED, "invalid credentials");
    }

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "token",       token);
    cJSON_AddBoolToObject  (d, "must_change", mustchg);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    char hdr[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK) {
        const char *tok = hdr;
        if (strncmp(tok, "Bearer ", 7) == 0) tok += 7;
        auth_service_logout(tok);
    }
    return web_send_json(req, WEB_CODE_OK, "ok", NULL);
}

static esp_err_t me_handler(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) return ESP_OK;
    char hdr[128] = {0};
    char user[AUTH_USER_MAX + 1] = {0};
    bool mustchg = false;
    httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr));
    const char *tok = hdr;
    if (strncmp(tok, "Bearer ", 7) == 0) tok += 7;
    if (auth_service_whoami(tok, user, sizeof(user), &mustchg) != ESP_OK)
        return web_send_error(req, WEB_CODE_UNAUTHORIZED, "no session");

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "user",        user);
    cJSON_AddBoolToObject  (d, "must_change", mustchg);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

static esp_err_t change_pw_handler(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) return ESP_OK;

    char *body = web_read_body(req, 1024);
    if (!body) return web_send_error(req, WEB_CODE_BAD_PARAM, "body required");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid json");

    const cJSON *o = cJSON_GetObjectItem(root, "old");
    const cJSON *n = cJSON_GetObjectItem(root, "new");
    if (!cJSON_IsString(o) || !cJSON_IsString(n)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "old/new required");
    }

    char hdr[128] = {0};
    httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr));
    const char *tok = hdr;
    if (strncmp(tok, "Bearer ", 7) == 0) tok += 7;

    esp_err_t err = auth_service_change_password(tok, o->valuestring, n->valuestring);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return web_send_error(req, WEB_CODE_BAD_PARAM,
                              "old password mismatch or new password rejected");
    }
    return web_send_json(req, WEB_CODE_OK, "ok", NULL);
}

esp_err_t web_register_auth_api(httpd_handle_t server)
{
    static const httpd_uri_t l   = { .uri = "/api/auth/login",          .method = HTTP_POST, .handler = login_handler     };
    static const httpd_uri_t out = { .uri = "/api/auth/logout",         .method = HTTP_POST, .handler = logout_handler    };
    static const httpd_uri_t me  = { .uri = "/api/auth/me",             .method = HTTP_GET,  .handler = me_handler        };
    static const httpd_uri_t ch  = { .uri = "/api/auth/change_password",.method = HTTP_POST, .handler = change_pw_handler };
    esp_err_t err;
    if ((err = httpd_register_uri_handler(server, &l))   != ESP_OK) return err;
    if ((err = httpd_register_uri_handler(server, &out)) != ESP_OK) return err;
    if ((err = httpd_register_uri_handler(server, &me))  != ESP_OK) return err;
    if ((err = httpd_register_uri_handler(server, &ch))  != ESP_OK) return err;
    return ESP_OK;
}
