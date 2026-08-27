idf_component_register(
    SRCS "board.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_uart esp_driver_i2c esp_driver_gpio
)
