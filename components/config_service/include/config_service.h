#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Config Service — the single gateway to persistent configuration.
 *
 * Design rules enforced by this interface:
 *   - Upper layers NEVER touch NVS directly.
 *   - Keys are always referenced via CFG_KEY_* macros in config_keys.h.
 *   - All setters stage the value; a commit() is required to persist.
 *
 * Return semantics:
 *   ESP_OK                 success
 *   ESP_ERR_INVALID_ARG    null pointer / bad key
 *   ESP_ERR_NOT_FOUND      key absent (getters fall back to default silently)
 *   ESP_ERR_INVALID_SIZE   buffer too small
 *   ESP_ERR_INVALID_STATE  service not initialised
 *   other                  propagated from the underlying driver
 */

typedef struct {
    const char *key;
    int32_t     min;
    int32_t     max;
} config_int_range_t;

/* Lifecycle */
esp_err_t config_service_init(void);
esp_err_t config_service_deinit(void);

/* Read — out-parameter is written only on success. `def` is returned (via out)
 * when the key is missing. */
esp_err_t config_get_str(const char *key, char *out, size_t out_sz, const char *def);
esp_err_t config_get_int(const char *key, int32_t *out, int32_t def);
esp_err_t config_get_bool(const char *key, bool *out, bool def);
esp_err_t config_get_blob(const char *key, void *out, size_t *inout_sz);

/* Write — staged in the NVS handle. Call config_commit() to persist. */
esp_err_t config_set_str(const char *key, const char *value);
esp_err_t config_set_int(const char *key, int32_t value);
esp_err_t config_set_bool(const char *key, bool value);
esp_err_t config_set_blob(const char *key, const void *data, size_t sz);

/* Delete a single key. */
esp_err_t config_erase(const char *key);

/* Persist staged writes. */
esp_err_t config_commit(void);

/* Wipe the config namespace and reload factory defaults. */
esp_err_t config_reset_defaults(void);

/* Validation helper — returns ESP_OK if value ∈ [min,max]. */
esp_err_t config_validate_int_range(int32_t value, int32_t min, int32_t max);

#ifdef __cplusplus
}
#endif
