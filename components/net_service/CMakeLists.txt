idf_component_register(
    SRCS        "src/net_service.c"
                "src/net_scan.c"
                "src/dns_redirect.c"
    INCLUDE_DIRS "include"
    REQUIRES    config_service
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_eth lwip ethernet_init
)
