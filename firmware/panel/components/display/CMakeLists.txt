# The display: the EK79007 panel on the ESP32-P4's MIPI DSI (Display Serial
# Interface) host, through ESP-IDF's esp_lcd DSI driver.
#   idf.py -D RCBENCH_DISPLAY_BARS=ON build
# shows the DSI host's colour bars for 3 s at start-up.
idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES gfx board esp_lcd esp_timer
    PRIV_REQUIRES esp_hw_support esp_driver_gpio
)
if(RCBENCH_DISPLAY_BARS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RCBENCH_DISPLAY_BARS=1)
endif()
