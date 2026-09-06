idf_component_register(
    SRCS "main.c" "selftest.c"
    REQUIRES board display gt911 storage link can can_twai bench outputs safety ui gfx touch artwork
             settings settings_nvs art_flash esp_psram
             esp_driver_gpio esp_driver_uart esp_timer
)
