# The board-photograph partition. The format and the order of a write belong
# to shared/artwork, which the host suite exercises against memory; this is
# the three operations that need real flash.
idf_component_register(
    SRCS "art_flash_esp.c"
    INCLUDE_DIRS include
    REQUIRES artwork spi_flash
)
