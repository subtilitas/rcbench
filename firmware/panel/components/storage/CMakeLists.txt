idf_component_register(
    SRCS "storage.c" "storage_list.c"
    INCLUDE_DIRS "include"
    REQUIRES board logfile esp_driver_spi fatfs sdmmc esp_driver_sdspi
)
