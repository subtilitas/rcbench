/**
 * @file
 * @brief The status band: the bench state on every screen, and the STOP control
 * that works from every screen.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Where STOP is, in panel coordinates.  The router hit-tests it. */
gfx_rect_t ui_band_stop_rect(void);

/**
 * The mode the band names: BENCH while the link is up and the panel drives
 * the IOmcu's outputs, SIM while it runs on its own models.  Mode
 * names, so English in every language.
 */
const char *ui_band_mode(bool link_up);

#ifdef RCBENCH_TOUCH_INJECTOR
/**
 * INJECT: the mode a panel built with RCBENCH_TOUCH_INJECTOR names in place
 * of BENCH and SIM while a touch injector (testbench/touch-injector) is
 * attached.  A mode name, so English in every language.  A build with
 * RCBENCH_TOUCH_INJECTOR=OFF has neither this nor INJECT's bubble text.
 */
const char *ui_band_mode_inject(void);
#endif

/**
 * The armed chip's label for @p roles, UI_ARMED_* bits: ARMED MOTOR, ARMED
 * SERVO, or ARMED for both and for none.  English in every language, as
 * STOP and the other controls are.
 */
const char *ui_band_armed_label(uint8_t roles);

/**
 * The label for the IOmcu's refused link frames, "Rejected link
 * frames: N" in the language in force, into @p out of @p n bytes: the one place it
 * is built.  MOTOR & ESC shows it in its header strip.  False, with @p out
 * empty, while the link is down or the count (st->link_refused) is 0, so an
 * IOmcu older than 4.21, which sends no count, shows no label.
 */
bool ui_link_refused_label(const ui_bench_status_t *st, char *out, size_t n);

/**
 * Where @p chip is drawn for @p st, in panel coordinates, into @p out (which
 * may be NULL).  False when @p st draws no such chip: no FAULT chip while
 * the bitmap is 0, no clock before a run, no mode chip without a mode.
 */
bool ui_band_chip_rect(const ui_bench_status_t *st, ui_band_chip_t chip,
                       gfx_rect_t *out);

/**
 * @brief The chip a touch lands on.
 *
 * A chip is hit across the band's whole height and 3 px either side, half
 * the gap to the next one; STOP and the home tag are not chips.
 * @param st The bench status the band shows.
 * @param x The touch's panel x.
 * @param y The touch's panel y.
 * @return The chip, or UI_BAND_CHIP_NONE.
 */
ui_band_chip_t ui_band_chip_at(const ui_bench_status_t *st, int x, int y);

/**
 * The help bubble of @p chip for @p st, in panel coordinates: under the
 * band, at the chip's left edge, moved left as far as the panel's right
 * edge needs, and as tall as its text.  FAULT's spans the panel, so a line
 * per bit has room.  False when the chip is not drawn.
 */
bool ui_band_help_rect(const ui_bench_status_t *st, ui_band_chip_t chip,
                       gfx_rect_t *out);

/**
 * Draw the help bubble of @p chip for @p st: the chip's label as its title,
 * then what the state means for the bench and what to do about it, in the
 * language showing.  Built from @p st each time, so it follows the state.
 */
void ui_band_help_render(gfx_canvas_t *c, const ui_bench_status_t *st,
                         ui_band_chip_t chip);

/**
 * @brief A number for what ui_band_render() draws from the same arguments:
 *        the title, the home tag's and STOP's presses, the theme, and every
 *        chip's place, label and colours.  Two calls that return the same
 *        number draw the same band, so the router repaints a buffer's band
 *        only when the number moves (ui_router_render()).
 * @param title the current screen's title, or NULL
 * @param show_home whether the title is the home tag
 * @param st the bench's status, or NULL
 * @param stop_pressed STOP is pressed
 * @param home_pressed the home tag is pressed
 * @return the number, a 32-bit FNV-1a hash
 */
uint32_t ui_band_key(const char *title, bool show_home,
                     const ui_bench_status_t *st, bool stop_pressed,
                     bool home_pressed);

/**
 * Draw the band.  @p title is the current screen's title, or NULL on the
 * splash, which has no home tag and no bench status.
 */
void ui_band_render(gfx_canvas_t *c, const char *title, bool show_home,
                    const ui_bench_status_t *st, bool stop_pressed,
                    bool home_pressed);

#ifdef __cplusplus
}
#endif
