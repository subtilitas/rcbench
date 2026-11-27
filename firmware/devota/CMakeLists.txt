# A device's end of the firmware update over Bluetooth LE: bleota's GATT
# service on NimBLE, the device side of protocols/bleota on the `ota` task,
# ESP-IDF's OTA calls and the rollback mark.  Taken by the M5Stack Dial's
# firmware (firmware/dial) and the sensor nodes' (firmware/node).
# The release key the Dials verify an image's signature with is the
# panel's own (bleota protocol 2, shared/relsig).
idf_component_register(SRCS devota.c
                       INCLUDE_DIRS include
                       REQUIRES bleota bt
                       PRIV_REQUIRES app_update esp_app_format esp_timer
                                     mbedtls relsig
                       EMBED_TXTFILES
                           "../panel/main/keys/release_signing_pub.pem")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow)
