#include "hal_timer.h"

#include <stdlib.h>
#include "esp_timer.h"

struct hal_timer_s {
    esp_timer_handle_t t;
};

esp_err_t hal_timer_create(const char *name, hal_timer_cb_t cb, void *arg,
                           hal_timer_handle_t *out)
{
    if (!cb || !out) return ESP_ERR_INVALID_ARG;
    struct hal_timer_s *h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;

    esp_timer_create_args_t args = {
        .callback        = (esp_timer_cb_t)cb,
        .arg             = arg,
        .name            = name ? name : "hal_timer",
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_err_t err = esp_timer_create(&args, &h->t);
    if (err != ESP_OK) { free(h); return err; }
    *out = h;
    return ESP_OK;
}

esp_err_t hal_timer_start_once(hal_timer_handle_t h, uint64_t delay_us)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    return esp_timer_start_once(h->t, delay_us);
}

esp_err_t hal_timer_start_periodic(hal_timer_handle_t h, uint64_t period_us)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    return esp_timer_start_periodic(h->t, period_us);
}

esp_err_t hal_timer_stop(hal_timer_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    return esp_timer_stop(h->t);
}

esp_err_t hal_timer_destroy(hal_timer_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    esp_timer_delete(h->t);
    free(h);
    return ESP_OK;
}
