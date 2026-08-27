idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES gfx board esp_lcd esp_timer
)
