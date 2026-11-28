# A device's end of the firmware update over Bluetooth LE: bleota's GATT
# service on NimBLE, the device side of protocols/bleota on the `ota` task,
# ESP-IDF's OTA calls and the rollback mark.  Taken by the M5Stack Dial's
# firmware (firmware/dial) and the sensor nodes' (firmware/node).
# The release signature's verifier is the image's own (devota_need_sig()):
# the Dials pass one, the nodes none, so the nodes link no verifier.
idf_component_register(SRCS devota.c
                       INCLUDE_DIRS include
                       REQUIRES bleota bt
                       PRIV_REQUIRES app_update esp_app_format esp_timer
                                     mbedtls)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow)
