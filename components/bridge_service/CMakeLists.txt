idf_component_register(
    SRCS         "src/bridge_service.c"
    INCLUDE_DIRS "include"
    REQUIRES     config_service serial_service protocol_service
    PRIV_REQUIRES esp_timer
)
