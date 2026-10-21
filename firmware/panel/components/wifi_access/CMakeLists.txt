# The panel's Wi-Fi: the radio, the page's server, the mDNS (multicast
# domain name system) name, the kept home network, and the HTTPS (HTTP over
# transport layer security) client that asks GitHub for a release.  What
# may run and when, the page, the card walk, the downloads, the ZIP file
# and the release check belong to shared/webaccess, which the host suite
# holds; this is the ESP-IDF (Espressif Internet-of-Things Development
# Framework) end of them.
#
# On the ESP32-P4 the radio is an ESP32-C6 on SDIO (Secure Digital
# input/output), reached through the managed components esp_wifi_remote
# and esp_hosted (idf_component.yml, wifi_radio.c).
idf_component_register(
    SRCS "wifi_access.c" "wifi_radio.c" "wifi_sock.c"
    INCLUDE_DIRS include
    REQUIRES board webaccess fwupdate storage esp_wifi esp_netif esp_event
             esp_http_server esp_http_client esp-tls mbedtls esp_timer
             nvs_flash lwip freertos esp_hw_support heap
)

# A task created without a core runs on core 0; core 1 belongs to the
# control task and the tasks pinned there (wifi_radio.c,
# __wrap_xTaskCreatePinnedToCore).
idf_build_set_property(LINK_OPTIONS
    "-Wl,--wrap=xTaskCreatePinnedToCore"
    "-Wl,--undefined=__wrap_xTaskCreatePinnedToCore" APPEND)
