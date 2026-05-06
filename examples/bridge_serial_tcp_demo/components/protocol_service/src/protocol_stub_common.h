#pragma once

/*
 * Boilerplate used by every stub protocol implementation.
 *
 * A stub exposes the full interface but performs no real IO. It is valid for
 * the base-distribution firmware per requirement FR-PRO-005.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "protocol_service_internal.h"

static inline esp_err_t protocol_stub_start(protocol_handle_t *h)
{
    ESP_LOGI(h->name, "stub start");
    protocol_notify_state(h, PROTOCOL_STATE_CONNECTED);
    return ESP_OK;
}

static inline esp_err_t protocol_stub_stop(protocol_handle_t *h)
{
    ESP_LOGI(h->name, "stub stop");
    protocol_notify_state(h, PROTOCOL_STATE_STOPPED);
    return ESP_OK;
}

static inline esp_err_t protocol_stub_send(protocol_handle_t *h,
                                           const uint8_t *data, size_t len)
{
    (void)data;
    ESP_LOGD(h->name, "stub send %u bytes", (unsigned)len);
    return ESP_OK;
}

static inline void protocol_stub_destroy(protocol_handle_t *h)
{
    free(h);
}

#define PROTOCOL_DEFINE_STUB(factory_name, display_name)                       \
    static const protocol_vtable_t s_vt_##factory_name = {                     \
        .start   = protocol_stub_start,                                        \
        .stop    = protocol_stub_stop,                                         \
        .send    = protocol_stub_send,                                         \
        .destroy = protocol_stub_destroy,                                      \
    };                                                                         \
    protocol_handle_t *factory_name(void)                                      \
    {                                                                          \
        protocol_handle_t *h = calloc(1, sizeof(*h));                          \
        if (!h) return NULL;                                                   \
        protocol_base_init(h, &s_vt_##factory_name, display_name, NULL);       \
        return h;                                                              \
    }
