#include "web_internal.h"
#include "net_service.h"

#include <string.h>
#include "esp_log.h"

/* ESP-IDF generates a symbol pair per EMBED_FILES entry. */
#define DECLARE_EMBED(name)                                          \
    extern const uint8_t _binary_##name##_start[] asm("_binary_"#name"_start");  \
    extern const uint8_t _binary_##name##_end[]   asm("_binary_"#name"_end");

DECLARE_EMBED(index_html)
DECLARE_EMBED(app_css)
DECLARE_EMBED(app_js)
DECLARE_EMBED(api_js)
DECLARE_EMBED(router_js)
DECLARE_EMBED(ui_js)
DECLARE_EMBED(login_js)
DECLARE_EMBED(info_js)
DECLARE_EMBED(network_js)
DECLARE_EMBED(serial_js)
DECLARE_EMBED(workmode_js)
DECLARE_EMBED(console_js)

typedef struct {
    const char    *uri;
    const char    *mime;
    const uint8_t *start;
    const uint8_t *end;
} static_asset_t;

static const static_asset_t ASSETS[] = {
    { "/",                        "text/html; charset=utf-8", _binary_index_html_start, _binary_index_html_end },
    { "/index.html",              "text/html; charset=utf-8", _binary_index_html_start, _binary_index_html_end },
    { "/app.css",                 "text/css",                 _binary_app_css_start,    _binary_app_css_end    },
    { "/app.js",                  "application/javascript",   _binary_app_js_start,     _binary_app_js_end     },
    { "/modules/api.js",          "application/javascript",   _binary_api_js_start,     _binary_api_js_end     },
    { "/modules/router.js",       "application/javascript",   _binary_router_js_start,  _binary_router_js_end  },
    { "/modules/ui.js",           "application/javascript",   _binary_ui_js_start,      _binary_ui_js_end      },
    { "/pages/login.js",          "application/javascript",   _binary_login_js_start,   _binary_login_js_end   },
    { "/pages/info.js",           "application/javascript",   _binary_info_js_start,    _binary_info_js_end    },
    { "/pages/network.js",        "application/javascript",   _binary_network_js_start,  _binary_network_js_end  },
    { "/pages/serial.js",         "application/javascript",   _binary_serial_js_start,   _binary_serial_js_end   },
    { "/pages/workmode.js",       "application/javascript",   _binary_workmode_js_start, _binary_workmode_js_end },
    { "/pages/console.js",        "application/javascript",   _binary_console_js_start,  _binary_console_js_end  },
};

static esp_err_t handler(httpd_req_t *req)
{
    const static_asset_t *a = req->user_ctx;
    httpd_resp_set_type(req, a->mime);
    httpd_resp_set_hdr (req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)a->start, a->end - a->start);
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* When upstream is unavailable, force clients into captive-portal flow. */
static bool should_captive_redirect(void)
{
    net_status_t st = {0};
    if (net_service_get_status(&st) != ESP_OK) return true;
    return !st.got_ip;
}

static esp_err_t send_portal_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "redirect");
}

static esp_err_t probe_204_handler(httpd_req_t *req)
{
    if (should_captive_redirect()) return send_portal_redirect(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t probe_ok_text_handler(httpd_req_t *req)
{
    if (should_captive_redirect()) return send_portal_redirect(req);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr (req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "Microsoft Connect Test");
}

static esp_err_t probe_hotspot_handler(httpd_req_t *req)
{
    if (should_captive_redirect()) return send_portal_redirect(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr (req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}

static esp_err_t notfound_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return web_send_error(req, WEB_CODE_NOT_FOUND, req->uri);
}

esp_err_t web_register_static(httpd_handle_t server)
{
    for (size_t i = 0; i < sizeof(ASSETS) / sizeof(ASSETS[0]); ++i) {
        httpd_uri_t u = {
            .uri      = ASSETS[i].uri,
            .method   = HTTP_GET,
            .handler  = handler,
            .user_ctx = (void *)&ASSETS[i],
        };
        esp_err_t err = httpd_register_uri_handler(server, &u);
        if (err != ESP_OK) return err;
    }

    static const httpd_uri_t fav = {
        .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler
    };
    esp_err_t err = httpd_register_uri_handler(server, &fav);
    if (err != ESP_OK) return err;

    /* Common captive-portal / connectivity probes from phones and desktops. */
    static const httpd_uri_t probes[] = {
        { .uri = "/connecttest.txt",    .method = HTTP_GET, .handler = probe_ok_text_handler },
        { .uri = "/ncsi.txt",           .method = HTTP_GET, .handler = probe_ok_text_handler },
        { .uri = "/hotspot-detect.html",.method = HTTP_GET, .handler = probe_hotspot_handler },
        { .uri = "/generate_204",       .method = HTTP_GET, .handler = probe_204_handler },
        { .uri = "/pop/probe_v6_addr",  .method = HTTP_GET, .handler = probe_204_handler },
        { .uri = "/mmtls/*",            .method = HTTP_GET, .handler = probe_204_handler },
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        err = httpd_register_uri_handler(server, &probes[i]);
        if (err != ESP_OK) return err;
    }

    err = httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, notfound_handler);
    if (err != ESP_OK) return err;
    return ESP_OK;
}
