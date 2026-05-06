/*
 * Base-distribution firmware entry point.
 *
 * Bring-up order only. Every concrete capability lives in a component under
 * ../components/.  Network bring-up is delegated to net_platform.c so that a
 * board variant can replace one file without touching the service layer.
 */

#include "esp_log.h"
#include "config_service.h"
#include "serial_service.h"
#include "web_minimal.h"
#include "net_service.h"
#include "auth_service.h"
#include "bridge_service.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "── base firmware boot ──");

    ESP_ERROR_CHECK(config_service_init());
    ESP_ERROR_CHECK(auth_service_init());

    ESP_ERROR_CHECK(net_service_init());

    ESP_ERROR_CHECK(serial_service_init());
    ESP_ERROR_CHECK(serial_service_start());

    /* Bridge wires serial ↔ active protocol per saved work mode.
     * init() subscribes to serial RX; start() spawns the protocol. */
    ESP_ERROR_CHECK(bridge_service_init());
    esp_err_t br = bridge_service_start();
    if (br != ESP_OK) {
        /* A bad config (e.g. empty TCP host) shouldn't abort boot —
         * Web UI must still come up so the user can fix it. */
        ESP_LOGW(TAG, "bridge_service_start: %s", esp_err_to_name(br));
    }

    web_minimal_config_t wc = { .port = 80, .max_sockets = 7 };
    ESP_ERROR_CHECK(web_minimal_start(&wc));

    ESP_LOGI(TAG, "── ready — HTTP on :80 ──");
}
