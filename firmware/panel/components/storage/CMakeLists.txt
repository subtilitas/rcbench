idf_component_register(
    SRCS "storage.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_spi fatfs sdmmc esp_driver_sdspi
)
