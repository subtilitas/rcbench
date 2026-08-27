# The GT911 and the driver around it.
#
# Named for the part rather than for "touch", because shared/touch already
# registers a component of that name and ESP-IDF has one flat namespace: two
# components called touch is a configure-time collision, and the pure half is
# the one whose name should not have to move.  The coordinate mapping and the
# press/move/release state machine are not here -- they are pure C in
# shared/touch, which is what lets the navigation tests exist.
idf_component_register(
    SRCS "gt911.c" "touch.c"
    INCLUDE_DIRS "include"
    REQUIRES board touch esp_driver_i2c esp_driver_gpio esp_timer
)
