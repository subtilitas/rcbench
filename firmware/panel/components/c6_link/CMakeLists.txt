# The SDIO (Secure Digital input/output) link to the board's ESP32-C6,
# which Wi-Fi (components/wifi_access) and Bluetooth LE share: the reset and
# start of the C6 once per panel start, its firmware version, a failed link
# held in reset (c6_link.c).  esp_hosted is the managed component
# components/wifi_access/idf_component.yml pins.
idf_component_register(
    SRCS "c6_link.c"
    INCLUDE_DIRS include
    REQUIRES board espressif__esp_hosted esp_event freertos esp_driver_gpio
)

# A task created through xTaskCreatePinnedToCore() or xTaskCreate()
# without a core runs on core 0; core 1 belongs to the control task and the
# tasks pinned there (c6_link.c, __wrap_xTaskCreatePinnedToCore).
idf_build_set_property(LINK_OPTIONS
    "-Wl,--wrap=xTaskCreatePinnedToCore"
    "-Wl,--undefined=__wrap_xTaskCreatePinnedToCore" APPEND)
