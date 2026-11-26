/**
 * @file
 * @brief ABOUT (SCREEN_ABOUT), which SETUP > PANEL's last row, About
 *        rcbench, opens: the firmware's versions, the designer, the
 *        project's licences, the trademark notice, the third-party
 *        components of the panel and the IOmcu firmware, and their notice
 *        texts.
 *
 * One column of rows across the body, 24px a row, 17 rows on screen from
 * body y 8, and a scroll bar 56px wide at x 960.  The rows, top to bottom:
 *
 * - rcbench, the panel firmware's version (RCBENCH_VERSION_STRING, as
 *   UPDATES shows it) and the IOmcu's, as UPDATES' IOmcu section has it,
 *   or that no IOmcu has named one;
 * - Design: Julian Wingert, Implemented by Claude and Julian, Copyright
 *   2026 Julian Wingert;
 * - LICENCES: firmware and software under the MIT License, the hardware
 *   designs in hardware/ under CC BY-NC-SA 4.0 (hardware/LICENSE), the
 *   repository github.com/subtilitas/rcbench;
 * - TRADEMARKS: the referential-use notice of NOTICE;
 * - the panel firmware's and the IOmcu firmware's third-party components,
 *   the references, and the notice texts (about_gen.h), each part under a
 *   heading of its own.
 *
 * A drag on the rows scrolls them a row per 24px, from where they stood at
 * the press.  A press on the bar puts the thumb's middle under the finger
 * and a drag moves it; a press on the thumb takes it where it is held.
 * PAGE UP and PAGE DOWN in the footer move 17 rows; BACK returns to SETUP
 * on PANEL's list as it was left, as the home tag does.  Every control acts
 * on a tap: the press arms it, the release of the same contact within 8px
 * acts.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"
#include "ui_screen.h"
#include "ui_setlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Rows on screen. */
#define ABOUT_ROWS 17

/** @brief The parts a test aims at. */
typedef enum {
    ABOUT_PART_TEXT = 0, /**< the rows' column           */
    ABOUT_PART_BAR,      /**< the scroll bar's track     */
    ABOUT_PART_BACK,     /**< BACK, the footer's left    */
    ABOUT_PART_PAGE_UP,  /**< PAGE UP, its middle        */
    ABOUT_PART_PAGE_DN,  /**< PAGE DOWN, its right       */
} about_part_t;

/**
 * @brief The screen, for the router.
 * @return The screen's table.
 */
const ui_screen_t *about_screen(void);

/** @brief Repaint the screen at the next frame. */
void about_screen_invalidate(void);

/** @brief Open ABOUT at its top; its home tag and BACK return to SETUP. */
void about_screen_open(void);

/**
 * @brief SETUP > PANEL's last row, About rcbench: a tap opens ABOUT.
 * @return The row.
 */
const ui_setlist_custom_t *about_setup_row(void);

/**
 * @brief A part's rectangle in the body.
 * @param part The part.
 * @return Its rectangle.
 */
gfx_rect_t about_screen_part(about_part_t part);

/**
 * @brief The rows the column holds in the language showing.
 * @return The count.
 */
int about_screen_rows(void);

/**
 * @brief The row on top of the column.
 * @return The row, from 0.
 */
int about_screen_top(void);

/**
 * @brief Scroll by @p rows rows, held to the first and the last screen.
 * @param rows Rows down; negative up.
 */
void about_screen_scroll(int rows);

/**
 * @brief Whether the last row shows at the column's foot.
 * @return True at the end.
 */
bool about_screen_at_end(void);

/**
 * @brief Row @p row's text, for a row of the generated parts.
 * @param row The row, from 0.
 * @return The text; NULL for a row of the screen's own lines or past the
 *         last.
 */
const char *about_screen_gen_text(int row);

#ifdef __cplusplus
}
#endif
