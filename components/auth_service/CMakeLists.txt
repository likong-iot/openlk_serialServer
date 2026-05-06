idf_component_register(
    SRCS         "src/auth_service.c"
    INCLUDE_DIRS "include"
    REQUIRES     config_service
    PRIV_REQUIRES mbedtls esp_timer esp_hw_support
)
