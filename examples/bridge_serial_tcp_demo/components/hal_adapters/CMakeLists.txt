idf_component_register(
    SRCS        "src/hal_gpio.c"
                "src/hal_uart.c"
                "src/hal_timer.c"
    INCLUDE_DIRS "include"
    REQUIRES    driver esp_timer
)
