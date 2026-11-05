# The bridge: main.c runs the emulator, the UART, KEY, STATE and the LED;
# bridge_bt.c is the Bluedroid side; bridge_store.c the settings in NVS.
idf_component_register(
    SRCS "main.c" "bridge_bt.c" "bridge_store.c"
    INCLUDE_DIRS "."
    REQUIRES hc05emu
    PRIV_REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio esp_timer
                  esp_app_format
)
