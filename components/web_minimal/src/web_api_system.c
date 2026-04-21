#include "web_internal.h"
#include "net_service.h"

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include <string.h>

static esp_err_t status_handler(httpd_req_t *req)
{
    net_status_t s = {0};
    net_service_get_status(&s);

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    const esp_app_desc_t *app = esp_app_get_description();

    cJSON *d = cJSON_CreateObject();

    cJSON *net = cJSON_CreateObject();
    bool wifi_up = strcmp(s.mode, "wifi") == 0 ? s.wifi_up : false;
    cJSON_AddStringToObject(net, "mode",     s.mode);
    cJSON_AddBoolToObject  (net, "link_up",  s.wifi_up);
    cJSON_AddBoolToObject  (net, "wifi_up",  wifi_up);
    cJSON_AddBoolToObject  (net, "got_ip",   s.got_ip);
    cJSON_AddStringToObject(net, "ssid",     s.ssid);
    cJSON_AddNumberToObject(net, "rssi",     s.rssi);
    cJSON_AddStringToObject(net, "ip",       s.ip);
    cJSON_AddStringToObject(net, "mask",     s.mask);
    cJSON_AddStringToObject(net, "gateway",  s.gateway);
    cJSON_AddItemToObject  (d, "net", net);

    cJSON *sys = cJSON_CreateObject();
    cJSON_AddNumberToObject(sys, "uptime_ms",     s.uptime_ms);
    cJSON_AddNumberToObject(sys, "free_heap",     s.free_heap);
    cJSON_AddNumberToObject(sys, "min_free_heap", s.min_free_heap);
    cJSON_AddItemToObject  (d, "sys", sys);

    cJSON *fw = cJSON_CreateObject();
    cJSON_AddStringToObject(fw, "version",       app->version);
    cJSON_AddStringToObject(fw, "idf_version",   app->idf_ver);
    cJSON_AddStringToObject(fw, "project_name",  app->project_name);
    cJSON_AddStringToObject(fw, "compile_date",  app->date);
    cJSON_AddStringToObject(fw, "compile_time",  app->time);
    cJSON_AddItemToObject  (d, "fw", fw);

    cJSON *hw = cJSON_CreateObject();
    cJSON_AddNumberToObject(hw, "cores",    chip.cores);
    cJSON_AddNumberToObject(hw, "revision", chip.revision);
    cJSON_AddItemToObject  (d, "hw", hw);

    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "delay_ms", 800);
    esp_err_t err = web_send_json(req, WEB_CODE_OK, "rebooting", d);
    net_service_request_reboot(800);
    return err;
}

esp_err_t web_register_system_api(httpd_handle_t server)
{
    static const httpd_uri_t s  = { .uri = "/api/system/status", .method = HTTP_GET,  .handler = status_handler };
    static const httpd_uri_t rb = { .uri = "/api/system/reboot", .method = HTTP_POST, .handler = reboot_handler };
    esp_err_t err = httpd_register_uri_handler(server, &s);
    if (err != ESP_OK) return err;
    err = httpd_register_uri_handler(server, &rb);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
