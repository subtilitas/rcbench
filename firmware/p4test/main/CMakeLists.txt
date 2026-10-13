idf_component_register(SRCS "p4test.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_psram spi_flash)
