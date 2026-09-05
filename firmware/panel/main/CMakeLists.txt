idf_component_register(
    SRCS "main.c" "selftest.c"
    REQUIRES board display gt911 storage link can can_twai bench outputs safety ui gfx touch artwork
             settings settings_nvs art_flash esp_psram
             esp_driver_gpio esp_driver_uart esp_timer
)

# The CAN (Controller Area Network) bring-up self-test is opt-in, because
# starting it takes native USB (Universal Serial Bus) away: GPIO19 and GPIO20
# carry both, and the multiplexer selects one.
#
#     idf.py -DRCBENCH_CAN_SELFTEST=1 build
#
# The CMake variable is forwarded as a compile definition.  Without that, the
# flag would set a variable that never reaches the compiler and the build
# would succeed unchanged.
if(RCBENCH_CAN_SELFTEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RCBENCH_CAN_SELFTEST=1)
    message(STATUS "rcbench: CAN self-test built in; USB is unavailable while "
                   "it runs, watch the UART socket")
endif()
