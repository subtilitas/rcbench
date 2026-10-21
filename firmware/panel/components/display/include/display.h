/**
 * @file display.h
 * @brief Double-buffered display driver for the 7-inch ESP32-P4 display
 *        board.
 *
 * An EK79007 panel of 1024 x 600 pixels on a 2-lane MIPI DSI (Display
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

/** @brief How display_init() sets the display up. */
typedef struct {
    bool double_buffer;   /**< false -> single frame buffer, no flip     */
    bool backlight_on;    /**< backlight to 100 % after the first frame  */
} display_config_t;

/** @brief Two buffers, backlight on after the first frame. */
#define DISPLAY_CONFIG_DEFAULT()             \
    (display_config_t) {                     \
        .double_buffer = true,               \
        .backlight_on = true,                \
    }

/**
 * @brief Bring up the DSI PHY supply, the bus, the panel and the video
 *        stream, and the board's I2C bus and backlight if not already up.
 * @param cfg The set-up; NULL takes DISPLAY_CONFIG_DEFAULT().
 * @return ESP_OK, also when already up; otherwise the error of the step
 *         that failed, with everything before it released.
 */
esp_err_t display_init(const display_config_t *cfg);

/**
 * @brief Stop the video stream and release the panel, the bus and the PHY
 *        supply.
 * @return ESP_OK, also when the display was not up.
 */
esp_err_t display_deinit(void);

/**
 * @brief The canvas the application draws into.  The returned pointer is
 * stable for the lifetime of the driver; display_flip() re-points it at the
 * new back buffer in place.
 *
 * @return The canvas; before display_init() it has no pixels.
 */
gfx_canvas_t *display_canvas(void);

/**
 * @brief Present the back buffer and block until the buffer it replaces is no
 * longer scanned out, at most 200 ms.  In single-buffer mode this only
 * waits for the next frame-done event.
 *
 * @return ESP_OK; ESP_ERR_TIMEOUT when no frame-done event came in 200 ms;
 *         ESP_ERR_INVALID_STATE before display_init(); or the DPI panel's
 *         error for the hand-over.
 */
esp_err_t display_flip(void);

/**
 * @brief Block until the next VSYNC (vertical sync) event.
 * @param timeout_ms The longest wait in milliseconds; 0 waits forever.
 * @return ESP_OK, ESP_ERR_TIMEOUT, or ESP_ERR_INVALID_STATE before
 *         display_init().
 */
esp_err_t display_wait_vsync(uint32_t timeout_ms);

/**
 * @brief The canvas's width: the UI's area, not the panel's.
 * @return BOARD_UI_H_RES, 800 pixels.
 */
int display_width(void);

/**
 * @brief The canvas's height: the UI's area, not the panel's.
 * @return BOARD_UI_V_RES, 480 pixels.
 */
int display_height(void);

/**
 * @brief Whether the display runs two frame buffers.
 * @return true with two buffers, false with one or before display_init().
 */
bool display_double_buffered(void);

/**
 * @brief Which frame buffer display_canvas() points at.
 *
 * The buffers alternate, so static content has to be drawn into both.  A
 * per-buffer validity flag keyed on this index keeps unchanging chrome out of
 * the per-frame path, which on a panel fed from PSRAM decides whether the
 * refresh rate is met.
 *
 * @return 0 or 1.
 */
int display_back_index(void);

/**
 * @brief How long the last display_flip() spent blocked.
 * @return Microseconds.
 */
uint32_t display_last_wait_us(void);

/**
 * @brief Frames presented since display_init().
 * @return The count; it wraps at 2^32.
 */
uint32_t display_frame_count(void);
/**
 * @brief Rolling frames per second, measured over the last 500 ms.
 * @return Frames per second; 0 before the first 500 ms.
 */
float display_fps(void);

/**
 * @brief VSYNC events since display_init(): the panel's own refresh,
 *        whatever the flips do.
 * @return The count; it wraps at 2^32.
 */
uint32_t display_vsync_count(void);

/**
 * @brief Backlight at 100 % or 0 %.
 * @param on true for 100 %, false for 0 % (dark).
 * @return As board_backlight().
 */
esp_err_t display_backlight(bool on);

#ifdef __cplusplus
}
#endif
