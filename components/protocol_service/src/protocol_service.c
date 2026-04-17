#include "protocol_service.h"
#include "protocol_service_internal.h"

#include <string.h>

void protocol_base_init(protocol_handle_t *h,
                        const protocol_vtable_t *vt,
                        const char *name,
                        void *impl)
{
    memset(h, 0, sizeof(*h));
    h->vt    = vt;
    h->name  = name;
    h->impl  = impl;
    h->state = PROTOCOL_STATE_STOPPED;
}

void protocol_notify_state(protocol_handle_t *h, protocol_state_t state)
{
    h->state = state;
    if (h->event_cb) h->event_cb(h, state, h->event_user);
}

void protocol_notify_rx(protocol_handle_t *h, const uint8_t *data, size_t len)
{
    h->rx_packets++;
    if (h->rx_cb) h->rx_cb(h, data, len, h->rx_user);
}

esp_err_t protocol_start(protocol_handle_t *h)
{
    if (!h || !h->vt || !h->vt->start) return ESP_ERR_INVALID_ARG;
    return h->vt->start(h);
}

esp_err_t protocol_stop(protocol_handle_t *h)
{
    if (!h || !h->vt || !h->vt->stop) return ESP_ERR_INVALID_ARG;
    return h->vt->stop(h);
}

esp_err_t protocol_send(protocol_handle_t *h, const uint8_t *data, size_t len)
{
    if (!h || !h->vt || !h->vt->send) return ESP_ERR_INVALID_ARG;
    esp_err_t err = h->vt->send(h, data, len);
    if (err == ESP_OK) h->tx_packets++;
    else               h->last_error = err;
    return err;
}

esp_err_t protocol_get_status(protocol_handle_t *h, protocol_status_t *out)
{
    if (!h || !out) return ESP_ERR_INVALID_ARG;
    out->name        = h->name;
    out->state       = h->state;
    out->tx_packets  = h->tx_packets;
    out->rx_packets  = h->rx_packets;
    out->last_error  = h->last_error;
    return ESP_OK;
}

void protocol_destroy(protocol_handle_t *h)
{
    if (!h) return;
    if (h->vt && h->vt->destroy) h->vt->destroy(h);
}

esp_err_t protocol_register_rx_cb(protocol_handle_t *h, protocol_rx_cb_t cb, void *user_data)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    h->rx_cb = cb; h->rx_user = user_data;
    return ESP_OK;
}

esp_err_t protocol_register_event_cb(protocol_handle_t *h, protocol_event_cb_t cb, void *user_data)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    h->event_cb = cb; h->event_user = user_data;
    return ESP_OK;
}
