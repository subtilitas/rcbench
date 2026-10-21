# The panel's Wi-Fi: the radio, the page's server, the mDNS (multicast
# domain name system) name, the kept home network, and the HTTPS (HTTP over
# transport layer security) client that asks GitHub for a release.  What
# may run and when, the page, the card walk, the downloads, the ZIP file
# and the release check belong to shared/webaccess, which the host suite
# holds; this is the ESP-IDF (Espressif Internet-of-Things Development
# Framework) end of them.
#
# RCBENCH_PANEL_WIFI (firmware/panel/CMakeLists.txt) OFF builds
# wifi_access_off.c in place of the radio: the ESP32-P4's radio is an
# ESP32-C6 that this build does not reach yet.
if(RCBENCH_PANEL_WIFI)
    set(srcs "wifi_access.c" "wifi_sock.c")
else()
    set(srcs "wifi_access_off.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES webaccess fwupdate storage esp_wifi esp_netif esp_event
             esp_http_server esp_http_client esp-tls mbedtls esp_timer
             nvs_flash lwip freertos esp_hw_support heap
)
