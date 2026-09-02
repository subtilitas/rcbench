# The GT911 and the task around it.
#
# Named for the part because shared/touch registers a component called touch
# and ESP-IDF (Espressif Internet-of-Things Development Framework) has one
# flat component namespace.  The coordinate mapping and the press, move and
# release state machine are pure C in shared/touch, where the host suite
# tests them.
idf_component_register(
    SRCS "gt911.c" "touch.c"
    INCLUDE_DIRS "include"
    REQUIRES board touch esp_driver_i2c esp_driver_gpio esp_timer
)
