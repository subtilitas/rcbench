# The panel's Wi-Fi: the radio, the page's server, the mDNS (multicast
# domain name system) name and the kept home network.  What may run and
# when, the page, the card walk, the downloads and the ZIP file belong to
# shared/webaccess, which the host suite holds; this is the ESP-IDF
# (Espressif Internet-of-Things Development Framework) end of them.
idf_component_register(
    SRCS "wifi_access.c"
    INCLUDE_DIRS include
    REQUIRES webaccess fwupdate storage esp_wifi esp_netif esp_event
             esp_http_server esp_timer nvs_flash lwip freertos
             esp_hw_support heap
)
