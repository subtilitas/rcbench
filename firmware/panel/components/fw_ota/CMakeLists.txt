# The application slots: the slot an update writes, the running build's
# version, and the start check after an update.  The order of a write and
# every check of the image belong to shared/fwupdate, which the host suite
# runs against memory; this is the ESP-IDF (Espressif Internet-of-Things
# Development Framework) calls that need real flash.
idf_component_register(
    SRCS "fw_ota.c"
    INCLUDE_DIRS include
    REQUIRES fwupdate app_update esp_partition esp_app_format esp_system
             nvs_flash
)
