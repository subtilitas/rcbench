/**
 * @file
 * @brief The alert band's text and drawing (ui_router_int.h): what
 *        ui_router.c draws across the bottom of the panel while an alert or
 *        a notice shows.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_router_int.h"

#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define PANEL_W UI_PANEL_W  /**< the panel's width, px */
#define PANEL_H UI_PANEL_H  /**< the panel's height, px */
#define ALERT_H UI_ALERT_H  /**< the alert band's height, px */

/* The alert's text, in UI_FONT_BAND (10 x 20): from x 12, the panel's width
 * less 12px at the left and 36px at the right, which clears the "x"
 * (976px, 97 cells of 10px, on the 1024px panel).  Text wider than one
 * line breaks at a space onto a second, 20px under the first: two rows
 * fill 40 of the band's 44px, 2px from its top. */
/** @brief The alert text's left edge: 12px. */
#define ALERT_TEXT_X 12
/** @brief The alert text's width: 976px, 97 cells, clear of the "x". */
#define ALERT_TEXT_W (PANEL_W - 2 * ALERT_TEXT_X - 24)
/** @brief An alert row to the next: 20px. */
#define ALERT_PITCH  20
/** @brief The alert's face: 10 x 20px. */
#define ALERT_FONT   UI_FONT_BAND

int ui_router_alert_rows(const char *text)
{
    return ui_text_wrap(NULL, gfx_rect_make(ALERT_TEXT_X, 0, ALERT_TEXT_W,
                                            ALERT_PITCH),
                        ALERT_PITCH, text, ALERT_FONT, 0);
}

void ui_router_alert_draw(gfx_canvas_t *c, const char *text, bool closable)
{
    /* A notice is amber with dark text; an alert is the danger colour. */
    const bool notice = ui_router_alert_is_notice();
    const gfx_color_t fill = ui_theme_color(notice ? UI_C_WARN : UI_C_DANGER);
    const gfx_color_t ink = ui_theme_color(notice ? UI_C_TEXT_ON_LIGHT
                                                  : UI_C_TEXT);
    const int y = PANEL_H - ALERT_H;
    /* One row centered in the band's height. */
    const int y1 = y + (ALERT_H - gfx_text_height(ALERT_FONT, 1)) / 2;
    gfx_fill_rect(c, 0, y, PANEL_W, ALERT_H, fill);
    if (ui_router_alert_rows(text) <= 1) {
        gfx_text(c, ALERT_TEXT_X, y1, text, ALERT_FONT, ink, 1);
    } else {
        (void)ui_text_wrap(c, gfx_rect_make(ALERT_TEXT_X, y + 2, ALERT_TEXT_W,
                                            ALERT_PITCH),
                           ALERT_PITCH, text, ALERT_FONT, ink);
    }
    if (closable) {
        gfx_text(c, PANEL_W - 12 - gfx_text_width(ALERT_FONT, "x", 1), y1,
                 "x", ALERT_FONT, ink, 1);
    }
}
