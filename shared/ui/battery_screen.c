/*
 * The battery screen: each cell's departure from the pack's mean.
 *
 * Six cell voltages that agree to two decimals hide a cell 40 mV adrift, so
 * the cells are drawn as departures from their own mean, and the verdict
 * follows the spread: the widest gap between any two cells.  The absolute
 * voltage of each cell is printed under its bar.
 *
 * SPDX-License-Identifier: MIT
 */

#include "battery_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

#define PAD     6
#define LCARD_W 494
#define RCARD_X (PAD + LCARD_W + 8)
#define RCARD_W (W - RCARD_X - PAD)

#define PLOT_X  (PAD + 18)
#define PLOT_W  (LCARD_W - 36)
#define MEAN_Y  200
#define REACH   96             /* pixels for the full divergence scale */
/*
 * The scale floor, in millivolts at full deflection.
 *
 * The range autoscales to the pack: a fixed 100 mV scale draws a healthy
 * pack and a pack with one weak cell as the same six near-flat bars.  The
 * floor stops a pack that agrees to 1 mV from being drawn as if it were
 * diverging.
 */
#define SCALE_FLOOR_MV 12.0f

/* Where a pack stops being fine.  Under load, cells that differ by more than
 * this are a pack with a weak cell, not a pack that needs balancing;
 * balancing hides the weak cell for one more cycle. */
#define SPREAD_WATCH_MV 30.0f
#define SPREAD_BAD_MV   60.0f

static struct {
    battery_state_t b;
    uint32_t rev;
    uint32_t drawn[2];
    unsigned drawn_mask;
} s;

void battery_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

void battery_screen_set(const battery_state_t *b)
{
    if (b == NULL) {
        if (s.b.valid) {
            memset(&s.b, 0, sizeof(s.b));
            ++s.rev;
        }
        return;
    }
    s.b = *b;
    ++s.rev;
}

static float mean_v(void)
{
    if (!s.b.valid || s.b.cells <= 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (int i = 0; i < s.b.cells; ++i) {
        sum += s.b.volts[i];
    }
    return sum / (float)s.b.cells;
}

float battery_screen_spread_mv(void)
{
    if (!s.b.valid || s.b.cells <= 0) {
        return 0.0f;
    }
    float lo = s.b.volts[0], hi = s.b.volts[0];
    for (int i = 1; i < s.b.cells; ++i) {
        if (s.b.volts[i] < lo) { lo = s.b.volts[i]; }
        if (s.b.volts[i] > hi) { hi = s.b.volts[i]; }
    }
    return (hi - lo) * 1000.0f;
}

/* ----------------------------------------------------------------- drawing */

/* Full deflection, in millivolts: the pack's own worst departure with a
 * little headroom, never less than the floor. */
static float scale_mv(void)
{
    float peak = 0.0f;
    if (s.b.valid) {
        const float mean = mean_v();
        for (int i = 0; i < s.b.cells; ++i) {
            const float d = fabsf(s.b.volts[i] - mean) * 1000.0f;
            if (d > peak) { peak = d; }
        }
    }
    peak *= 1.2f;
    return (peak < SCALE_FLOOR_MV) ? SCALE_FLOOR_MV : peak;
}

static void draw_cells(gfx_canvas_t *c)
{
    const gfx_color_t grid = ui_theme_color(UI_C_GRID);
    const gfx_color_t dim  = ui_theme_color(UI_C_TEXT_DIM);
    const float full = scale_mv();

    /* The mean, and the band inside which a pack is not interesting. */
    gfx_hline(c, PLOT_X, MEAN_Y, PLOT_W, ui_theme_color(UI_C_GRID_STRONG));
    for (int i = -1; i <= 1; i += 2) {
        float f = (SPREAD_WATCH_MV / 2.0f) / full;
        if (f > 1.0f) { f = 1.0f; }
        gfx_hline(c, PLOT_X, MEAN_Y + i * (int)(f * (float)REACH),
                  PLOT_W, grid);
    }

    /* Autoscaled, so the scale has to be on the picture: without it a bar
     * that fills the plot could be 4 mV or 40 mV. */
    char sc[24];
    snprintf(sc, sizeof(sc), "+%d mV", (int)(full + 0.5f));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(PLOT_X + PLOT_W - 120),
                                 (int16_t)(MEAN_Y - REACH - 4), 120, 16 },
                sc, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    snprintf(sc, sizeof(sc), "-%d mV", (int)(full + 0.5f));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(PLOT_X + PLOT_W - 120),
                                 (int16_t)(MEAN_Y + REACH - 12), 120, 16 },
                sc, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);

    if (!s.b.valid || s.b.cells <= 0) {
        gfx_text_in(c, (gfx_rect_t){ PAD, (int16_t)(MEAN_Y + 40),
                                     LCARD_W, 16 },
                    "no monitor on the balance lead", UI_FONT_LABEL,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_CENTER);
        return;
    }

    const float mean = mean_v();
    const int slot = PLOT_W / s.b.cells;
    int bw = slot - 12;
    if (bw > 34) { bw = 34; }
    if (bw < 8)  { bw = 8;  }

    /* The weakest cell is named rather than left to be spotted. */
    int weak = 0;
    for (int i = 1; i < s.b.cells; ++i) {
        if (s.b.volts[i] < s.b.volts[weak]) { weak = i; }
    }

    for (int i = 0; i < s.b.cells; ++i) {
        const int cx = PLOT_X + i * slot + slot / 2;
        const float d = (s.b.volts[i] - mean) * 1000.0f;
        float f = d / full;
        if (f >  1.0f) { f =  1.0f; }
        if (f < -1.0f) { f = -1.0f; }
        const int span = (int)(f * (float)REACH);

        const bool bad = (i == weak)
                         && (battery_screen_spread_mv() >= SPREAD_WATCH_MV);
        const gfx_color_t ink = bad ? ui_theme_color(UI_C_DANGER)
                                    : ui_theme_color(UI_C_ACCENT);

        if (span >= 0) {
            gfx_fill_round_rect(c, cx - bw / 2, MEAN_Y - span, bw,
                                (span < 4) ? 4 : span, 3, ink);
        } else {
            gfx_fill_round_rect(c, cx - bw / 2, MEAN_Y, bw,
                                (-span < 4) ? 4 : -span, 3, ink);
        }

        char n[16];
        snprintf(n, sizeof(n), "%d", (i + 1) & 0xFF);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(cx - slot / 2),
                                     (int16_t)(MEAN_Y + REACH + 16),
                                     (int16_t)slot, 16 },
                    n, UI_FONT_LABEL, dim, 1, GFX_ALIGN_CENTER);

        char v[16];
        snprintf(v, sizeof(v), "%.2f", (double)s.b.volts[i]);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(cx - slot / 2),
                                     (int16_t)(MEAN_Y + REACH + 38),
                                     (int16_t)slot, 16 },
                    v, UI_FONT_LABEL,
                    bad ? ui_theme_color(UI_C_DANGER)
                        : ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_CENTER);
    }
}

static void row(gfx_canvas_t *c, int y, const char *k, const char *v,
                gfx_color_t ink)
{
    const int x = RCARD_X + 14;
    gfx_text(c, x, y, k, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 84), (int16_t)y,
                                 (int16_t)(RCARD_W - 28 - 84), 16 },
                v, UI_FONT_LABEL, ink, 1, GFX_ALIGN_RIGHT);
}

static void draw_verdict(gfx_canvas_t *c)
{
    const int x = RCARD_X + 14;
    const int w = RCARD_W - 28;
    const float spread = battery_screen_spread_mv();

    const char *word, *why;
    gfx_color_t tone;
    if (!s.b.valid) {
        word = "NO PACK"; why = "nothing on the lead";
        tone = ui_theme_color(UI_C_TEXT_FAINT);
    } else if (spread >= SPREAD_BAD_MV) {
        word = "REPLACE"; why = "one cell is adrift";
        tone = ui_theme_color(UI_C_DANGER);
    } else if (spread >= SPREAD_WATCH_MV) {
        word = "WATCH";   why = "the spread is opening";
        tone = ui_theme_color(UI_C_WARN);
    } else {
        word = "HEALTHY"; why = "cells agree under load";
        tone = ui_theme_color(UI_C_OK);
    }

    gfx_fill_round_rect(c, x, 56, w, 56, UI_R_CARD,
                        gfx_lerp(ui_theme_color(UI_C_PANEL), tone, 40));
    gfx_draw_round_rect(c, x, 56, w, 56, UI_R_CARD, tone);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, 74, (int16_t)w, 20 },
                word, UI_FONT_HEAD, tone, 1, GFX_ALIGN_CENTER);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, 120, (int16_t)w, 16 },
                why, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_CENTER);
}

static void draw_right(gfx_canvas_t *c)
{
    draw_verdict(c);

    const int x = RCARD_X + 14;
    const int w = RCARD_W - 28;
    gfx_hline(c, x, 152, w, ui_theme_color(UI_C_EDGE));

    char buf[32];
    const gfx_color_t ink = ui_theme_color(UI_C_TEXT);
    if (!s.b.valid) {
        for (int i = 0; i < 6; ++i) {
            static const char *const k[6] = { "CELLS", "PACK", "MEAN",
                                              "SPREAD", "PACK iR", "USED" };
            row(c, 168 + i * 30, k[i], "---",
                ui_theme_color(UI_C_TEXT_FAINT));
        }
        return;
    }

    const float mean = mean_v();
    float pack = 0.0f, ir = 0.0f;
    for (int i = 0; i < s.b.cells; ++i) {
        pack += s.b.volts[i];
        ir   += s.b.milliohms[i];
    }
    const float spread = battery_screen_spread_mv();

    snprintf(buf, sizeof(buf), "%dS", s.b.cells & 0xFF);
    row(c, 168, "CELLS", buf, ink);
    snprintf(buf, sizeof(buf), "%.2f V", (double)pack);
    row(c, 198, "PACK", buf, ink);
    snprintf(buf, sizeof(buf), "%.3f V", (double)mean);
    row(c, 228, "MEAN", buf, ink);
    snprintf(buf, sizeof(buf), "%d mV", (int)(spread + 0.5f));
    row(c, 258, "SPREAD", buf,
        (spread >= SPREAD_BAD_MV)   ? ui_theme_color(UI_C_DANGER)
      : (spread >= SPREAD_WATCH_MV) ? ui_theme_color(UI_C_WARN)
                                    : ui_theme_color(UI_C_OK));
    snprintf(buf, sizeof(buf), "%.1f mOhm", (double)ir);
    row(c, 288, "PACK iR", buf, ink);
    snprintf(buf, sizeof(buf), "%d of %d mAh",
             (int)s.b.drawn_mah, (int)s.b.capacity_mah);
    row(c, 318, "USED", buf, ink);

    gfx_hline(c, x, 350, w, ui_theme_color(UI_C_EDGE));
    gfx_text(c, x, 362, "Spread is measured under", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text(c, x, 380, "load. At rest a tired cell", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text(c, x, 398, "looks like every other one.", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        s.drawn_mask |= bit;
    }
    if (s.drawn[buf] == s.rev) {
        return;
    }
    s.drawn[buf] = s.rev;
    gfx_clear(c, ui_theme_color(UI_C_BG));

    ui_card(c, (gfx_rect_t){ PAD, PAD, LCARD_W, (int16_t)(H - 2 * PAD) },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ RCARD_X, PAD, RCARD_W,
                             (int16_t)(H - 2 * PAD) },
            ui_theme_color(UI_C_PANEL));
    gfx_text(c, PLOT_X, 20, "CELL DIVERGENCE", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    draw_cells(c);
    draw_right(c);
}

static const ui_screen_t k_screen = {
    .title  = "BATTERY",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = NULL,
    .render = render,
};

const ui_screen_t *battery_screen(void) { return &k_screen; }
