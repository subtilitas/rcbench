# The node: start-up, Bluetooth LE, the battery, the console, the policy,
# and the role this image is built for.
set(SRCS node_main.c node_ble.c node_bat.c node_console.c node_policy.c)
set(REQS rcnode bt nvs_flash esp_adc esp_timer console)
if(CONFIG_RCNODE_ROLE_BALANCE)
    list(APPEND SRCS node_bal.c)
else()
    list(APPEND SRCS node_pwr.c)
    list(APPEND REQS ina228 esp_driver_i2c)
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
