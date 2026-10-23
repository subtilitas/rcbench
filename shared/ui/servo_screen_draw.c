/**
 * @file servo_screen_draw.c
 * @brief The SERVO screen's drawing.
 *
 * The SERVO screen's drawing: the servo on the dial, the left card, the
 * right card with its readings, plot, set points and controls, and the
 * frame, which repaints what changed.  The state and the seam are
 * servo_screen_int.h and servo_screen_core_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sensors_rows.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "gfx_subpixel.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/**
 * @brief The widths of the three readings on the line over the plot, in px
 *        of 10 px cells.
 *
 * Beside PD MINI 88, 88 and 90, the rest of the column.  Beside an INA3221
 * channel's 12-cell name, "INA 0x41 CH2", 64, 84 and 74, which hold the
 * widest reading of the part's registers: a voltage of 6 cells, `9.99 V`
 * or `65.5 V`, `-32.77 A`, and a power of 7 cells, `-99.9 W` or `-2147 W`.
 */
static const uint8_t k_line_w[2][PS_COUNT] = {
    { 88, 88, 90 },
    { 64, 84, 74 },
};
_Static_assert(PWR_LAB_W + 88 + 88 + 90 == RC_W,
               "PD MINI's line spans the column");
_Static_assert(PWR_LAB_W_INA + 64 + 84 + 74 == RC_W,
               "an INA3221 channel's line spans the column");

static gfx_rect_t sup_row_rect(void)
{
    return (gfx_rect_t){ RC_X, SUP_Y, RC_W, SUP_H };
}

/* ----------------------------------------------------------------- drawing */

/**
 * @brief The point @p r px from the shaft at @p deg, in fractions of a
 *        pixel.
 * @param deg the angle, degrees
 * @param r the distance from the shaft, px
 * @param x the column, px
 * @param y the row, px
 */
static void at_f(float deg, float r, float *x, float *y)
{
    const float k = 3.14159265358979f / 180.0f;
    const float phi = PHI(deg) * k;
    *x = (float)SHAFT_X + r * cosf(phi);
    *y = (float)SHAFT_Y - r * sinf(phi);
}

/**
 * @brief The same on whole pixels: the dial's ticks and labels, which stand
 *        still.
 * @param deg the angle, degrees
 * @param r the distance from the shaft, px
 * @param x the column, px
 * @param y the row, px
 */
static void at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    const float phi = PHI(deg) * k;
    *x = SHAFT_X + (int)((float)r * cosf(phi) + 0.5f);
    *y = SHAFT_Y - (int)((float)r * sinf(phi) + 0.5f);
}

/* The case, its tabs and its boss.  Drawn per framebuffer, not per frame:
 * thing on this card that moves is the horn. */
static void draw_body(gfx_canvas_t *c)
{
    const gfx_color_t shell = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const int bx = SHAFT_X - BODY_W + 24;
    const int by = SHAFT_Y - BODY_H / 2;

    /* Mounting tabs first, so the case overlaps them. */
    for (int i = 0; i < 2; ++i) {
        /* Two holes, and the flange centred on the case: a servo's mounting
         * lugs are symmetric about its centreline. */
        const int tx = (i == 0) ? bx - TAB_W + TAB_OVER
                                : bx + BODY_W - TAB_OVER;
        const int ty = by;
        gfx_fill_round_rect(c, tx, ty, TAB_W, TAB_H, 5, shell);
        gfx_draw_round_rect(c, tx, ty, TAB_W, TAB_H, 5, edge);
        /*
         * Open slots, not drilled holes: a servo's lugs are cut through to
         * the outer edge so it drops into a mount whose screws are already
         * in.
         */
        /* Set in from the flange's own outer edge, so the bore keeps its
         * distance from the case whichever end it is on. */
        const int cxh   = (i == 0) ? tx + TAB_MARGIN + TAB_HOLE_R
                                   : tx + TAB_W - TAB_MARGIN - TAB_HOLE_R;
        const int mouth = (i == 0) ? tx : tx + TAB_W;   /* the open edge */
        /*
         * The bore is darker than both the lug and the card behind it: at
         * these two greys a hole showing the card reads as a smudge, and a
         * recess reads as a hole.
         */
        const gfx_color_t bore = ui_theme_color(UI_C_PANEL_SUNK);
        for (int h = 0; h < 2; ++h) {
            const int hy = by + BODY_H / 2
                           + ((h == 0) ? -TAB_HOLE_DY : TAB_HOLE_DY);
            for (int pass = 0; pass < 2; ++pass) {
                const gfx_color_t ink = (pass == 0) ? edge : bore;
                const int r  = TAB_HOLE_R - pass;
                /* The throat is a fraction of the bore, not a fixed
                 * inset off it: subtracting a couple of pixels from a large
                 * hole leaves a slot with no waist at all. */
                const int nk = TAB_HOLE_R * 3 / 5 - pass;
                gfx_fill_circle_aa(c, cxh, hy, r, ink);
                /* Both passes run flush to the lug's edge, so the mouth is
                 * open rather than capped by its own outline. */
                const int x0 = (mouth < cxh) ? mouth : cxh;
                const int x1 = (mouth < cxh) ? cxh : mouth;
                gfx_fill_rect(c, x0, hy - nk, x1 - x0, 2 * nk, ink);
            }
        }
    }

    gfx_fill_round_rect(c, bx, by, BODY_W, BODY_H, 8, shell);
    gfx_draw_round_rect(c, bx, by, BODY_W, BODY_H, 8, edge);
    gfx_hline(c, bx + 8, by + 1, BODY_W - 16, gfx_lerp(shell, GFX_WHITE, 34));
    /* The band across the case, which is the one place a servo has colour. */
    gfx_fill_rect(c, bx + 26, by + 1, 24, BODY_H - 2,
                  ui_theme_color(UI_C_ACCENT));
    /* The label. */
    gfx_fill_round_rect(c, bx + 74, by + 20, 92, 46, 4,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, bx + 74, by + 20, 92, 46, 4, edge);
    /* The lead, leaving the case at the end away from the output. */
    for (int i = 0; i < 3; ++i) {
        gfx_hline(c, bx - 62, by + 32 + i * 8, 34,
                  ui_theme_color(UI_C_TEXT_FAINT));
    }
}

/*
 * Where a ring of radius @p r has to stop so that a constant band of dark
 * card shows between its end and the arm.
 *
 * A fixed angle does not do it: the arm is the same width at every radius,
 * so it subtends less angle further out, and two rings sharing one angular
 * gap leave the outer one further clear of the arm than the inner.  The
 * outer ring is therefore the longer of the two.
 */
static float ring_gap_deg(float r, float half_w, float dark_px)
{
    const float k = 180.0f / 3.14159265358979f;
    float sine = half_w / r;
    if (sine > 0.99f) {
        sine = 0.99f;
    }
    return (asinf(sine) + dark_px / r) * k;
}

/**
 * @brief The horn at @p deg in @p col: the arm, its holes, its rings and
 *        the boss, placed in fractions of a pixel, so that a horn turning
 *        by a tenth of a degree moves by a sixth of a pixel rather than
 *        standing still and then jumping one.
 * @param c the canvas
 * @param deg the angle, degrees
 * @param col the arm's colour
 */
static void draw_horn(gfx_canvas_t *c, float deg, gfx_color_t col)
{
    float tx, ty;
    at_f(deg, (float)HORN_L, &tx, &ty);
    const float sx = (float)SHAFT_X, sy = (float)SHAFT_Y;

    /*
     * The unlit rings are drawn first and whole, so the arm is drawn over
     * them and they pass behind it.
     */
    const gfx_color_t ghost =
        gfx_lerp(ui_theme_color(UI_C_PANEL), col, 46);
    gfx_ring_aaf(c, tx, ty, 25.0f, 3.0f, 0.0f, 360.0f, ghost, ghost, 0.0f);
    gfx_ring_aaf(c, tx, ty, 33.0f, 2.0f, 0.0f, 360.0f, ghost, ghost, 0.0f);

    gfx_capsule_aaf(c, sx, sy, tx, ty, 26.0f, col);
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 25, col);

    /* Holes along the arm, spaced from the tip inwards so the outermost stays
     * on the arm whatever HORN_L is; a hole drawn past the tip reads as a
     * bite out of the card. */
    for (int i = 1; i <= 3; ++i) {
        float hx, hy;
        at_f(deg, (float)(HORN_L - 12 - (3 - i) * 14), &hx, &hy);
        gfx_fill_circle_aaf(c, hx, hy, 5.0f, ui_theme_color(UI_C_PANEL));
    }
    /* And the outermost one, which is the hole the ring is pointing at. */
    gfx_fill_circle_aaf(c, tx, ty, 5.0f, ui_theme_color(UI_C_PANEL));

    /*
     * The lit part of each ring, over the top.  It is split on the side the
     * arm comes in from, with a constant 9 px band of card either side of
     * the metal (ring_gap_deg), and each end fades into the unlit ring
     * beneath.
     *
     * The rings breathe while the output is being held: the on-picture sign
     * that the servo is under command.
     */
    const float breath = s.driving
                             ? 0.42f + 0.58f * (0.5f + 0.5f * sinf(s.pulse))
                             : 0.62f;
    const gfx_color_t grip =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(150.0f + 100.0f * breath));
    const gfx_color_t faint =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(90.0f + 90.0f * breath));

    const float half_w = 13.0f;   /* the arm's half width, as drawn */
    const float dark   = 9.0f;    /* the band of card between ring and arm */
    const float g_in   = ring_gap_deg(25.0f, half_w, dark);
    const float g_out  = ring_gap_deg(33.0f, half_w, dark);
    gfx_ring_aaf(c, tx, ty, 25.0f, 3.0f, deg + 180.0f + g_in,
                 deg + 540.0f - g_in, grip, ghost, 78.0f);
    gfx_ring_aaf(c, tx, ty, 33.0f, 2.0f, deg + 180.0f + g_out,
                 deg + 540.0f - g_out, faint, ghost, 78.0f);

    /* The boss and its splines. */
    /* An outline is two discs, not a circle walked round: an arc closed on
     * itself lays some pixels twice and misses others, and at two pixels wide
     * that stipples. */
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 17, gfx_lerp(col, GFX_BLACK, 60));
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 15,
                       ui_theme_color(UI_C_PANEL_SUNK));
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 6, col);
}

static void draw_dial(gfx_canvas_t *c)
{
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_FAINT);

    /* The travel the settings allow, over the travel the servo has. */
    gfx_arc(c, SHAFT_X, SHAFT_Y, ARC_R, 2, PHI(90.0f), PHI(-90.0f),
            ui_theme_color(UI_C_GRID));
    gfx_arc(c, SHAFT_X, SHAFT_Y, ARC_R, 4, PHI(s.travel_deg),
            PHI(-s.travel_deg), ui_theme_color(UI_C_EDGE_HI));

    for (int i = -2; i <= 2; ++i) {
        const float a = (float)i * 45.0f;
        int x0, y0, x1, y1;
        at(a, ARC_R - 10, &x0, &y0);
        at(a, ARC_R + 8, &x1, &y1);
        gfx_thick_line(c, x0, y0, x1, y1, (i == 0) ? 3 : 2,
                       (i == 0) ? ui_theme_color(UI_C_TEXT_DIM) : dim);

        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%+d", (int)a);
        int lx, ly;
        at(a, ARC_R + 26, &lx, &ly);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(lx - 24), (int16_t)(ly - 10),
                                     48, 20 },
                    lbl, SV_FONT_LABEL, dim, 1, GFX_ALIGN_CENTER);
    }
}

/*
 * The double space a line breaks at first is the gap between two fields.
 * A word longer than a line stays whole.
 */
int wrap_text(gfx_canvas_t *c, int x, int y, int pitch,
              const char *text, int cells, gfx_color_t ink,
              const gfx_font_t *f)
{
    int lines = 0;
    const char *rest = text;
    while (*rest != '\0') {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", rest);
        size_t len = strlen(buf);
        if (gfx_text_cells(buf) > cells) {
            size_t brk = 0u, dbl = 0u;
            for (size_t i = 1u; i < len; ++i) {
                if (buf[i] != ' ' || buf[i - 1u] == ' ') {
                    continue;
                }
                buf[i] = '\0';
                const bool fits = gfx_text_cells(buf) <= cells;
                buf[i] = ' ';
                if (!fits) {
                    break;
                }
                brk = i;
                if (buf[i + 1u] == ' ') {
                    dbl = i;
                }
            }
            if (dbl > 0u) {
                brk = dbl;
            }
            if (brk == 0u) {
                const char *sp = strchr(buf, ' ');
                brk = (sp != NULL) ? (size_t)(sp - buf) : len;
            }
            buf[brk] = '\0';
            len = brk;
        }
        if (c != NULL) {
            gfx_text(c, x, y + lines * pitch, buf, f, ink, 1);
        }
        ++lines;
        rest += len;
        while (*rest == ' ') {
            ++rest;
        }
    }
    return lines;
}

static void draw_readings(gfx_canvas_t *c);

/**
 * @brief Whether the commanded horn is drawn behind the one shown: while
 *        the two are more than a degree apart.
 * @return true when it is
 */
static bool ghost_shown(void)
{
    return fabsf(s.shown_deg - s.commanded_deg) > 1.0f;
}

/**
 * @brief LIVE's card: the dial, the case, the horn, the horn's angle, why
 *        the dial takes no press, the three readings and a run's box.
 * @param c the canvas
 */
static void draw_left(gfx_canvas_t *c)
{
    /* Everything that moves lives in this rectangle, so this is what gets
     * cleared -- the case and the dial below it are chrome. */
    gfx_fill_rect(c, PAD + 1, CARD_Y + 1, LCARD_W - 2, CARD_H - 2,
                  ui_theme_color(UI_C_PANEL));
    draw_dial(c);
    /* The case first: the horn bolts to the top of it and must be drawn over
     * it, not under. */
    draw_body(c);

    /* The commanded position behind the measured one, so a lag is visible as
     * two arms rather than as a number that disagrees with a picture. */
    if (ghost_shown()) {
        draw_horn(c, s.commanded_deg,
                  gfx_lerp(ui_theme_color(UI_C_PANEL),
                           ui_theme_color(UI_C_ACCENT), 70));
    }
    /* Dimmed while the bench is disarmed, as a button that takes no press
     * is: the dial takes none either. */
    draw_horn(c, s.shown_deg,
              value_live() ? ui_theme_color(UI_C_ACCENT)
                      : gfx_lerp(ui_theme_color(UI_C_PANEL),
                                 ui_theme_color(UI_C_ACCENT), 120));

    /* The horn's angle as drawn, top left, in the bench's numerals. */
    char deg[16];
    snprintf(deg, sizeof(deg), "%+.1f", (double)s.shown_deg);
    const gfx_seg_style_t seg = ui_seg_hero();
    const int dw = gfx_seg_width(deg, &seg);
    gfx_seg_text(c, SEG_X, SEG_Y, deg, &seg,
                 ui_theme_color(UI_C_ACCENT),
                 gfx_lerp(ui_theme_color(UI_C_PANEL),
                          ui_theme_color(UI_C_ACCENT), 30));
    gfx_text(c, SEG_X + 6 + dw, SEG_Y + 12, "DEG",
             SV_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    /* Why the dial takes no press, in the words START TEST's line uses. */
    if (!value_live()) {
        gfx_text(c, SEG_X, ARM_NOTE_Y,
                 ui_servo_str(SERVO_STR_START_NOT_ARMED), SV_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
    draw_readings(c);
    if (s.test_box) {
        draw_test_box(c);
    } else if (s.lim_box) {
        draw_lim_box(c);
    }
}

/*
 * The same fill as MOTOR & ESC's ARM, from the same widget: armed is the
 * danger red the press fades towards, and the flash is the whole button.
 */
static gfx_color_t arm_fill(void)
{
    gfx_color_t fill = s.gate.armed ? ui_theme_color(UI_C_DANGER)
                               : ui_theme_color(UI_C_OK);
    if (s.arm.flash_ms > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), s.arm.flash_ms);
    }
    if (!s.gate.armed && s.arm.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER), s.arm.held_s);
    }
    return fill;
}

/**
 * @brief ARM, or DISARM while armed, across the column.
 * @param c the canvas
 */
static void draw_arm(gfx_canvas_t *c)
{
    ui_button_font(c, s.arm_btn, s.gate.armed ? "DISARM" : "ARM", arm_fill(),
                   s.arm_down, !svs_arm_locked(), SV_FONT_LABEL);
}

static gfx_rect_t power_rect(void)
{
    return (gfx_rect_t){ RC_X, (int16_t)(PWR_TXT_Y - 2), RC_W,
                         (int16_t)(PWR_Y + PWR_H - PWR_TXT_Y + 2) };
}

/**
 * @brief @p q, a signed count of 10^-@p places of @p unit, as text: the
 *        sign kept, and a reading that rounds to nothing drawn without
 *        one.  An empty @p unit leaves the number alone.
 * @param out the text
 * @param n its size
 * @param q the count
 * @param places 1 or 2
 * @param unit the unit after a space, or ""
 */
static void fixed_text(char *out, size_t n, int32_t q, int places,
                       const char *unit)
{
    const int32_t  scale = (places == 2) ? 100 : 10;
    const uint32_t mag   = (q < 0) ? (uint32_t)(-(int64_t)q) : (uint32_t)q;
    snprintf(out, n, "%s%u.%0*u%s%s", (q < 0) ? "-" : "",
             (unsigned)(mag / (uint32_t)scale), places,
             (unsigned)(mag % (uint32_t)scale), (unit[0] != '\0') ? " " : "",
             unit);
}

/* The servo rail's voltage, current and power, read and plotted, under the
 * name of its meter: the supply, or the INA3221 channel that watches the
 * tester's target, "INA 0x41 CH2", while that is the meter. */
static void draw_power(gfx_canvas_t *c)
{
    const gfx_rect_t r = power_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    const bool ina = (s.source == SERVO_SOURCE_INA3221);
    /* The supply is the PD mini, named so in both languages, also while
     * the panel's model stands in for it; the model carries the pill
     * below. */
    char src[16];
    (void)sensors_rows_short(src, sizeof(src));
    gfx_text(c, RC_X, PWR_TXT_Y, ina ? src : "PD MINI",
             SV_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    const rail_t m = svs_rail_now();
    char v[20], a[20], w[20];
    const uint8_t *fw = k_line_w[ina ? 1 : 0];
    /* Hundredths of a volt, and tenths where those do not fit. */
    if (m.v_ok) {
        snprintf(v, sizeof(v), "%.2f V", (double)m.v);
        if (gfx_text_width(SV_FONT_LABEL, v, 1) > fw[PS_V]) {
            snprintf(v, sizeof(v), "%.1f V", (double)m.v);
        }
    } else {
        snprintf(v, sizeof(v), "-- V");
    }
    if (m.i_ok) { fixed_text(a, sizeof(a), m.q_a, 2, "A"); }
    else        { snprintf(a, sizeof(a), "-- A"); }
    if (m.v_ok && m.i_ok) {
        /* Tenths of a watt, and whole watts where those do not fit. */
        const float p = isfinite(m.p) ? m.p : 0.0f;
        fixed_text(w, sizeof(w), svs_fixed_of(p, 10.0f), 1, "W");
        if (gfx_text_width(SV_FONT_LABEL, w, 1) > fw[PS_W]) {
            snprintf(w, sizeof(w), "%ld W", (long)svs_fixed_of(p, 1.0f));
        }
    } else {
        snprintf(w, sizeof(w), "-- W");
    }
    /* Each in its trace's colour, so the numbers name the lines; a current
     * from a window with a sample at an end of the range in the warning
     * colour. */
    const gfx_color_t col[PS_COUNT] = { ui_theme_color(UI_C_VOLT),
                                        ui_theme_color(UI_C_CURR),
                                        ui_theme_color(UI_C_POWER) };
    const char *txt[PS_COUNT] = { v, a, w };
    int x = RC_X + (ina ? PWR_LAB_W_INA : PWR_LAB_W);
    for (int k = 0; k < PS_COUNT; ++k) {
        s.power.series[k].color = col[k];
        const gfx_color_t tc = (k == PS_A && m.clipped)
                                   ? ui_theme_color(UI_C_WARN) : col[k];
        gfx_text_in(c, (gfx_rect_t){ (int16_t)x, PWR_TXT_Y, fw[k], 20 },
                    txt[k], SV_FONT_LABEL, tc, 1, GFX_ALIGN_RIGHT);
        x += fw[k];
    }
    ui_plot_render(&s.power, c, (gfx_rect_t){ RC_X, PWR_Y, RC_W, PWR_H });
    /*
     * The readings of the panel's model are no measurement: the overview's
     * pill, 110 px or as wide as a longer word and 26 px high, on the
     * plot's top-left corner under the name, as the line beside the name
     * is full.
     */
    if (!ina && supply_screen_model()) {
        int mw = 30 + gfx_text_width(SV_FONT_LABEL, TR(SV_MODELLED), 1);
        mw = (mw < 110) ? 110 : mw;
        ui_pill_font(c, (gfx_rect_t){ RC_X + 2, PWR_Y + 2, (int16_t)mw, 26 },
                     TR(SV_MODELLED), ui_theme_color(UI_C_WARN),
                     ui_theme_color(UI_C_PANEL_SUNK), 30, SV_FONT_LABEL);
    }
}

/**
 * @brief A reading's numerals at (@p x, @p y), left aligned in @p w px:
 *        in SV_FONT_NUM, or where they are wider than that -- a reading
 *        held at +-1000000.00 A is 12 cells -- in SV_FONT_HEAD, or in
 *        SV_FONT_LABEL.
 * @param c the canvas
 * @param x the column's left edge, px
 * @param y the numerals' top, px
 * @param w the column's width, px
 * @param t the numerals
 * @param col their colour
 */
static void hero_text(gfx_canvas_t *c, int x, int y, int w, const char *t,
                      gfx_color_t col)
{
    const gfx_font_t *f = SV_FONT_NUM;
    if (gfx_text_width(f, t, 1) > w) {
        f = SV_FONT_HEAD;
    }
    if (gfx_text_width(f, t, 1) > w) {
        f = SV_FONT_LABEL;
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)w,
                                 READ_NUM_H },
                t, f, col, 1, GFX_ALIGN_LEFT);
}

/**
 * @brief The CURRENT reading's numerals on LIVE's card.
 * @return the box draw_cur_value() repaints
 */
static gfx_rect_t cur_val_rect(void)
{
    return (gfx_rect_t){ (int16_t)READ_X(2), READ_NUM_Y,
                         (int16_t)(READ_W - 4), READ_NUM_H };
}

/**
 * @brief The rail's current as its meter reads it, signed, in hundredths
 *        of an amp; in the warning colour from a clipped window.
 * @param c the canvas
 */
static void cur_value(gfx_canvas_t *c)
{
    const rail_t m = svs_rail_now();
    char buf[20];
    if (m.i_ok) { fixed_text(buf, sizeof(buf), m.q_a, 2, ""); }
    else        { snprintf(buf, sizeof(buf), "---"); }
    const gfx_rect_t r = cur_val_rect();
    hero_text(c, r.x, r.y, r.w, buf,
              m.clipped ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT));
}

/* The same over its own background: the row's repaint between two repaints
 * of the card. */
static void draw_cur_value(gfx_canvas_t *c)
{
    const gfx_rect_t r = cur_val_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    cur_value(c);
}

/**
 * @brief LIVE's three readings along the bottom of the card: COMMANDED
 *        in microseconds, MEASURED in degrees and CURRENT in amps, each
 *        its label and unit over its numerals.
 *
 * MEASURED is the horn's angle from the encoder where one reads; with the
 * encoder off in SETUP, from the feedback; with it on and not reading, the
 * dashes: the pulse position is not the horn's angle then.
 * @param c the canvas
 */
static void draw_readings(gfx_canvas_t *c)
{
    static const ui_text_id_t k_label[3] = { TX_SV_COMMANDED,
                                             TX_SV_MEASURED, TX_SV_CURRENT };
    static const char *const k_unit[3] = { "us", "deg", "A" };
    for (int k = 0; k < 3; ++k) {
        const gfx_rect_t lr = { (int16_t)READ_X(k), READ_LBL_Y,
                                (int16_t)(READ_W - 14), 20 };
        gfx_text(c, lr.x, lr.y, ui_tr(k_label[k]), SV_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text_in(c, lr, k_unit[k], SV_FONT_LABEL,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_RIGHT);
    }
    /* The pulse without its unit, which the label row carries. */
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "%s", servo_screen_commanded_text());
    char *sp = strchr(cmd, ' ');
    if (sp != NULL) {
        *sp = '\0';
    }
    hero_text(c, READ_X(0), READ_NUM_Y, READ_W - 4, cmd,
              ui_theme_color(UI_C_TEXT));
    char mea[16];
    if (enc_shown()) {
        snprintf(mea, sizeof(mea), "%+.1f", (double)enc_deg_now());
    } else if (s.have_feedback && !settings_get_bool(SET_ENC_EN)) {
        snprintf(mea, sizeof(mea), "%+.1f", (double)s.measured_deg);
    } else {
        snprintf(mea, sizeof(mea), "---");
    }
    hero_text(c, READ_X(1), READ_NUM_Y, READ_W - 4, mea,
              ui_theme_color(UI_C_ACCENT));
    cur_value(c);
}

/* OUTPUT ON and OFF in SUPPLY's colours: off is the green the hold fades
 * from, on the danger red it fades to. */
static gfx_color_t out_fill(void)
{
    if (s.out_hold.flash_ms > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER),
                             s.out_hold.flash_ms);
    }
    gfx_color_t fill = s.out_on ? ui_theme_color(UI_C_DANGER)
                                : ui_theme_color(UI_C_OK);
    if (!s.out_on && s.out_hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER),
                            s.out_hold.held_s);
    }
    return fill;
}

/* SET, the two set points, and the output switch. */
static void draw_sup_row(gfx_canvas_t *c)
{
    const gfx_rect_t r = sup_row_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    gfx_text(c, RC_X, SUP_Y + 14, TR(SV_SET), SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    char v[12], a[12];
    snprintf(v, sizeof(v), "%.2f V", (double)supply_screen_set_v());
    snprintf(a, sizeof(a), "%.2f A", (double)supply_screen_set_i());
    /* Held by a programming port they take no press, and say so. */
    const bool held = supply_screen_port_held();
    const struct { const char *txt; gfx_color_t col; int op; } k[2] = {
        { v, held ? ui_theme_color(UI_C_TEXT_FAINT)
                  : ui_theme_color(UI_C_VOLT), OP_SUP_V },
        { a, held ? ui_theme_color(UI_C_TEXT_FAINT)
                  : ui_theme_color(UI_C_CURR), OP_SUP_I },
    };
    for (int i = 0; i < 2; ++i) {
        const gfx_rect_t b = sup_val_rect(i);
        const bool down = s.ov_have && s.ov_pressed == k[i].op;
        gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                            down ? ui_theme_color(UI_C_PANEL_HI)
                                 : ui_theme_color(UI_C_PANEL_SUNK));
        gfx_text_in(c, b, k[i].txt, SV_FONT_LABEL, k[i].col, 1,
                    GFX_ALIGN_CENTER);
    }
    ui_button_font(c, s.out_btn,
                   s.out_on ? TR(SUP_OUTPUT_OFF) : TR(SUP_OUTPUT_ON),
                   out_fill(), s.out_down, s.out_on || !held, SV_FONT_LABEL);
}

static gfx_rect_t tag_rect(void)
{
    return (gfx_rect_t){ RC_X, TAG_Y, RC_W, TAG_H };
}

/**
 * @brief The profile in force, its frame rate and its pulse range in
 *        microseconds, in the danger colour when it is one that can
 *        destroy a servo not made for it.
 *
 * The longest, a 13-character type, 560 Hz and two 4-digit ends, is 32
 * of the pill's 35 cells.
 * @param c the canvas
 */
static void draw_tag(gfx_canvas_t *c)
{
    const gfx_rect_t r = tag_rect();
    const bool danger = in_force_dangerous();
    const gfx_color_t fill = danger ? ui_theme_color(UI_C_DANGER)
                                    : ui_theme_color(UI_C_PANEL_SUNK);
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL, fill);
    char line[40];
    snprintf(line, sizeof(line), "%s  %u Hz  %u-%u", type()->name,
             (unsigned)s.frame_hz, (unsigned)s.min_us, (unsigned)s.max_us);
    gfx_text_in(c, r, line, SV_FONT_LABEL,
                danger ? GFX_WHITE : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
}

/**
 * @brief The output card.  The power plot only when @p power: a repaint
 *        for anything else leaves the plot, which its own samples repaint,
 *        clipped.
 * @param c the canvas
 * @param power whether the plot is drawn too
 */
static void draw_right(gfx_canvas_t *c, bool power)
{
    const gfx_rect_t pr = power_rect();
    if (power) {
        gfx_fill_rect(c, RCARD_X + 1, RCARD_Y + 1, RCARD_W - 2, RCARD_H - 2,
                      ui_theme_color(UI_C_PANEL));
    } else {
        /* Around the plot, which keeps what it shows. */
        gfx_fill_rect(c, RCARD_X + 1, RCARD_Y + 1, RCARD_W - 2,
                      pr.y - RCARD_Y - 1, ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, RCARD_X + 1, pr.y + pr.h, RCARD_W - 2,
                      H - PAD - 1 - (pr.y + pr.h), ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, RCARD_X + 1, pr.y, pr.x - RCARD_X - 1, pr.h,
                      ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, pr.x + pr.w, pr.y, RCARD_X + RCARD_W - 1 - (pr.x + pr.w),
                      pr.h, ui_theme_color(UI_C_PANEL));
    }

    draw_tag(c);
    if (power) {
        draw_power(c);
    }
    draw_sup_row(c);

    /* SPEED's row says when SPEED, not CURVE, decides how a sweep moves:
     * the curves then look alike. */
    char buf[24];
    snprintf(buf, sizeof(buf), "%d %%", s.speed_pct);
    const bool limits = svs_speed_limits_sweep();
    gfx_text(c, RC_X, SPEED_Y, limits ? TR(SV_SPEED_LIMITS) : TR(SV_SPEED),
             SV_FONT_LABEL, limits ? ui_theme_color(UI_C_WARN)
                                   : ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(c, (gfx_rect_t){ RC_X, SPEED_Y, RC_W, 20 }, buf,
                SV_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
    s.speed.color = ui_theme_color(UI_C_ACCENT);
    ui_slider_render(&s.speed, c);

    draw_target(c);

    /* CENTRE sets a position, so it is dimmed with SWEEP while the bench
     * is disarmed. */
    ui_button_font(c, s.centre_btn, TR(SV_CENTRE_BTN),
                   ui_theme_color(UI_C_ACCENT), false, value_live(),
                   SV_FONT_LABEL);
    /* PAUSE while a sweep runs, in the accent; PAUSED (PAUSIERT) while it
     * is paused, filled in the warning colour, so the two read apart by
     * fill as well as by the word. */
    const gfx_color_t sweep_fill = s.sweeping ? ui_theme_color(UI_C_ACCENT)
                                   : s.paused ? ui_theme_color(UI_C_WARN)
                                              : ui_theme_color(UI_C_PANEL_HI);
    ui_button_font(c, s.sweep_btn,
                   s.paused     ? TR(SV_PAUSED)
                   : s.sweeping ? TR(SV_PAUSE) : "SWEEP",
                   sweep_fill, false,
                   s.sweeping || s.paused
                       || (value_live() && s.sweep_able && s.surfaces
                           && s.link_up),
                   SV_FONT_LABEL);
    ui_button_font(c, s.release_btn, TR(SV_RELEASE),
                   ui_theme_color(UI_C_PANEL_HI), false, true, SV_FONT_LABEL);
    draw_arm(c);
}

/* Draw @p fn clipped to @p box. */
static void clipped(gfx_canvas_t *c, gfx_rect_t box, void (*fn)(gfx_canvas_t *))
{
    gfx_rect_t old_clip = c->clip;
    if (gfx_clip_set(c, box)) {
        fn(c);
    }
    c->clip = old_clip;
}

/* The same for a session, drawn through its own function (PROG_CALLS). */
static void clipped_session(gfx_canvas_t *c, gfx_rect_t box,
                            const prog_page_set_t *ps)
{
    gfx_rect_t old_clip = c->clip;
    if (gfx_clip_set(c, box)) {
        prog_ps_render(ps, c);
    }
    c->clip = old_clip;
}

/* ---------------------------------------------------------- the dial's part */

/** @brief How far a horn reaches from its tip, px: the outer ring's 33 px
 *         radius, its 1 px half width, its blended edge and a pixel more. */
#define HORN_TIP_REACH   36.0f
/** @brief And from the shaft: the boss's 25 px disc, its edge and a pixel. */
#define HORN_SHAFT_REACH 27.0f

/**
 * @brief The smallest rectangle holding @p a and @p b, neither empty.
 * @param a one rectangle
 * @param b the other
 * @return the rectangle holding both
 */
static gfx_rect_t rect_union(gfx_rect_t a, gfx_rect_t b)
{
    const int x0 = (a.x < b.x) ? a.x : b.x;
    const int y0 = (a.y < b.y) ? a.y : b.y;
    const int x1 = (a.x + a.w > b.x + b.w) ? a.x + a.w : b.x + b.w;
    const int y1 = (a.y + a.h > b.y + b.h) ? a.y + a.h : b.y + b.h;
    return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0),
                         (int16_t)(y1 - y0) };
}

/**
 * @brief Every pixel a horn at @p deg draws: the shaft's disc and the tip's
 *        rings, which hold the arm between them.
 * @param deg the horn's angle, degrees
 * @return the box, whole pixels
 */
static gfx_rect_t horn_box(float deg)
{
    float tx, ty;
    at_f(deg, (float)HORN_L, &tx, &ty);
    const float sx = (float)SHAFT_X, sy = (float)SHAFT_Y;
    const int x0 = (int)floorf(fminf(sx - HORN_SHAFT_REACH,
                                     tx - HORN_TIP_REACH));
    const int y0 = (int)floorf(fminf(sy - HORN_SHAFT_REACH,
                                     ty - HORN_TIP_REACH));
    const int x1 = (int)ceilf(fmaxf(sx + HORN_SHAFT_REACH,
                                    tx + HORN_TIP_REACH));
    const int y1 = (int)ceilf(fmaxf(sy + HORN_SHAFT_REACH,
                                    ty + HORN_TIP_REACH));
    return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0),
                         (int16_t)(y1 - y0) };
}

/**
 * @brief The horn's angle in the bench's numerals and DEG after them, at
 *        their widest, -888.8.
 * @return the box draw_left() draws them in
 */
static gfx_rect_t angle_box(void)
{
    const gfx_seg_style_t seg = ui_seg_hero();
    const int w = gfx_seg_width("-888.8", &seg) + 6
                  + gfx_text_width(SV_FONT_LABEL, "DEG", 1);
    return (gfx_rect_t){ (int16_t)(SEG_X - 2), (int16_t)(SEG_Y - 2),
                         (int16_t)(w + 4), (int16_t)(seg.digit_h + 8) };
}

/**
 * @brief Whether the dial's changes are repainted on their own: LIVE's
 *        card shown with nothing over it.  A box of a run or a limit run
 *        over the card, or any other page, repaints whole for them.
 * @param cover a panel or a session is over the left column
 * @return true when they are
 */
static bool dial_on_its_own(bool cover)
{
    return !cover && wb_task() == UI_WB_LIVE && rows_page() < 0
           && !s.test_box && !s.lim_box;
}

/**
 * @brief Note what buffer @p buf now shows of the dial.
 * @param buf the buffer, 0 or 1
 */
static void dial_drawn(int buf)
{
    s.drawn_dial[buf]  = s.dial_rev;
    s.drawn_deg[buf]   = s.shown_deg;
    s.drawn_cmd[buf]   = s.commanded_deg;
    s.drawn_ghost[buf] = ghost_shown();
}

/**
 * @brief The dial's change repainted in buffer @p buf: LIVE's card, clipped
 *        to the boxes of the horns as this buffer last drew them and as
 *        they are now, to the angle's numerals and to COMMANDED and
 *        MEASURED.  Each buffer keeps its own last angles, so both end up
 *        with the horn where it is, whichever frames each was drawn in.
 * @param c the canvas
 * @param buf the buffer, 0 or 1
 */
static void draw_dial_part(gfx_canvas_t *c, int buf)
{
    gfx_rect_t box = rect_union(horn_box(s.drawn_deg[buf]),
                                horn_box(s.shown_deg));
    if (s.drawn_ghost[buf]) {
        box = rect_union(box, horn_box(s.drawn_cmd[buf]));
    }
    if (ghost_shown()) {
        box = rect_union(box, horn_box(s.commanded_deg));
    }
    clipped(c, box, draw_left);
    clipped(c, angle_box(), draw_left);
    clipped(c, (gfx_rect_t){ (int16_t)READ_X(0), READ_NUM_Y,
                             (int16_t)(2 * READ_W), READ_NUM_H },
            draw_left);
    dial_drawn(buf);
    /* The grip is in the horn's box, at the breath it has now. */
    s.drawn_pulse[buf] = (int)(s.pulse * 8.0f);
}

void svs_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;
    /* A panel or a session over the left column, the strip's tabs with
     * it: one that opens or goes repaints the buffer whole. */
    const prog_page_set_t *ps = prog_shown_set();
    const bool cover = modal_open() || ps != NULL;
    if (cover != s.drawn_cover[buf]) {
        s.drawn_mask &= ~bit;
    }

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_card(c, page_area(), ui_theme_color(UI_C_PANEL));
        ui_card(c, (gfx_rect_t){ RCARD_X, RCARD_Y, RCARD_W, RCARD_H },
                ui_theme_color(UI_C_PANEL));
        if (!cover) {               /* under a cover, the cover's */
            draw_strip(c);
        }
        s.drawn_mask |= bit;
        s.drawn_cover[buf] = cover;
        s.drawn_strip[buf] = s.strip_rev;
        s.drawn_ctrl[buf]  = UINT32_MAX;
        s.drawn_power[buf] = UINT32_MAX;
    }
    if (!cover && s.drawn_strip[buf] != s.strip_rev) {
        s.drawn_strip[buf] = s.strip_rev;
        clipped(c, strip_area(), draw_strip);
    }

    /* The keypad and the keyboard keep their own counts of what changed;
     * the dial too, which repaints whole only where it is not on its own. */
    const uint32_t ov_rev = s.ctrl_rev + s.kp.revision + s.tk.revision;
    const bool dial_whole = !dial_on_its_own(cover)
                            && s.drawn_dial[buf] != s.dial_rev;
    if (s.drawn_ctrl[buf] != ov_rev || dial_whole) {
        /* The plot when it moved or this buffer has never had it. */
        const bool power = (s.drawn_power[buf] != s.power_rev);
        s.drawn_ctrl[buf]  = ov_rev;
        s.drawn_arm[buf]   = s.arm_rev;
        s.drawn_sup[buf]   = s.sup_rev;
        s.drawn_power[buf] = s.power_rev;
        s.drawn_cur[buf]   = s.cur_rev;
        s.drawn_warn[buf]  = s.warn.rev;
        s.drawn_ask[buf]   = s.ask.rev;
        s.drawn_test[buf]  = s.test_rev;
        s.drawn_save[buf]  = save_state();
        s.drawn_pulse[buf] = (int)(s.pulse * 8.0f);
        s.drawn_kst[buf]   = (ps != NULL) ? prog_ps_revision(ps) : 0u;
        s.drawn_kst_hold[buf] = (ps != NULL) ? prog_ps_hold_revision(ps) : 0u;
        dial_drawn(buf);
        if (modal_open()) {
            draw_overlay(c);
        } else if (ps != NULL) {
            prog_ps_render(ps, c);
        } else if (wb_task() == UI_WB_LIVE) {
            draw_left(c);
        } else if (rows_page() >= 0) {
            draw_rows_page(c);
        } else if (wb_task() == UI_WB_PROGRAM) {
            draw_prog_page(c);
        } else {
            draw_test_page(c);
        }
        draw_right(c, power);
        return;
    }

    /*
     * Otherwise only what moved on its own, each clipped to itself: ARM's
     * fade, the warning's hold, the power plot, the CURRENT row, the save
     * line, the START holds and the grip.  The right card is 292 x 388 and
     * the fades run for two seconds: asking for whole cards every frame
     * would spend most of the panel's bandwidth on a button.
     */
    if (s.drawn_arm[buf] != s.arm_rev) {
        s.drawn_arm[buf] = s.arm_rev;
        clipped(c, s.arm_btn, draw_arm);
    }
    if (s.drawn_power[buf] != s.power_rev) {
        s.drawn_power[buf] = s.power_rev;
        clipped(c, power_rect(), draw_power);
    }
    if (s.drawn_sup[buf] != s.sup_rev) {
        s.drawn_sup[buf] = s.sup_rev;
        clipped(c, sup_row_rect(), draw_sup_row);
    }
    if (modal_open()) {
        if (s.warn.open && s.drawn_warn[buf] != s.warn.rev) {
            s.drawn_warn[buf] = s.warn.rev;
            clipped(c, warn_apply_rect(), draw_warn_apply);
        }
        if (s.ask.open && s.ask.hv && s.drawn_ask[buf] != s.ask.rev) {
            s.drawn_ask[buf] = s.ask.rev;
            clipped(c, warn_apply_rect(), draw_ask_apply);
        }
        return;
    }
    if (ps != NULL) {
        /* The session, when it changed; of a hold only its button. */
        if (s.drawn_kst[buf] != prog_ps_revision(ps)) {
            s.drawn_kst[buf] = prog_ps_revision(ps);
            s.drawn_kst_hold[buf] = prog_ps_hold_revision(ps);
            clipped_session(c, overlay_area(), ps);
        } else if (s.drawn_kst_hold[buf] != prog_ps_hold_revision(ps)) {
            s.drawn_kst_hold[buf] = prog_ps_hold_revision(ps);
            clipped_session(c, prog_ps_hold_rect(ps), ps);
        }
        return;
    }
    if (rows_page() >= 0) {
        if (s.drawn_save[buf] != save_state()) {
            s.drawn_save[buf] = save_state();
            clipped(c, save_line_rect(), draw_save_line);
        }
        return;
    }
    if (wb_task() == UI_WB_TEST) {
        /* START TEST's and START LIMIT RUN's fades. */
        if (s.drawn_test[buf] != s.test_rev) {
            s.drawn_test[buf] = s.test_rev;
            clipped(c, test_start_rect(), draw_test_start);
            clipped(c, lim_start_rect(), draw_lim_start);
        }
        return;
    }

    /* PROGRAM's page with no session open: none of LIVE's parts are on it. */
    if (wb_task() != UI_WB_LIVE) {
        return;
    }

    /* LIVE: the CURRENT reading when it changed, on its own. */
    if (s.drawn_cur[buf] != s.cur_rev) {
        s.drawn_cur[buf] = s.cur_rev;
        clipped(c, cur_val_rect(), draw_cur_value);
    }

    /*
     * The horn when it moved: boxes about 160 px across, where the two
     * cards are 620 and 380 px wide.  A sweep or a drag moves it every
     * frame, and the cards whole at that rate would cost most of the
     * panel's bandwidth.
     */
    if (s.drawn_dial[buf] != s.dial_rev) {
        draw_dial_part(c, buf);
    }

    /*
     * The grip, only while it breathes.  Clipped to the tip: repainting the
     * 488x386 card at the frame rate to animate a ring of 36 px radius would
     * cost most of the panel's bandwidth.
     */
    const int grip_r = 36;
    const int step = (int)(s.pulse * 8.0f);
    if (!s.driving || s.drawn_pulse[buf] == step) {
        return;
    }
    s.drawn_pulse[buf] = step;
    float tx, ty;
    at_f(s.shown_deg, (float)HORN_L, &tx, &ty);
    const int gx = (int)floorf(tx) - grip_r, gy = (int)floorf(ty) - grip_r;
    const gfx_rect_t box = { (int16_t)gx, (int16_t)gy,
                             (int16_t)(grip_r * 2 + 1),
                             (int16_t)(grip_r * 2 + 1) };
    clipped(c, box, draw_left);
}
