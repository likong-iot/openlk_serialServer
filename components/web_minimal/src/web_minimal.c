#include "web_minimal.h"
#include "web_internal.h"

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "web_minimal";
static httpd_handle_t s_server;

esp_err_t web_minimal_start(const web_minimal_config_t *cfg)
{
    if (s_server) return ESP_OK;

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.server_port    = cfg && cfg->port ? cfg->port : 80;
    /* API + WS + static assets + probe routes need headroom. */
    hc.max_uri_handlers = 32;
    uint16_t req_socks = cfg && cfg->max_sockets ? cfg->max_sockets : 7;
    /* httpd internally reserves 3 sockets; DNS captive service uses one more. */
    int safe_open_socks = CONFIG_LWIP_MAX_SOCKETS - 4;
    if (safe_open_socks < 2) safe_open_socks = 2;
    if (req_socks > (uint16_t)safe_open_socks) {
        ESP_LOGW(TAG, "max_sockets=%u too high for LWIP=%d, clamped to %d",
                 (unsigned)req_socks, CONFIG_LWIP_MAX_SOCKETS, safe_open_socks);
        req_socks = (uint16_t)safe_open_socks;
    }
    hc.max_open_sockets = req_socks;
    hc.uri_match_fn     = httpd_uri_match_wildcard;
    hc.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &hc);
    if (err != ESP_OK) { ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err)); return err; }

    ESP_ERROR_CHECK(web_register_network_api(s_server));
    ESP_ERROR_CHECK(web_register_scan_api   (s_server));
    ESP_ERROR_CHECK(web_register_serial_api (s_server));
    err = web_register_serial_ws(s_server);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "serial ws disabled: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(web_register_system_api (s_server));
    ESP_ERROR_CHECK(web_register_static     (s_server));

    ESP_LOGI(TAG, "listening on :%u", (unsigned)hc.server_port);
    return ESP_OK;
}

esp_err_t web_minimal_stop(void)
{
    if (!s_server) return ESP_OK;
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}

bool web_minimal_is_running(void)
{
    return s_server != NULL;
}
