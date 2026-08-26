#include "ui_band.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define PANEL_W 800

/*
 * STOP is 132 px wide at the right edge, which is wider than it needs to be
 * for its label and exactly as wide as it needs to be for a thumb arriving in
 * a hurry.  It is the only control in the band, so nothing can be hit by
 * mistake on the way to it.
 */
#define STOP_W 132
#define STOP_M 6

gfx_rect_t ui_band_stop_rect(void)
{
    const gfx_rect_t r = {
        (int16_t)(PANEL_W - STOP_W - STOP_M),
        (int16_t)STOP_M,
        (int16_t)STOP_W,
        (int16_t)(UI_BAND_H - 2 * STOP_M),
    };
    return r;
}

/* Right to left, so no item has to guess another's width.  An earlier version
 * of this idea laid out left to right and put the clock underneath the ARMED
 * badge the first time a mode string got long. */
static int chip(gfx_canvas_t *c, int right, const char *label,
                gfx_color_t dot, gfx_color_t fill)
{
    const int w = 14 + (int)strlen(label) * 8 + 14 + (dot ? 14 : 0);
    const gfx_rect_t r = { (int16_t)(right - w), 10,
                           (int16_t)w, (int16_t)(UI_BAND_H - 20) };
    ui_pill(c, r, label, dot, fill);
    return right - w - 8;
}

void ui_band_render(gfx_canvas_t *c, const char *title, bool show_home,
                    const ui_bench_status_t *st, bool stop_pressed,
                    bool home_pressed)
{
    gfx_fill_rect(c, 0, 0, PANEL_W, UI_BAND_H, ui_theme_color(UI_C_PANEL));
    ui_rule(c, 0, UI_BAND_H - 1, PANEL_W, ui_theme_color(UI_C_EDGE));

    if (show_home && title != NULL) {
        ui_home_tag(c, title, home_pressed);
    } else if (title != NULL) {
        ui_wordmark(c, title, ui_theme_color(UI_C_TEXT));
    }

    /* STOP is drawn on every screen and is always live.  A control that is
     * sometimes there and sometimes not is one nobody trusts under load. */
    ui_button(c, ui_band_stop_rect(), "STOP", ui_theme_color(UI_C_DANGER),
              stop_pressed, true);

    if (st == NULL) {
        return;
    }

    int right = PANEL_W - STOP_W - STOP_M - 12;

    if (st->run_seconds > 0 || st->armed) {
        char clock[16];
        ui_clock(clock, sizeof(clock), st->run_seconds);
        right = chip(c, right, clock, 0, ui_theme_color(UI_C_PANEL_SUNK));
    }

    /* ARMED is a filled badge rather than a dot: at arm's length across a
     * bench, colour alone is not a signal you can bet a propeller on. */
    right = chip(c, right, st->armed ? "ARMED" : "SAFE", 0,
                 st->armed ? ui_theme_color(UI_C_DANGER)
                           : ui_theme_color(UI_C_PANEL_SUNK));

    if (st->faults != 0) {
        char fault[16];
        snprintf(fault, sizeof(fault), "FAULT %02X", (unsigned)st->faults);
        right = chip(c, right, fault, 0, ui_theme_color(UI_C_WARN));
    }

    if (st->mode != NULL) {
        right = chip(c, right, st->mode, 0, ui_theme_color(UI_C_PANEL_SUNK));
    }

    (void)chip(c, right, st->link_up ? "LINK" : "NO LINK",
               st->link_up ? ui_theme_color(UI_C_OK)
                           : ui_theme_color(UI_C_DANGER),
               ui_theme_color(UI_C_PANEL_SUNK));
}
