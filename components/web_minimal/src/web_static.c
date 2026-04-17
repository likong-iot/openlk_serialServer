#include "web_internal.h"

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
DECLARE_EMBED(network_js)
DECLARE_EMBED(serial_js)
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
    { "/pages/network.js",        "application/javascript",   _binary_network_js_start, _binary_network_js_end },
    { "/pages/serial.js",         "application/javascript",   _binary_serial_js_start,  _binary_serial_js_end  },
    { "/pages/console.js",        "application/javascript",   _binary_console_js_start, _binary_console_js_end },
};

static esp_err_t handler(httpd_req_t *req)
{
    const static_asset_t *a = req->user_ctx;
    httpd_resp_set_type(req, a->mime);
    httpd_resp_set_hdr (req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)a->start, a->end - a->start);
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
        httpd_register_uri_handler(server, &u);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, notfound_handler);
    return ESP_OK;
}
