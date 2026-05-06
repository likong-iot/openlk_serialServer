#include "net_service.h"

#include <string.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_log.h"

static const char *TAG = "net_scan";

esp_err_t net_service_scan(net_scan_ap_t *out, size_t cap, size_t *out_n)
{
    if (!out || !out_n || cap == 0) return ESP_ERR_INVALID_ARG;

    wifi_scan_config_t cfg = {
        .scan_type             = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min  = 60,
        .scan_time.active.max  = 150,
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) { ESP_LOGW(TAG, "scan_start: %s", esp_err_to_name(err)); return err; }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) { *out_n = 0; return ESP_OK; }

    wifi_ap_record_t *records = calloc(n, sizeof(*records));
    if (!records) { esp_wifi_clear_ap_list(); return ESP_ERR_NO_MEM; }
    esp_wifi_scan_get_ap_records(&n, records);

    size_t kept = 0;
    for (size_t i = 0; i < n && kept < cap; ++i) {
        /* De-dup by SSID, keep the strongest. */
        bool duplicate = false;
        for (size_t j = 0; j < kept; ++j) {
            if (strncmp(out[j].ssid, (const char *)records[i].ssid, sizeof(out[j].ssid)) == 0) {
                duplicate = true;
                if (records[i].rssi > out[j].rssi) out[j].rssi = records[i].rssi;
                break;
            }
        }
        if (duplicate || records[i].ssid[0] == 0) continue;

        strlcpy(out[kept].ssid, (const char *)records[i].ssid, sizeof(out[kept].ssid));
        out[kept].rssi     = records[i].rssi;
        out[kept].channel  = records[i].primary;
        out[kept].authmode = (uint8_t)records[i].authmode;
        kept++;
    }
    free(records);
    *out_n = kept;
    return ESP_OK;
}
