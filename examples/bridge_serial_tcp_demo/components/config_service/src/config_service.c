#include "config_service.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "config_service";

static bool        s_inited;
static nvs_handle_t s_handle;

esp_err_t config_service_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(CONFIG_SERVICE_NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "initialised (ns=%s)", CONFIG_SERVICE_NVS_NAMESPACE);
    return ESP_OK;
}

esp_err_t config_service_deinit(void)
{
    if (!s_inited) return ESP_OK;
    nvs_close(s_handle);
    s_inited = false;
    return ESP_OK;
}

static esp_err_t check_ready(const char *key)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!key || !*key) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

esp_err_t config_get_str(const char *key, char *out, size_t out_sz, const char *def)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    if (!out || out_sz == 0) return ESP_ERR_INVALID_ARG;

    size_t req = out_sz;
    err = nvs_get_str(s_handle, key, out, &req);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (def) {
            strlcpy(out, def, out_sz);
        } else {
            out[0] = '\0';
        }
        return ESP_OK;
    }
    return err;
}

esp_err_t config_get_int(const char *key, int32_t *out, int32_t def)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    if (!out) return ESP_ERR_INVALID_ARG;

    err = nvs_get_i32(s_handle, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = def;
        return ESP_OK;
    }
    return err;
}

esp_err_t config_get_bool(const char *key, bool *out, bool def)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    if (!out) return ESP_ERR_INVALID_ARG;

    uint8_t v = 0;
    err = nvs_get_u8(s_handle, key, &v);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = def;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    *out = v != 0;
    return ESP_OK;
}

esp_err_t config_get_blob(const char *key, void *out, size_t *inout_sz)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    if (!out || !inout_sz) return ESP_ERR_INVALID_ARG;
    return nvs_get_blob(s_handle, key, out, inout_sz);
}

esp_err_t config_set_str(const char *key, const char *value)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    return nvs_set_str(s_handle, key, value ? value : "");
}

esp_err_t config_set_int(const char *key, int32_t value)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    return nvs_set_i32(s_handle, key, value);
}

esp_err_t config_set_bool(const char *key, bool value)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    return nvs_set_u8(s_handle, key, value ? 1 : 0);
}

esp_err_t config_set_blob(const char *key, const void *data, size_t sz)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    return nvs_set_blob(s_handle, key, data, sz);
}

esp_err_t config_erase(const char *key)
{
    esp_err_t err = check_ready(key);
    if (err != ESP_OK) return err;
    return nvs_erase_key(s_handle, key);
}

esp_err_t config_commit(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    return nvs_commit(s_handle);
}

esp_err_t config_reset_defaults(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_erase_all(s_handle);
    if (err != ESP_OK) return err;
    return nvs_commit(s_handle);
}

esp_err_t config_validate_int_range(int32_t value, int32_t min, int32_t max)
{
    return (value >= min && value <= max) ? ESP_OK : ESP_ERR_INVALID_ARG;
}
