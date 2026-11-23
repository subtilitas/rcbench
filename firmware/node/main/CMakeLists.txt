# The node: start-up, Bluetooth LE, the battery, the console, the policy,
# the role this image is built for, and the board's display when it has
# one (main/node_board.h).
set(SRCS node_main.c node_ble.c node_console.c node_policy.c)
# The requirements are the same for every role and board: ESP-IDF expands
# them before it reads the sdkconfig, so a CONFIG_ test cannot choose them.
# The image links only what its files call.
set(REQS rcnode vibe ina228 devota gfx bt nvs_flash esp_adc esp_timer console
         esp_driver_i2c esp_driver_gpio esp_driver_spi esp_lcd esp_driver_uart
         link safety mstime)
if(CONFIG_RCNODE_ROLE_BALANCE)
    # The battery is part of the balance role's ADC stream (node_bal.c).
    list(APPEND SRCS node_bal.c)
elseif(CONFIG_RCNODE_ROLE_SERVO)
    # The remote servo node's ES3C28P: the link to its Core2350B, the
    # policy, the poll and the safety pins (node_srv.h).
    list(APPEND SRCS node_bat.c node_srv.c node_srv_link.c node_srv_hb.c
                     node_srv_policy.c node_srv_poll.c)
else()
    list(APPEND SRCS node_bat.c node_pwr.c)
endif()
if(CONFIG_RCNODE_BOARD_ES3C28P AND NOT CONFIG_RCNODE_ROLE_SERVO)
    # The display node: the screen's logic, the display, the touch.
    list(APPEND SRCS node_view.c node_lcd.c node_disp.c)
endif()

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES ${REQS})
# Stricter than ESP-IDF's default.  -Wconversion on every file but
# node_ble.c: NimBLE's os_mbuf.h trips it in an inline function each file
# that includes it compiles.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow)
set(CONV ${SRCS})
list(REMOVE_ITEM CONV node_ble.c)
set_source_files_properties(${CONV} PROPERTIES COMPILE_OPTIONS
                            "-Wconversion;-Wno-sign-conversion")
