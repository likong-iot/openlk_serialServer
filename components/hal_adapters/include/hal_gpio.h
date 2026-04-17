#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin adapter over the chip GPIO driver. Higher-level services must not
 * include the chip-specific driver header directly.
 */

typedef enum {
    HAL_GPIO_INPUT = 0,
    HAL_GPIO_OUTPUT,
    HAL_GPIO_INPUT_PULLUP,
    HAL_GPIO_INPUT_PULLDOWN,
} hal_gpio_mode_t;

typedef enum {
    HAL_GPIO_IRQ_DISABLE = 0,
    HAL_GPIO_IRQ_RISING,
    HAL_GPIO_IRQ_FALLING,
    HAL_GPIO_IRQ_ANY,
} hal_gpio_irq_t;

typedef void (*hal_gpio_isr_t)(void *arg);

esp_err_t hal_gpio_configure(int pin, hal_gpio_mode_t mode);
esp_err_t hal_gpio_set_level(int pin, bool level);
esp_err_t hal_gpio_get_level(int pin, bool *out_level);
esp_err_t hal_gpio_register_isr(int pin, hal_gpio_irq_t trigger,
                                hal_gpio_isr_t isr, void *arg);
esp_err_t hal_gpio_unregister_isr(int pin);

#ifdef __cplusplus
}
#endif
