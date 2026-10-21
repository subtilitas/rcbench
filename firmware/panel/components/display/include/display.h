/*
 * Double-buffered display driver for the 7-inch ESP32-P4 display board:
 * an EK79007 panel of 1024 x 600 pixels on a 2-lane MIPI DSI (Display
 * Serial Interface) link, fed in video mode by the DPI (Display Pixel
 * Interface) side of the ESP32-P4's DSI host.
 *
 * Two full RGB565 (16-bit red, green, blue) frame buffers of 1,228,800
 * bytes each live in PSRAM (pseudo-static random-access memory), allocated
 * by the DPI panel.  The application draws into the one that is not being
 * scanned out; display_flip() hands the finished buffer to the DPI panel,
 * which takes the pointer without a copy and scans it from the next frame,
 * waits for the frame-done event that frees the other buffer, and
 * re-points the canvas at it.
 *
 *     gfx_canvas_t *c = display_canvas();
 *     for (;;) {
 *         gfx_clear(c, GFX_BLACK);
 *         gfx_fill_circle(c, x, y, 40, GFX_RGB(0, 229, 255));
 *         display_flip();          // c re-points at the new back buffer
 *     }
 *
 * The canvas is BOARD_UI_H_RES x BOARD_UI_V_RES (800 x 480) in the
 * top-left corner of each buffer, with the buffer's 1024-pixel stride.
 * The rest of each buffer is black from allocation and nothing draws
 * there.
 *
 * Because the two buffers alternate, the back buffer after a flip holds the
 * frame from *two* flips ago, not the one just shown.  Redraw everything
 * each frame, or keep a per-buffer record keyed on display_back_index().
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool double_buffer;   /**< false -> single frame buffer, no flip     */
    bool backlight_on;    /**< backlight to 100 % after the first frame  */
} display_config_t;

#define DISPLAY_CONFIG_DEFAULT()             \
    (display_config_t) {                     \
        .double_buffer = true,               \
        .backlight_on = true,                \
    }

/**
 * Bring up the DSI PHY supply, the bus, the panel and the video stream,
 * and the board's I2C bus and backlight if not already up.
 */
esp_err_t display_init(const display_config_t *cfg);

/** Stop the video stream and release the panel, the bus and the PHY
 *  supply. */
esp_err_t display_deinit(void);

/**
 * The canvas the application should draw into.  The returned pointer is
 * stable for the lifetime of the driver; display_flip() re-points it at the
 * new back buffer in place.
 */
gfx_canvas_t *display_canvas(void);

/**
 * Present the back buffer and block until the buffer it replaces is no
 * longer scanned out, at most 200 ms.  In single-buffer mode this only
 * waits for the next frame-done event.
 */
esp_err_t display_flip(void);

/** Block until the next VSYNC (vertical sync) event.  0 means "wait
 *  forever". */
esp_err_t display_wait_vsync(uint32_t timeout_ms);

/** The canvas's size: the UI's area, not the panel's. */
int display_width(void);
int display_height(void);
bool display_double_buffered(void);

/**
 * Which frame buffer display_canvas() points at (0 or 1).
 *
 * The buffers alternate, so static content has to be drawn into both.  A
 * per-buffer validity flag keyed on this index keeps unchanging chrome out of
 * the per-frame path, which on a panel fed from PSRAM decides whether the
 * refresh rate is met.
 */
int display_back_index(void);

/** How long the last display_flip() spent blocked, in microseconds. */
uint32_t display_last_wait_us(void);

/** Frames presented since init. */
uint32_t display_frame_count(void);
/** Rolling frames per second, measured over the last 500 ms. */
float display_fps(void);

/** VSYNC events since init: the panel's own refresh, whatever the flips
 *  do. */
uint32_t display_vsync_count(void);

/** Backlight at 100 % (true) or 0 % (false). */
esp_err_t display_backlight(bool on);

#ifdef __cplusplus
}
#endif
