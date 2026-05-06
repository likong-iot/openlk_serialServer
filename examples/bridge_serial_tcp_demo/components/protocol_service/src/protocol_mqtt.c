/*
 * Real MQTT implementation (esp-mqtt).  Subscribes to the configured sub
 * topic and forwards received payloads as rx events; protocol_send() publishes
 * to the pub topic.  Business (when/what to publish) is NOT done here.
 */

#include "protocol_service_internal.h"
#include "config_service.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "mqtt_client.h"

typedef struct {
    esp_mqtt_client_handle_t client;
    char  uri[128];
    char  client_id[48];
    char  user[48];
    char  pass[64];
    char  pub_topic[64];
    char  sub_topic[64];
    int   qos;
} mqtt_state_t;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    protocol_handle_t *h  = arg;
    mqtt_state_t      *st = h->impl;
    esp_mqtt_event_handle_t ev = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        protocol_notify_state(h, PROTOCOL_STATE_CONNECTED);
        if (st->sub_topic[0]) {
            esp_mqtt_client_subscribe(st->client, st->sub_topic, st->qos);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        protocol_notify_state(h, PROTOCOL_STATE_DISCONNECTED);
        break;
    case MQTT_EVENT_DATA:
        protocol_notify_rx(h, (const uint8_t *)ev->data, ev->data_len);
        break;
    case MQTT_EVENT_ERROR:
        h->last_error = ESP_FAIL;
        protocol_notify_state(h, PROTOCOL_STATE_ERROR);
        break;
    default: break;
    }
}

static esp_err_t mqtt_start(protocol_handle_t *h)
{
    mqtt_state_t *st = h->impl;
    if (st->client) return ESP_OK;

    config_get_str(CFG_KEY_MQTT_URI,        st->uri,       sizeof(st->uri),       "");
    config_get_str(CFG_KEY_MQTT_CLIENT_ID,  st->client_id, sizeof(st->client_id), "");
    config_get_str(CFG_KEY_MQTT_USER,       st->user,      sizeof(st->user),      "");
    config_get_str(CFG_KEY_MQTT_PASS,       st->pass,      sizeof(st->pass),      "");
    config_get_str(CFG_KEY_MQTT_PUB_TOPIC,  st->pub_topic, sizeof(st->pub_topic), "");
    config_get_str(CFG_KEY_MQTT_SUB_TOPIC,  st->sub_topic, sizeof(st->sub_topic), "");
    int32_t v;
    config_get_int(CFG_KEY_MQTT_QOS, &v, 0); st->qos = (int)v;

    if (!st->uri[0]) return ESP_ERR_INVALID_STATE;

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = st->uri;
    if (st->client_id[0]) cfg.credentials.client_id = st->client_id;
    if (st->user[0])      cfg.credentials.username  = st->user;
    if (st->pass[0])      cfg.credentials.authentication.password = st->pass;

    st->client = esp_mqtt_client_init(&cfg);
    if (!st->client) return ESP_FAIL;

    esp_mqtt_client_register_event(st->client, ESP_EVENT_ANY_ID, on_event, h);
    protocol_notify_state(h, PROTOCOL_STATE_STARTING);
    return esp_mqtt_client_start(st->client);
}

static esp_err_t mqtt_stop(protocol_handle_t *h)
{
    mqtt_state_t *st = h->impl;
    if (!st->client) return ESP_OK;
    esp_mqtt_client_stop(st->client);
    esp_mqtt_client_destroy(st->client);
    st->client = NULL;
    protocol_notify_state(h, PROTOCOL_STATE_STOPPED);
    return ESP_OK;
}

static esp_err_t mqtt_send(protocol_handle_t *h, const uint8_t *data, size_t len)
{
    mqtt_state_t *st = h->impl;
    if (!st->client || !st->pub_topic[0]) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(st->client, st->pub_topic,
                                         (const char *)data, (int)len,
                                         st->qos, 0);
    return msg_id < 0 ? ESP_FAIL : ESP_OK;
}

static void mqtt_destroy(protocol_handle_t *h)
{
    mqtt_state_t *st = h->impl;
    if (st) { mqtt_stop(h); free(st); }
    free(h);
}

static const protocol_vtable_t VT = {
    .start = mqtt_start, .stop = mqtt_stop, .send = mqtt_send, .destroy = mqtt_destroy,
};

protocol_handle_t *protocol_mqtt_create(void)
{
    protocol_handle_t *h = calloc(1, sizeof(*h));
    mqtt_state_t      *s = calloc(1, sizeof(*s));
    if (!h || !s) { free(h); free(s); return NULL; }
    protocol_base_init(h, &VT, "mqtt", s);
    return h;
}
