#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Auth Service — local-account authentication for the web layer.
 *
 * Design rules:
 *   - Cleartext passwords are never persisted; only SHA-256(salt || password)
 *     and the random per-device salt are written through config_service.
 *   - Sessions are RAM-only opaque bearer tokens; reboot invalidates all
 *     active sessions intentionally.
 *   - This module owns no HTTP code — web_minimal calls into us.
 */

#define AUTH_USER_MAX        31
#define AUTH_PASS_MIN        4
#define AUTH_PASS_MAX        63
#define AUTH_TOKEN_LEN       32          /* hex chars, not including NUL */
#define AUTH_TOKEN_BUF       (AUTH_TOKEN_LEN + 1)
#define AUTH_MAX_SESSIONS    4
#define AUTH_SESSION_TTL_MS  (30 * 60 * 1000)   /* 30 min sliding */

/* Lifecycle. Seeds factory defaults (admin/admin, mustchg=true) on first run. */
esp_err_t auth_service_init(void);

/* Verify credentials. On success writes a fresh token to `out_token`
 * (AUTH_TOKEN_BUF chars) and `*out_must_change` reflects the mustchg flag. */
esp_err_t auth_service_login(const char *user,
                             const char *password,
                             char       *out_token,
                             bool       *out_must_change);

/* Drop the session for `token`. Idempotent. */
esp_err_t auth_service_logout(const char *token);

/* Returns true iff `token` is a live session. Touches the session's TTL. */
bool auth_service_session_valid(const char *token);

/* Snapshot of the active user. `out_user` receives username. */
esp_err_t auth_service_whoami(const char *token,
                              char *out_user, size_t out_sz,
                              bool *out_must_change);

/* Change password. Requires old_password to match current. Clears mustchg. */
esp_err_t auth_service_change_password(const char *token,
                                       const char *old_password,
                                       const char *new_password);

#ifdef __cplusplus
}
#endif
