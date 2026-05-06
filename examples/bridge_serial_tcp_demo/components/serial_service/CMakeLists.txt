idf_component_register(
    SRCS        "src/serial_service.c"
    INCLUDE_DIRS "include"
    REQUIRES    driver config_service hal_adapters
)
