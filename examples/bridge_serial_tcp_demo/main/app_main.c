#include "esp_log.h"

#include "config_service.h"
#include "config_keys.h"
#include "serial_service.h"
#include "net_service.h"
#include "web_minimal.h"
#include "bridge_serial_tcp.h"

static const char *TAG = "demo_main";

#define DEMO_TCP_DEFAULT_HOST      "192.168.1.100"
#define DEMO_TCP_DEFAULT_PORT      9000
#define DEMO_TCP_DEFAULT_RECONN_MS 2000

static void ensure_demo_tcp_defaults(void)
{
    bool dirty = false;

    char host[64] = {0};
    int32_t port = 0;
    int32_t reconn = 0;

    config_get_str(CFG_KEY_TCP_HOST, host, sizeof(host), "");
    if (!host[0]) {
        ESP_LOGW(TAG, "tcp.host empty, set default=%s", DEMO_TCP_DEFAULT_HOST);
        config_set_str(CFG_KEY_TCP_HOST, DEMO_TCP_DEFAULT_HOST);
        dirty = true;
    }

    config_get_int(CFG_KEY_TCP_PORT, &port, 0);
    if (port <= 0 || port > 65535) {
        ESP_LOGW(TAG, "tcp.port invalid=%ld, set default=%d", (long)port, DEMO_TCP_DEFAULT_PORT);
        config_set_int(CFG_KEY_TCP_PORT, DEMO_TCP_DEFAULT_PORT);
        dirty = true;
    }

    config_get_int(CFG_KEY_TCP_RECONN_MS, &reconn, 0);
    if (reconn < 200 || reconn > 60000) {
        ESP_LOGW(TAG, "tcp.reconn invalid=%ld, set default=%d", (long)reconn, DEMO_TCP_DEFAULT_RECONN_MS);
        config_set_int(CFG_KEY_TCP_RECONN_MS, DEMO_TCP_DEFAULT_RECONN_MS);
        dirty = true;
    }

    if (dirty) {
        ESP_ERROR_CHECK(config_commit());
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "== bridge_serial_tcp demo boot ==");

    ESP_ERROR_CHECK(config_service_init());
    ensure_demo_tcp_defaults();

    ESP_ERROR_CHECK(net_service_init());

    ESP_ERROR_CHECK(serial_service_init());
    ESP_ERROR_CHECK(serial_service_start());

    esp_err_t err = bridge_serial_tcp_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bridge start failed: %s", esp_err_to_name(err));
    }

    web_minimal_config_t wc = { .port = 80, .max_sockets = 7 };
    ESP_ERROR_CHECK(web_minimal_start(&wc));

    ESP_LOGI(TAG, "== demo ready: web=:80, bridge=tcp_client ==");
}
