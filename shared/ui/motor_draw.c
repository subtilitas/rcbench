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

/*
 * The line under the strip: on LIVE, the poll rate and the link's errors on
 * the left of it, after PLOT and TABLE; on every task, how hot the three
 * things that get hot, on the right, over the rail.  MCU is the panel's own
 * die, not the IOmcu's.
 */
static void draw_header(gfx_canvas_t *c, bool on_live)
{
    const ui_bench_status_t *st = ui_router_status();
    const int x0 = on_live ? LEFT_X + TAB_W + 10 : RIGHT_X;
    gfx_fill_rect(c, x0, HDR_Y, W - PAD - x0, HDR_H,
                  ui_theme_color(UI_C_BG));

    /* The band already carries the output mode, so this strip does not
     * repeat it: what it adds is the poll rate and the link's error count,
     * and while the IOmcu has refused a frame, how many. */
    if (on_live) {
        char line[64];
        snprintf(line, sizeof(line), TR(MO_RATE_ERR), (int)SAMPLE_HZ,
                 (unsigned long)st->link_errors);
        gfx_text(c, x0, HDR_Y + 1, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        /* At its widest, a 10-digit count after a 3-digit error count in
         * German, the label ends at x = 572: left of the temperatures while
         * the panel's own MCU reads under 100 C. */
        char refused[48];
        if (ui_link_refused_label(st, refused, sizeof(refused))) {
            gfx_text(c, x0 + gfx_text_width(&gfx_font_8x16, line, 1) + 16,
                     HDR_Y + 1, refused, &gfx_font_8x16,
                     ui_theme_color(UI_C_TEXT_DIM), 1);
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
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x0, HDR_Y + 1,
                                 (int16_t)(W - PAD - x0), 16 },
                temps, &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1,
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
    /* Right of the panel's tag: its 20 cells and the tab's 26 px. */
    const gfx_rect_t box = { (int16_t)(LEFT_X + 186), UP_Y + 2,
                             (int16_t)(LEFT_W - 192), 16 };
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
        /* Over 100 % means the rated value is wrong, which is worth seeing;
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
    gfx_text_in(c, box, line, &gfx_font_8x16,
                ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_RIGHT);
}

/* The table's rows, 26 px apart from under its heading rule. */
#define TABLE_ROW_Y(i) (LEG_Y + 34 + (i) * 26)
/* The ESC's own figures under the four channels, two rows 22 px apart. */
#define ESC_RULE_Y     (TABLE_ROW_Y(S_COUNT) - 8)
#define ESC_ROW_Y(i)   (ESC_RULE_Y + 8 + (i) * 22)
_Static_assert(ESC_ROW_Y(1) + 16 <= UP_Y + UP_H - INNER,
               "the ESC's second row ends inside the telemetry panel");

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
            gfx_text(c, PLOT_X, y, TR(MO_ESC_SAYS), &gfx_font_8x16,
                     ui_theme_color(UI_C_TEXT_DIM), 1);
        }
        gfx_text(c, PLOT_X + 128, y, v, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + 248, y, mo_series[i].unit, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        snprintf(line, sizeof(line), "%s %s %s", TR(MO_TABLE_DIFF), d,
                 mo_series[i].unit);
        gfx_text(c, PLOT_X + 348, y, line, &gfx_font_8x16,
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

    gfx_text(c, PLOT_X, LEG_Y + 4, TR(MO_TABLE_HEAD),
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    if (sensed) {
        /* Where the numbers come from, and whether its current hit the end
         * of its range: then current and power are bounds. */
        char src[32];
        snprintf(src, sizeof(src), "INA228%s%s", s.bench.clipped ? " " : "",
                 s.bench.clipped ? TR(MO_TABLE_CLIPPED) : "");
        gfx_text_in(c, (gfx_rect_t){ PLOT_X, LEG_Y + 4, PLOT_W, 16 }, src,
                    &gfx_font_8x16,
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
        gfx_text(c, PLOT_X, y, ui_tr(rows[i]), &gfx_font_8x16,
                 s.plot.series[i].color, 1);
        gfx_text(c, PLOT_X + 128, y, v, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + 248, y, mo_series[i].unit, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[40];
        snprintf(line, sizeof(line), "%s %s", ui_tr(pk_label[i]), p);
        gfx_text(c, PLOT_X + 348, y, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    if (sensed) {
        draw_esc_says(c);
    }
}

/*
 * One card of the right rail, 57 px high: the name and the extreme stacked
 * on the left, the live value in a numeral face that fits, right-aligned
 * with its unit.
 */
static void draw_rail_card(gfx_canvas_t *c, gfx_rect_t r,
                           const ui_hero_def_t *def, float value, float peak)
{
    ui_card(c, r, ui_theme_color(UI_C_PANEL));
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, def->color);

    gfx_fill_round_rect(c, r.x + 8, r.y + 7, 3, 10, 1, def->color);
    gfx_text(c, r.x + 16, r.y + 6, def->label, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);

    if (isfinite(peak)) {
        char pk[24], line[32];
        ui_fmt(pk, sizeof(pk), peak, def->decimals);
        snprintf(line, sizeof(line), "%s %s",
                 (def->extreme_label != NULL) ? def->extreme_label
                                              : TR(HERO_PEAK), pk);
        gfx_text(c, r.x + 8, r.y + 26, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }

    char number[24];
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, def->decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    /* Narrower and shorter than the hero face, so four of these stack in the
     * height one hero card wants. */
    const gfx_seg_style_t seg = { .digit_w = 15, .digit_h = 24,
                                  .thickness = 3, .gap = 4, .slant = 2,
                                  .ghost = true };
    const int uw = gfx_text_width(&gfx_font_8x16, def->unit, 1);
    const int nw = gfx_seg_width(number, &seg);
    const int nx = r.x + r.w - 8 - uw - 6 - nw;
    gfx_seg_text(c, nx, r.y + 16, number, &seg, def->color,
                 gfx_lerp(ui_theme_color(UI_C_PANEL), def->color, 34));
    gfx_text(c, nx + nw + 6, r.y + 30, def->unit, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* The right rail: one card per channel, live value over its extreme. */
static void draw_heroes(gfx_canvas_t *c)
{
    const float now[S_COUNT] = { s.bench.voltage, s.bench.current,
                                 s.bench.power, s.bench.rpm };
    const float pk[S_COUNT]  = { s.bench.voltage_min, s.bench.current_max,
                                 s.bench.power_max, s.bench.rpm_max };
    for (int i = 0; i < S_COUNT; ++i) {
        const gfx_rect_t r = { RIGHT_X,
                               (int16_t)(UP_Y + i * (HERO_H + HERO_GAP)),
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

/* What the run has taken out of the pack, under the arming button. */
static void draw_totals(gfx_canvas_t *c)
{
    const gfx_rect_t box = { (int16_t)(RIGHT_X + INNER), TOTALS_Y,
                             (int16_t)(RIGHT_W - 2 * INNER), 16 };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h, ui_theme_color(UI_C_PANEL));
    /* Each total only when the panel has counted it: a run with no current
     * to count has no consumption, and 0 mAh reads as a measurement. */
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
    snprintf(line, sizeof(line), "%s mAh   %s Wh", mah, wh);
    gfx_text_in(c, box, line, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1,
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

/*
 * LIVE's throttle panel: the readout, the step buttons and the track.  A
 * throttle that moved on its own clears the readout's own box and the
 * slider's painted region, not the panel: a drag asks for it on every
 * frame.  The slider's region is what the widget reports rather than its
 * track, because the thumb and its shadow stand proud of the track.
 */
static void draw_throttle(gfx_canvas_t *c, bool ctrl_moved)
{
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
        gfx_fill_rect(c, LEFT_X + INNER, ROW_Y, READOUT_W, READOUT_H,
                      ui_theme_color(UI_C_PANEL));
        const gfx_rect_t sl = ui_slider_painted_rect(&s.slider);
        gfx_fill_rect(c, sl.x, sl.y, sl.w, sl.h, ui_theme_color(UI_C_PANEL));
    }

    /* The throttle in the numeral face the readings use: it is a reading
     * too, and the one the operator's hand is on. */
    char pct[16];
    snprintf(pct, sizeof(pct), "%.1f", (double)s.slider.value);
    const gfx_seg_style_t seg = ui_seg_hero();
    const int pw = gfx_seg_width(pct, &seg);
    const int px = LEFT_X + LEFT_W - INNER - 20 - pw;
    /* The unlit segments are a ghost of the lit ones against whatever they
     * sit on.  That is the panel now, not the screen's background; against
     * the wrong base they read as a dark block behind the digits. */
    gfx_seg_text(c, px, ROW_Y, pct, &seg, ui_theme_color(UI_C_ACCENT),
                 gfx_lerp(ui_theme_color(UI_C_PANEL),
                          ui_theme_color(UI_C_ACCENT), 34));
    gfx_text(c, LEFT_X + LEFT_W - INNER - 14, ROW_Y + 14, "%",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);

    /* Dimmed while the value is not live, as a button that takes no press
     * is: the track and its step buttons take none. */
    ui_button(c, s.down_rect, "-1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == 3, mo_value_live());
    s.slider.dim = !mo_value_live();
    ui_slider_render(&s.slider, c);
    ui_button(c, s.up_rect, "+1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == 4, mo_value_live());
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
        ui_panel(c, (gfx_rect_t){ RIGHT_X, LO_Y, RIGHT_W, LO_H },
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
     * new numbers.  The panel refreshes at 39 Hz and samples arrive at 20 Hz,
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
                            TR(MO_NO_RUN), &gfx_font_8x16,
                            ui_theme_color(UI_C_TEXT_FAINT), 1,
                            GFX_ALIGN_CENTER);
            }
        }
    } else if (data_moved) {
        gfx_fill_rect(c, LEFT_X + 1, UP_Y + TITLE_H, LEFT_W - 2,
                      UP_H - TITLE_H - 1 - UI_CHAMFER,
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
        ui_button(c, s.arm_rect, s.gate.armed ? "DISARM" : "ARM", arm_fill(),
                  s.pressed == 1, !mo_arm_locked());
        return;
    }

    if (on_live) {
        draw_throttle(c, ctrl_moved);
    }
    if (!ctrl_moved && !arm_moved) {
        return;
    }
    ui_button(c, s.arm_rect, s.gate.armed ? "DISARM" : "ARM", arm_fill(),
              s.pressed == 1, !mo_arm_locked());
    if (!ctrl_moved) {
        return;
    }
    /* How the throttle is moved; disarmed, why it is not, in the words
     * SERVO's dial uses. */
    if (on_live && mo_value_live()) {
        gfx_text(c, LEFT_X + INNER, HINT_Y, TR(MO_HINT), &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    } else if (on_live) {
        gfx_text(c, LEFT_X + INNER, HINT_Y,
                 ui_servo_str(SERVO_STR_START_NOT_ARMED), &gfx_font_8x16,
                 ui_theme_color(UI_C_WARN), 1);
    }
    draw_totals(c);
    ui_button(c, s.reset_rect, TR(MO_RESET_PEAKS),
              ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == 2, true);
    /* What the throttle drives, on every task: `protocol pins`, in the
     * warning colour while no pin is bound or the binding is not read. */
    char line[32];
    const bool warn = motor_throttle_target(line, sizeof(line), s.thr_proto,
                                            s.thr_pins, s.thr_pins_known,
                                            motor_target_cells());
    mwb_render_target(c, line, warn, mo_target_locked());
}
