#pragma once

/*
 * Internal vtable for protocol implementations.
 * Private header — DO NOT include from outside components/protocol_service.
 */

#include "protocol_service.h"

typedef struct protocol_vtable_s {
    esp_err_t (*start)(protocol_handle_t *h);
    esp_err_t (*stop)(protocol_handle_t *h);
    esp_err_t (*send)(protocol_handle_t *h, const uint8_t *data, size_t len);
    void      (*destroy)(protocol_handle_t *h);
} protocol_vtable_t;

struct protocol_handle_s {
    const protocol_vtable_t *vt;
    const char              *name;
    protocol_state_t         state;
    uint32_t                 tx_packets;
    uint32_t                 rx_packets;
    int32_t                  last_error;

    protocol_rx_cb_t         rx_cb;
    void                    *rx_user;
    protocol_event_cb_t      event_cb;
    void                    *event_user;

    void                    *impl;    /* concrete impl state */
};

/* Helpers for implementations. */
void protocol_base_init(protocol_handle_t *h,
                        const protocol_vtable_t *vt,
                        const char *name,
                        void *impl);

void protocol_notify_state(protocol_handle_t *h, protocol_state_t state);
void protocol_notify_rx(protocol_handle_t *h, const uint8_t *data, size_t len);
