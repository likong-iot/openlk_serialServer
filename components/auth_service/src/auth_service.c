#include "auth_service.h"
#include "config_service.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"

static const char *TAG = "auth_svc";

#define DEFAULT_USER  "admin"
#define DEFAULT_PASS  "admin"
#define SALT_BYTES    16
#define SALT_HEX_LEN  (SALT_BYTES * 2)
#define HASH_HEX_LEN  64

typedef struct {
    char     token[AUTH_TOKEN_BUF];
    char     user [AUTH_USER_MAX + 1];
    int64_t  expires_us;
    bool     in_use;
} session_t;

static session_t        s_sessions[AUTH_MAX_SESSIONS];
static SemaphoreHandle_t s_lock;
static bool             s_inited;

/* ── helpers ─────────────────────────────────────────────────────────── */

static void bytes_to_hex(const uint8_t *in, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[i * 2]     = H[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = H[ in[i]       & 0xf];
    }
    out[n * 2] = '\0';
}

static int hex_to_bytes(const char *in, uint8_t *out, size_t out_sz)
{
    size_t need = out_sz * 2;
    if (strlen(in) < need) return -1;
    for (size_t i = 0; i < out_sz; ++i) {
        unsigned v;
        if (sscanf(in + i * 2, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}

static void rand_bytes(uint8_t *out, size_t n)
{
    /* esp_fill_random uses the hardware RNG once WiFi or BT is up; before
     * that it falls back to a pseudo-source. Good enough for salt + token. */
    esp_fill_random(out, n);
}

static void hash_password(const char *salt_hex, const char *password,
                          char *out_hex)
{
    uint8_t salt[SALT_BYTES];
    if (hex_to_bytes(salt_hex, salt, sizeof(salt)) != 0) {
        memset(salt, 0, sizeof(salt));
    }
    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, salt, sizeof(salt));
    mbedtls_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    bytes_to_hex(digest, sizeof(digest), out_hex);
}

static int constant_time_eq(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la != lb) return 0;
    unsigned diff = 0;
    for (size_t i = 0; i < la; ++i) diff |= (unsigned)(a[i] ^ b[i]);
    return diff == 0;
}

static esp_err_t persist_credentials(const char *user, const char *password,
                                     bool mustchg)
{
    uint8_t salt[SALT_BYTES];
    rand_bytes(salt, sizeof(salt));
    char salt_hex[SALT_HEX_LEN + 1];
    bytes_to_hex(salt, sizeof(salt), salt_hex);

    char hash_hex[HASH_HEX_LEN + 1];
    hash_password(salt_hex, password, hash_hex);

    esp_err_t err;
    if ((err = config_set_str (CFG_KEY_AUTH_USER,    user))     != ESP_OK) return err;
    if ((err = config_set_str (CFG_KEY_AUTH_SALT,    salt_hex)) != ESP_OK) return err;
    if ((err = config_set_str (CFG_KEY_AUTH_PWHASH,  hash_hex)) != ESP_OK) return err;
    if ((err = config_set_bool(CFG_KEY_AUTH_MUSTCHG, mustchg))  != ESP_OK) return err;
    return config_commit();
}

/* ── session table ───────────────────────────────────────────────────── */

static int64_t now_us(void) { return esp_timer_get_time(); }

static void prune_locked(void)
{
    int64_t t = now_us();
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i) {
        if (s_sessions[i].in_use && t > s_sessions[i].expires_us) {
            memset(&s_sessions[i], 0, sizeof(s_sessions[i]));
        }
    }
}

static session_t *find_locked(const char *token)
{
    if (!token || strlen(token) != AUTH_TOKEN_LEN) return NULL;
    prune_locked();
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i) {
        if (s_sessions[i].in_use &&
            memcmp(s_sessions[i].token, token, AUTH_TOKEN_LEN) == 0) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

static session_t *alloc_locked(void)
{
    prune_locked();
    /* first free slot */
    for (int i = 0; i < AUTH_MAX_SESSIONS; ++i)
        if (!s_sessions[i].in_use) return &s_sessions[i];
    /* otherwise evict the soonest-to-expire */
    int oldest = 0;
    for (int i = 1; i < AUTH_MAX_SESSIONS; ++i)
        if (s_sessions[i].expires_us < s_sessions[oldest].expires_us)
            oldest = i;
    memset(&s_sessions[oldest], 0, sizeof(s_sessions[oldest]));
    return &s_sessions[oldest];
}

static void make_token(char *out)
{
    uint8_t raw[AUTH_TOKEN_LEN / 2];
    rand_bytes(raw, sizeof(raw));
    bytes_to_hex(raw, sizeof(raw), out);
}

/* ── public API ──────────────────────────────────────────────────────── */

esp_err_t auth_service_init(void)
{
    if (s_inited) return ESP_OK;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(s_sessions, 0, sizeof(s_sessions));

    char user[AUTH_USER_MAX + 1] = {0};
    config_get_str(CFG_KEY_AUTH_USER, user, sizeof(user), "");
    if (user[0] == '\0') {
        ESP_LOGW(TAG, "no admin set, seeding factory default %s/%s",
                 DEFAULT_USER, DEFAULT_PASS);
        esp_err_t err = persist_credentials(DEFAULT_USER, DEFAULT_PASS, true);
        if (err != ESP_OK) return err;
    }
    s_inited = true;
    return ESP_OK;
}

esp_err_t auth_service_login(const char *user,
                             const char *password,
                             char       *out_token,
                             bool       *out_must_change)
{
    if (!s_inited)             return ESP_ERR_INVALID_STATE;
    if (!user || !password || !out_token) return ESP_ERR_INVALID_ARG;

    char stored_user[AUTH_USER_MAX + 1] = {0};
    char salt_hex   [SALT_HEX_LEN  + 1] = {0};
    char hash_hex   [HASH_HEX_LEN  + 1] = {0};
    bool mustchg = false;
    config_get_str (CFG_KEY_AUTH_USER,    stored_user, sizeof(stored_user), "");
    config_get_str (CFG_KEY_AUTH_SALT,    salt_hex,    sizeof(salt_hex),    "");
    config_get_str (CFG_KEY_AUTH_PWHASH,  hash_hex,    sizeof(hash_hex),    "");
    config_get_bool(CFG_KEY_AUTH_MUSTCHG, &mustchg, false);

    char calc_hex[HASH_HEX_LEN + 1];
    hash_password(salt_hex, password, calc_hex);
    if (!constant_time_eq(stored_user, user) || !constant_time_eq(calc_hex, hash_hex)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    session_t *s = alloc_locked();
    make_token(s->token);
    strncpy(s->user, stored_user, AUTH_USER_MAX);
    s->user[AUTH_USER_MAX] = '\0';
    s->expires_us = now_us() + (int64_t)AUTH_SESSION_TTL_MS * 1000;
    s->in_use = true;
    memcpy(out_token, s->token, AUTH_TOKEN_BUF);
    if (out_must_change) *out_must_change = mustchg;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "login %s ok", user);
    return ESP_OK;
}

esp_err_t auth_service_logout(const char *token)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    session_t *s = find_locked(token);
    if (s) memset(s, 0, sizeof(*s));
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool auth_service_session_valid(const char *token)
{
    if (!s_inited) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    session_t *s = find_locked(token);
    if (s) s->expires_us = now_us() + (int64_t)AUTH_SESSION_TTL_MS * 1000;
    bool ok = s != NULL;
    xSemaphoreGive(s_lock);
    return ok;
}

esp_err_t auth_service_whoami(const char *token,
                              char *out_user, size_t out_sz,
                              bool *out_must_change)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!out_user || out_sz == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    session_t *s = find_locked(token);
    if (!s) { xSemaphoreGive(s_lock); return ESP_ERR_NOT_FOUND; }
    strncpy(out_user, s->user, out_sz - 1);
    out_user[out_sz - 1] = '\0';
    xSemaphoreGive(s_lock);

    if (out_must_change) {
        bool m = false;
        config_get_bool(CFG_KEY_AUTH_MUSTCHG, &m, false);
        *out_must_change = m;
    }
    return ESP_OK;
}

esp_err_t auth_service_change_password(const char *token,
                                       const char *old_password,
                                       const char *new_password)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!old_password || !new_password) return ESP_ERR_INVALID_ARG;
    size_t nl = strlen(new_password);
    if (nl < AUTH_PASS_MIN || nl > AUTH_PASS_MAX) return ESP_ERR_INVALID_ARG;

    char user[AUTH_USER_MAX + 1] = {0};
    if (auth_service_whoami(token, user, sizeof(user), NULL) != ESP_OK)
        return ESP_ERR_NOT_FOUND;

    /* Verify old password by re-hashing under the existing salt. */
    char salt_hex[SALT_HEX_LEN + 1] = {0};
    char hash_hex[HASH_HEX_LEN + 1] = {0};
    config_get_str(CFG_KEY_AUTH_SALT,   salt_hex, sizeof(salt_hex), "");
    config_get_str(CFG_KEY_AUTH_PWHASH, hash_hex, sizeof(hash_hex), "");
    char calc_hex[HASH_HEX_LEN + 1];
    hash_password(salt_hex, old_password, calc_hex);
    if (!constant_time_eq(calc_hex, hash_hex)) return ESP_ERR_INVALID_ARG;

    return persist_credentials(user, new_password, false);
}
