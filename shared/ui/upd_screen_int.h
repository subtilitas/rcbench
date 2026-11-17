/**
 * @file
 * @brief UPDATES' private seam (upd_screen.c, upd_single.c, upd_remind.c,
 *        help_upd.c): the layout of CHECK, the card and the IOmcu section,
 *        the faces and drawing helpers the screens share, what the reminder
 *        and the screen guide read of the screen's state, and the guide's
 *        pages.
 *
 * The macros have short names (BX, RX); nothing but these four files
 * includes this header.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_screen.h"

/*
 * The faces of the type scale these screens draw in:
 * UPD_FONT_LABEL (10 x 20) for the buttons, the IOmcu section's lines and
 * the reminder's buttons; UPD_FONT_BODY (12 x 24) for the card's text and
 * the reminder's lines.
 */
/** @brief The buttons and the IOmcu section: 10 x 20. */
#define UPD_FONT_LABEL (&gfx_font_10x20)
/** @brief The card's text and the reminder's lines: 12 x 24. */
#define UPD_FONT_BODY  UI_FONT_BODY

/*
 * UPDATES, in the body below the band: 1024 x 540px:
 * CHECK on the left, 460 x 56px at y 16.  The card on the right, x 484 to
 * 1016, y 16 to 336: what is kept and what last happened, each text broken
 * into lines of its 500px column (41 cells of UPD_FONT_BODY), 28px apart.
 * The footer (ui_footer.h) under both: UPDATES, the download's hold in the
 * middle slot while a newer release is found, and OPTIONS, the list of the
 * reminder, RELEASES and Single updates (upd_settings.c).
 *
 * SINGLE UPDATES (upd_single.c) covers the body: its heading in the card's
 * place, the IOmcu section under CHECK's place, the WIRELESS DEVICES entry
 * (upd_dev.h) between the section's lines and its hold, the C6 RADIO row
 * (upd_c6.h) under the card: its line at y 348, its hold beside the
 * IOmcu's at y 380; BACK in the footer's right slot.
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
 * The IOmcu section, on SINGLE UPDATES in the left column under CHECK's
 * place:
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
 * @brief One line of @p t in @p f at (@p x, @p y), @p w wide.
 * @param c   Canvas, the body.
 * @param x   Its left edge.
 * @param y   Its top.
 * @param w   Its width.
 * @param t   The text.
 * @param f   Its face.
 * @param col Its colour.
 */
void upd_text_in(gfx_canvas_t *c, int x, int y, int w, const char *t,
                 const gfx_font_t *f, gfx_color_t col);

/**
 * @brief A button of these screens: ui_button_font() in UPD_FONT_LABEL.
 * @param c       Canvas, the body.
 * @param r       The button.
 * @param label   Its label.
 * @param fill    Its fill.
 * @param pressed Whether a finger is on it.
 * @param enabled Whether it takes a press.
 */
void upd_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                gfx_color_t fill, bool pressed, bool enabled);

/**
 * @brief A hold button's fill: the panel's, filling to the OK colour.
 * @param h The hold.
 * @return The fill.
 */
gfx_color_t upd_hold_fill(const ui_hold_t *h);

/**
 * @brief Whether a download may start now: the last check found a newer
 *        release, nothing gates it and the home network is kept.
 * @return True then.
 */
bool upd_download_allowed(void);

/**
 * @brief Ask the application for @p req (UPD_REQ_* bits), drained by
 *        upd_screen_take().
 * @param req The bits.
 */
void upd_screen_ask(unsigned req);

/**
 * @brief Why a job did not start (BLOCKED): one line, or two for the gate.
 * @param why    The reason.
 * @param out    The first line.
 * @param n      Bytes of @p out.
 * @param second The second line, "" for none.
 * @param n2     Bytes of @p second.
 */
void upd_blocked_text(upd_why_t why, char *out, size_t n, char *second,
                      size_t n2);

/** @brief The reminder is drawn again (upd_remind.c). */
void upd_remind_touched(void);

/** @brief A press on the reminder's hold that a download no longer allows
 *         ends unfired (upd_remind.c). */
void upd_remind_drop_if_gone(void);

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
 * @brief The pages of UPDATES' guide as the screen is now: two on UPDATES
 *        and on SINGLE UPDATES, one on the wireless devices' page, or the
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
