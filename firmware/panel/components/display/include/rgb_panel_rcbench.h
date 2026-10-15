/*
 * The RGB (red, green, blue) panel driver this firmware links.
 *
 * It is ESP-IDF's components/esp_lcd/rgb/esp_lcd_panel_rgb.c, copied at
 * configure time by tools/patch_rgb_driver.py with one change: every
 * restart of the scan is the start the panel gets at boot.  ESP-IDF v5.4
 * and v5.5 restart the ESP32-S3's scan through a DMA (direct memory
 * access) link that skips 17 px of a FIFO (first-in, first-out buffer)
 * they have just emptied, which leaves the picture 17 px to the left with
 * each line's tail wrapped one line down until the next power-on.  The
 * tool's docstring has the mechanism.
 *
 * The driver restarts the scan at a VBlank (vertical blanking interval)
 * when that frame counted fewer than 48 bounce-buffer refills, i.e. when
 * its refill interrupt was held off for longer than one 10-line buffer
 * (512 us at 16 MHz): a main-flash operation masks it for its whole
 * length.  With the clean start that frame is the only disturbed one; the
 * next frame scans from its first pixel.
 *
 * The copy's public functions carry an rcbench_ prefix.  Every file that
 * calls them includes this header before esp_lcd_panel_rgb.h (display.h
 * does), so the names in the code stay ESP-IDF's and ESP-IDF's own copy is
 * never linked in place of this one.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#define esp_lcd_new_rgb_panel rcbench_lcd_new_rgb_panel
#define esp_lcd_rgb_panel_register_event_callbacks \
    rcbench_lcd_rgb_panel_register_event_callbacks
#define esp_lcd_rgb_panel_set_pclk rcbench_lcd_rgb_panel_set_pclk
#define esp_lcd_rgb_panel_restart rcbench_lcd_rgb_panel_restart
#define esp_lcd_rgb_panel_get_frame_buffer rcbench_lcd_rgb_panel_get_frame_buffer
#define esp_lcd_rgb_panel_refresh rcbench_lcd_rgb_panel_refresh
#define esp_lcd_rgb_panel_set_yuv_conversion \
    rcbench_lcd_rgb_panel_set_yuv_conversion

#ifdef __cplusplus
extern "C" {
#endif

/** Restarts of the scan since boot, each a clean start.  Written by the
 *  VBlank interrupt; a 32-bit aligned read is atomic on the ESP32-S3. */
extern volatile uint32_t rcbench_lcd_rgb_restarts;

#ifdef __cplusplus
}
#endif
