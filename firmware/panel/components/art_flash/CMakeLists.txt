# The board-photograph partition. The format and the order of a write belong
# to shared/artwork, which the host suite exercises against memory; this is
# the three operations that need real flash.
idf_component_register(
    SRCS "art_flash_esp.c"
    INCLUDE_DIRS include
    # esp_partition is its own component in IDF v5; spi_flash carries the
    # sector size the store erases in.
    REQUIRES artwork esp_partition spi_flash
)
