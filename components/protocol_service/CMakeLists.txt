idf_component_register(
    SRCS        "src/protocol_service.c"
                "src/protocol_mqtt.c"
                "src/protocol_tcp_client.c"
                "src/protocol_tcp_server.c"
                "src/protocol_http.c"
                "src/protocol_udp.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES    config_service
    PRIV_REQUIRES esp_event esp_netif lwip mqtt esp_http_client
)
