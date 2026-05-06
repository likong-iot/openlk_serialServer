idf_component_register(
    SRCS        "src/web_minimal.c"
                "src/web_api_auth.c"
                "src/web_api_network.c"
                "src/web_api_scan.c"
                "src/web_api_serial.c"
                "src/web_api_system.c"
                "src/web_api_workmode.c"
                "src/web_auth.c"
                "src/web_ws_serial.c"
                "src/web_static.c"
                "src/web_json.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES    config_service serial_service net_service auth_service
                bridge_service protocol_service
    PRIV_REQUIRES esp_http_server esp_wifi esp_netif json
                  esp_timer app_update esp_hw_support spi_flash
    EMBED_FILES "www/index.html"
                "www/app.css"
                "www/app.js"
                "www/modules/api.js"
                "www/modules/router.js"
                "www/modules/ui.js"
                "www/pages/login.js"
                "www/pages/info.js"
                "www/pages/network.js"
                "www/pages/serial.js"
                "www/pages/workmode.js"
                "www/pages/console.js"
)
