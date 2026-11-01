/**
 * @file
 * @brief The view of HOTSPOT PASSWORD or WEB CODE on WI-FI: wifi_kept.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_kept.h"

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The pitch of the view's lines, px. */
#define LINE_P 28

/**
 * @brief A sentence in the 12 x 24 face, broken into lines of the card.
 * @param c   Canvas.
 * @param a   The room for the lines; its top is the first line's.
 * @param t   The text.
 * @param col Its colour.
 * @return The room under its last line.
 */
static gfx_rect_t para(gfx_canvas_t *c, gfx_rect_t a, const char *t,
                       gfx_color_t col)
{
    const int n = ui_text_wrap(c, gfx_rect_make(a.x, a.y, a.w, LINE_P), LINE_P,
                               t, UI_FONT_BODY, col);
    a.y = (int16_t)(a.y + n * LINE_P);
    return a;
}

void wifi_kept_draw(gfx_canvas_t *c, gfx_rect_t card, wifi_kept_t which,
                    const wifi_view_t *v)
{
    const bool pass = (which == WIFI_KEPT_PASS);
    const char *value = pass ? v->kept_pass : v->kept_code;
    ui_card(c, card, UI_PANEL);
    gfx_rect_t a = gfx_rect_make(card.x + 24, card.y + 16, card.w - 48, 44);
    gfx_text_in(c, a, pass ? TR(WF_KEPT_PASS_TITLE) : TR(WF_KEPT_CODE_TITLE),
                &gfx_font_20x36, UI_TEXT, 1, GFX_ALIGN_LEFT);
    a.y = (int16_t)(a.y + 56);
    a = para(c, a, pass ? TR(WF_KEPT_PASS_WHAT) : TR(WF_KEPT_CODE_WHAT),
             UI_TEXT_DIM);
    a.y = (int16_t)(a.y + 12);
    if (value[0] == '\0') {
        a = para(c, a, TR(WF_KEPT_IS_RANDOM), UI_TEXT);
    } else {
        a = para(c, a, TR(WF_KEPT_IS_SET), UI_TEXT);
        /* 48 characters take the card in the 20 x 36 face, 63 in 12 x 24. */
        const gfx_font_t *f = (gfx_text_width(&gfx_font_20x36, value, 1) <= a.w)
                                  ? &gfx_font_20x36 : UI_FONT_BODY;
        gfx_text_in(c, gfx_rect_make(a.x, a.y, a.w, 44), value, f, UI_TEXT, 1,
                    GFX_ALIGN_LEFT);
        a.y = (int16_t)(a.y + 48);
    }
    a.y = (int16_t)(a.y + 12);
    a = para(c, a, TR(WF_KEPT_RULE), UI_TEXT_DIM);
    a = para(c, a, TR(WF_KEPT_NEXT), UI_TEXT_DIM);
    if (!pass && !v->code_on) {
        a = para(c, a, TR(WF_KEPT_CODE_OFF), UI_WARN);
    }
    if (v->save_failed) {
        a = para(c, a, TR(WF_SAVE_FAILED_1), UI_WARN);
        (void)para(c, a, TR(WF_SAVE_FAILED_2), UI_WARN);
    }
}
