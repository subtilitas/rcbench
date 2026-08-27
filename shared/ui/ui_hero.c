#include "ui_hero.h"

#include <math.h>
#include <stdio.h>

#include "ui_theme.h"
#include "ui_widgets.h"

void ui_hero_render(gfx_canvas_t *c, gfx_rect_t r, const ui_hero_def_t *def,
                    float value, float peak)
{
    if (c == NULL || def == NULL) {
        return;
    }

    gfx_text(c, r.x, r.y, def->label, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    char number[24];
    /* A non-finite reading is shown as such rather than printed: "nan" in a
     * hero numeral is a reading, and "---" is the absence of one. */
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, def->decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    gfx_text(c, r.x, r.y + 18, number, &gfx_font_num_24x30, def->color, 1);

    const int nw = gfx_text_width(&gfx_font_num_24x30, number, 1);
    gfx_text(c, r.x + nw + 6, r.y + 32, def->unit, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    if (isfinite(peak)) {
        char pk[24];
        ui_fmt(pk, sizeof(pk), peak, def->decimals);
        char line[32];
        const char *tag = (def->extreme_label != NULL)
                              ? def->extreme_label : "pk";
        snprintf(line, sizeof(line), "%s %s", tag, pk);
        gfx_text(c, r.x, r.y + 52, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}
