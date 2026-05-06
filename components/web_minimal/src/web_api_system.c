#include "web_internal.h"
#include "net_service.h"

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

static esp_err_t status_handler(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) return ESP_OK;
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
    if (web_auth_require(req) != ESP_OK) return ESP_OK;
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "delay_ms", 800);
    esp_err_t err = web_send_json(req, WEB_CODE_OK, "rebooting", d);
    net_service_request_reboot(800);
    return err;
}

/* Map esp_reset_reason() into a human-readable label. The frontend just
 * displays the string — we do not depend on numeric stability. */
static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_EXT:      return "external";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT:      return "wdt";
    case ESP_RST_DEEPSLEEP:return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    case ESP_RST_UNKNOWN:
    default:               return "unknown";
    }
}

static const char *chip_model_str(esp_chip_model_t m)
{
    switch (m) {
    case CHIP_ESP32:    return "ESP32";
    case CHIP_ESP32S2:  return "ESP32-S2";
    case CHIP_ESP32S3:  return "ESP32-S3";
    case CHIP_ESP32C2:  return "ESP32-C2";
    case CHIP_ESP32C3:  return "ESP32-C3";
    case CHIP_ESP32C6:  return "ESP32-C6";
    case CHIP_ESP32H2:  return "ESP32-H2";
    default:            return "ESP32?";
    }
}

static esp_err_t info_handler(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) return ESP_OK;

    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    /* Use net_status snapshot (it already has uptime_ms / heap fields). */
    net_status_t st = {0};
    net_service_get_status(&st);
    uint64_t uptime_ms = st.uptime_ms;

    uint8_t mac_sta[6] = {0}, mac_ap[6] = {0};
    esp_read_mac(mac_sta, ESP_MAC_WIFI_STA);
    esp_read_mac(mac_ap,  ESP_MAC_WIFI_SOFTAP);
    char mac_sta_s[18], mac_ap_s[18];
    snprintf(mac_sta_s, sizeof(mac_sta_s), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_sta[0], mac_sta[1], mac_sta[2], mac_sta[3], mac_sta[4], mac_sta[5]);
    snprintf(mac_ap_s,  sizeof(mac_ap_s),  "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_ap[0],  mac_ap[1],  mac_ap[2],  mac_ap[3],  mac_ap[4],  mac_ap[5]);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    cJSON *d = cJSON_CreateObject();

    cJSON *fw = cJSON_CreateObject();
    cJSON_AddStringToObject(fw, "version",      app->version);
    cJSON_AddStringToObject(fw, "project",      app->project_name);
    cJSON_AddStringToObject(fw, "compile_date", app->date);
    cJSON_AddStringToObject(fw, "compile_time", app->time);
    cJSON_AddStringToObject(fw, "idf_version",  app->idf_ver);
    cJSON_AddItemToObject  (d, "fw", fw);

    cJSON *hw = cJSON_CreateObject();
    cJSON_AddStringToObject(hw, "model",      chip_model_str(chip.model));
    cJSON_AddNumberToObject(hw, "cores",      chip.cores);
    cJSON_AddNumberToObject(hw, "revision",   chip.revision);
    cJSON_AddNumberToObject(hw, "flash_mb",   flash_size / (1024 * 1024));
    cJSON_AddStringToObject(hw, "mac_sta",    mac_sta_s);
    cJSON_AddStringToObject(hw, "mac_ap",     mac_ap_s);
    cJSON_AddItemToObject  (d, "hw", hw);

    cJSON *sys = cJSON_CreateObject();
    cJSON_AddNumberToObject(sys, "uptime_ms",     uptime_ms);
    cJSON_AddNumberToObject(sys, "free_heap",     esp_get_free_heap_size());
    cJSON_AddNumberToObject(sys, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(sys, "psram_total",   heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(sys, "psram_free",    heap_caps_get_free_size (MALLOC_CAP_SPIRAM));
    cJSON_AddStringToObject(sys, "reset_reason",  reset_reason_str(esp_reset_reason()));
    cJSON_AddItemToObject  (d, "sys", sys);

    cJSON *net = cJSON_CreateObject();
    cJSON_AddStringToObject(net, "mode",    st.mode);
    cJSON_AddBoolToObject  (net, "wifi_up", st.wifi_up);
    cJSON_AddBoolToObject  (net, "got_ip",  st.got_ip);
    cJSON_AddStringToObject(net, "ssid",    st.ssid);
    cJSON_AddNumberToObject(net, "rssi",    st.rssi);
    cJSON_AddStringToObject(net, "ip",      st.ip);
    cJSON_AddStringToObject(net, "mask",    st.mask);
    cJSON_AddStringToObject(net, "gateway", st.gateway);
    cJSON_AddItemToObject  (d, "net", net);

    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

esp_err_t web_register_system_api(httpd_handle_t server)
{
    static const httpd_uri_t s  = { .uri = "/api/system/status", .method = HTTP_GET,  .handler = status_handler };
    static const httpd_uri_t rb = { .uri = "/api/system/reboot", .method = HTTP_POST, .handler = reboot_handler };
    static const httpd_uri_t in = { .uri = "/api/system/info",   .method = HTTP_GET,  .handler = info_handler   };
    esp_err_t err;
    if ((err = httpd_register_uri_handler(server, &s))  != ESP_OK) return err;
    if ((err = httpd_register_uri_handler(server, &rb)) != ESP_OK) return err;
    if ((err = httpd_register_uri_handler(server, &in)) != ESP_OK) return err;
    return ESP_OK;
}
