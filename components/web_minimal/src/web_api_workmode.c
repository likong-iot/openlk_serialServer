#include "web_internal.h"
#include "bridge_service.h"
#include "config_service.h"

#include <string.h>
#include "esp_log.h"
#include "lwip/ip4_addr.h"

/*
 * /api/workmode         GET   → current mode + per-mode params + status
 * /api/workmode         POST  → persist params, switch mode, restart bridge
 * /api/workmode/status  GET   → runtime status only (cheap, fits a 1 Hz poll)
 *
 * Passwords are NEVER returned. POST treats an empty password as
 * "leave current value untouched".
 */

static const char *proto_state_str(protocol_state_t s)
{
    switch (s) {
    case PROTOCOL_STATE_STARTING:     return "starting";
    case PROTOCOL_STATE_CONNECTED:    return "connected";
    case PROTOCOL_STATE_DISCONNECTED: return "disconnected";
    case PROTOCOL_STATE_ERROR:        return "error";
    case PROTOCOL_STATE_STOPPED:
    default:                          return "stopped";
    }
}

static cJSON *status_node(void)
{
    bridge_status_t st = {0};
    bridge_service_get_status(&st);
    cJSON *s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "mode",        bridge_mode_str(st.mode));
    cJSON_AddStringToObject(s, "state",       proto_state_str(st.proto_state));
    cJSON_AddNumberToObject(s, "tx_bytes",    (double)st.tx_bytes);
    cJSON_AddNumberToObject(s, "rx_bytes",    (double)st.rx_bytes);
    cJSON_AddNumberToObject(s, "tx_packets",  st.tx_packets);
    cJSON_AddNumberToObject(s, "rx_packets",  st.rx_packets);
    cJSON_AddNumberToObject(s, "last_error",  st.last_error);
    cJSON_AddNumberToObject(s, "started_ms",  (double)st.started_ms);
    return s;
}

static cJSON *tcp_client_node(void)
{
    char host[64] = {0};
    int32_t port = 0, reconn = 0;
    config_get_str(CFG_KEY_TCP_HOST,      host, sizeof(host), "");
    config_get_int(CFG_KEY_TCP_PORT,      &port,   8080);
    config_get_int(CFG_KEY_TCP_RECONN_MS, &reconn, 2000);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "host",      host);
    cJSON_AddNumberToObject(o, "port",      port);
    cJSON_AddNumberToObject(o, "reconn_ms", reconn);
    return o;
}

static cJSON *tcp_server_node(void)
{
    int32_t port = 0, max_clients = 0;
    config_get_int(CFG_KEY_TCPS_PORT,        &port,        8080);
    config_get_int(CFG_KEY_TCPS_MAX_CLIENTS, &max_clients, 4);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "port",        port);
    cJSON_AddNumberToObject(o, "max_clients", max_clients);
    return o;
}

static cJSON *udp_node(void)
{
    char host[64] = {0};
    int32_t lport = 0, rport = 0;
    config_get_int(CFG_KEY_UDP_LOCAL_PORT,  &lport, 9000);
    config_get_str(CFG_KEY_UDP_REMOTE_HOST,  host, sizeof(host), "");
    config_get_int(CFG_KEY_UDP_REMOTE_PORT, &rport, 9000);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "local_port",  lport);
    cJSON_AddStringToObject(o, "remote_host", host);
    cJSON_AddNumberToObject(o, "remote_port", rport);
    return o;
}

static cJSON *http_node(void)
{
    char url[128] = {0};
    char method[8] = {0};
    int32_t timeout = 0;
    config_get_str(CFG_KEY_HTTP_URL,        url,    sizeof(url),    "");
    config_get_str(CFG_KEY_HTTP_METHOD,     method, sizeof(method), "POST");
    config_get_int(CFG_KEY_HTTP_TIMEOUT_MS, &timeout, 5000);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url",        url);
    cJSON_AddStringToObject(o, "method",     method);
    cJSON_AddNumberToObject(o, "timeout_ms", timeout);
    return o;
}

static cJSON *mqtt_node(void)
{
    char uri[128]={0}, cid[64]={0}, user[64]={0}, pub[96]={0}, sub[96]={0};
    int32_t qos = 0;
    config_get_str(CFG_KEY_MQTT_URI,       uri,  sizeof(uri),  "");
    config_get_str(CFG_KEY_MQTT_CLIENT_ID, cid,  sizeof(cid),  "");
    config_get_str(CFG_KEY_MQTT_USER,      user, sizeof(user), "");
    config_get_str(CFG_KEY_MQTT_PUB_TOPIC, pub,  sizeof(pub),  "");
    config_get_str(CFG_KEY_MQTT_SUB_TOPIC, sub,  sizeof(sub),  "");
    config_get_int(CFG_KEY_MQTT_QOS,       &qos, 0);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "uri",       uri);
    cJSON_AddStringToObject(o, "client_id", cid);
    cJSON_AddStringToObject(o, "user",      user);
    cJSON_AddStringToObject(o, "pub_topic", pub);
    cJSON_AddStringToObject(o, "sub_topic", sub);
    cJSON_AddNumberToObject(o, "qos",       qos);
    /* password intentionally omitted */
    return o;
}

/* ── handlers ────────────────────────────────────────────────────────── */

static esp_err_t get_handler(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) return ESP_OK;

    bridge_status_t st = {0};
    bridge_service_get_status(&st);

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "mode", bridge_mode_str(st.mode));

    cJSON *modes = cJSON_CreateArray();
    static const char *NAMES[] = { "off","tcp_client","tcp_server","udp","mqtt","http" };
    for (size_t i = 0; i < sizeof(NAMES)/sizeof(NAMES[0]); ++i)
        cJSON_AddItemToArray(modes, cJSON_CreateString(NAMES[i]));
    cJSON_AddItemToObject(d, "modes", modes);

    cJSON_AddItemToObject(d, "tcp_client", tcp_client_node());
    cJSON_AddItemToObject(d, "tcp_server", tcp_server_node());
    cJSON_AddItemToObject(d, "udp",        udp_node());
    cJSON_AddItemToObject(d, "mqtt",       mqtt_node());
    cJSON_AddItemToObject(d, "http",       http_node());
    cJSON_AddItemToObject(d, "status",     status_node());
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) return ESP_OK;
    return web_send_json(req, WEB_CODE_OK, "ok", status_node());
}

/* helpers used by post_handler */

static bool valid_port(int v) { return v > 0 && v < 65536; }

static bool valid_host_or_empty(const char *s)
{
    if (!s || !s[0]) return true;
    if (strlen(s) > 63) return false;
    /* Accept either dotted IPv4 or anything that looks like a hostname.
     * No DNS lookup here — protocol layer does that at start time. */
    return true;
}

static esp_err_t apply_tcp_client(const cJSON *o)
{
    const cJSON *host = cJSON_GetObjectItem(o, "host");
    const cJSON *port = cJSON_GetObjectItem(o, "port");
    const cJSON *re   = cJSON_GetObjectItem(o, "reconn_ms");
    if (cJSON_IsString(host) && !valid_host_or_empty(host->valuestring)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(port) && !valid_port(port->valueint))             return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(re) && (re->valueint < 200 || re->valueint > 60000)) return ESP_ERR_INVALID_ARG;

    if (cJSON_IsString(host)) config_set_str(CFG_KEY_TCP_HOST, host->valuestring);
    if (cJSON_IsNumber(port)) config_set_int(CFG_KEY_TCP_PORT, port->valueint);
    if (cJSON_IsNumber(re))   config_set_int(CFG_KEY_TCP_RECONN_MS, re->valueint);
    return ESP_OK;
}

static esp_err_t apply_tcp_server(const cJSON *o)
{
    const cJSON *port = cJSON_GetObjectItem(o, "port");
    const cJSON *mc   = cJSON_GetObjectItem(o, "max_clients");
    if (cJSON_IsNumber(port) && !valid_port(port->valueint))            return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(mc) && (mc->valueint < 1 || mc->valueint > 16))  return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(port)) config_set_int(CFG_KEY_TCPS_PORT, port->valueint);
    if (cJSON_IsNumber(mc))   config_set_int(CFG_KEY_TCPS_MAX_CLIENTS, mc->valueint);
    return ESP_OK;
}

static esp_err_t apply_udp(const cJSON *o)
{
    const cJSON *lp   = cJSON_GetObjectItem(o, "local_port");
    const cJSON *host = cJSON_GetObjectItem(o, "remote_host");
    const cJSON *rp   = cJSON_GetObjectItem(o, "remote_port");
    if (cJSON_IsNumber(lp) && !valid_port(lp->valueint))                 return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(rp) && !valid_port(rp->valueint))                 return ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(host) && !valid_host_or_empty(host->valuestring)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(lp))   config_set_int(CFG_KEY_UDP_LOCAL_PORT,  lp->valueint);
    if (cJSON_IsString(host)) config_set_str(CFG_KEY_UDP_REMOTE_HOST, host->valuestring);
    if (cJSON_IsNumber(rp))   config_set_int(CFG_KEY_UDP_REMOTE_PORT, rp->valueint);
    return ESP_OK;
}

static esp_err_t apply_http(const cJSON *o)
{
    const cJSON *url = cJSON_GetObjectItem(o, "url");
    const cJSON *m   = cJSON_GetObjectItem(o, "method");
    const cJSON *t   = cJSON_GetObjectItem(o, "timeout_ms");
    if (cJSON_IsString(url) && strlen(url->valuestring) > 127) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(m) && strcmp(m->valuestring, "GET") &&
                             strcmp(m->valuestring, "POST"))   return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(t) && (t->valueint < 100 || t->valueint > 60000)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(url)) config_set_str(CFG_KEY_HTTP_URL,        url->valuestring);
    if (cJSON_IsString(m))   config_set_str(CFG_KEY_HTTP_METHOD,     m->valuestring);
    if (cJSON_IsNumber(t))   config_set_int(CFG_KEY_HTTP_TIMEOUT_MS, t->valueint);
    return ESP_OK;
}

static esp_err_t apply_mqtt(const cJSON *o)
{
    const cJSON *uri  = cJSON_GetObjectItem(o, "uri");
    const cJSON *cid  = cJSON_GetObjectItem(o, "client_id");
    const cJSON *user = cJSON_GetObjectItem(o, "user");
    const cJSON *pass = cJSON_GetObjectItem(o, "password");
    const cJSON *pub  = cJSON_GetObjectItem(o, "pub_topic");
    const cJSON *sub  = cJSON_GetObjectItem(o, "sub_topic");
    const cJSON *qos  = cJSON_GetObjectItem(o, "qos");
    if (cJSON_IsString(uri) && strlen(uri->valuestring) > 127) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(qos) && (qos->valueint < 0 || qos->valueint > 2)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(uri))  config_set_str(CFG_KEY_MQTT_URI,       uri->valuestring);
    if (cJSON_IsString(cid))  config_set_str(CFG_KEY_MQTT_CLIENT_ID, cid->valuestring);
    if (cJSON_IsString(user)) config_set_str(CFG_KEY_MQTT_USER,      user->valuestring);
    /* Empty password = keep current */
    if (cJSON_IsString(pass) && pass->valuestring[0])
                              config_set_str(CFG_KEY_MQTT_PASS,      pass->valuestring);
    if (cJSON_IsString(pub))  config_set_str(CFG_KEY_MQTT_PUB_TOPIC, pub->valuestring);
    if (cJSON_IsString(sub))  config_set_str(CFG_KEY_MQTT_SUB_TOPIC, sub->valuestring);
    if (cJSON_IsNumber(qos))  config_set_int(CFG_KEY_MQTT_QOS,       qos->valueint);
    return ESP_OK;
}

static esp_err_t post_handler(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) return ESP_OK;

    char *body = web_read_body(req, 4096);
    if (!body) return web_send_error(req, WEB_CODE_BAD_PARAM, "body required");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid json");

    const cJSON *jmode = cJSON_GetObjectItem(root, "mode");
    if (!cJSON_IsString(jmode)) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "mode required");
    }
    bridge_mode_t mode = bridge_mode_from(jmode->valuestring);
    if (mode == BRIDGE_MODE_OFF && strcmp(jmode->valuestring, "off") != 0) {
        cJSON_Delete(root);
        return web_send_error(req, WEB_CODE_BAD_PARAM, "unknown mode");
    }

    /* Apply per-mode params if present (any combination is allowed; the
     * frontend usually sends only the active mode's block to keep the body
     * small, but we accept all of them). */
    esp_err_t err = ESP_OK;
    const cJSON *o;
    if ((o = cJSON_GetObjectItem(root, "tcp_client")) && cJSON_IsObject(o)) err = apply_tcp_client(o);
    if (err == ESP_OK && (o = cJSON_GetObjectItem(root, "tcp_server")) && cJSON_IsObject(o)) err = apply_tcp_server(o);
    if (err == ESP_OK && (o = cJSON_GetObjectItem(root, "udp"))        && cJSON_IsObject(o)) err = apply_udp(o);
    if (err == ESP_OK && (o = cJSON_GetObjectItem(root, "http"))       && cJSON_IsObject(o)) err = apply_http(o);
    if (err == ESP_OK && (o = cJSON_GetObjectItem(root, "mqtt"))       && cJSON_IsObject(o)) err = apply_mqtt(o);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return web_send_error(req, WEB_CODE_BAD_PARAM, "invalid parameter");
    }
    err = config_commit();
    if (err != ESP_OK) return web_send_error(req, WEB_CODE_INTERNAL, esp_err_to_name(err));

    err = bridge_service_apply_mode(mode);
    if (err != ESP_OK) {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddItemToObject(d, "status", status_node());
        return web_send_json(req, WEB_CODE_INTERNAL, esp_err_to_name(err), d);
    }
    cJSON *d = cJSON_CreateObject();
    cJSON_AddItemToObject(d, "status", status_node());
    return web_send_json(req, WEB_CODE_OK, "ok", d);
}

esp_err_t web_register_workmode_api(httpd_handle_t server)
{
    static const httpd_uri_t g_  = { .uri = "/api/workmode",        .method = HTTP_GET,  .handler = get_handler        };
    static const httpd_uri_t p_  = { .uri = "/api/workmode",        .method = HTTP_POST, .handler = post_handler       };
    static const httpd_uri_t st_ = { .uri = "/api/workmode/status", .method = HTTP_GET,  .handler = status_get_handler };
    esp_err_t err;
    if ((err = httpd_register_uri_handler(server, &g_))  != ESP_OK) return err;
    if ((err = httpd_register_uri_handler(server, &p_))  != ESP_OK) return err;
    if ((err = httpd_register_uri_handler(server, &st_)) != ESP_OK) return err;
    return ESP_OK;
}
