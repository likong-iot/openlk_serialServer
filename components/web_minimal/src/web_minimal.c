#include "web_minimal.h"
#include "web_internal.h"

#include "esp_log.h"

static const char *TAG = "web_minimal";
static httpd_handle_t s_server;

esp_err_t web_minimal_start(const web_minimal_config_t *cfg)
{
    if (s_server) return ESP_OK;

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.server_port    = cfg && cfg->port ? cfg->port : 80;
    hc.max_uri_handlers = 16;
    hc.max_open_sockets = cfg && cfg->max_sockets ? cfg->max_sockets : 7;
    hc.uri_match_fn     = httpd_uri_match_wildcard;
    hc.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &hc);
    if (err != ESP_OK) { ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err)); return err; }

    ESP_ERROR_CHECK(web_register_network_api(s_server));
    ESP_ERROR_CHECK(web_register_scan_api   (s_server));
    ESP_ERROR_CHECK(web_register_serial_api (s_server));
    ESP_ERROR_CHECK(web_register_serial_ws  (s_server));
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
