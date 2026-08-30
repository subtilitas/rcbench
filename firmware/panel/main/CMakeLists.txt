idf_component_register(
    SRCS "main.c"
    REQUIRES board display gt911 storage link can can_twai bench outputs safety ui gfx touch
             esp_driver_gpio esp_driver_uart esp_timer
)

# The CAN bring-up self-test is opt-in, because starting it takes native USB
# away -- GPIO19/20 carry both and the multiplexer has to choose.
#
#     idf.py -DRCBENCH_CAN_SELFTEST=1 build
#
# Without this the flag would set a CMake variable that never reaches the
# compiler, and the build would succeed while changing nothing -- which it did
# once, and the identical binary size is what gave it away.
if(RCBENCH_CAN_SELFTEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RCBENCH_CAN_SELFTEST=1)
    message(STATUS "rcbench: CAN self-test built in; USB is unavailable while "
                   "it runs, watch the UART socket")
endif()
