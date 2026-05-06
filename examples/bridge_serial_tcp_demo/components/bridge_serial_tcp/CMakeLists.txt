idf_component_register(
    SRCS        "src/bridge_serial_tcp.c"
    INCLUDE_DIRS "include"
    REQUIRES    serial_service protocol_service
)
