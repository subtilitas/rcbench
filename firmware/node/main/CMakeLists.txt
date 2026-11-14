# The node: start-up, Bluetooth LE, the battery, the console, the policy,
# and the role this image is built for.
set(SRCS node_main.c node_ble.c node_console.c node_policy.c)
# The requirements are the same for both roles: ESP-IDF expands them before
# it reads the sdkconfig, so a CONFIG_ test cannot choose them.  The image
# links only what its role's files call.
set(REQS rcnode vibe ina228 bt nvs_flash esp_adc esp_timer console
         esp_driver_i2c esp_driver_gpio)
if(CONFIG_RCNODE_ROLE_BALANCE)
    # The battery is part of the balance role's ADC stream (node_bal.c).
    list(APPEND SRCS node_bal.c)
else()
    list(APPEND SRCS node_bat.c node_pwr.c)
endif()

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                                    "${CMAKE_CURRENT_LIST_DIR}/../../../shared/link/include"
                       PRIV_REQUIRES ${REQS})
# Stricter than ESP-IDF's default.  -Wconversion on every file but
# node_ble.c: NimBLE's os_mbuf.h trips it in an inline function each file
# that includes it compiles.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow)
set(CONV ${SRCS})
list(REMOVE_ITEM CONV node_ble.c)
set_source_files_properties(${CONV} PROPERTIES COMPILE_OPTIONS
                            "-Wconversion;-Wno-sign-conversion")
