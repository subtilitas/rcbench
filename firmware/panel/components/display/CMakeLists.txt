# The display, and the RGB (red, green, blue) panel driver it runs on:
# ESP-IDF's own source, copied with a clean restart by
# tools/patch_rgb_driver.py (include/rgb_panel_rcbench.h says why).  A
# driver the tool cannot patch stops the configure step.
idf_build_get_property(idf_path IDF_PATH)
set(rgb_src "${idf_path}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c")
set(srcs "display.c")
set(priv_includes "")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_build_get_property(python PYTHON)
    set(rgb_tool "${CMAKE_CURRENT_LIST_DIR}/../../../../tools/patch_rgb_driver.py")
    set(rgb_out "${CMAKE_CURRENT_BINARY_DIR}/esp_lcd_panel_rgb_rcbench.c")
    execute_process(
        COMMAND "${python}" "${rgb_tool}" --source "${rgb_src}" --out "${rgb_out}"
        RESULT_VARIABLE rgb_rc)
    if(NOT rgb_rc EQUAL 0)
        message(FATAL_ERROR "tools/patch_rgb_driver.py failed on ${rgb_src}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${rgb_src}" "${rgb_tool}")
    list(APPEND srcs "${rgb_out}")
    # The driver's own private headers: esp_lcd_common.h and
    # rgb_lcd_rotation_sw.h.
    list(APPEND priv_includes "${idf_path}/components/esp_lcd/priv_include"
                              "${idf_path}/components/esp_lcd/rgb")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS ${priv_includes}
    REQUIRES gfx board esp_lcd esp_timer
    PRIV_REQUIRES esp_mm esp_psram esp_pm esp_hw_support esp_driver_gpio hal soc
)
