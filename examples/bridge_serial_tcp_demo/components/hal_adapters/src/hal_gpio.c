#include "hal_gpio.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "hal_gpio";
static bool s_isr_service_installed;

static esp_err_t ensure_isr_service(void)
{
    if (s_isr_service_installed) return ESP_OK;
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_isr_service_installed = true;
        return ESP_OK;
    }
    return err;
}

esp_err_t hal_gpio_configure(int pin, hal_gpio_mode_t mode)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << pin,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    switch (mode) {
    case HAL_GPIO_INPUT:            c.mode = GPIO_MODE_INPUT;  c.pull_up_en = 0; c.pull_down_en = 0; break;
    case HAL_GPIO_OUTPUT:           c.mode = GPIO_MODE_OUTPUT; break;
    case HAL_GPIO_INPUT_PULLUP:     c.mode = GPIO_MODE_INPUT;  c.pull_up_en = 1; break;
    case HAL_GPIO_INPUT_PULLDOWN:   c.mode = GPIO_MODE_INPUT;  c.pull_down_en = 1; break;
    default: return ESP_ERR_INVALID_ARG;
    }
    return gpio_config(&c);
}

esp_err_t hal_gpio_set_level(int pin, bool level)
{
    return gpio_set_level(pin, level ? 1 : 0);
}

esp_err_t hal_gpio_get_level(int pin, bool *out_level)
{
    if (!out_level) return ESP_ERR_INVALID_ARG;
    *out_level = gpio_get_level(pin) != 0;
    return ESP_OK;
}

esp_err_t hal_gpio_register_isr(int pin, hal_gpio_irq_t trigger,
                                hal_gpio_isr_t isr, void *arg)
{
    gpio_int_type_t t;
    switch (trigger) {
    case HAL_GPIO_IRQ_DISABLE: t = GPIO_INTR_DISABLE;   break;
    case HAL_GPIO_IRQ_RISING:  t = GPIO_INTR_POSEDGE;   break;
    case HAL_GPIO_IRQ_FALLING: t = GPIO_INTR_NEGEDGE;   break;
    case HAL_GPIO_IRQ_ANY:     t = GPIO_INTR_ANYEDGE;   break;
    default: return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_isr_service();
    if (err != ESP_OK) return err;
    err = gpio_set_intr_type(pin, t);
    if (err != ESP_OK) return err;
    err = gpio_isr_handler_add(pin, isr, arg);
    if (err != ESP_OK) ESP_LOGW(TAG, "isr_handler_add(%d): %s", pin, esp_err_to_name(err));
    return err;
}

esp_err_t hal_gpio_unregister_isr(int pin)
{
    return gpio_isr_handler_remove(pin);
}
