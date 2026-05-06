#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One-shot / periodic timer adapter, backed by esp_timer.
 * Purely convenience — services use it for housekeeping ticks.
 */

typedef struct hal_timer_s *hal_timer_handle_t;
typedef void (*hal_timer_cb_t)(void *arg);

esp_err_t hal_timer_create(const char *name, hal_timer_cb_t cb, void *arg,
                           hal_timer_handle_t *out);
esp_err_t hal_timer_start_once(hal_timer_handle_t h, uint64_t delay_us);
esp_err_t hal_timer_start_periodic(hal_timer_handle_t h, uint64_t period_us);
esp_err_t hal_timer_stop(hal_timer_handle_t h);
esp_err_t hal_timer_destroy(hal_timer_handle_t h);

#ifdef __cplusplus
}
#endif
