/**
 * @file supply_draw.c
 * @brief The SUPPLY screen's drawing: the strip row, the rail with the
 *        readings and the CONTROL card, LIVE's plot or table and set points,
 *        SETTINGS' page and the question.  supply_screen.c holds the state,
 *        the commands and the screen's interface (supply_screen.h);
 *        supply_screen_int.h is what it shares with this file and
 *        supply_events.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "bench_state.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_hero.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#include "supply_screen_int.h"

/* ----------------------------------------------------------------- drawing */

/**
 * @brief Returns the OUTPUT button's fill: the OK green when off, the danger
 *        red when on.
 */
static gfx_color_t out_fill(void)
{
    /* Off is the OK green the hold fades from; on is the danger red, so the
     * fade previews the colour the switch is about to hold. */
    gfx_color_t fill = s.on ? ui_theme_color(UI_C_DANGER)
                            : ui_theme_color(UI_C_OK);
    if (s.hold.flash_ms > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), s.hold.flash_ms);
    }
    if (!s.on && s.hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER), s.hold.held_s);
    }
    return fill;
}

/**
 * @brief OUTPUT ON / OFF at the CONTROL card's foot.
 * @param c the canvas
 */
static void draw_out_button(gfx_canvas_t *c)
{
    ui_button_font(c, s.out_rect,
                   s.on ? TR(SUP_OUTPUT_OFF) : TR(SUP_OUTPUT_ON), out_fill(),
                   s.pressed == P_OUTPUT, s.on || !s.port_held,
                   SU_FONT_LABEL);
}

/**
 * @brief Returns the mode's name: CV, CC, the tripped or off word, or "--"
 *        when the supply does not answer.
 */
static const char *mode_text(void)
{
    if (!s.sup.online) {
        return "--";
    }
    if (!s.sup.output && s.sup.trip != SUPPLY_TRIP_NONE) {
        return TR(SUP_MODE_TRIP);
    }
    switch (s.sup.mode) {
    case SUPPLY_MODE_CV: return "CV";
    case SUPPLY_MODE_CC: return "CC";
    case SUPPLY_MODE_OFF:
    default:             return TR(SUP_MODE_OFF);
    }
}

/**
 * @brief Returns the mode's explanation line: a held port, a trip, CC or CV;
 *        "" when there is none.
 */
static const char *mode_line(void)
{
    if (!s.sup.online) {
        return "";
    }
    if (s.port_held) {
        return TR(SUP_LINE_PORT);
    }
    if (!s.sup.output && s.sup.trip == SUPPLY_TRIP_CURRENT) {
        return TR(SUP_LINE_TRIP_I);
    }
    if (!s.sup.output && s.sup.trip == SUPPLY_TRIP_VOLTAGE) {
        return TR(SUP_LINE_TRIP_V);
    }
    if (s.sup.mode == SUPPLY_MODE_CC) {
        return TR(SUP_LINE_CC);
    }
    return (s.sup.mode == SUPPLY_MODE_CV) ? TR(SUP_LINE_CV) : "";
}

void supply_screen_header_text(char *out, size_t size)
{
    char rate[16] = "";
    if (!s.model && s.sup.online && s.baud != 0u) {
        snprintf(rate, sizeof(rate), " %lu", (unsigned long)s.baud);
    }
    char who[40];
    if (s.model) {
        snprintf(who, sizeof(who), "%s", TR(SUP_MODEL));
    } else if (s.tx1 > 0 && s.rx1 > 0) {
        snprintf(who, sizeof(who), TR(SUP_PD_PINS), s.tx1 - 1, s.rx1 - 1);
    } else {
        snprintf(who, sizeof(who), "%s", TR(SUP_PD_NO_PINS));
    }
    snprintf(out, size, "%s   %s%s", who,
             s.sup.online ? TR(SUP_ONLINE) : TR(SUP_NOT_ANSWERING), rate);
}

/**
 * @brief The row right of the strip: where the numbers come from, and at
 *        its right end the range the set points may take.  The longest,
 *        `PD MINI TX GP12 RX GP13   ANTWORTET NICHT` and
 *        `3.3-21.0V  0.50-5.00A`, are 41 and 23 cells of the 68 the row
 *        holds.
 * @param c the canvas
 */
static void draw_header(gfx_canvas_t *c)
{
    const int x0 = INFO_X;
    const int x1 = W - PAD;
    gfx_fill_rect(c, x0, 0, W - x0, OV_Y - 1, ui_theme_color(UI_C_BG));
    char line[80];
    supply_screen_header_text(line, sizeof(line));
    gfx_text(c, x0, INFO_Y, line, SU_FONT_LABEL,
             s.sup.online ? ui_theme_color(UI_C_TEXT_DIM)
                          : ui_theme_color(UI_C_WARN), 1);
    char range[64];
    snprintf(range, sizeof(range), "%.1f-%.1fV  %.2f-%.2fA",
             (double)s.eff.v_min, (double)s.eff.v_max,
             (double)s.eff.i_min, (double)s.eff.i_max);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x0, INFO_Y, (int16_t)(x1 - x0),
                                 LINE_H },
                range, SU_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_RIGHT);
}

/** @brief Draws the task strip, LIVE | SETTINGS, on the background. */
static void draw_strip(gfx_canvas_t *c)
{
    const gfx_rect_t a = sup_strip_area();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_BG));
    ui_wb_strip_render(&s.wb, c);
}

/**
 * @brief A reading's numerals in the 32 x 44 face, the 20 x 36 one where
 *        they are wider than @p w, right-aligned to @p right.
 * @param c the canvas
 * @param right their right edge, px
 * @param y their top, px
 * @param w the width they may take, px
 * @param t the numerals
 * @param col their colour
 */
static void num_text(gfx_canvas_t *c, int right, int y, int w, const char *t,
                     gfx_color_t col)
{
    const gfx_font_t *f = SU_FONT_NUM;
    int tw = gfx_text_width(f, t, 1);
    int dy = 0;
    if (tw > w) {
        f = SU_FONT_HEAD;
        tw = gfx_text_width(f, t, 1);
        dy = 4;
    }
    gfx_text(c, right - tw, y + dy, t, f, col, 1);
}

/** @brief A reading card's left part: its name and the set point in
 *         brackets end by x + 170 ("SPANN. (21.00)", 14 cells). */
#define CARD_NUM_DX 170

/**
 * @brief One card of the rail, CARD_H px high, as MOTOR & ESC draws them:
 *        the name, with the set point in brackets after it when there is
 *        one, over the extreme, and the live value in the 32 x 44 numerals,
 *        right-aligned with its unit.
 * @param c the canvas
 * @param r the card
 * @param label the reading's name
 * @param set the set point in brackets, or NULL
 * @param unit the unit
 * @param color the reading's colour
 * @param decimals the value's decimals
 * @param value the live value, NAN for none
 * @param extreme the extreme's name, or NULL for none
 * @param peak the extreme, NAN for none
 * @param pressed whether a press holds the card
 */
static void draw_card(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                      const char *set, const char *unit, gfx_color_t color,
                      int decimals, float value, const char *extreme,
                      float peak, bool pressed)
{
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_card(c, r, pressed ? ui_theme_color(UI_C_PANEL_HI)
                          : ui_theme_color(UI_C_PANEL));
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, color);
    gfx_fill_round_rect(c, r.x + 10, r.y + 11, 4, 14, 1, color);
    const int lw = gfx_text(c, r.x + 20, r.y + 8, label, SU_FONT_LABEL,
                            ui_theme_color(UI_C_TEXT), 1);
    if (set != NULL) {
        gfx_text(c, r.x + 20 + lw + 10, r.y + 8, set, SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    if (extreme != NULL && isfinite(peak)) {
        char pk[24], line[32];
        ui_fmt(pk, sizeof(pk), peak, decimals);
        snprintf(line, sizeof(line), "%s %s", extreme, pk);
        gfx_text(c, r.x + 10, r.y + 36, line, SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    char number[24];
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    const int uw = gfx_text_width(SU_FONT_LABEL, unit, 1);
    const int ux = r.x + r.w - 10 - uw;
    gfx_text(c, ux, r.y + 34, unit, SU_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    num_text(c, ux - 6, r.y + 10, ux - 6 - (r.x + CARD_NUM_DX), number,
             color);
}

/**
 * @brief The set points as the supply reports holding them, or as this screen holds
 *        them while it does not answer.
 */
static float shown_set_v(void)
{
    return s.sup.online ? s.sup.set_v : s.cv;
}

/**
 * @brief Returns the current limit as the supply reports it, or as this screen
 *        holds it while the supply does not answer.
 */
static float shown_set_i(void)
{
    return s.sup.online ? s.sup.set_i : s.ci;
}

/**
 * @brief The rail's four cards: VOLT, CURR and PWR with their set points
 *        and extremes, and MODE.
 * @param c the canvas
 */
static void draw_cards(gfx_canvas_t *c)
{
    const bool v_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_CURRENT) != 0u;
    const bool run = s.sup.output || s.plot.filled > 0;
    char vset[16], iset[16];
    snprintf(vset, sizeof(vset), "(%.2f)", (double)shown_set_v());
    snprintf(iset, sizeof(iset), "(%.2f)", (double)shown_set_i());
    draw_card(c, sup_card_rect(0), TR(MO_VOLT), vset, "V",
              ui_theme_color(UI_C_VOLT), 2, v_ok ? s.sup.v : NAN, TR(HERO_MIN),
              (v_ok && run && s.sup.sag_seeded) ? s.sup.v_min : NAN,
              s.pressed == P_CARD_V);
    draw_card(c, sup_card_rect(1), TR(MO_CURR), iset, "A",
              ui_theme_color(UI_C_CURR), 2, i_ok ? s.sup.i : NAN,
              TR(HERO_PEAK), (i_ok && run) ? s.sup.i_max : NAN,
              s.pressed == P_CARD_I);
    draw_card(c, sup_card_rect(2), TR(MO_PWR), NULL, "W",
              ui_theme_color(UI_C_POWER), 1, (v_ok && i_ok) ? s.sup.p : NAN,
              TR(HERO_PEAK),
              (v_ok && i_ok && run) ? s.sup.p_max : NAN, false);

    /* The mode, in letters: which of the two set points the supply is
     * holding.  CC in the warning colour, because a supply at its limit is
     * not giving the load the voltage it was set to; a trip in the danger
     * colour until the output is switched on again. */
    const gfx_rect_t r = sup_card_rect(3);
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_card(c, r, ui_theme_color(UI_C_PANEL));
    const bool tripped = s.sup.online && !s.sup.output
                         && s.sup.trip != SUPPLY_TRIP_NONE;
    const gfx_color_t mc = tripped ? ui_theme_color(UI_C_DANGER)
                           : (s.sup.online && s.sup.mode == SUPPLY_MODE_CC)
                               ? ui_theme_color(UI_C_WARN)
                               : ui_theme_color(UI_C_ACCENT);
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, mc);
    gfx_fill_round_rect(c, r.x + 10, r.y + 11, 4, 14, 1, mc);
    gfx_text(c, r.x + 20, r.y + 8, TR(SUP_MODE), SU_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    /* The line under the label: why the supply holds what it holds, or
     * why it tripped, in up to 36 cells ("Wegen Überspannung
     * abgeschaltet." is 32). */
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 10), (int16_t)(r.y + 36),
                                 (int16_t)(r.w - 20), LINE_H },
                mode_line(), SU_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT),
                1, GFX_ALIGN_LEFT);
    if (tripped) {
        /* A trip's word is too wide for the 20 x 36 face beside the label
         * ("ABGESCHALTET", 12 cells): it goes on the label's row. */
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 10), (int16_t)(r.y + 8),
                                     (int16_t)(r.w - 20), LINE_H },
                    mode_text(), SU_FONT_LABEL, mc, 1, GFX_ALIGN_RIGHT);
        return;
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 10), (int16_t)(r.y + 10),
                                 (int16_t)(r.w - 20), 36 },
                mode_text(), SU_FONT_HEAD, mc, 1, GFX_ALIGN_RIGHT);
}

/**
 * @brief TABLE: each reading's set point and its value now, under the
 *        header line, in the legend's and the plot's place.
 * @param c the canvas
 */
static void draw_table(gfx_canvas_t *c)
{
    gfx_text(c, PLOT_X, LEG_Y, TR(SUP_TABLE_HEAD), SU_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_rule(c, PLOT_X, LEG_Y + 24, PLOT_W, ui_theme_color(UI_C_EDGE));
    const bool v_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_CURRENT) != 0u;
    static const ui_text_id_t rows[S_READINGS] = {
        TX_MO_ROW_VOLTAGE, TX_MO_ROW_CURRENT, TX_MO_ROW_POWER,
    };
    const float set[S_READINGS] = { shown_set_v(), shown_set_i(), NAN };
    const float now[S_READINGS] = { v_ok ? s.sup.v : NAN, i_ok ? s.sup.i : NAN,
                                    (v_ok && i_ok) ? s.sup.p : NAN };
    for (int i = 0; i < S_READINGS; ++i) {
        const int y = LEG_Y + 34 + i * TBL_PITCH;
        char a[24], b[24];
        if (isfinite(set[i])) {
            ui_fmt(a, sizeof(a), set[i], sup_series[i].decimals);
        } else {
            snprintf(a, sizeof(a), "--");
        }
        if (isfinite(now[i])) {
            ui_fmt(b, sizeof(b), now[i], sup_series[i].decimals);
        } else {
            snprintf(b, sizeof(b), "--");
        }
        gfx_text(c, PLOT_X, y, ui_tr(rows[i]), SU_FONT_LABEL,
                 s.plot.series[i].color, 1);
        gfx_text(c, PLOT_X + TBL_SET_DX, y, a, SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + TBL_NOW_DX, y, b, SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + TBL_UNIT_DX, y, sup_series[i].unit,
                 SU_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

/**
 * @brief What the run has taken from the supply, on the CONTROL card's
 *        title line, right of its tag: STEUERUNG's 9 cells and the tab's
 *        26px.
 * @param c the canvas
 */
static void draw_totals(gfx_canvas_t *c)
{
    const gfx_rect_t box = { (int16_t)(RIGHT_X + 130), (int16_t)(CTL_Y + 1),
                             (int16_t)(RIGHT_W - 130 - 12), LINE_H };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h, ui_theme_color(UI_C_PANEL));
    char mah[16], wh[16];
    if ((s.sup.counted & BENCH_COUNTED_CHARGE) != 0u) {
        snprintf(mah, sizeof(mah), "%.0f", (double)s.sup.charge_mah);
    } else {
        snprintf(mah, sizeof(mah), "--");
    }
    if ((s.sup.counted & BENCH_COUNTED_ENERGY) != 0u) {
        snprintf(wh, sizeof(wh), "%.2f", (double)s.sup.energy_wh);
    } else {
        snprintf(wh, sizeof(wh), "--");
    }
    char line[48];
    snprintf(line, sizeof(line), "%smAh   %sWh", mah, wh);
    gfx_text_in(c, box, line, SU_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

/**
 * @brief The two set points: the label, the track, and the value in its
 *        box at the right end of the controls, in the reading's colour.
 *        Held by a programming port the value takes no press, and is drawn
 *        faint, as SERVO's SET row draws it.
 * @param c the canvas
 */
static void draw_setters(gfx_canvas_t *c)
{
    static const ui_text_id_t label[2] = { TX_SUP_VOLTAGE,
                                           TX_SUP_CURRENT_LIMIT };
    const float value[2] = { s.v_slider.value, s.i_slider.value };
    const char *unit[2] = { "V", "A" };
    const gfx_color_t col[2] = { ui_theme_color(UI_C_VOLT),
                                 ui_theme_color(UI_C_CURR) };
    for (int row = 0; row < 2; ++row) {
        const int y = SET_Y0 + row * SET_ROW_H;
        gfx_fill_rect(c, LEFT_X + INNER, y, SET_UP_X - (LEFT_X + INNER),
                      SET_TEXT_H, ui_theme_color(UI_C_PANEL));
        gfx_text(c, LEFT_X + INNER, y, ui_tr(label[row]), SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[24];
        snprintf(v, sizeof(v), "%.2f%s", (double)value[row], unit[row]);
        const bool pressed = s.pressed == ((row == 0) ? P_VALUE_V
                                                      : P_VALUE_I);
        const gfx_rect_t b = sup_set_value_rect(row);
        gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                            pressed ? ui_theme_color(UI_C_PANEL_HI)
                                    : ui_theme_color(UI_C_PANEL_SUNK));
        gfx_text_in(c, b, v, SU_FONT_LABEL,
                    s.port_held ? ui_theme_color(UI_C_TEXT_FAINT) : col[row],
                    1, GFX_ALIGN_CENTER);
        const ui_slider_t *sl = (row == 0) ? &s.v_slider : &s.i_slider;
        const gfx_rect_t pr = ui_slider_painted_rect(sl);
        gfx_fill_rect(c, pr.x, pr.y, pr.w, pr.h, ui_theme_color(UI_C_PANEL));
        ui_slider_render(sl, c);
    }
}

/**
 * @brief The fine steps at both ends of each track, dim while a
 *        programming port holds the supply.
 * @param c the canvas
 */
static void draw_steps(gfx_canvas_t *c)
{
    const bool free = !s.port_held;
    const gfx_color_t f = ui_theme_color(UI_C_PANEL_SUNK);
    ui_button_font(c, s.v_down, "-0.1", f, s.pressed == P_V_DOWN, free,
                   SU_FONT_LABEL);
    ui_button_font(c, s.v_up, "+0.1", f, s.pressed == P_V_UP, free,
                   SU_FONT_LABEL);
    ui_button_font(c, s.i_down, "-0.1", f, s.pressed == P_I_DOWN, free,
                   SU_FONT_LABEL);
    ui_button_font(c, s.i_up, "+0.1", f, s.pressed == P_I_UP, free,
                   SU_FONT_LABEL);
}

/**
 * @brief Whether the settings reached the medium: 0 saved, 1 waiting for a quiet
 *        moment, 2 refused, 3 changed on SETUP and not asked to be saved -- which
 *        nothing writes until SAVE there, or a change here, asks for it.
 */
static uint8_t save_state(void)
{
    if (settings_save_failed()) {
        return 2u;
    }
    if (settings_save_asked()) {
        return 1u;
    }
    return settings_dirty() ? 3u : 0u;
}

/** @brief Writes the value of SETTINGS' row @p i with its unit. */
static void row_value_text(int i, char *buf, size_t n)
{
    const setting_def_t *d = settings_def(k_rows[i].id);
    const float v = settings_get(k_rows[i].id);
    const bool trip = (k_rows[i].id == SET_SUPPLY_TRIP_I
                       || k_rows[i].id == SET_SUPPLY_TRIP_V);
    if (d->type == SET_TYPE_BOOL) {
        snprintf(buf, n, "%s", ui_on_off(v != 0.0f));
    } else if (trip && !(v > 0.0f)) {
        snprintf(buf, n, "%s", TR(OFF));
    } else {
        snprintf(buf, n, "%.*f%s", k_rows[i].decimals, (double)v, d->unit);
    }
}

/**
 * @brief SETTINGS' page in the left column under the strip: the caps and
 *        the start values on the left with RESET PD MINI under them, the
 *        trips, their help and the confirmations on the right, and the
 *        save line at the foot.
 * @param c the canvas
 */
static void draw_settings(gfx_canvas_t *c)
{
    const gfx_rect_t a = sup_overlay_area();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (!s.model) {
        /* A module in ERR -- a set point over its input -- comes back only
         * with a restart. */
        ui_button_font(c, s.modreset_btn, TR(SUP_RESET_PDMINI),
                       ui_theme_color(UI_C_PANEL_SUNK),
                       s.pressed == P_MODRESET, true, SU_FONT_LABEL);
    }

    const int lx = OV_COL0_X;
    const int rx = OV_COL1_X;
    const int head = (OV_HEAD_H - LINE_H) / 2;
    const gfx_color_t acc = ui_theme_color(UI_C_ACCENT);
    gfx_text(c, lx, a.y + OV_CAPS_DY + head, TR(SUP_CAPS), SU_FONT_LABEL,
             acc, 1);
    gfx_text(c, lx, a.y + OV_RESTART_DY + head, TR(SUP_AFTER_RESTART),
             SU_FONT_LABEL, acc, 1);
    gfx_text(c, rx, a.y + OV_CAPS_DY + head, TR(SUP_TRIPS), SU_FONT_LABEL,
             acc, 1);
    gfx_text(c, rx, a.y + OV_CONFIRM_DY + head, TR(SUP_CONFIRM_ON),
             SU_FONT_LABEL, acc, 1);
    for (int i = 0; i < ROW_COUNT; ++i) {
        const gfx_rect_t r = sup_row_rect(i);
        gfx_text(c, r.x, r.y + (OV_ROW_H - LINE_H) / 2,
                 ui_tr(k_rows[i].label), SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[32];
        row_value_text(i, v, sizeof(v));
        ui_button_font(c, sup_row_value_rect(i), v,
                       ui_theme_color(UI_C_PANEL_SUNK),
                       s.pressed == P_ROW && s.press_row == i, true,
                       SU_FONT_LABEL);
    }
    static const ui_text_id_t help[] = {
        TX_SUP_HELP_1, TX_SUP_HELP_2, TX_SUP_HELP_3,
    };
    for (size_t i = 0; i < sizeof(help) / sizeof(help[0]); ++i) {
        gfx_text(c, rx, a.y + OV_HELP_DY + (int)i * OV_HELP_PITCH,
                 ui_tr(help[i]), SU_FONT_FINE,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    const uint8_t st = save_state();
    static const ui_text_id_t k_save[] = {
        TX_SUP_SAVED, TX_SUP_SAVE_WAITING, TX_SUP_NOT_SAVED,
        TX_SUP_SETUP_NOT_SAVED,
    };
    gfx_text(c, lx, OV_SAVE_Y, ui_tr(k_save[st]), SU_FONT_LABEL,
             (st == 2u) ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1);
}

/**
 * @brief The question a change to a live output asks: what changes, from
 *        what to what, and APPLY or CANCEL; where to turn it off at its
 *        foot.  OUTPUT OFF and STOP stay where they are.
 * @param c the canvas
 */
static void draw_confirm(gfx_canvas_t *c)
{
    const gfx_rect_t a = sup_overlay_area();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_WARN));
    const int tx = a.x + 20;
    const int tw = a.w - 40;
    gfx_text(c, tx, a.y + 16, TR(SUP_OUTPUT_IS_ON), SU_FONT_HEAD,
             ui_theme_color(UI_C_WARN), 1);
    (void)ui_text_wrap(c, (gfx_rect_t){ (int16_t)tx, (int16_t)(a.y + 62),
                                        (int16_t)tw, 24 },
                       ASK_PITCH, TR(SUP_ASK_WHY), SU_FONT_BODY,
                       ui_theme_color(UI_C_TEXT_DIM));
    int y = a.y + 124;
    const struct { const char *label; float was, now; const char *unit;
                   gfx_color_t col; } k[] = {
        { TR(SUP_VOLTAGE), s.cv, s.pend_v, "V", ui_theme_color(UI_C_VOLT) },
        { TR(SUP_CURRENT_LIMIT), s.ci, s.pend_i, "A",
          ui_theme_color(UI_C_CURR) },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (k[i].was == k[i].now) {
            continue;
        }
        gfx_text(c, tx, y + 10, k[i].label, SU_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[48];
        snprintf(line, sizeof(line), "%.2f -> %.2f%s", (double)k[i].was,
                 (double)k[i].now, k[i].unit);
        gfx_text(c, tx + 170, y, line, SU_FONT_HEAD, k[i].col, 1);
        y += 50;
    }
    ui_button_font(c, s.apply_btn, TR(SUP_APPLY), ui_theme_color(UI_C_WARN),
                   s.pressed == P_APPLY, true, SU_FONT_LABEL);
    ui_button_font(c, s.discard_btn, TR(CANCEL),
                   ui_theme_color(UI_C_PANEL_SUNK), s.pressed == P_DISCARD,
                   true, SU_FONT_LABEL);
    /* At the foot: its last line ends 4px above the alert band. */
    const gfx_rect_t first = { (int16_t)tx, 0, (int16_t)tw, 24 };
    const int rows = ui_text_wrap(NULL, first, ASK_PITCH, TR(SUP_ASK_OFF),
                                  SU_FONT_BODY, 0);
    (void)ui_text_wrap(c, (gfx_rect_t){ (int16_t)tx,
                                        (int16_t)(STOP_FLOOR - 24
                                                  - (rows - 1) * ASK_PITCH),
                                        (int16_t)tw, 24 },
                       ASK_PITCH, TR(SUP_ASK_OFF), SU_FONT_BODY,
                       ui_theme_color(UI_C_TEXT_FAINT));
}

enum { TAG_LIVE = 0, TAG_HELD, TAG_IDLE, TAG_COUNT };
/** @brief Text of each output tag: LIVE, HELD, IDLE. */
static const ui_text_id_t k_tag[TAG_COUNT] = {
    TX_SUP_TAG_LIVE, TX_SUP_TAG_HELD, TX_SUP_TAG_IDLE,
};

/**
 * @brief The heading says what the output is doing, on either pane: a held run is
 *        the plot's to show, so TABLE says IDLE where PLOT says HELD.
 */
static uint8_t output_tag(void)
{
    if (s.plot.running) {
        return (uint8_t)TAG_LIVE;
    }
    return (uint8_t)((s.tabs.selected == SUPPLY_PANE_PLOT && s.plot.filled > 0)
                         ? TAG_HELD : TAG_IDLE);
}

/**
 * @brief A frame: the chrome once per framebuffer, then each part on its
 *        own counter.
 * @param c the canvas
 * @param buffer_index the framebuffer drawn into, 0 or 1
 */
void sup_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;
    sup_bind_colours();

    /* The left column shows LIVE's plot and set points unless the keypad
     * or the question covers it, or SETTINGS' page has it. */
    const bool covered = s.kp.open || s.confirm_open || !sup_on_live();
    const uint8_t tag = output_tag();
    char tag_buf[48];
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        if (!covered) {
            ui_panel(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H },
                     ui_tr_pad(k_tag, TAG_COUNT, tag, tag_buf,
                               sizeof(tag_buf)),
                     ui_theme_color(UI_C_ACCENT));
            ui_panel(c, (gfx_rect_t){ LEFT_X, LO_Y, LEFT_W, LO_H },
                     TR(SUP_SET_POINTS), ui_theme_color(UI_C_ACCENT));
        }
        ui_panel(c, (gfx_rect_t){ RIGHT_X, CTL_Y, RIGHT_W, CTL_H },
                 TR(MO_CONTROL), ui_theme_color(UI_C_ACCENT));
        s.drawn_mask |= bit;
        s.drawn_title[buf] = tag;
        s.drawn_strip[buf] = UINT32_MAX;
    }
    if (s.drawn_strip[buf] != s.strip_rev) {
        s.drawn_strip[buf] = s.strip_rev;
        draw_strip(c);
    }
    if (!covered && s.drawn_title[buf] != tag) {
        s.drawn_title[buf] = tag;
        ui_panel_header(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H },
                        ui_tr_pad(k_tag, TAG_COUNT, tag, tag_buf,
                                  sizeof(tag_buf)),
                        ui_theme_color(UI_C_ACCENT));
    }
    const bool data_moved = (s.drawn_push[buf] != s.plot.pushes);
    const bool ctrl_moved = (s.drawn_ctrl[buf] != s.ctrl_rev);
    const bool set_moved  = (s.drawn_set[buf] != s.set_rev);
    const bool out_moved  = (s.drawn_out[buf] != s.out_rev);

    if (ctrl_moved || data_moved) {
        if (!covered) {
            ui_tabs_render(&s.tabs, c);
        }
        draw_header(c);
    }

    if (s.kp.open) {
        if (s.drawn_kp[buf] != s.kp.revision) {
            s.drawn_kp[buf] = s.kp.revision;
            ui_keypad_render(&s.kp, c);
        }
    } else if (s.confirm_open) {
        if (s.drawn_ov[buf] != s.ov_rev) {
            s.drawn_ov[buf] = s.ov_rev;
            draw_confirm(c);
        }
    } else if (!sup_on_live()) {
        const uint8_t st = save_state();
        if (s.drawn_ov[buf] != s.ov_rev || s.drawn_save[buf] != st) {
            s.drawn_ov[buf]   = s.ov_rev;
            s.drawn_save[buf] = st;
            draw_settings(c);
        }
    } else if (s.tabs.selected == SUPPLY_PANE_PLOT) {
        if (s.drawn_plot[buf] != s.plot.revision) {
            s.drawn_plot[buf] = s.plot.revision;
            gfx_fill_rect(c, PLOT_X, LEG_Y, PLOT_W, LEG_H,
                          ui_theme_color(UI_C_PANEL));
            ui_plot_render_legend(&s.plot, c,
                                  (gfx_rect_t){ PLOT_X, LEG_Y, PLOT_W, LEG_H });
            ui_plot_render(&s.plot, c,
                           (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H });
            if (!s.plot.running && s.plot.filled == 0) {
                gfx_text_in(c, (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H },
                            TR(SUP_NOT_ON_YET), SU_FONT_LABEL,
                            ui_theme_color(UI_C_TEXT_FAINT), 1,
                            GFX_ALIGN_CENTER);
            }
        }
    } else if (data_moved || set_moved) {
        /* From the legend's row down: PLOT | TABLE above it stay. */
        gfx_fill_rect(c, LEFT_X + 1, LEG_Y, LEFT_W - 2,
                      UP_Y + UP_H - 1 - UI_CHAMFER - LEG_Y,
                      ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, LEFT_X + 1, UP_Y + UP_H - 1 - UI_CHAMFER,
                      LEFT_W - 2 - UI_CHAMFER, UI_CHAMFER,
                      ui_theme_color(UI_C_PANEL));
        draw_table(c);
    }
    /* The cards carry the set point in brackets, so a set point that moves
     * repaints them too, in each buffer. */
    if (data_moved || ctrl_moved || set_moved) {
        s.drawn_push[buf] = s.plot.pushes;
        draw_cards(c);
        draw_totals(c);
    }

    if (!ctrl_moved && !set_moved && !out_moved) {
        return;
    }
    s.drawn_ctrl[buf] = s.ctrl_rev;
    s.drawn_set[buf]  = s.set_rev;
    s.drawn_out[buf]  = s.out_rev;

    /* The switch animating on its own repaints its own button only. */
    if (out_moved && !ctrl_moved && !set_moved) {
        draw_out_button(c);
        return;
    }
    if (!covered && (set_moved || ctrl_moved)) {
        draw_setters(c);
    }
    if (ctrl_moved) {
        if (!covered) {
            draw_steps(c);
        }
        ui_button_font(c, s.reset_rect, TR(MO_RESET_PEAKS),
                       ui_theme_color(UI_C_PANEL_SUNK), s.pressed == P_RESET,
                       true, SU_FONT_LABEL);
    }
    if (ctrl_moved || out_moved) {
        draw_out_button(c);
    }
}
