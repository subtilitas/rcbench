#include "ui_hero.h"

#include <math.h>
#include <stdio.h>

#include "ui_theme.h"
#include "ui_widgets.h"

/*
 * A reading, in a card of its own.
 *
 * The four of these used to be bare text on the background, laid out by
 * spacing alone, and four columns of label-number-unit-footer with nothing
 * around them read as one wall rather than as four instruments.  The card is
 * doing the work; the colour tag ties it to its trace on the plot above.
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
     * beside the name.
     *
     * A single small tag was too quiet to tie a card to its trace at a
     * glance -- which is the whole job, since the plot above draws four
     * lines and this is what says which is which.  Ringing the entire card
     * in it would give four competing outlines and no hierarchy, so it takes
     * the top edge only: enough to read across the bench, contained enough
     * that the four still sit as a set.
     */
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, def->color);
    gfx_hline(c, r.x + UI_R_CARD + 2, r.y + 2, r.w - 2 * UI_R_CARD - 4,
              gfx_lerp(def->color, ui_theme_color(UI_C_PANEL), 120));

    gfx_fill_round_rect(c, r.x + 12, r.y + 13, 3, 12, 1, def->color);
    gfx_text(c, r.x + 22, r.y + 12, def->label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);

    char number[24];
    /* A non-finite reading is shown as such rather than printed: "nan" in a
     * hero numeral is a reading, and "---" is the absence of one. */
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, def->decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    gfx_text(c, r.x + 12, r.y + 33, number, UI_FONT_NUM, def->color, 1);

    /* The unit sits on the numeral's baseline rather than its top, so it
     * reads as part of the same word. */
    const int nw = gfx_text_width(UI_FONT_NUM, number, 1);
    gfx_text(c, r.x + 12 + nw + 7, r.y + 47, def->unit, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    /* One rule above the footer.  The peak is a different kind of number from
     * the live one -- history rather than now -- and a hairline says so more
     * quietly than another colour would. */
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
