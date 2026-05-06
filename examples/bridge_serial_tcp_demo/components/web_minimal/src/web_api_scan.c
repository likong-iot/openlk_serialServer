#include "web_internal.h"
#include "net_service.h"

#include "esp_wifi.h"
#include "esp_wifi_types.h"

#define MAX_APS 24

static const char *authmode_name(uint8_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:            return "open";
    case WIFI_AUTH_WEP:             return "wep";
    case WIFI_AUTH_WPA_PSK:         return "wpa";
    case WIFI_AUTH_WPA2_PSK:        return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "wpa/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-ent";
    case WIFI_AUTH_WPA3_PSK:        return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "wpa2/3";
    default:                        return "?";
    }
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    net_scan_ap_t aps[MAX_APS];
    size_t n = 0;
    esp_err_t err = net_service_scan(aps, MAX_APS, &n);
    if (err == ESP_ERR_WIFI_STATE) {
        return web_send_error(req, WEB_CODE_BAD_STATE, "scan busy");
    }
    if (err != ESP_OK) return web_send_error(req, WEB_CODE_INTERNAL, esp_err_to_name(err));

    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < n; ++i) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid",     aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi",     aps[i].rssi);
        cJSON_AddNumberToObject(ap, "channel",  aps[i].channel);
        cJSON_AddStringToObject(ap, "auth",     authmode_name(aps[i].authmode));
        cJSON_AddBoolToObject  (ap, "open",     aps[i].authmode == WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, ap);
    }
    cJSON *d = cJSON_CreateObject();
    cJSON_AddItemToObject(d, "aps", arr);
    cJSON_AddNumberToObject(d, "count", n);
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

esp_err_t web_register_scan_api(httpd_handle_t server)
{
    static const httpd_uri_t s = { .uri = "/api/network/scan", .method = HTTP_GET, .handler = scan_handler };
    esp_err_t err = httpd_register_uri_handler(server, &s);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
