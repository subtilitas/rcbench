idf_component_register(
    SRCS "main.c" "panel_card.c" "panel_log.c" "panel_link.c" "panel_poll.c"
         "panel_services.c" "panel_supply.c" "panel_arm.c"
         "panel_render.c" "panel_art.c" "panel_update.c" "selftest.c"
         "knob_task.c" "kst_files.c"
    REQUIRES board display gt911 storage link can can_twai bench outputs safety servo ui gfx touch artwork esc
             settings settings_nvs art_flash esp_psram
             fwupdate webaccess fw_ota wifi_access
             esp_driver_gpio esp_driver_uart esp_timer
    LDFRAGMENTS "psram_bss.lf"
)

# The coprocessor image of the same release, carried so the panel can move
# the coprocessor to it over CAN (Controller Area Network):
#   idf.py -D RCBENCH_IOMCU_BIN=<path>/rcbench-iomcu.bin build
# A relative path is taken from firmware/panel.  A build without it carries
# none and offers no coprocessor update.
if(RCBENCH_IOMCU_BIN)
    get_filename_component(rcbench_iomcu_bin "${RCBENCH_IOMCU_BIN}" ABSOLUTE
                           BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${rcbench_iomcu_bin}")
        message(FATAL_ERROR "RCBENCH_IOMCU_BIN: ${rcbench_iomcu_bin} not found")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${rcbench_iomcu_bin}" BINARY
                           RENAME_TO rcbench_iomcu_bin)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RCBENCH_IOMCU_IMAGE=1)
endif()
