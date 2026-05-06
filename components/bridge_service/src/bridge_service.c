#include "bridge_service.h"
#include "config_service.h"
#include "serial_service.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bridge_svc";

static struct {
    SemaphoreHandle_t  lock;
    bool               inited;
    bool               running;          /* a protocol is currently spawned */
    bool               serial_subscribed;
    bridge_mode_t      mode;
    protocol_handle_t *proto;
    bridge_status_t    status;
} g;

static const char *MODE_NAMES[BRIDGE_MODE__COUNT] = {
    [BRIDGE_MODE_OFF]        = "off",
    [BRIDGE_MODE_TCP_CLIENT] = "tcp_client",
    [BRIDGE_MODE_TCP_SERVER] = "tcp_server",
    [BRIDGE_MODE_UDP]        = "udp",
    [BRIDGE_MODE_MQTT]       = "mqtt",
    [BRIDGE_MODE_HTTP]       = "http",
};

const char *bridge_mode_str(bridge_mode_t m)
{
    return (m >= 0 && m < BRIDGE_MODE__COUNT) ? MODE_NAMES[m] : "off";
}

bridge_mode_t bridge_mode_from(const char *s)
{
    if (!s) return BRIDGE_MODE_OFF;
    for (int i = 0; i < BRIDGE_MODE__COUNT; ++i) {
        if (strcmp(s, MODE_NAMES[i]) == 0) return (bridge_mode_t)i;
    }
    return BRIDGE_MODE_OFF;
}

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

/* ── data path ───────────────────────────────────────────────────────── */

/* serial RX → protocol.  Called from serial RX task; keep it short. */
static void on_serial_rx(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    /* Sample the handle under lock, then drop the lock before calling
     * into the protocol (which may take its own locks / block briefly). */
    protocol_handle_t *p = NULL;
    bool connected = false;
    if (xSemaphoreTake(g.lock, 0) == pdTRUE) {
        p = g.proto;
        connected = (g.status.proto_state == PROTOCOL_STATE_CONNECTED);
        xSemaphoreGive(g.lock);
    }
    if (!p || !connected) return;

    esp_err_t err = protocol_send(p, data, len);
    if (err == ESP_OK) {
        g.status.tx_bytes   += len;
        g.status.tx_packets += 1;
    } else {
        g.status.last_error = err;
    }
}

/* protocol RX → serial. */
static void on_proto_rx(protocol_handle_t *h, const uint8_t *data, size_t len, void *user)
{
    (void)h; (void)user;
    esp_err_t err = serial_service_send(data, len);
    if (err == ESP_OK) {
        g.status.rx_bytes   += len;
        g.status.rx_packets += 1;
    } else {
        g.status.last_error = err;
    }
}

static void on_proto_event(protocol_handle_t *h, protocol_state_t s, void *user)
{
    (void)h; (void)user;
    g.status.proto_state = s;
    ESP_LOGI(TAG, "proto state=%d", (int)s);
}

/* ── lifecycle helpers (caller holds g.lock) ─────────────────────────── */

static void teardown_locked(void)
{
    if (!g.proto) return;
    /* Detach callbacks first so a late event does not arrive on a freed handle. */
    protocol_register_rx_cb   (g.proto, NULL, NULL);
    protocol_register_event_cb(g.proto, NULL, NULL);
    protocol_stop   (g.proto);
    protocol_destroy(g.proto);
    g.proto = NULL;
    g.status.proto_state = PROTOCOL_STATE_STOPPED;
    g.running = false;
}

static esp_err_t spawn_locked(bridge_mode_t mode)
{
    g.status.mode        = mode;
    g.status.proto_state = PROTOCOL_STATE_STOPPED;
    g.status.tx_bytes = g.status.rx_bytes = 0;
    g.status.tx_packets = g.status.rx_packets = 0;
    g.status.last_error  = 0;
    g.status.started_ms  = now_ms();

    protocol_handle_t *p = NULL;
    switch (mode) {
    case BRIDGE_MODE_OFF:        return ESP_OK;
    case BRIDGE_MODE_TCP_CLIENT: p = protocol_tcp_client_create(); break;
    case BRIDGE_MODE_TCP_SERVER: p = protocol_tcp_server_create(); break;
    case BRIDGE_MODE_UDP:        p = protocol_udp_create();        break;
    case BRIDGE_MODE_MQTT:       p = protocol_mqtt_create();       break;
    case BRIDGE_MODE_HTTP:       p = protocol_http_create();       break;
    default:                     return ESP_ERR_INVALID_ARG;
    }
    if (!p) return ESP_ERR_NO_MEM;

    protocol_register_rx_cb   (p, on_proto_rx,    NULL);
    protocol_register_event_cb(p, on_proto_event, NULL);
    g.proto   = p;
    g.running = true;

    esp_err_t err = protocol_start(p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "protocol_start(%s) failed: %s",
                 bridge_mode_str(mode), esp_err_to_name(err));
        g.status.last_error = err;
        teardown_locked();
    }
    return err;
}

/* ── public API ──────────────────────────────────────────────────────── */

esp_err_t bridge_service_init(void)
{
    if (g.inited) return ESP_OK;
    g.lock = xSemaphoreCreateMutex();
    if (!g.lock) return ESP_ERR_NO_MEM;

    char buf[16] = {0};
    config_get_str(CFG_KEY_WORKMODE, buf, sizeof(buf), "off");
    g.mode        = bridge_mode_from(buf);
    g.status.mode = g.mode;

    /* Subscribe once for the lifetime of the service. The cb checks the
     * proto pointer + state, so an idle bridge is harmless. */
    esp_err_t err = serial_service_register_rx_cb(on_serial_rx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "serial_service_register_rx_cb: %s", esp_err_to_name(err));
        return err;
    }
    g.serial_subscribed = true;
    g.inited = true;
    ESP_LOGI(TAG, "init mode=%s", bridge_mode_str(g.mode));
    return ESP_OK;
}

esp_err_t bridge_service_start(void)
{
    if (!g.inited) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(g.lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!g.running && g.mode != BRIDGE_MODE_OFF) {
        err = spawn_locked(g.mode);
    }
    xSemaphoreGive(g.lock);
    return err;
}

esp_err_t bridge_service_stop(void)
{
    if (!g.inited) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(g.lock, portMAX_DELAY);
    teardown_locked();
    xSemaphoreGive(g.lock);
    return ESP_OK;
}

esp_err_t bridge_service_apply_mode(bridge_mode_t mode)
{
    if (!g.inited)                          return ESP_ERR_INVALID_STATE;
    if (mode < 0 || mode >= BRIDGE_MODE__COUNT) return ESP_ERR_INVALID_ARG;

    esp_err_t err = config_set_str(CFG_KEY_WORKMODE, bridge_mode_str(mode));
    if (err == ESP_OK) err = config_commit();
    if (err != ESP_OK) return err;

    xSemaphoreTake(g.lock, portMAX_DELAY);
    teardown_locked();
    g.mode = mode;
    if (mode != BRIDGE_MODE_OFF) {
        err = spawn_locked(mode);
    }
    xSemaphoreGive(g.lock);
    return err;
}

esp_err_t bridge_service_get_status(bridge_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!g.inited) { memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_STATE; }
    xSemaphoreTake(g.lock, portMAX_DELAY);
    *out = g.status;
    out->mode = g.mode;
    xSemaphoreGive(g.lock);
    return ESP_OK;
}
