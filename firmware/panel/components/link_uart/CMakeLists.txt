idf_component_register(
    SRCS "link_uart.c"
    INCLUDE_DIRS "include"
    # No dependency on main/: the pins arrive as arguments, so this component
    # knows about a wire and nothing about a board.
    REQUIRES link esp_driver_uart esp_driver_gpio
)
