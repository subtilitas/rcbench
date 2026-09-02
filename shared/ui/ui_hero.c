/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_hero.h"

#include <math.h>
#include <stdio.h>

#include "ui_theme.h"
#include "ui_widgets.h"

/*
 * One reading in a card of its own.  The card separates it from the readings
 * beside it; the colour tag along the top edge ties it to its trace on the
 * plot above.
 */
void ui_hero_render(gfx_canvas_t *c, gfx_rect_t r, const ui_hero_def_t *def,
                    float value, float peak)
{
    if (c == NULL || def == NULL) {
        return;
    }

    ui_card(c, r, ui_theme_color(UI_C_PANEL));

    /*
     * The channel's colour along the card's top edge, and again as a tick
     * beside the name.  The top edge only: a full outline in the channel
     * colour would give four competing outlines and no hierarchy between the
     * cards.
     */
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, def->color);
    gfx_hline(c, r.x + UI_R_CARD + 2, r.y + 2, r.w - 2 * UI_R_CARD - 4,
              gfx_lerp(def->color, ui_theme_color(UI_C_PANEL), 120));

    gfx_fill_round_rect(c, r.x + 12, r.y + 13, 3, 12, 1, def->color);
    gfx_text(c, r.x + 22, r.y + 12, def->label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);

    char number[24];
    /* A non-finite reading prints as "---"; "nan" in a hero numeral would
     * read as a value. */
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, def->decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    const gfx_seg_style_t seg = ui_seg_hero();
    gfx_seg_text(c, r.x + 12, r.y + 29, number, &seg, def->color,
                 gfx_lerp(ui_theme_color(UI_C_PANEL), def->color, 34));

    /* The unit sits on the numerals' baseline rather than their top, so it
     * reads as part of the same word. */
    const int nw = gfx_seg_width(number, &seg);
    gfx_text(c, r.x + 12 + nw + 8, r.y + 45, def->unit, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    /* One hairline above the footer separates the peak, a value from the
     * past, from the live value. */
    gfx_hline(c, r.x + 12, r.y + 62, r.w - 24,
              gfx_lerp(ui_theme_color(UI_C_PANEL),
                       ui_theme_color(UI_C_EDGE), 150));

    if (isfinite(peak)) {
        char pk[24];
        ui_fmt(pk, sizeof(pk), peak, def->decimals);
        char line[32];
        const char *tag = (def->extreme_label != NULL)
                              ? def->extreme_label : "pk";
        snprintf(line, sizeof(line), "%s %s", tag, pk);
        gfx_text(c, r.x + 12, r.y + 68, line, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}
