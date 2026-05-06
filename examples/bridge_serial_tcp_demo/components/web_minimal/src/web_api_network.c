#include "web_internal.h"
#include "config_service.h"
#include <string.h>
#include "lwip/ip4_addr.h"

/*
 * GET  /api/network  →  current network config (wifi+eth+ip)
 * POST /api/network  →  apply new config, persist, return { reboot_required }
 *
 * Actual network re-apply logic is completed in M2 by the platform layer;
 * this file owns only the API contract and persistence.
 */

static esp_err_t get_handler(httpd_req_t *req)
{
    char  mode[8]  = {0};
    char  ssid[33] = {0};
    char  ip[16]   = {0};
    char  mask[16] = {0};
    char  gw[16]   = {0};
    bool  dhcp     = true;

    config_get_str (CFG_KEY_NET_MODE, mode, sizeof(mode), "wifi");
    config_get_bool(CFG_KEY_NET_DHCP, &dhcp, true);
    config_get_str (CFG_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
    config_get_str (CFG_KEY_NET_IP,   ip,   sizeof(ip),   "");
    config_get_str (CFG_KEY_NET_MASK, mask, sizeof(mask), "");
    config_get_str (CFG_KEY_NET_GW,   gw,   sizeof(gw),   "");

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "mode", mode);
    cJSON_AddBoolToObject  (d, "dhcp", dhcp);
    cJSON_AddStringToObject(d, "ssid", ssid);
    cJSON_AddStringToObject(d, "ip",   ip);
    cJSON_AddStringToObject(d, "mask", mask);
    cJSON_AddStringToObject(d, "gateway", gw);
    /* Password intentionally not returned. */
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

static bool is_valid_mode(const char *s)
{
    return s && (!strcmp(s, "wifi") || !strcmp(s, "eth"));
}

static bool is_valid_ipv4_or_empty(const char *s)
{
    if (!s || !s[0]) return true;
    ip4_addr_t tmp;
    return ip4addr_aton(s, &tmp) != 0;
}

static esp_err_t post_handler(httpd_req_t *req)
{
    char *body = web_read_body(req, 4096);
    if (!body) return web_send_error(req, WEB_CODE_BAD_PARAM, "body required");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid json");

    const cJSON *mode = cJSON_GetObjectItem(root, "mode");
    const cJSON *dhcp = cJSON_GetObjectItem(root, "dhcp");
    const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *pass = cJSON_GetObjectItem(root, "password");
    const cJSON *ip   = cJSON_GetObjectItem(root, "ip");
    const cJSON *mask = cJSON_GetObjectItem(root, "mask");
    const cJSON *gw   = cJSON_GetObjectItem(root, "gateway");

    if (cJSON_IsString(mode) && !is_valid_mode(mode->valuestring)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "mode must be wifi|eth");
    }
    if (cJSON_IsString(ip) && !is_valid_ipv4_or_empty(ip->valuestring)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "bad ip");
    }
    if (cJSON_IsString(mask) && !is_valid_ipv4_or_empty(mask->valuestring)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "bad mask");
    }
    if (cJSON_IsString(gw) && !is_valid_ipv4_or_empty(gw->valuestring)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "bad gateway");
    }

    esp_err_t err = ESP_OK;
    if (cJSON_IsString(mode)) err = config_set_str(CFG_KEY_NET_MODE, mode->valuestring);
    if (err == ESP_OK && cJSON_IsBool(dhcp)) err = config_set_bool(CFG_KEY_NET_DHCP, cJSON_IsTrue(dhcp));
    if (err == ESP_OK && cJSON_IsString(ssid)) err = config_set_str(CFG_KEY_WIFI_SSID, ssid->valuestring);
    /* Empty password means "keep current password". */
    if (err == ESP_OK && cJSON_IsString(pass) && pass->valuestring[0]) err = config_set_str(CFG_KEY_WIFI_PASS, pass->valuestring);
    if (err == ESP_OK && cJSON_IsString(ip)) err = config_set_str(CFG_KEY_NET_IP, ip->valuestring);
    if (err == ESP_OK && cJSON_IsString(mask)) err = config_set_str(CFG_KEY_NET_MASK, mask->valuestring);
    if (err == ESP_OK && cJSON_IsString(gw)) err = config_set_str(CFG_KEY_NET_GW, gw->valuestring);

    cJSON_Delete(root);
    if (err != ESP_OK) return web_send_error(req, WEB_CODE_INTERNAL, esp_err_to_name(err));
    err = config_commit();
    if (err != ESP_OK) return web_send_error(req, WEB_CODE_INTERNAL, esp_err_to_name(err));

    cJSON *d = cJSON_CreateObject();
    cJSON_AddBoolToObject(d, "applied", true);
    cJSON_AddBoolToObject(d, "reboot_required", true);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

esp_err_t web_register_network_api(httpd_handle_t server)
{
    static const httpd_uri_t g = { .uri = "/api/network", .method = HTTP_GET,  .handler = get_handler  };
    static const httpd_uri_t p = { .uri = "/api/network", .method = HTTP_POST, .handler = post_handler };
    esp_err_t err = httpd_register_uri_handler(server, &g);
    if (err != ESP_OK) return err;
    err = httpd_register_uri_handler(server, &p);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
