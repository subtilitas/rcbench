/**
 * @file
 * @brief UPDATES' private seam (upd_screen.c, help_upd.c): the layout of
 *        CHECK, the card and the IOmcu section, what the screen guide
 *        reads of the screen's state, and the guide's pages.
 *
 * The macros have short names (BX, RX); nothing but the two files includes
 * this header.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "upd_c6.h"

/*
 * UPDATES, in the body below the band: 1024 x 540px:
 * CHECK on the left, 460 x 56px at y 16.  The card on the right, x 484 to
 * 1016, y 16 to 336: what is kept and what last happened, each text broken
 * into lines of its 500px column (41 cells of UPD_FONT_BODY), 28px apart.
 * Under it the C6 RADIO row (upd_c6.h): its line at y 348, its hold beside
 * the IOmcu's at y 380.  The footer (ui_footer.h) under both: UPDATES,
 * the download's hold in the middle slot while a newer release is found,
 * and SETTINGS, the list of the reminder and RELEASES (upd_settings.c).
 */
/** @brief The buttons' left edge. */
#define BX      16
/** @brief The buttons' width, px. */
#define BW      460
/** @brief The buttons' height, px. */
#define BH      56
/** @brief The step from one button to the next, px. */
#define BPITCH  64
/** @brief The card's left edge. */
#define RX      484
/** @brief The card's top, and the first button's. */
#define RY      16
/** @brief The card's width, px. */
#define RW      532
/** @brief The card's height, px: it ends 12px over the C6 row's line. */
#define RH      320
/** @brief The card's inner padding, px. */
#define RPAD    16
/** @brief A UPD_FONT_BODY line's pitch, px. */
#define PITCH   28
/*
 * The IOmcu section, in the left column under CHECK:
 * Its heading, the two versions and up to two lines of state in
 * UPD_FONT_LABEL 24px apart, and the hold, 460 x 56px, whose foot is
 * body y 436, over the footer, beside the C6 RADIO's hold.
 */
/** @brief The IOmcu section's heading. */
#define CU_HEAD_Y  88
/** @brief The version the IOmcu runs. */
#define CU_RUN_Y   112
/** @brief The version this panel carries. */
#define CU_CAR_Y   136
/** @brief The first line of state. */
#define CU_LINE_Y  164
/** @brief The state's line pitch, px. */
#define CU_PITCH   24
/** @brief HOLD 2s: UPDATE IOMCU's top. */
#define CU_BTN_Y   UPD_C6_BTN_Y
/** @brief HOLD 2s: UPDATE IOMCU. */
#define CU_BTN     gfx_rect_make(BX, CU_BTN_Y, BW, BH)

/**
 * @brief Whether the last check found a newer release and nothing runs:
 *        the download's hold stands in the footer's middle slot.
 * @return True then.
 */
bool upd_guide_newer(void);

/**
 * @brief Whether CHECK reads CANCEL: a check runs.
 * @return True then.
 */
bool upd_guide_cancels(void);

struct ui_help_page;

/**
 * @brief The pages of UPDATES' guide as the screen is now: two, or the
 *        OPTIONS list's while it is open.
 * @return The count.
 */
int upd_help_count(void);

/**
 * @brief Page @p i of UPDATES' guide.
 * @param i   The page, from 0.
 * @param out The page, filled.
 * @return False past the last page.
 */
bool upd_help_page(int i, struct ui_help_page *out);
