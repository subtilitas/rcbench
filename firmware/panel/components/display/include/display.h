/*
 * Double-buffered display driver for the Waveshare ESP32-S3-Touch-LCD-7.
 *
 * Two full 800x480 RGB565 (16-bit red, green, blue) framebuffers live in
 * PSRAM (pseudo-static random-access memory).  The application draws into
 * the one that is not being scanned out; display_flip() hands the finished
 * buffer to the LCD (liquid-crystal display) peripheral, waits for the
 * VSYNC (vertical sync) that makes the swap take effect, and re-points the
 * canvas at the buffer that came free.  No pixels are copied; the swap is a
 * DMA (direct memory access) descriptor change, so a flip costs one frame of
 * latency and nothing else.
 *
 *     gfx_canvas_t *c = display_canvas();
 *     for (;;) {
 *         gfx_clear(c, GFX_BLACK);
 *         gfx_fill_circle(c, x, y, 40, GFX_RGB(0, 229, 255));
 *         display_flip();          // c re-points at the new back buffer
 *     }
 *
 * Because the two buffers alternate, the back buffer after a flip holds the
 * frame from *two* flips ago, not the one just shown.  Redraw everything each
 * frame, or use display_flip_retain() to carry the visible image forward.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t pclk_hz;             /**< 0 -> BOARD_LCD_PCLK_HZ (16 MHz)        */
    uint16_t bounce_buffer_lines; /**< internal-RAM (random-access memory)
                                   *   bounce buffer height.  Bouncing costs
                                   *   CPU (central processing unit) time and
                                   *   stops the panel for the length of any
                                   *   main-flash operation, because the
                                   *   refill runs in an interrupt handler and
                                   *   reads PSRAM through the data cache.
                                   *   0 disables it and the EDMA (external
                                   *   direct memory access) fetches each
                                   *   frame from PSRAM directly, bypassing
                                   *   that cache.  Whether the EDMA keeps up
                                   *   at this pixel clock is not measured. */
    bool double_buffer;           /**< false -> single framebuffer, no flip   */
    bool backlight_on;            /**< turn the backlight on after init       */
} display_config_t;

#define DISPLAY_CONFIG_DEFAULT()             \
    (display_config_t) {                     \
        .pclk_hz = 0,                        \
        .bounce_buffer_lines = 10,           \
        .double_buffer = true,               \
        .backlight_on = true,                \
    }

/** Bring up the panel (and the board I2C/expander if not already up). */
esp_err_t display_init(const display_config_t *cfg);

/** Tear the panel down and free both framebuffers. */
esp_err_t display_deinit(void);

/**
 * The canvas the application should draw into.  The returned pointer is
 * stable for the lifetime of the driver; display_flip() re-points it at the
 * new back buffer in place.
 */
gfx_canvas_t *display_canvas(void);

/**
 * Present the back buffer and block until the swap has taken effect.
 * In single-buffer mode this only waits for the next VSYNC.
 */
esp_err_t display_flip(void);

/**
 * Like display_flip(), but copies the presented image into the new back
 * buffer so incremental drawing works.  That is a 750 KB PSRAM-to-PSRAM copy
 * per frame, only worth it when redrawing from scratch costs more.
 */
esp_err_t display_flip_retain(void);

/** Block until the next VSYNC.  0 means "wait forever". */
esp_err_t display_wait_vsync(uint32_t timeout_ms);

int display_width(void);
int display_height(void);
bool display_double_buffered(void);

/**
 * Which framebuffer display_canvas() points at (0 or 1).
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

/** Change the pixel clock at runtime; takes effect on the next VSYNC. */
esp_err_t display_set_pclk(uint32_t hz);

/** Panel DISP + backlight enable. */
esp_err_t display_backlight(bool on);

/** Underlying esp_lcd handle, for the raw API (application programming
 *  interface). */
esp_lcd_panel_handle_t display_panel(void);

#ifdef __cplusplus
}
#endif
