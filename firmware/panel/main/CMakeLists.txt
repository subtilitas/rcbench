idf_component_register(
    SRCS "main.c"
    REQUIRES link link_uart esp_driver_gpio esp_driver_uart esp_timer
)
