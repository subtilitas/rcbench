# The Dial's application: start-up and the face task, the BLE peripheral,
# the console and the face renderer for every board; the input task, the
# display, the touch controller, the buzzer and the power latch of the
# board the image is built for (main/dial_board.h, main/Kconfig.projbuild).
set(SRCS "dial_main.c" "dial_ble.c" "dial_console.c")
if(CONFIG_RCBENCH_DIAL_BOARD_K518)
    # The Guition JC3636K518C: the display, its boot face and its reading
    # faces at 360 x 360 (dial_k518_lcd.c, dial_k518_screen.c); its other
    # parts do nothing yet (dial_k518.c).
    list(APPEND SRCS "dial_k518.c" "dial_k518_lcd.c" "dial_k518_screen.c")
else()
    list(APPEND SRCS "dial_screen.c" "dial_power.c" "dial_buzz.c"
                     "dial_lcd.c" "dial_touch.c" "dial_input.c")
endif()
# The requirements are the same for every board: ESP-IDF expands them
# before it reads the sdkconfig, so a CONFIG_ test cannot choose them.
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES dial dialknob devota bleota gfx bt nvs_flash console esp_app_format esp_timer
             esp_lcd esp_driver_gpio esp_driver_pcnt esp_driver_ledc
             esp_driver_spi esp_driver_i2c esp_driver_usb_serial_jtag
)

# The console's test hooks; see RCBENCH_DIAL_HOOKS in firmware/dial/CMakeLists.txt.
if(RCBENCH_DIAL_HOOKS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RCBENCH_DIAL_HOOKS=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RCBENCH_DIAL_HOOKS=0)
endif()

# The application's own files build without a warning: -Wextra on top of
# ESP-IDF's -Wall, and every warning an error.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wshadow -Werror)
