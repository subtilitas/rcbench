# The GT911 and the task around it.
#
# Named for the part because shared/touch registers a component called touch
# and ESP-IDF (Espressif Internet-of-Things Development Framework) has one
# flat component namespace.  The coordinate mapping and the press, move and
# release state machine are pure C in shared/touch, where the host suite
# tests them.
#
# A bench build reads a touch injector at 0x14 beside the glass
# (testbench/touch-injector):
#   idf.py -D RCBENCH_TOUCH_INJECTOR=ON build
# A build without the option compiles none of it and never addresses 0x14.
set(srcs "gt911.c" "touch.c")
if(RCBENCH_TOUCH_INJECTOR)
    list(APPEND srcs "touch_inject.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES board touch esp_driver_i2c esp_driver_gpio esp_timer
)
if(RCBENCH_TOUCH_INJECTOR)
    # PUBLIC: main.c names the injector on the band.
    target_compile_definitions(${COMPONENT_LIB} PUBLIC RCBENCH_TOUCH_INJECTOR=1)
endif()
