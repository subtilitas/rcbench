/**
 * @file motor_draw.c
 * @brief The MOTOR & ESC screen's drawing: the strip row, the rail with the
 *        readouts and the CONTROL card, LIVE's plot or table and the throttle,
 *        and the page of the task shown.  motor_screen.c holds the state, the
 *        commands and the screen's interface (motor_screen.h);
 *        motor_live_int.h is what it shares with this file and motor_events.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "motor_screen.h"
#include "ui_band.h"
#include "ui_hero.h"
#include "ui_plot.h"
#include "link_pages.h"
#include "settings.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "outputs_screen.h"
#include "port_view.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "motor_live_int.h"

/* Voltage's interesting extreme is the minimum -- what the pack does under
 * load is how far down it goes -- and every other channel's is the maximum. */
static const ui_text_id_t k_extreme[S_COUNT] = { TX_HERO_MIN, TX_HERO_PEAK,
                                                 TX_HERO_PEAK, TX_HERO_PEAK };
/* ----------------------------------------------------------------- drawing */

/*
 * Green while it will arm, red by the time the fade completes, and white for
 * the flash as the arm takes effect.  With nothing held and nothing flashing
 * this is the plain OK or WARN the button has always carried.
 */
static gfx_color_t arm_fill(void)
{
    /* Armed is the danger red the press fades towards, so the fade previews
     * the colour the button is about to hold. */
    gfx_color_t fill = s.gate.armed ? ui_theme_color(UI_C_DANGER)
                               : ui_theme_color(UI_C_OK);
    /* The flash is the whole button, its colour by the time it has run, and
     * it overrides everything else while it runs. */
    if (s.arm.flash_ms > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), s.arm.flash_ms);
    }
    if (!s.gate.armed && s.arm.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER), s.arm.held_s);
    }
    return fill;
}

/**
 * @brief ARM, or DISARM while the bench is armed, in its fill of the
 *        moment, on the rail where SERVO has its ARM.
 * @param c the canvas
 */
static void draw_arm(gfx_canvas_t *c)
{
    ui_button_font(c, s.arm_rect, s.gate.armed ? "DISARM" : "ARM",
                   arm_fill(), s.pressed == 1, !mo_arm_locked(),
                   MO_FONT_LABEL);
}

/* "44C", or "--" when nothing has answered. */
static void temp_text(char *out, size_t n, const char *label, float v,
                      bool ok)
{
    if (ok && isfinite(v)) {
        snprintf(out, n, "%s %dC", label, (int)(v + 0.5f));
    } else {
        snprintf(out, n, "%s --", label);
    }
}

/**
 * @brief On LIVE, the poll rate and the link's errors right of PLOT |
 *        TABLE, and under them, while the IOmcu has refused a frame, how
 *        many; on every task, how hot the three things that get hot, on
 *        the line over the rail.  MCU is the panel's own die, not the
 *        IOmcu's.
 * @param c the canvas
 * @param on_live whether LIVE is shown
 */
static void draw_header(gfx_canvas_t *c, bool on_live)
{
    const ui_bench_status_t *st = ui_router_status();

    /* The band already carries the output mode, so this line does not
     * repeat it: what it adds is the poll rate and the link's error count,
     * and while the IOmcu has refused a frame, how many. */
    if (on_live) {
        const int x0 = RATE_X;
        const int w0 = PLOT_X + PLOT_W - x0;
        gfx_fill_rect(c, x0, SUB_Y, w0, SUB_H, ui_theme_color(UI_C_PANEL));
        char line[64];
        snprintf(line, sizeof(line), TR(MO_RATE_ERR), (int)SAMPLE_HZ,
                 (unsigned long)st->link_errors);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)x0, SUB_Y, (int16_t)w0, 20 },
                    line, MO_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                    GFX_ALIGN_LEFT);
        /* At its widest, a 10-digit count in German, 40 cells of 8px:
         * 320 of the line's 386px. */
        char refused[48];
        if (ui_link_refused_label(st, refused, sizeof(refused))) {
            gfx_text_in(c, (gfx_rect_t){ (int16_t)x0, SUB_Y + 22,
                                         (int16_t)w0, 16 },
                        refused, MO_FONT_FINE,
                        ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_LEFT);
        }
    }

    /*
     * The protocol carries a flag per channel for exactly this: a bench whose
     * IOmcu has no temperature sensor sends zero, and "ESC 0C" is a
     * reading rather than a gap.
     */
    const bool temp_ok = s.bench.valid
                         && (s.bench.flags & LINK_BN_TEMP_OK) != 0u;
    /* The motor's sensor is its own, and usually there is not one: an ESC
     * reporting its own temperature says nothing about the motor it drives,
     * and one flag for both would draw "MOT 0C" as a reading. */
    const bool mot_ok = s.bench.valid
                        && (s.bench.flags & LINK_BN_TEMP_MOT_OK) != 0u;
    char esc[16], mot[16], mcu[16], temps[52];
    temp_text(esc, sizeof(esc), "ESC", s.bench.temp_esc, temp_ok);
    temp_text(mot, sizeof(mot), "MOT", s.bench.temp_motor, mot_ok);
    temp_text(mcu, sizeof(mcu), "MCU", st->mcu_temp_c,
              isfinite(st->mcu_temp_c));
    snprintf(temps, sizeof(temps), "%s  %s  %s", esc, mot, mcu);
    const gfx_rect_t t = { RIGHT_X, HDR_Y, (int16_t)(RIGHT_W - 4), HDR_H };
    gfx_fill_rect(c, t.x, t.y, t.w, t.h, ui_theme_color(UI_C_BG));
    gfx_text_in(c, t, temps, MO_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_RIGHT);
}

/*
 * The rated kV, what the motor is actually turning per volt, and what the
 * ratio of the two says about the drivetrain.
 *
 * The arithmetic is sound and the number is still an estimate, which is why
 * it is labelled EFF rather than efficiency.  Terminal voltage splits into
 * the resistive drop and the back-EMF, so rpm/V under load is the rated kV
 * scaled by the fraction of the voltage reaching the back-EMF, and in an
 * ideal motor that fraction is the conversion efficiency exactly.
 *
 * This motor is not that one.  The figure counts copper loss only, so iron
 * loss, friction and windage are missing and it is an upper bound rather
 * than a value.  The bench measures pack voltage rather than the motor's
 * terminals, so the ESC's losses are inside it and it describes the
 * drivetrain.  And it is only as good as the rated kV, an error in which
 * maps straight into the percentage.  docs/Screens.md says so in the words
 * an operator reads.
 *
 * The rated value comes from the connected ESC when it reports one, and from
 * SET_MOTOR_KV when it does not.  Nothing is assumed: with neither, the field
 * is drawn empty and no efficiency is shown, because a guessed kV produces a
 * plausible-looking percentage that is wrong.
 */
static void draw_derived(gfx_canvas_t *c)
{
    /* Right of the card's tag: its 18 cells of 8px and the tab's 26px,
     * on the title line in the 10 x 20 face. */
    const gfx_rect_t box = { (int16_t)(LEFT_X + 186), UP_Y + 1,
                             (int16_t)(LEFT_W - 186 - INNER), 20 };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h, ui_theme_color(UI_C_PANEL));

    /* What the ESC says, or failing that what somebody typed in. */
    const int rated = (s.esc_kv > 0) ? s.esc_kv
                                     : settings_get_int(SET_MOTOR_KV);
    const bool measurable =
        s.bench.valid
        && (s.bench.flags & LINK_BN_VOLTAGE_OK) != 0u
        && (s.bench.flags & LINK_BN_RPM_OK) != 0u
        && s.bench.voltage > 0.5f && s.bench.rpm > 1.0f;
    const float per_volt = measurable ? (s.bench.rpm / s.bench.voltage) : 0.0f;

    char kv[24], rv[24], eff[24];
    if (rated > 0) {
        snprintf(kv, sizeof(kv), "kV %d", rated);
    } else {
        snprintf(kv, sizeof(kv), "kV ----");
    }
    if (measurable) {
        int shown = (int)(per_volt + 0.5f);
        if (shown > 99999) {
            shown = 99999;   /* the field has a width; a reading this high
                              * is a broken sensor, not a motor */
        }
        snprintf(rv, sizeof(rv), "rpm/V %d", shown);
    } else {
        snprintf(rv, sizeof(rv), "rpm/V --");
    }
    if (rated > 0 && measurable) {
        int pct = (int)(per_volt * 100.0f / (float)rated + 0.5f);
        /* Over 100% means the rated value is wrong, which is worth seeing;
         * it is capped only so the field cannot grow without bound. */
        if (pct > 199) {
            pct = 199;
        }
        snprintf(eff, sizeof(eff), "EFF %d%%", pct);
    } else {
        snprintf(eff, sizeof(eff), "EFF --");
    }

    char line[96];
    snprintf(line, sizeof(line), "%s  %s  %s", kv, rv, eff);
    gfx_text_in(c, box, line, MO_FONT_LABEL,
                ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_RIGHT);
}

/*
 * The table, in the legend's and the plot's place: its heading, a rule,
 * the four channels 32px apart and, while the INA228 is BENCH's source,
 * the ESC's own figures in two rows 28px apart.  Each row's columns: the
 * name, the value from TBL_VAL_DX, its unit from TBL_UNIT_DX and the
 * extreme from TBL_PK_DX, where the heading's NOW and PEAK stand.
 */
/** @brief Channel row @p i's top, px. */
#define TABLE_ROW_Y(i) (LEG_Y + 30 + (i) * 32)
/** @brief The rule over the ESC's rows, px. */
#define ESC_RULE_Y     (TABLE_ROW_Y(S_COUNT) - 6)
/** @brief The ESC's row @p i's top, px: 0 voltage, 1 current. */
#define ESC_ROW_Y(i)   (ESC_RULE_Y + 8 + (i) * 28)
#define TBL_VAL_DX     150  /**< The value's column, from the left, px. */
#define TBL_UNIT_DX    250  /**< The unit's column, px. */
#define TBL_PK_DX      300  /**< The extreme's column, under PEAK, px. */
_Static_assert(ESC_ROW_Y(1) + 20 <= UP_Y + UP_H - INNER,
               "the ESC's second row ends inside the telemetry card");

/*
 * What the ESC says beside what the INA228 measures, while the INA228 is
 * BENCH's source: the ESC's own telemetry voltage and current from the
 * SENSE page, and how far each is from the INA228's, the ESC's less the
 * INA228's.  An ESC that reports a current with no sensor behind it shows
 * here as a difference the size of the reading.
 */
static void draw_esc_says(gfx_canvas_t *c)
{
    ui_rule(c, PLOT_X, ESC_RULE_Y, PLOT_W, ui_theme_color(UI_C_EDGE));
    for (int i = 0; i < 2; ++i) {
        const int y = ESC_ROW_Y(i);
        float esc = 0.0f;
        const bool esc_ok = (i == S_VOLT)
                                ? bench_state_esc_voltage(&s.bench, &esc)
                                : bench_state_esc_current(&s.bench, &esc);
        const float ina = (i == S_VOLT) ? s.bench.voltage : s.bench.current;
        const bool ina_ok = (s.bench.flags & mo_ok[i]) == mo_ok[i];
        char v[24], d[32], line[48];
        ui_fmt(v, sizeof(v), esc_ok ? esc : NAN, mo_series[i].decimals);
        if (esc_ok && ina_ok) {
            const float diff = esc - ina;
            char mag[24];
            ui_fmt(mag, sizeof(mag), fabsf(diff), mo_series[i].decimals);
            snprintf(d, sizeof(d), "%c%s", (diff < 0.0f) ? '-' : '+', mag);
        } else {
            snprintf(d, sizeof(d), "--");
        }
        if (i == 0) {
            gfx_text(c, PLOT_X, y, TR(MO_ESC_SAYS), MO_FONT_LABEL,
                     ui_theme_color(UI_C_TEXT_DIM), 1);
        }
        gfx_text(c, PLOT_X + TBL_VAL_DX, y, v, MO_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + TBL_UNIT_DX, y, mo_series[i].unit,
                 MO_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        snprintf(line, sizeof(line), "%s %s%s", TR(MO_TABLE_DIFF), d,
                 mo_series[i].unit);
        gfx_text(c, PLOT_X + TBL_PK_DX, y, line, MO_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}

static void draw_table(gfx_canvas_t *c)
{
    static const ui_text_id_t rows[S_COUNT] = {
        TX_MO_ROW_VOLTAGE, TX_MO_ROW_CURRENT, TX_MO_ROW_POWER, TX_MO_ROW_RPM,
    };
    const float now[S_COUNT] = { s.bench.voltage, s.bench.current,
                                 s.bench.power, s.bench.rpm };
    const float pk[S_COUNT]  = { s.bench.voltage_min, s.bench.current_max,
                                 s.bench.power_max, s.bench.rpm_max };
    /* Voltage's interesting extreme is the minimum, not the maximum: what a
     * pack does under load is how far down it goes. */
    static const ui_text_id_t pk_label[S_COUNT] = {
        TX_HERO_MIN, TX_MO_TABLE_MAX, TX_MO_TABLE_MAX, TX_MO_TABLE_MAX,
    };
    const bool sensed = s.bench.valid
                        && (s.bench.flags & LINK_BN_SENSED) != 0u;

    gfx_text(c, PLOT_X, LEG_Y, TR(MO_TABLE_HEAD),
             MO_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    if (sensed) {
        /* Where the numbers come from, and whether its current hit the end
         * of its range: then current and power are bounds. */
        char src[32];
        snprintf(src, sizeof(src), "INA228%s%s", s.bench.clipped ? " " : "",
                 s.bench.clipped ? TR(MO_TABLE_CLIPPED) : "");
        gfx_text_in(c, (gfx_rect_t){ PLOT_X, LEG_Y, PLOT_W, 20 }, src,
                    MO_FONT_LABEL,
                    ui_theme_color(s.bench.clipped ? UI_C_WARN
                                                   : UI_C_TEXT_DIM),
                    1, GFX_ALIGN_RIGHT);
    }
    ui_rule(c, PLOT_X, LEG_Y + 24, PLOT_W, ui_theme_color(UI_C_EDGE));

    for (int i = 0; i < S_COUNT; ++i) {
        const int y = TABLE_ROW_Y(i);
        /* A field nothing answered for is empty, as on the rail. */
        const bool ok = s.bench.valid
                        && (s.bench.flags & mo_ok[i]) == mo_ok[i];
        char v[24], p[24];
        ui_fmt(v, sizeof(v), ok ? now[i] : NAN, mo_series[i].decimals);
        ui_fmt(p, sizeof(p), s.pk_ok[i] ? pk[i] : NAN,
               mo_series[i].decimals);
        gfx_text(c, PLOT_X, y, ui_tr(rows[i]), MO_FONT_LABEL,
                 s.plot.series[i].color, 1);
        gfx_text(c, PLOT_X + TBL_VAL_DX, y, v, MO_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + TBL_UNIT_DX, y, mo_series[i].unit,
                 MO_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[40];
        snprintf(line, sizeof(line), "%s %s", ui_tr(pk_label[i]), p);
        gfx_text(c, PLOT_X + TBL_PK_DX, y, line, MO_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    if (sensed) {
        draw_esc_says(c);
    }
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
 * @return their left edge, px
 */
static int num_text(gfx_canvas_t *c, int right, int y, int w, const char *t,
                    gfx_color_t col)
{
    const gfx_font_t *f = MO_FONT_NUM;
    int tw = gfx_text_width(f, t, 1);
    int dy = 0;
    if (tw > w) {
        f = MO_FONT_HEAD;
        tw = gfx_text_width(f, t, 1);
        dy = 4;
    }
    gfx_text(c, right - tw, y + dy, t, f, col, 1);
    return right - tw;
}

/**
 * @brief One card of the rail, HERO_H px high: the name and the extreme
 *        stacked on the left, the live value in the 32 x 44 numerals,
 *        right-aligned with its unit.
 * @param c the canvas
 * @param r the card
 * @param def the channel's name, unit, colour and decimals
 * @param value the live value, NAN for none
 * @param peak the extreme, NAN for none
 */
static void draw_rail_card(gfx_canvas_t *c, gfx_rect_t r,
                           const ui_hero_def_t *def, float value, float peak)
{
    ui_card(c, r, ui_theme_color(UI_C_PANEL));
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, def->color);

    gfx_fill_round_rect(c, r.x + 10, r.y + 9, 4, 14, 1, def->color);
    gfx_text(c, r.x + 20, r.y + 6, def->label, MO_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);

    if (isfinite(peak)) {
        char pk[24], line[32];
        ui_fmt(pk, sizeof(pk), peak, def->decimals);
        snprintf(line, sizeof(line), "%s %s",
                 (def->extreme_label != NULL) ? def->extreme_label
                                              : TR(HERO_PEAK), pk);
        gfx_text(c, r.x + 10, r.y + 30, line, MO_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }

    char number[24];
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, def->decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    /* The unit at the card's right, the numerals right of the name and the
     * extreme: those are 12 cells at most ("max 120000"). */
    const int uw = gfx_text_width(MO_FONT_LABEL, def->unit, 1);
    const int ux = r.x + r.w - 10 - uw;
    gfx_text(c, ux, r.y + 30, def->unit, MO_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    (void)num_text(c, ux - 6, r.y + 6, ux - 6 - (r.x + 140), number,
                   def->color);
}

/* The rail: one card per channel, live value over its extreme. */
static void draw_heroes(gfx_canvas_t *c)
{
    const float now[S_COUNT] = { s.bench.voltage, s.bench.current,
                                 s.bench.power, s.bench.rpm };
    const float pk[S_COUNT]  = { s.bench.voltage_min, s.bench.current_max,
                                 s.bench.power_max, s.bench.rpm_max };
    for (int i = 0; i < S_COUNT; ++i) {
        const gfx_rect_t r = { RIGHT_X,
                               (int16_t)(HERO_Y0 + i * (HERO_H + HERO_GAP)),
                               RIGHT_W, HERO_H };
        gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
        const ui_hero_def_t def = { ui_tr(mo_names[i]), mo_series[i].unit,
                                    s.plot.series[i].color,
                                    mo_series[i].decimals,
                                    ui_tr(k_extreme[i]) };
        const bool ok = s.bench.valid
                        && (s.bench.flags & mo_ok[i]) == mo_ok[i];
        draw_rail_card(c, r, &def, ok ? now[i] : NAN,
                       s.pk_ok[i] ? pk[i] : NAN);
    }
}

/* What the run has taken out of the pack, on the CONTROL card's title
 * line, right of its tag: STEUERUNG's 9 cells of 8px and the tab's 26. */
static void draw_totals(gfx_canvas_t *c)
{
    const gfx_rect_t box = { (int16_t)(RIGHT_X + 110), (int16_t)(CTL_Y + 1),
                             (int16_t)(RIGHT_W - 110 - 12), 20 };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h, ui_theme_color(UI_C_PANEL));
    /* Each total only when the panel has counted it: a run with no current
     * to count has no consumption, and 0mAh reads as a measurement. */
    char mah[16];
    char wh[16];
    if ((s.bench.counted & BENCH_COUNTED_CHARGE) != 0u) {
        snprintf(mah, sizeof(mah), "%.0f", (double)s.bench.charge_mah);
    } else {
        snprintf(mah, sizeof(mah), "--");
    }
    if ((s.bench.counted & BENCH_COUNTED_ENERGY) != 0u) {
        snprintf(wh, sizeof(wh), "%.2f", (double)s.bench.energy_wh);
    } else {
        snprintf(wh, sizeof(wh), "--");
    }
    char line[48];
    snprintf(line, sizeof(line), "%smAh   %sWh", mah, wh);
    gfx_text_in(c, box, line, MO_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

/*
 * What the panel above the plot is called.
 *
 * The tag names what the panel is showing, so the TABLE pane reads
 * LIVE TELEMETRY whatever the plot is doing: that pane repaints on every
 * sample and is live on a disarmed bench.  Each is drawn padded to the
 * longest of the three in the language showing, so ui_panel_header() lays
 * out the same tab for each and one repaints over another in place.
 */
enum { TAG_LIVE = 0, TAG_HELD, TAG_IDLE, TAG_COUNT };
static const ui_text_id_t k_tag[TAG_COUNT] = {
    TX_MO_TAG_LIVE, TX_MO_TAG_HELD, TX_MO_TAG_IDLE,
};

static const char *tag_text(uint8_t tag, char *buf, size_t n)
{
    return ui_tr_pad(k_tag, TAG_COUNT, tag, buf, n);
}

static uint8_t telemetry_tag(void)
{
    if (s.tabs.selected != MOTOR_PANE_PLOT || s.plot.running) {
        return (uint8_t)TAG_LIVE;
    }
    return (uint8_t)((s.plot.filled > 0) ? TAG_HELD : TAG_IDLE);
}

/**
 * @brief LIVE's throttle card: the readout on the title line, the step keys
 *        and the track.  A throttle that moved on its own clears the
 *        readout's own box and the slider's painted region, not the card:
 *        a drag asks for it on every frame.  The slider's region is what
 *        the widget reports rather than its track, because the thumb and
 *        its shadow stand proud of the track.
 * @param c the canvas
 * @param ctrl_moved whether a control changed its look: the card under
 *        the title line is cleared whole
 */
static void draw_throttle(gfx_canvas_t *c, bool ctrl_moved)
{
    gfx_fill_rect(c, READOUT_X, ROW_Y, READOUT_W, READOUT_H,
                  ui_theme_color(UI_C_PANEL));
    if (ctrl_moved) {
        /*
         * Inside the chamfer, not inside the border.  ui_panel cuts the
         * bottom-right corner by UI_CHAMFER and draws it once per
         * framebuffer, so a clear that reached the corner would square it off
         * for the life of that buffer with nothing to repaint it.
         */
        gfx_fill_rect(c, LEFT_X + 1, LO_Y + TITLE_H, LEFT_W - 2,
                      LO_H - TITLE_H - 1 - UI_CHAMFER,
                      ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, LEFT_X + 1, LO_Y + LO_H - 1 - UI_CHAMFER,
                      LEFT_W - 2 - UI_CHAMFER, UI_CHAMFER,
                      ui_theme_color(UI_C_PANEL));
    } else {
        const gfx_rect_t sl = ui_slider_painted_rect(&s.slider);
        gfx_fill_rect(c, sl.x, sl.y, sl.w, sl.h, ui_theme_color(UI_C_PANEL));
    }

    /* The throttle in the numerals the readings use: it is a reading too,
     * and the one the operator's hand is on.  The per cent sign in the
     * label face beside it, as the readings' units are. */
    char pct[16];
    snprintf(pct, sizeof(pct), "%.1f", (double)s.slider.value);
    const int ux = READOUT_X + READOUT_W - 12;
    gfx_text(c, ux, ROW_Y + 24, "%", MO_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    (void)num_text(c, ux - 4, ROW_Y, READOUT_W - 20, pct,
                   ui_theme_color(UI_C_ACCENT));

    /* Dimmed while the value is not live, as a button that takes no press
     * is: the track and its step keys take none. */
    ui_button_font(c, s.down_rect, "-1", ui_theme_color(UI_C_PANEL_SUNK),
                   s.pressed == 3, mo_value_live(), MO_FONT_LABEL);
    s.slider.dim = !mo_value_live();
    ui_slider_render(&s.slider, c);
    ui_button_font(c, s.up_rect, "+1", ui_theme_color(UI_C_PANEL_SUNK),
                   s.pressed == 4, mo_value_live(), MO_FONT_LABEL);
}

/*
 * A session on PROGRAM, full width: the output strip in the strip row, on
 * its text, and the session under it, on its revision.
 */
__attribute__((noinline))
static void render_session(gfx_canvas_t *c, unsigned bit, int buf,
                           const prog_page_set_t *ps)
{
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        s.drawn_mask |= bit;
        s.drawn_sess[buf]   = UINT32_MAX;
        s.drawn_ostrip[buf] = UINT32_MAX;
    }
    const uint32_t sig = mpp_strip_signature(s.thr_proto, s.thr_pins,
                                             s.thr_pins_known, s.gate.armed);
    if (s.drawn_ostrip[buf] != sig) {
        s.drawn_ostrip[buf] = sig;
        mpp_render_strip(c, s.thr_proto, s.thr_pins, s.thr_pins_known,
                         s.gate.armed);
    }
    const uint32_t rev = prog_ps_revision(ps);
    if (s.drawn_sess[buf] != rev) {
        s.drawn_sess[buf] = rev;
        prog_ps_render(ps, c);
    }
}

static void render_body(gfx_canvas_t *c, unsigned bit, int buf);

/*
 * The session's way and the body's are two functions: the body's frame,
 * 416 bytes on the panel, is not on the stack under a session's page
 * drawers (tools/stack_check.py).
 */
void mo_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;
    mo_bind_colours();

    /* A session opening or closing repaints the buffer whole. */
    const prog_page_set_t *ps = mpp_shown();
    if ((ps != NULL) != s.drawn_sess_shown[buf]) {
        s.drawn_sess_shown[buf] = ps != NULL;
        s.drawn_mask &= ~bit;
        s.drawn_push[buf]  = UINT32_MAX;
        s.drawn_ctrl[buf]  = UINT32_MAX;
        s.drawn_thr[buf]   = UINT32_MAX;
        s.drawn_arm[buf]   = UINT32_MAX;
        s.drawn_plot[buf]  = UINT32_MAX;
        s.drawn_title[buf] = 0xFFu;
    }
    if (ps != NULL) {
        render_session(c, bit, buf, ps);
    } else {
        render_body(c, bit, buf);
    }
}

__attribute__((noinline))
static void render_body(gfx_canvas_t *c, unsigned bit, int buf)
{
    const bool on_live = mwb_task() == UI_WB_LIVE;

    /* Chrome per framebuffer, not per frame; the rest only when its counter
     * moves.  tools/frame_cost.py measures the two cases as the `chrome` and
     * `frame-idle` modes. */
    const uint8_t tag = telemetry_tag();
    char tag_buf[48];
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        if (on_live) {
            ui_panel(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H },
                     tag_text(tag, tag_buf, sizeof(tag_buf)),
                     ui_theme_color(UI_C_ACCENT));
            ui_panel(c, (gfx_rect_t){ LEFT_X, LO_Y, LEFT_W, LO_H },
                     TR(MO_THROTTLE), ui_theme_color(UI_C_ACCENT));
        }
        ui_panel(c, (gfx_rect_t){ RIGHT_X, CTL_Y, RIGHT_W, CTL_H },
                 TR(MO_CONTROL), ui_theme_color(UI_C_ACCENT));
        s.drawn_mask |= bit;
        s.drawn_title[buf] = tag;
        s.drawn_strip[buf] = UINT32_MAX;
        s.drawn_page[buf]  = UINT32_MAX;
    }

    /* The strip, unless SETTINGS' keypad covers it. */
    const bool strip_covered = !on_live && msp_modal();
    if (!strip_covered && s.drawn_strip[buf] != mwb_revision()) {
        s.drawn_strip[buf] = mwb_revision();
        mwb_render_strip(c);
    }

    /* Outside the pane branch, so an arm or a disarm repaints the tag on
     * whichever pane is up.  All three strings are the same length, so this
     * redraws the header's tab over itself without a clear. */
    if (on_live && s.drawn_title[buf] != tag) {
        s.drawn_title[buf] = tag;
        ui_panel_header(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H },
                        tag_text(tag, tag_buf, sizeof(tag_buf)),
                        ui_theme_color(UI_C_ACCENT));
    }

    if (s.drawn_ctrl[buf] != s.ctrl_rev
        || s.drawn_push[buf] != s.plot.pushes) {
        if (on_live) {
            ui_tabs_render(&s.tabs, c);
        }
        draw_header(c, on_live);
    }

    /*
     * Everything that comes from the numbers is painted only when there are
     * new numbers.  The panel refreshes at 39Hz and samples arrive at 20Hz,
     * so about half of all frames would otherwise repaint an identical plot
     * and identical readouts into the PSRAM (pseudo-static random-access
     * memory) the LCD (liquid-crystal display) is scanning out of.
     *
     * Per framebuffer, because the panel alternates between two: a buffer
     * whose last paint was two samples ago still needs one even when the
     * other is current.
     */
    /*
     * The plot on its own gate.  A stopped plot takes no samples, so its
     * revision stands still while the readouts beside it go on moving --
     * which is most of the time, since a bench spends longer between runs
     * than in them.
     */
    const bool data_moved = (s.drawn_push[buf] != s.plot.pushes);
    if (mwb_task() == UI_WB_PROGRAM) {
        /* PROGRAM's list, on its own counter. */
        if (s.drawn_page[buf] != mpp_revision()) {
            s.drawn_page[buf] = mpp_revision();
            mpp_render_page(c);
        }
    } else if (!on_live) {
        /* SETTINGS' page, on its own counter: a press, the keypad, the
         * store's state. */
        if (s.drawn_page[buf] != msp_revision()) {
            s.drawn_page[buf] = msp_revision();
            msp_render(c, mo_endpoints_free());
            /* The keypad covers the strip's row; with the page shown the
             * row is cleared and the strip drawn again. */
            s.drawn_strip[buf] = UINT32_MAX;
            if (!msp_modal()) {
                gfx_fill_rect(c, PAGE_X, 3, PAGE_W, PAGE_Y - 3,
                              ui_theme_color(UI_C_BG));
                s.drawn_strip[buf] = mwb_revision();
                mwb_render_strip(c);
            }
        }
    } else if (s.tabs.selected == MOTOR_PANE_PLOT) {
        if (s.drawn_plot[buf] != s.plot.revision) {
            s.drawn_plot[buf] = s.plot.revision;
            /* Cleared first: the legend prints each channel's full scale,
             * that scale autoranges, and a shorter number drawn over a
             * longer one leaves the tail of the old one behind. */
            gfx_fill_rect(c, PLOT_X, LEG_Y, PLOT_W, LEG_H,
                          ui_theme_color(UI_C_PANEL));
            ui_plot_render_legend(&s.plot, c,
                                  (gfx_rect_t){ PLOT_X, LEG_Y, PLOT_W, LEG_H });
            ui_plot_render(&s.plot, c,
                           (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H });
            /* An empty stopped plot is not a plot of zeroes.  The widget has
             * no idea what a run is, so the sentence belongs here. */
            if (!s.plot.running && s.plot.filled == 0) {
                gfx_text_in(c, (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H },
                            TR(MO_NO_RUN), MO_FONT_LABEL,
                            ui_theme_color(UI_C_TEXT_FAINT), 1,
                            GFX_ALIGN_CENTER);
            }
        }
    } else if (data_moved) {
        /* From the legend's line down: PLOT | TABLE and the rate over
         * it are drawn on their own counters. */
        gfx_fill_rect(c, LEFT_X + 1, LEG_Y, LEFT_W - 2,
                      UP_Y + UP_H - 1 - UI_CHAMFER - LEG_Y,
                      ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, LEFT_X + 1, UP_Y + UP_H - 1 - UI_CHAMFER,
                      LEFT_W - 2 - UI_CHAMFER, UI_CHAMFER,
                      ui_theme_color(UI_C_PANEL));
        draw_table(c);
    }
    if (data_moved) {
        s.drawn_push[buf] = s.plot.pushes;
        if (on_live) {
            draw_derived(c);
        }
        draw_heroes(c);
        draw_totals(c);
    }

    /*
     * The controls change when somebody touches them, not when a sample
     * arrives, so they get their own revision counter.  On a frame where
     * neither the numbers nor a control moved, this screen paints nothing,
     * which on a panel whose refresh outruns its sample rate is most frames.
     */
    const bool ctrl_moved = (s.drawn_ctrl[buf] != s.ctrl_rev);
    const bool thr_moved  = (s.drawn_thr[buf] != s.thr_rev);
    const bool arm_moved  = (s.drawn_arm[buf] != s.arm_rev);
    if (!ctrl_moved && !thr_moved && !arm_moved) {
        return;
    }
    s.drawn_ctrl[buf] = s.ctrl_rev;
    s.drawn_thr[buf]  = s.thr_rev;
    s.drawn_arm[buf]  = s.arm_rev;

    /* ARM animating on its own repaints its own button and nothing else. */
    if (arm_moved && !ctrl_moved && !thr_moved) {
        draw_arm(c);
        return;
    }

    if (on_live) {
        draw_throttle(c, ctrl_moved);
    }
    if (!ctrl_moved && !arm_moved) {
        return;
    }
    draw_arm(c);
    if (!ctrl_moved) {
        return;
    }
    /* How the throttle is moved; disarmed, why it is not, in the words
     * SERVO's dial uses. */
    if (on_live && mo_value_live()) {
        gfx_text(c, HINT_X, HINT_Y + 2, TR(MO_HINT), MO_FONT_FINE,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    } else if (on_live) {
        gfx_text(c, HINT_X, HINT_Y,
                 ui_servo_str(SERVO_STR_START_NOT_ARMED), MO_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
    draw_totals(c);
    ui_button_font(c, s.reset_rect, TR(MO_RESET_PEAKS),
                   ui_theme_color(UI_C_PANEL_SUNK), s.pressed == 2, true,
                   MO_FONT_LABEL);
    /* What the throttle drives, on every task: `protocol pins`, in the
     * warning colour while no pin is bound or the binding is not read. */
    char line[32];
    const bool warn = motor_throttle_target(line, sizeof(line), s.thr_proto,
                                            s.thr_pins, s.thr_pins_known,
                                            motor_target_cells());
    mwb_render_target(c, line, warn, mo_target_locked());
}
