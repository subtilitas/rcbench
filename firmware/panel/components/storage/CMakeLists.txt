idf_component_register(
    SRCS "storage.c" "storage_list.c"
    INCLUDE_DIRS "include"
    REQUIRES board logfile fatfs sdmmc esp_driver_sdmmc
)
