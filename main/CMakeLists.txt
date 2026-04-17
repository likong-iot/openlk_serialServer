idf_component_register(
    SRCS        "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES    config_service
                serial_service
                net_service
                web_minimal
                protocol_service
                hal_adapters
)
