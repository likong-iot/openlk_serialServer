#include "net_service.h"
/* net_service: WiFi STA/AP bring-up, optional Ethernet, static-IP application
 * and reconnect policy. Swap this file (or the whole folder) per board variant
 * without touching the service layer.
 *
 * Bring-up order:
 *   1. esp_netif_init + default event loop
 *   2. Always create AP netif (admin access while primary is down)
 *   3. If mode=="eth" → init ethernet_init + eth netif + eth_netif glue
 *      Else (default, "wifi") → create STA netif and start WiFi STA
 *   4. WiFi AP always starts so the web UI is reachable at 192.168.4.1
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_eth_netif_glue.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"

#include "config_service.h"
#include "ethernet_init.h"
#include "dns_redirect.h"

static const char *TAG = "net";

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_eth_netif;
static esp_eth_handle_t *s_eth_handles;
static uint8_t       s_eth_cnt;
static TaskHandle_t  s_reconnect_task;

static struct {
    char    mode[8];            /* "wifi" | "eth" */
    bool    link_up;            /* STA joined | ETH link up */
    bool    got_ip;
    bool    sta_configured;     /* whether STA credentials are present */
    int8_t  rssi;
    char    ssid[33];
    char    ip[16];
    char    mask[16];
    char    gw[16];
    uint32_t backoff_ms;
} s;

#define BACKOFF_MIN_MS   1000
#define BACKOFF_MAX_MS   60000

static void apply_static_ip(esp_netif_t *netif)
{
    bool dhcp = true;
    config_get_bool(CFG_KEY_NET_DHCP, &dhcp, true);
    if (dhcp || !netif) return;

    char ip[16]={0}, mask[16]={0}, gw[16]={0};
    config_get_str(CFG_KEY_NET_IP,   ip,   sizeof(ip),   "");
    config_get_str(CFG_KEY_NET_MASK, mask, sizeof(mask), "");
    config_get_str(CFG_KEY_NET_GW,   gw,   sizeof(gw),   "");

    if (!*ip || !*mask || !*gw) {
        ESP_LOGW(TAG, "static IP requested but incomplete — falling back to DHCP");
        return;
    }

    esp_netif_ip_info_t info = {0};
    info.ip.addr      = ipaddr_addr(ip);
    info.netmask.addr = ipaddr_addr(mask);
    info.gw.addr      = ipaddr_addr(gw);

    esp_netif_dhcpc_stop(netif);
    esp_netif_set_ip_info(netif, &info);
    ESP_LOGI(TAG, "static IP applied to %s: %s/%s gw %s",
             esp_netif_get_desc(netif), ip, mask, gw);
}

/* -- WiFi STA events ------------------------------------------------------- */

static void schedule_reconnect(void)
{
    if (s.backoff_ms < BACKOFF_MIN_MS) s.backoff_ms = BACKOFF_MIN_MS;
    ESP_LOGI(TAG, "STA reconnect in %lu ms", (unsigned long)s.backoff_ms);
    vTaskDelay(pdMS_TO_TICKS(s.backoff_ms));
    if (s.link_up) return;
    s.backoff_ms = (s.backoff_ms * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : s.backoff_ms * 2;
    esp_wifi_connect();
}

static void reconnect_task(void *arg)
{
    (void)arg;
    schedule_reconnect();
    s_reconnect_task = NULL;
    vTaskDelete(NULL);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            apply_static_ip(s_sta_netif);
            if (s.sta_configured) {
                esp_wifi_connect();
            } else {
                ESP_LOGI(TAG, "STA enabled but no credentials configured");
            }
            break;
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *e = data;
            strlcpy(s.ssid, (const char *)e->ssid, sizeof(s.ssid));
            s.link_up = true;
            s.backoff_ms = BACKOFF_MIN_MS;
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
            s.link_up = false;
            s.got_ip  = false;
            if (s.sta_configured) {
                if (!s_reconnect_task) {
                    BaseType_t ok = xTaskCreate(reconnect_task, "wifi_rc", 2048, NULL, 3, &s_reconnect_task);
                    if (ok != pdPASS) {
                        s_reconnect_task = NULL;
                        ESP_LOGW(TAG, "failed to create reconnect task");
                    }
                }
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:    ESP_LOGI(TAG, "AP: sta joined"); break;
        case WIFI_EVENT_AP_STADISCONNECTED: ESP_LOGI(TAG, "AP: sta left");   break;
        default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        esp_ip4addr_ntoa(&e->ip_info.ip,      s.ip,   sizeof(s.ip));
        esp_ip4addr_ntoa(&e->ip_info.netmask, s.mask, sizeof(s.mask));
        esp_ip4addr_ntoa(&e->ip_info.gw,      s.gw,   sizeof(s.gw));
        s.got_ip = true;
        ESP_LOGI(TAG, "STA got IP: %s", s.ip);
    }
}

/* -- Ethernet events ------------------------------------------------------- */

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == ETH_EVENT) {
        switch (id) {
        case ETHERNET_EVENT_CONNECTED:
            s.link_up = true;
            apply_static_ip(s_eth_netif);
            ESP_LOGI(TAG, "ETH link up");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            s.link_up = false;
            s.got_ip  = false;
            ESP_LOGI(TAG, "ETH link down");
            break;
        default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *e = data;
        esp_ip4addr_ntoa(&e->ip_info.ip,      s.ip,   sizeof(s.ip));
        esp_ip4addr_ntoa(&e->ip_info.netmask, s.mask, sizeof(s.mask));
        esp_ip4addr_ntoa(&e->ip_info.gw,      s.gw,   sizeof(s.gw));
        s.got_ip = true;
        ESP_LOGI(TAG, "ETH got IP: %s", s.ip);
    }
}

/* -- Bring-up helpers ------------------------------------------------------ */

static void make_default_ap_ssid(char *out, size_t out_sz)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, out_sz, "Gateway-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static esp_err_t bring_up_wifi(bool with_sta)
{
    s_ap_netif  = esp_netif_create_default_wifi_ap();
    if (with_sta) s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wic));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));

    char ap_ssid[33] = {0}, ap_pass[65] = {0};
    config_get_str(CFG_KEY_WIFI_AP_SSID, ap_ssid, sizeof(ap_ssid), "");
    config_get_str(CFG_KEY_WIFI_AP_PASS, ap_pass, sizeof(ap_pass), "");
    if (!ap_ssid[0]) make_default_ap_ssid(ap_ssid, sizeof(ap_ssid));

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len       = strlen(ap_ssid);
    ap.ap.channel        = 1;
    ap.ap.max_connection = 4;
    if (strlen(ap_pass) >= 8) {
        strlcpy((char *)ap.ap.password, ap_pass, sizeof(ap.ap.password));
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }

    char sta_ssid[33] = {0}, sta_pass[65] = {0};
    wifi_config_t sta = {0};
    if (with_sta) {
        config_get_str(CFG_KEY_WIFI_SSID, sta_ssid, sizeof(sta_ssid), "");
        config_get_str(CFG_KEY_WIFI_PASS, sta_pass, sizeof(sta_pass), "");
        if (sta_ssid[0]) {
            strlcpy((char *)sta.sta.ssid,     sta_ssid, sizeof(sta.sta.ssid));
            strlcpy((char *)sta.sta.password, sta_pass, sizeof(sta.sta.password));
            sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        }
    }

    s.sta_configured = with_sta && sta_ssid[0];
    wifi_mode_t mode = with_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (with_sta) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Captive-portal DNS: redirect every A query to the AP's gateway IP so
     * phones/tablets auto-open the configuration page when joining the SoftAP. */
    esp_netif_ip_info_t ap_ip = {0};
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ap_ip) == ESP_OK) {
        dns_redirect_start(ap_ip.ip.addr);
    }

    ESP_LOGI(TAG, "AP=\"%s\" (%s)  STA=%s",
             ap_ssid,
             ap.ap.authmode == WIFI_AUTH_OPEN ? "open" : "WPA2",
             s.sta_configured ? sta_ssid : "<configured=no>");
    return ESP_OK;
}

static esp_err_t bring_up_eth(void)
{
    esp_err_t err = example_eth_init(&s_eth_handles, &s_eth_cnt);
    if (err != ESP_OK || s_eth_cnt == 0) {
        ESP_LOGW(TAG, "no ethernet hw detected (%s) — falling back to WiFi",
                 esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&ncfg);
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif,
                                     esp_eth_new_netif_glue(s_eth_handles[0])));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP, on_eth_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handles[0]));

    ESP_LOGI(TAG, "Ethernet started");
    return ESP_OK;
}

/* -- Public API ------------------------------------------------------------ */

esp_err_t net_service_init(void)
{
    s.backoff_ms = BACKOFF_MIN_MS;
    config_get_str(CFG_KEY_NET_MODE, s.mode, sizeof(s.mode), "wifi");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    bool want_eth = strcmp(s.mode, "eth") == 0;

    if (want_eth) {
        /* ETH primary; still need WiFi AP for management. */
        ESP_ERROR_CHECK(bring_up_wifi(/*with_sta=*/false));
        if (bring_up_eth() != ESP_OK) {
            ESP_LOGW(TAG, "eth failed — staying on AP only");
        }
    } else {
        ESP_ERROR_CHECK(bring_up_wifi(/*with_sta=*/true));
    }
    return ESP_OK;
}

esp_err_t net_service_get_status(net_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    strlcpy(out->mode,    s.mode, sizeof(out->mode));
    out->wifi_up = s.link_up;
    out->got_ip  = s.got_ip;
    strlcpy(out->ssid,    s.ssid, sizeof(out->ssid));
    strlcpy(out->ip,      s.ip,   sizeof(out->ip));
    strlcpy(out->mask,    s.mask, sizeof(out->mask));
    strlcpy(out->gateway, s.gw,   sizeof(out->gateway));

    wifi_ap_record_t ap;
    if (strcmp(s.mode, "wifi") == 0 && s.link_up &&
        esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out->rssi = ap.rssi;
    }

    out->uptime_ms     = esp_timer_get_time() / 1000;
    out->free_heap     = esp_get_free_heap_size();
    out->min_free_heap = esp_get_minimum_free_heap_size();
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    ESP_LOGW(TAG, "reboot in %lu ms", (unsigned long)delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}

void net_service_request_reboot(uint32_t delay_ms)
{
    if (delay_ms < 200) delay_ms = 200;
    xTaskCreate(reboot_task, "reboot", 2048, (void *)(uintptr_t)delay_ms, 5, NULL);
}
