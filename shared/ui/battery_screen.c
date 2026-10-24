/**
 * @file
 * @brief The battery screen: each cell's departure from the pack's mean.
 *
 * Six cell voltages that agree to two decimals hide a cell 40mV adrift, so
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

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W /**< The panel's width, px. */
#define H UI_BODY_H  /**< The body's height under the band, px. */

#define PAD     UI_MARGIN /**< The screen's outer margin, px. */
/** @brief The plot card's width, px: 640, the right card 360. */
#define LCARD_W 640
#define RCARD_X (PAD + LCARD_W + 8)            /**< x 656. */
#define RCARD_W (W - RCARD_X - PAD)            /**< 360px. */

/**
 * @brief The lowest row a line of text may reach: 4px above the alert
 *        band, body y 492.
 */
#define TEXT_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The faces of the 1024 x 600 type scale this screen draws in:
 *        BT_FONT_LABEL (10 x 20) for the title, the rows and the note,
 *        BT_FONT_FINE (8 x 16) for the plot's scale, BT_FONT_BODY (12 x 24)
 *        for the verdict's line, the cells row and the empty plot's line,
 *        BT_FONT_HEAD (20 x 36) for the verdict.
 */
#define BT_FONT_LABEL (&gfx_font_10x20)
#define BT_FONT_FINE  UI_FONT_FINE       /**< 8 x 16. */
#define BT_FONT_BODY  UI_FONT_BODY       /**< 12 x 24. */
#define BT_FONT_HEAD  (&gfx_font_20x36)  /**< 20 x 36. */

#define PLOT_X  (PAD + 18)               /**< The plot's left edge: x 26. */
#define PLOT_W  (LCARD_W - 36)           /**< Its width: 604px. */
#define MEAN_Y  232                      /**< The mean's row, px. */
#define REACH   164   /**< Pixels for the full divergence scale. */
#define NUM_Y   (MEAN_Y + REACH + 14)    /**< The cell numbers' row: y 410. */
#define VOLT_Y  (NUM_Y + 30)             /**< The voltages' row: y 440. */
#define BAR_MAX 48                       /**< A bar's widest, px. */
_Static_assert(VOLT_Y + 24 <= TEXT_FLOOR,
               "the cells row ends above the alert band");
_Static_assert(MEAN_Y - REACH - 4 >= 48, "the scale's top is under the title");

/*
 * The right card, top down: the verdict box, its reason in up to two
 * wrapped lines of the 12 x 24 face (27 cells), six rows of 36px and the
 * note in lines of the 10 x 20 face (33 cells).
 */
#define VERDICT_Y 20                     /**< The verdict box's top, px. */
#define VERDICT_H 64                     /**< Its height, px. */
#define WHY_Y     (VERDICT_Y + VERDICT_H + 8)  /**< Its reason: y 92. */
#define WHY_PITCH 26                     /**< A wrapped line of the reason. */
#define WHY_LINES 2                      /**< The lines the reason takes. */
#define RULE1_Y   (WHY_Y + WHY_LINES * WHY_PITCH + 8)  /**< y 152. */
#define ROW_Y     (RULE1_Y + 8)          /**< The first row: y 160. */
#define ROW_PITCH 36                     /**< A row, px. */
#define RULE2_Y   (ROW_Y + 6 * ROW_PITCH + 6)          /**< y 382. */
#define NOTE_Y    (RULE2_Y + 10)         /**< The note: y 392. */
#define NOTE_PITCH 22                    /**< A wrapped line of the note. */
#define NOTE_LINES 4                     /**< The lines the note takes. */
_Static_assert(NOTE_Y + NOTE_LINES * NOTE_PITCH <= TEXT_FLOOR,
               "the note ends above the alert band");
/*
 * The scale floor, in millivolts at full deflection.
 *
 * The range autoscales to the pack: a fixed 100mV scale draws a healthy
 * pack and a pack with one weak cell as the same six near-flat bars.  The
 * floor stops a pack that agrees to 1mV from being drawn as if it were
 * diverging.
 */
#define SCALE_FLOOR_MV 12.0f /**< The scale's floor, mV. */

/* Where a pack stops being fine.  Under load, cells that differ by more than
 * this are a pack with a weak cell, not a pack that needs balancing;
 * balancing hides the weak cell for one more cycle. */
#define SPREAD_WATCH_MV 30.0f /**< WATCH from this spread, mV. */
#define SPREAD_BAD_MV   60.0f /**< REPLACE from this spread, mV. */

static struct {
    battery_state_t b;
    uint32_t rev;
    uint32_t drawn[2];
    unsigned drawn_mask;
} s; /**< The screen's state. */

void battery_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

/** @brief The screen's state cleared: no pack. */
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

/**
 * @brief The pack's mean cell voltage.
 * @return Volts; 0 with no pack.
 */
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

/**
 * @brief Full deflection: the pack's own worst departure with a little
 *        headroom, never less than the floor.
 * @return Millivolts.
 */
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

const gfx_font_t *battery_screen_cells_font(int cells)
{
    if (cells < 1) {
        cells = 1;
    }
    const int slot = PLOT_W / cells;
    /* The widest a voltage prints, 4 cells: "3.81". */
    if (gfx_text_width(BT_FONT_BODY, "0.00", 1) <= slot - 4) {
        return BT_FONT_BODY;
    }
    if (gfx_text_width(BT_FONT_LABEL, "0.00", 1) <= slot - 2) {
        return BT_FONT_LABEL;
    }
    return BT_FONT_FINE;
}

/**
 * @brief The plot card: the mean, the band a pack is fine inside, the
 *        scale, and each cell's bar with its number and voltage under it.
 * @param c The canvas.
 */
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
     * that fills the plot could be 4mV or 40mV. */
    char sc[24];
    snprintf(sc, sizeof(sc), "+%dmV", (int)(full + 0.5f));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(PLOT_X + PLOT_W - 120),
                                 (int16_t)(MEAN_Y - REACH - 4), 120, 16 },
                sc, BT_FONT_FINE, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    snprintf(sc, sizeof(sc), "-%dmV", (int)(full + 0.5f));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(PLOT_X + PLOT_W - 120),
                                 (int16_t)(MEAN_Y + REACH - 12), 120, 16 },
                sc, BT_FONT_FINE, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);

    if (!s.b.valid || s.b.cells <= 0) {
        gfx_text_in(c, (gfx_rect_t){ PAD, (int16_t)(MEAN_Y + 40),
                                     LCARD_W, 24 },
                    TR(BAT_NO_MONITOR), BT_FONT_BODY,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_CENTER);
        return;
    }

    const float mean = mean_v();
    const int slot = PLOT_W / s.b.cells;
    int bw = slot - 12;
    if (bw > BAR_MAX) { bw = BAR_MAX; }
    if (bw < 8)       { bw = 8;  }
    /* The cells row: 12 x 24 while a voltage fits its slot, which it does
     * up to 11 cells; the 10 x 20 face for 12 to 14. */
    const gfx_font_t *const rf = battery_screen_cells_font(s.b.cells);
    const int rh = (rf == BT_FONT_BODY) ? 24 : (rf == BT_FONT_LABEL) ? 20 : 16;

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
                                     (int16_t)NUM_Y, (int16_t)slot,
                                     (int16_t)rh },
                    n, rf, dim, 1, GFX_ALIGN_CENTER);

        char v[16];
        snprintf(v, sizeof(v), "%.2f", (double)s.b.volts[i]);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(cx - slot / 2),
                                     (int16_t)VOLT_Y, (int16_t)slot,
                                     (int16_t)rh },
                    v, rf,
                    bad ? ui_theme_color(UI_C_DANGER)
                        : ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_CENTER);
    }
}

/**
 * @brief The bytes of the next line of @p at that fit @p cells: up to the
 *        last space in it, or the whole width where no space falls after
 *        its start.
 * @param at    The text from the line's start.
 * @param cells The line's width, in cells.
 * @return The line's length, in bytes.
 */
static size_t line_len(const char *at, int cells)
{
    if (gfx_text_cells(at) <= cells) {
        return strlen(at);
    }
    const size_t len = gfx_text_prefix(at, cells);
    size_t cut = len;
    while (cut > 0u && at[cut] != ' ') {
        --cut;
    }
    return (cut > 0u) ? cut : len;
}

/**
 * @brief @p text in lines of face @p f as wide as @p box, broken after a
 *        space, from the box's top down by @p pitch, aligned in the box by
 *        @p align.
 * @param c     The canvas, or NULL to measure only.
 * @param box   The first line's box; its height is not used.
 * @param text  The text.
 * @param f     The face.
 * @param pitch A line under the one before, px.
 * @param ink   The colour.
 * @param align The lines' alignment.
 * @return The y under the last line.
 */
static int wrap_in(gfx_canvas_t *c, gfx_rect_t box, const char *text,
                   const gfx_font_t *f, int pitch, gfx_color_t ink,
                   gfx_align_t align)
{
    const int cells = box.w / gfx_text_width(f, "M", 1);
    const int fh = gfx_text_height(f, 1);
    int y = box.y;
    const char *at = text;
    while (*at != '\0') {
        char line[96];
        const size_t len = line_len(at, cells);
        const size_t n = (len < sizeof(line)) ? len : sizeof(line) - 1u;
        memcpy(line, at, n);
        line[n] = '\0';
        if (c != NULL) {
            gfx_text_in(c, (gfx_rect_t){ box.x, (int16_t)y, box.w,
                                         (int16_t)fh },
                        line, f, ink, 1, align);
        }
        y += pitch;
        at += len;
        while (*at == ' ') {
            ++at;
        }
    }
    return y;
}

/**
 * @brief One reading row of the right card: its label at the left, its
 *        value at the right, centred in a row ROW_PITCH tall.
 * @param c   The canvas.
 * @param i   The row, 0 for the first.
 * @param k   The label.
 * @param v   The value.
 * @param ink The value's colour.
 */
static void row(gfx_canvas_t *c, int i, const char *k, const char *v,
                gfx_color_t ink)
{
    const int x = RCARD_X + 14;
    const int y = ROW_Y + i * ROW_PITCH + (ROW_PITCH - 20) / 2;
    gfx_text(c, x, y, k, BT_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 120), (int16_t)y,
                                 (int16_t)(RCARD_W - 28 - 120), 20 },
                v, BT_FONT_LABEL, ink, 1, GFX_ALIGN_RIGHT);
}

int battery_screen_text_lines(const char *text, bool note)
{
    if (text == NULL) {
        return 0;
    }
    const gfx_rect_t box = { RCARD_X + 14, 0, RCARD_W - 28, 0 };
    return wrap_in(NULL, box, text, note ? BT_FONT_LABEL : BT_FONT_BODY, 1,
                   0, GFX_ALIGN_LEFT);
}

/**
 * @brief The verdict box, its word in the 20 x 36 face, and its reason
 *        under it.
 * @param c The canvas.
 */
static void draw_verdict(gfx_canvas_t *c)
{
    const int x = RCARD_X + 14;
    const int w = RCARD_W - 28;
    const float spread = battery_screen_spread_mv();

    const char *word, *why;
    gfx_color_t tone;
    if (!s.b.valid) {
        word = TR(BAT_NO_PACK); why = TR(BAT_NO_PACK_WHY);
        tone = ui_theme_color(UI_C_TEXT_FAINT);
    } else if (spread >= SPREAD_BAD_MV) {
        word = TR(BAT_REPLACE); why = TR(BAT_REPLACE_WHY);
        tone = ui_theme_color(UI_C_DANGER);
    } else if (spread >= SPREAD_WATCH_MV) {
        word = TR(BAT_WATCH);   why = TR(BAT_WATCH_WHY);
        tone = ui_theme_color(UI_C_WARN);
    } else {
        word = TR(BAT_HEALTHY); why = TR(BAT_HEALTHY_WHY);
        tone = ui_theme_color(UI_C_OK);
    }

    gfx_fill_round_rect(c, x, VERDICT_Y, w, VERDICT_H, UI_R_CARD,
                        gfx_lerp(ui_theme_color(UI_C_PANEL), tone, 40));
    gfx_draw_round_rect(c, x, VERDICT_Y, w, VERDICT_H, UI_R_CARD, tone);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, VERDICT_Y + 14, (int16_t)w, 36 },
                word, BT_FONT_HEAD, tone, 1, GFX_ALIGN_CENTER);
    (void)wrap_in(c, (gfx_rect_t){ (int16_t)x, WHY_Y, (int16_t)w, 0 }, why,
                  BT_FONT_BODY, WHY_PITCH, ui_theme_color(UI_C_TEXT_DIM),
                  GFX_ALIGN_CENTER);
}

/**
 * @brief The right card: the verdict, the six rows and the note; with no
 *        pack the rows read "---" and the note is left out.
 * @param c The canvas.
 */
static void draw_right(gfx_canvas_t *c)
{
    draw_verdict(c);

    const int x = RCARD_X + 14;
    const int w = RCARD_W - 28;
    gfx_hline(c, x, RULE1_Y, w, ui_theme_color(UI_C_EDGE));

    char buf[32];
    const gfx_color_t ink = ui_theme_color(UI_C_TEXT);
    if (!s.b.valid) {
        for (int i = 0; i < 6; ++i) {
            static const ui_text_id_t k[6] = {
                TX_BAT_CELLS, TX_BAT_PACK, TX_BAT_MEAN, TX_BAT_SPREAD,
                TX_BAT_PACK_IR, TX_BAT_USED,
            };
            row(c, i, ui_tr(k[i]), "---",
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
    row(c, 0, TR(BAT_CELLS), buf, ink);
    snprintf(buf, sizeof(buf), "%.2fV", (double)pack);
    row(c, 1, TR(BAT_PACK), buf, ink);
    snprintf(buf, sizeof(buf), "%.3fV", (double)mean);
    row(c, 2, TR(BAT_MEAN), buf, ink);
    snprintf(buf, sizeof(buf), "%dmV", (int)(spread + 0.5f));
    row(c, 3, TR(BAT_SPREAD), buf,
        (spread >= SPREAD_BAD_MV)   ? ui_theme_color(UI_C_DANGER)
      : (spread >= SPREAD_WATCH_MV) ? ui_theme_color(UI_C_WARN)
                                    : ui_theme_color(UI_C_OK));
    snprintf(buf, sizeof(buf), "%.1fmΩ", (double)ir);
    row(c, 4, TR(BAT_PACK_IR), buf, ink);
    snprintf(buf, sizeof(buf), TR(BAT_USED_OF),
             (int)s.b.drawn_mah, (int)s.b.capacity_mah);
    row(c, 5, TR(BAT_USED), buf, ink);

    gfx_hline(c, x, RULE2_Y, w, ui_theme_color(UI_C_EDGE));
    /* The note is two sentences, the second given in two parts; each
     * sentence wraps to the card. */
    char rest[128];
    snprintf(rest, sizeof(rest), "%s %s", TR(BAT_NOTE_2), TR(BAT_NOTE_3));
    const gfx_rect_t note = { (int16_t)x, NOTE_Y, (int16_t)w, 0 };
    const int y = wrap_in(c, note, TR(BAT_NOTE_1), BT_FONT_LABEL, NOTE_PITCH,
                          ui_theme_color(UI_C_TEXT_FAINT), GFX_ALIGN_LEFT);
    (void)wrap_in(c, (gfx_rect_t){ note.x, (int16_t)y, note.w, 0 }, rest,
                  BT_FONT_LABEL, NOTE_PITCH, ui_theme_color(UI_C_TEXT_FAINT),
                  GFX_ALIGN_LEFT);
}

/**
 * @brief The screen drawn on a revision not yet drawn: both cards whole.
 * @param c The canvas.
 * @param buffer_index The framebuffer, whose revision it follows.
 */
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
    gfx_text(c, PLOT_X, 20, TR(BAT_DIVERGENCE), BT_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    draw_cells(c);
    draw_right(c);
}

/** @brief The screen, as the router takes it. */
static const ui_screen_t k_screen = {
    .title  = "BATTERY",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = NULL,
    .render = render,
};

/**
 * @brief The BATTERY screen.
 * @return The screen.
 */
const ui_screen_t *battery_screen(void) { return &k_screen; }
