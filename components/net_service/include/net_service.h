#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Network platform — owns TCP/IP stack init, WiFi bring-up, static-IP
 * application and reconnect policy.
 *
 * Firmware orchestration lives here, NOT in `app_main.c`, so that a board
 * variant can swap the whole file without touching the service layer.
 */

typedef struct {
    bool     wifi_up;           /* STA joined an AP */
    bool     got_ip;            /* STA has an IP (static or DHCP-assigned) */
    char     mode[8];           /* "wifi" | "eth" */
    char     ssid[33];
    int8_t   rssi;              /* dBm, 0 if not connected */
    char     ip[16];
    char     mask[16];
    char     gateway[16];
    uint64_t uptime_ms;
    uint32_t free_heap;
    uint32_t min_free_heap;
} net_status_t;

esp_err_t net_service_init(void);

esp_err_t net_service_get_status(net_status_t *out);

/* Trigger an async reboot after `delay_ms`. Safe to call from HTTP handlers. */
void net_service_request_reboot(uint32_t delay_ms);

/* Blocking WiFi scan. Fills up to `cap` entries into `out`; writes the count
 * to `*out_n`. Takes ~1–2 s on active scan. Safe to call from HTTP threads. */
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    uint8_t authmode;   /* raw wifi_auth_mode_t */
} net_scan_ap_t;

esp_err_t net_service_scan(net_scan_ap_t *out, size_t cap, size_t *out_n);

#ifdef __cplusplus
}
#endif
