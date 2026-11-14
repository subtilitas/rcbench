# The Dial's application: start-up and the face task, the input task, the
# BLE peripheral, the display, the touch controller, the buzzer, the power
# latch and the console.
idf_component_register(
    SRCS "dial_main.c" "dial_power.c" "dial_buzz.c" "dial_lcd.c"
         "dial_touch.c" "dial_input.c" "dial_ble.c" "dial_console.c"
         "dial_screen.c"
    INCLUDE_DIRS "."
    REQUIRES dial dialknob gfx bt nvs_flash console esp_app_format esp_timer
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
