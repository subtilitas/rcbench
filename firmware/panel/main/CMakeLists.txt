idf_component_register(
    SRCS "main.c"
    REQUIRES board display gt911 storage link link_uart bench safety ui gfx touch
             esp_driver_gpio esp_driver_uart esp_timer
)
