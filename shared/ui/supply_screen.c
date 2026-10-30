/**
 * @file supply_screen.c
 * @brief The SUPPLY screen.  See supply_screen.h.  This file holds the
 *        state, the commands and the screen's interface; supply_events.c
 *        takes the touch events and frames, supply_draw.c draws, and
 *        supply_screen_int.h is what the three share.
 *
 * MOTOR & ESC's frame and its rules for what repaints when: chrome once per
 * framebuffer, the numbers when a sample arrives, the plot when it moves,
 * and each control on its own counter.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdatomic.h>

#include "supply_screen.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "settings.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#include "supply_screen_int.h"

const ui_plot_series_t sup_series[S_COUNT] = {
    { "VOLT", "V", 0, 2, 5.0f,  0 },
    { "CURR", "A", 0, 2, 1.0f,  0 },
    { "PWR",  "W", 0, 1, 10.0f, 0 },
    { "VSET", "V", 0, 2, 5.0f,  1 + S_VOLT },
    { "ISET", "A", 0, 2, 1.0f,  1 + S_CURR },
};

static const ui_text_id_t k_tab_labels[] = { TX_MO_TAB_PLOT,
                                              TX_MO_TAB_TABLE };
/* The screen's state; supply_screen_int.h names it s. */
sup_state_t sup_s;

static void forget_drawn(void)
{
    s.drawn_mask = 0;
    for (int b = 0; b < 2; ++b) {
        s.drawn_push[b]  = UINT32_MAX;
        s.drawn_plot[b]  = UINT32_MAX;
        s.drawn_ctrl[b]  = UINT32_MAX;
        s.drawn_set[b]   = UINT32_MAX;
        s.drawn_out[b]   = UINT32_MAX;
        s.drawn_ov[b]    = UINT32_MAX;
        s.drawn_kp[b]    = UINT32_MAX;
        s.drawn_strip[b] = UINT32_MAX;
        s.drawn_p5[b]    = UINT32_MAX;
        s.drawn_title[b] = 0xFFu;
        s.drawn_save[b]  = 0xFFu;
    }
}

void supply_invalidate(void)
{
    forget_drawn();
}

void sup_bind_colours(void)
{
    s.plot.series[S_VOLT].color  = ui_theme_color(UI_C_VOLT);
    s.plot.series[S_CURR].color  = ui_theme_color(UI_C_CURR);
    s.plot.series[S_POWER].color = ui_theme_color(UI_C_POWER);
    s.plot.series[S_VSET].color  = ui_theme_color(UI_C_VOLT);
    s.plot.series[S_ISET].color  = ui_theme_color(UI_C_CURR);
    /* The names follow the language as the colours follow the theme. */
    s.plot.series[S_VOLT].name  = TR(MO_VOLT);
    s.plot.series[S_CURR].name  = TR(MO_CURR);
    s.plot.series[S_POWER].name = TR(MO_PWR);
    s.v_slider.color = ui_theme_color(UI_C_VOLT);
    s.i_slider.color = ui_theme_color(UI_C_CURR);
}

/**
 * @brief The top of set point @p row's controls.
 * @param row 0 the voltage, 1 the current limit
 * @return their top, px
 */
static int16_t set_ctrl_y(int row)
{
    return (int16_t)(SET_Y0 + row * SET_ROW_H + SET_CTRL_DY);
}

/**
 * @brief Set point @p row's track, between its two step keys.
 * @param row 0 the voltage, 1 the current limit
 * @return the track
 */
static gfx_rect_t set_track(int row)
{
    return (gfx_rect_t){ SET_TRACK_X, set_ctrl_y(row), SET_TRACK_W,
                         SET_CTRL_H };
}

/**
 * @brief Set point @p row's fine step key, down at the track's left end or
 *        up at its right.
 * @param row 0 the voltage, 1 the current limit
 * @param up the step up
 * @return the key
 */
static gfx_rect_t nudge_rect(int row, bool up)
{
    return (gfx_rect_t){ (int16_t)(up ? SET_UP_X : LEFT_X + INNER),
                         set_ctrl_y(row), NUDGE_W, SET_CTRL_H };
}

/**
 * @brief A set point's value, at the right end of its controls: tapped, the
 *        keypad.
 * @param row 0 the voltage, 1 the current limit
 * @return the value's box
 */
gfx_rect_t sup_set_value_rect(int row)
{
    return (gfx_rect_t){ SET_VAL_X, set_ctrl_y(row), SET_VAL_W,
                         SET_CTRL_H };
}

gfx_rect_t sup_card_rect(int i)
{
    return (gfx_rect_t){ RIGHT_X, (int16_t)(RAIL_Y + i * (CARD_H + CARD_GAP)),
                         RIGHT_W, CARD_H };
}

/**
 * @brief The strip's row with its trough's 3px around it.
 * @return the row, px
 */
gfx_rect_t sup_strip_area(void)
{
    const gfx_rect_t r = ui_wb_strip_rect(&s.wb);
    return (gfx_rect_t){ (int16_t)(r.x - 3), (int16_t)(r.y - 3),
                         (int16_t)(r.w + 6), (int16_t)(r.h + 6) };
}

/**
 * @brief The strip's tabs laid out again at SUPPLY's sizes, SU_TAB_W x
 *        SU_STRIP_H from (SU_STRIP_X, SU_STRIP_Y): the tasks, their order
 *        and their labels as ui_wb_strip_init() set them.
 */
static void strip_widen(void)
{
    ui_tabs_t *t = &s.wb.tabs;
    const int n = t->count;
    ui_text_id_t ids[UI_TABS_MAX] = { (ui_text_id_t)0 };
    for (int i = 0; i < n; ++i) {
        ids[i] = (ui_text_id_t)t->text[i];
    }
    ui_tabs_init_text(t, ids, n,
                      (gfx_rect_t){ SU_STRIP_X, SU_STRIP_Y,
                                    (int16_t)(n * SU_TAB_W
                                              + (n - 1) * SU_TAB_GAP),
                                    SU_STRIP_H });
}

bool sup_on_live(void)
{
    return ui_wb_strip_task(&s.wb) == UI_WB_LIVE;
}

bool sup_p5_shown(void)
{
    return sup_on_live() && !s.kp.open && !s.confirm_open
           && prog5v_box_shown();
}

gfx_rect_t sup_overlay_area(void)
{
    return (gfx_rect_t){ OV_X, OV_Y, OV_W, OV_H };
}

/**
 * @brief SETTINGS' row @p i: its label and its value, in its column.
 * @param i the row, an index of k_rows
 * @return the row, px
 */
gfx_rect_t sup_row_rect(int i)
{
    const bool left = (k_rows[i].col == 0);
    return (gfx_rect_t){ (int16_t)(left ? OV_COL0_X : OV_COL1_X),
                         (int16_t)(OV_Y + k_rows[i].y),
                         (int16_t)(left ? OV_COL_W : OV_COL1_W), OV_ROW_H };
}

/**
 * @brief The value of SETTINGS' row @p i, at the row's right end: tapped,
 *        the keypad or a switch's flip.
 * @param i the row, an index of k_rows
 * @return the value's box, px
 */
gfx_rect_t sup_row_value_rect(int i)
{
    const gfx_rect_t r = sup_row_rect(i);
    return (gfx_rect_t){ (int16_t)(r.x + r.w - OV_VAL_W), r.y, OV_VAL_W,
                         OV_ROW_H };
}

/* The sliders take the effective caps' ranges and steps; their values are
 * kept and snapped into them. */
static void apply_caps(void)
{
    /* Held by a programming port, the set points are the port's: they are
     * brought under the caps when it lets go. */
    if (!s.port_held) {
        s.cv = supply_snap(s.cv, s.eff.v_min, s.eff.v_max, s.eff.v_step);
        s.ci = supply_snap(s.ci, s.eff.i_min, s.eff.i_max, s.eff.i_step);
    }
    s.pend_v = supply_snap(s.pend_v, s.eff.v_min, s.eff.v_max, s.eff.v_step);
    s.pend_i = supply_snap(s.pend_i, s.eff.i_min, s.eff.i_max, s.eff.i_step);
    const float v = s.cv;
    const float i = s.ci;
    ui_slider_init(&s.v_slider, set_track(0), s.eff.v_min, s.eff.v_max,
                   ui_theme_color(UI_C_VOLT));
    ui_slider_init(&s.i_slider, set_track(1), s.eff.i_min, s.eff.i_max,
                   ui_theme_color(UI_C_CURR));
    /* A set point commands nothing until the output is on, and then only a
     * supply's own regulation: a tap that sets the value under it is safe
     * here where it is not on a throttle. */
    ui_slider_set_tap_to_set(&s.v_slider, true);
    ui_slider_set_tap_to_set(&s.i_slider, true);
    ui_slider_set_ticks(&s.v_slider, 4);
    ui_slider_set_ticks(&s.i_slider, 4);
    ui_slider_set(&s.v_slider, supply_snap(v, s.eff.v_min, s.eff.v_max,
                                           s.eff.v_step));
    ui_slider_set(&s.i_slider, supply_snap(i, s.eff.i_min, s.eff.i_max,
                                           s.eff.i_step));
    s.v_slider.dim = s.port_held;
    s.i_slider.dim = s.port_held;
}

static supply_limits_t limits_from_settings(void)
{
    const supply_limits_t l = {
        settings_get(SET_SUPPLY_V_MAX),
        settings_get(SET_SUPPLY_I_MAX),
        settings_get(SET_SUPPLY_TRIP_I),
        settings_get(SET_SUPPLY_TRIP_V),
        settings_get(SET_SUPPLY_TRIP_MS) / 1000.0f,
    };
    return l;
}

/* The limits as the settings hold them now, applied to the sliders. */
void sup_refresh_limits(void)
{
    s.lim = limits_from_settings();
    s.eff = supply_caps_limited(&s.caps, &s.lim);
    /* A start value over a cap that was lowered under it comes down with
     * it, so the overlay never shows one the restart would not use -- and
     * is kept, or a pair read back that way (a save that reached one key
     * and not the other) would be corrected again at every restart. */
    bool corrected = false;
    if (settings_get(SET_SUPPLY_V_START) > s.eff.v_max) {
        settings_set(SET_SUPPLY_V_START, s.eff.v_max);
        corrected = true;
    }
    if (settings_get(SET_SUPPLY_I_START) > s.eff.i_max) {
        settings_set(SET_SUPPLY_I_START, s.eff.i_max);
        corrected = true;
    }
    if (corrected) {
        settings_request_save();
    }
    apply_caps();
    ++s.set_rev;
    ++s.ctrl_rev;
    ++s.ov_rev;
}

/** @brief The screen at its start: the state cleared, every control laid
 *         out, the model's set points. */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
    forget_drawn();
    prog5v_box_init(&s.p5, (gfx_rect_t){ P5_X, P5_Y, P5_W, PROG5V_BOX_H },
                    PROG5V_BY_SUPPLY);
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    s.caps = caps;
    /* Nothing limited and no trip until the settings are read, which is
     * the settings' own default. */
    s.lim = (supply_limits_t){ caps.v_max, caps.i_max, 0.0f, 0.0f, 0.1f };
    s.eff = supply_caps_limited(&s.caps, &s.lim);
    ui_plot_init(&s.plot, sup_series, S_COUNT, (float)PLOT_W / SAMPLE_HZ);
    ui_plot_set_running(&s.plot, false);
    ui_wb_strip_init(&s.wb, TASKS, SU_STRIP_X, SU_STRIP_Y);
    strip_widen();
    ui_tabs_init_text(&s.tabs, k_tab_labels, SUPPLY_PANE_COUNT,
                      (gfx_rect_t){ TAB_X, TAB_Y, TAB_W, TAB_H });
    /* Where the panel's model starts, so a screen and its model agree before
     * anything has been set. */
    s.cv = 6.0f;
    s.ci = 2.0f;
    apply_caps();
    s.v_down = nudge_rect(0, false);
    s.v_up   = nudge_rect(0, true);
    s.i_down = nudge_rect(1, false);
    s.i_up   = nudge_rect(1, true);
    s.out_rect   = (gfx_rect_t){ RC_X, OUT_Y, RC_W, OUT_H };
    s.reset_rect = (gfx_rect_t){ RC_X, RESET_Y, RC_W, BTN_H };
    s.modreset_btn = (gfx_rect_t){ OV_COL0_X, (int16_t)(OV_Y + OV_MODRESET_DY),
                                   OV_COL_W, OV_ROW_H };
    /* The question's buttons stand clear of its last lines, which end 4px
     * above the alert band. */
    s.apply_btn   = (gfx_rect_t){ (int16_t)(OV_X + 40), ASK_BTN_Y,
                                  ASK_BTN_W, ASK_BTN_H };
    s.discard_btn = (gfx_rect_t){ (int16_t)(OV_X + OV_W - 40 - ASK_BTN_W),
                                  ASK_BTN_Y, ASK_BTN_W, ASK_BTN_H };
    s.pressed = P_NONE;
    s.kp.pressed = -1;
    sup_bind_colours();
}

/* ---------------------------------------------------------------- commands */

/*
 * The output's owner goes with the output: once it is off and no ON is on
 * its way, nobody owns it and no OFF is owed.
 */
void sup_owner_settle(void)
{
    if (!s.on && !s.on_asked && !s.pending.on) {
        s.owner    = SUPPLY_BY_NONE;
        s.off_owed = false;
    }
}

void sup_post_on(supply_by_t by)
{
    /* An OFF already asked for stands: two touches in one frame must not
     * let an ON land on top of it.  And none while a programming port
     * holds the supply. */
    if (!s.pending.off && !s.port_held) {
        s.pending.on = true;
        s.on_asked   = true;
        s.owner      = by;
        s.off_owed   = false;
    }
}

void sup_post_off(void)
{
    s.pending.off = true;
    s.pending.on  = false;
    ++s.off_asks;
    s.on_asked    = false;
    s.off_owed    = false;
    /* Nor is it the live output a port left: no set points are offered
     * back onto it (supply_screen_restore_offer()). */
    s.offer       = false;
    /* The OFF is asked: an output still reported on is nobody's to switch
     * off again on a leave. */
    s.owner       = SUPPLY_BY_NONE;
}

bool supply_screen_poll_cmd(supply_cmd_t *out)
{
    const bool any = s.pending.on || s.pending.off || s.pending.reset
                     || s.pending.module_reset;
    if (!any) {
        return false;
    }
    if (out != NULL) {
        *out = s.pending;
    }
    memset(&s.pending, 0, sizeof(s.pending));
    return true;
}

/* -------------------------------------------------------------- from outside */

void supply_screen_push(const supply_state_t *st)
{
    if (st == NULL) {
        return;
    }
    s.sup = *st;
    /* Only what arrived: a reading that did not is not drawn as the last
     * one that did, which would hide the gap.  The plot draws it at 0. */
    const bool v_ok = st->online && (st->ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = st->online && (st->ok & SUPPLY_OK_CURRENT) != 0u;
    const float v[S_COUNT] = {
        v_ok ? st->v : NAN,
        i_ok ? st->i : NAN,
        (v_ok && i_ok) ? st->p : NAN,
        st->online ? st->set_v : s.cv,
        st->online ? st->set_i : s.ci,
    };
    ui_plot_push(&s.plot, v);
    ui_plot_update_scales(&s.plot, PLOT_W);
}

const supply_state_t *supply_screen_reading(void)
{
    return &s.sup;
}

/* The press is over, whatever it was on. */
void sup_let_go(void)
{
    if (s.have_press || s.pressed != P_NONE) {
        s.have_press = false;
        s.pressed    = P_NONE;
        ++s.ctrl_rev;
        ++s.ov_rev;
    }
}

void supply_screen_set_output(bool on)
{
    if (s.out_reported != on) {
        s.out_reported = on;
        if (on) {
            /* A run's trace is that run's, kept after it until the next. */
            ui_plot_clear(&s.plot);
        }
        ui_plot_set_running(&s.plot, on);
    }
    if (on) {
        s.on_asked = false;   /* seen applied */
    }
    if (s.on != on) {
        s.on = on;
        if (!on) {
            ++s.off_count;
        }
        if (on) {
            ui_hold_reached(&s.hold);
        } else if (ui_hold_left(&s.hold) && s.pressed == P_OUTPUT) {
            sup_let_go();
        }
        if (!on && s.confirm_open) {
            /* The question was about a live output and there is none: it
             * would go on saying OUTPUT IS ON beside a switch that says off.
             * The change it held is dropped, unanswered. */
            s.confirm_open = false;
            ++s.set_rev;
            supply_invalidate();
        }
        ++s.out_rev;
        ++s.ctrl_rev;
    }
    sup_owner_settle();
}

void supply_screen_set_caps(const supply_caps_t *caps)
{
    if (caps == NULL) {
        return;
    }
    s.caps = *caps;
    s.eff  = supply_caps_limited(&s.caps, &s.lim);
    apply_caps();
    ++s.set_rev;
    ++s.ctrl_rev;
    ++s.ov_rev;      /* a question open shows the change as snapped now */
}

void supply_screen_set_baud(uint32_t baud)
{
    if (s.baud != baud) {
        s.baud = baud;
        ++s.ctrl_rev;                   /* the header says it */
    }
}

void supply_screen_set_pins(int tx, int rx)
{
    /* One pin for both is a wiring the page is written disabled with
     * (supply_link.c): no pins, as the header says. */
    const bool none = tx < 0 || rx < 0 || tx == rx;
    const int t1 = none ? 0 : tx + 1;
    const int r1 = none ? 0 : rx + 1;
    if (s.tx1 != t1 || s.rx1 != r1) {
        s.tx1 = t1;
        s.rx1 = r1;
        ++s.ctrl_rev;                   /* the header says it */
    }
}

/** @brief The ports' own supply as the control task last published it:
 *         state << 8 | pin. */
static atomic_uint s_prog_5v;

void supply_screen_set_prog_5v(uint8_t state, uint8_t pin)
{
    atomic_store(&s_prog_5v, ((unsigned)state << 8) | pin);
}

uint32_t sup_prog_5v(void)
{
    return (uint32_t)atomic_load(&s_prog_5v);
}

void supply_screen_set_model(bool model)
{
    if (s.model != model) {
        s.model = model;
        ++s.ctrl_rev;
    }
}

bool supply_screen_model(void) { return s.model; }

float supply_screen_set_v(void) { return s.cv; }
float supply_screen_set_i(void) { return s.ci; }

void supply_screen_settings_loaded(void)
{
    sup_refresh_limits();
    s.cv = supply_snap(settings_get(SET_SUPPLY_V_START), s.eff.v_min,
                       s.eff.v_max, s.eff.v_step);
    s.ci = supply_snap(settings_get(SET_SUPPLY_I_START), s.eff.i_min,
                       s.eff.i_max, s.eff.i_step);
    ui_slider_set(&s.v_slider, s.cv);
    ui_slider_set(&s.i_slider, s.ci);
    ++s.set_rev;
}

/*
 * Whether a change from @p from waits for a confirmation: only while the
 * output is on, and only as the operator's settings ask.
 */
bool sup_confirm_needed(int from)
{
    /* Live from the moment an ON is asked for: the first sample to show it
     * can be 50ms behind, and a step button acts on its press. */
    if (!s.on && !s.on_asked) {
        return false;
    }
    return settings_get_bool((from == FROM_KEYPAD) ? SET_SUPPLY_CONFIRM_KEYS
                                                   : SET_SUPPLY_CONFIRM_SLIDE);
}

/*
 * The sliders' values become the set points, or wait for a confirmation.
 * While one is asked, the sliders go back to the set points the supply has,
 * and the change is held in pend_v and pend_i.
 */
void sup_propose(int from)
{
    /* Snapped here and not on the slider: a slider written during a drag
     * re-anchors the drag on the written value while the press point stays
     * where it was, and every move then adds the whole distance again. */
    const float v = supply_snap(s.v_slider.value, s.eff.v_min, s.eff.v_max,
                                s.eff.v_step);
    const float i = supply_snap(s.i_slider.value, s.eff.i_min, s.eff.i_max,
                                s.eff.i_step);
    if (s.port_held) {
        /* Nothing moves under a programming port: the sliders go back. */
        ui_slider_set(&s.v_slider, s.cv);
        ui_slider_set(&s.i_slider, s.ci);
        ++s.set_rev;
        return;
    }
    if (v == s.cv && i == s.ci) {
        return;
    }
    if (sup_confirm_needed(from)) {
        s.pend_v = v;
        s.pend_i = i;
        ui_slider_set(&s.v_slider, s.cv);
        ui_slider_set(&s.i_slider, s.ci);
        s.confirm_open  = true;
        s.confirm_stops = s.stops;
        ++s.set_rev;
        ++s.ov_rev;
        supply_invalidate();
        return;
    }
    s.cv = v;
    s.ci = i;
    ++s.set_rev;
}

/*
 * Whether the question about a live output still stands: the output on or
 * an ON on its way, and no stop since it was asked.  A stop switches the
 * output off, but the supply reports that a sample later; an APPLY drained
 * in the frame of the stop finds the output still on and is refused by the
 * count.
 */
bool sup_confirm_stands(void)
{
    return (s.on || s.on_asked) && s.stops == s.confirm_stops;
}

/* The question answered: APPLY gives the supply the change, CANCEL drops it. */
void sup_confirm_close(bool apply)
{
    if (apply && !s.port_held) {
        s.cv = s.pend_v;
        s.ci = s.pend_i;
        ui_slider_set(&s.v_slider, s.cv);
        ui_slider_set(&s.i_slider, s.ci);
    }
    s.confirm_open = false;
    ++s.set_rev;
    supply_invalidate();
}

supply_limits_t supply_screen_limits(void)
{
    return s.lim;
}

supply_caps_t supply_screen_caps(void) { return s.eff; }

const ui_keypad_t *supply_screen_keypad(void) { return &s.kp; }

bool supply_screen_typed_asks(void) { return sup_confirm_needed(FROM_KEYPAD); }

/**
 * @brief The operator's set points as the caps take them now.
 * @param v takes the voltage, V
 * @param i takes the current limit, A
 */
static void own_snapped(float *v, float *i)
{
    *v = supply_snap(s.own_v, s.eff.v_min, s.eff.v_max, s.eff.v_step);
    *i = supply_snap(s.own_i, s.eff.i_min, s.eff.i_max, s.eff.i_step);
}

/**
 * @brief Whether the operator's set points can be offered back now: the
 *        port's stand unchanged on an output reported on, no port holds
 *        the supply, no OFF is on its way or owed, and the operator's
 *        differ from the port's.
 * @return true when they can
 */
static bool offer_fits(void)
{
    float v;
    float i;

    if (!s.lent || s.port_held || !s.on || s.pending.off || s.off_owed
        || s.cv != s.lent_v || s.ci != s.lent_i) {
        return false;
    }
    own_snapped(&v, &i);
    return v != s.cv || i != s.ci;
}

/**
 * @brief The supply's lock as the two programmers' words make it: held while
 *        either holds a port.
 * @param held whether a programming port holds the supply
 * @param by_leave whether a programmer's LEAVE ends its hold with this call:
 *        only then does a release arm the restore offer
 */
static void port_held_apply(bool held, bool by_leave)
{
    if (s.port_held == held) {
        return;
    }
    s.port_held = held;
    if (held) {
        /* What was under way and would change the supply ends here: a
         * hold on OUTPUT ON, an ON not collected, a drag, a set point
         * being typed, a question about one.  An ON already collected is
         * on the wire: it stays asked, and its owner with it, until it
         * lands or the application drops it (supply_screen_set_on_coming()). */
        if (s.pressed == P_V_SLIDER || s.pressed == P_I_SLIDER
            || (s.pressed == P_OUTPUT && !s.press_on)
            || s.pressed == P_APPLY || s.pressed == P_CARD_V
            || s.pressed == P_CARD_I || s.pressed == P_VALUE_V
            || s.pressed == P_VALUE_I) {
            sup_let_go();
        }
        ui_slider_release(&s.v_slider);
        ui_slider_release(&s.i_slider);
        ui_hold_reset(&s.hold);
        if (s.pending.on) {
            s.pending.on = false;
            s.on_asked   = false;
        }
        if (s.kp.open && (s.kp_target == KP_SET_V
                          || s.kp_target == KP_SET_I)) {
            if (s.pressed == P_KEYPAD) {
                sup_let_go();
            }
            ui_keypad_close(&s.kp);
            s.kp_target = KP_NONE;
        }
        s.confirm_open = false;
        s.offer = false;
        ui_slider_set(&s.v_slider, s.cv);
        ui_slider_set(&s.i_slider, s.ci);
        sup_owner_settle();
    } else if (s.off_owed) {
        /* The screen that switched the output on was left while the port
         * held it: the OFF it owed goes now. */
        sup_post_off();
    }
    /* Let go, the set points come under the caps again. */
    apply_caps();
    /* And a LEAVE that let go of an output it left on, at the port's set
     * points: the operator's are offered back onto it, once.  Any other
     * end of the hold offers nothing. */
    if (!held) {
        s.offer       = by_leave && offer_fits();
        s.offer_stops = s.stops;
        s.offer_offs  = s.off_count;
    }
    ++s.set_rev;
    ++s.out_rev;
    ++s.ctrl_rev;
    supply_invalidate();
}

/**
 * @brief The hold as the programmers' words and the board make it: either
 *        programmer's word, on a board whose ports run from the PD mini.
 * @return true while a port holds this screen.
 */
static bool port_held_now(void)
{
    return !s.ports_own && (s.held_kst || s.held_hitec);
}

/**
 * @brief One programmer's word on the hold, and whether its LEAVE ends it.
 * @param held the programmer's word as this screen has it
 * @param left whether its LEAVE ended since the word last changed
 * @param now the word now
 * @return true when the word turns from held to free after a LEAVE
 */
static bool port_word(bool *held, bool *left, bool now)
{
    const bool by_leave = *held && !now && *left;

    /* A LEAVE counts for the end of the hold it ends: a hold that begins
     * or ends drops it either way. */
    if (*held != now) {
        *left = false;
    }
    *held = now;
    return by_leave;
}

void supply_screen_set_port_held(bool held)
{
    const bool by_leave = port_word(&s.held_kst, &s.left_kst, held);

    port_held_apply(port_held_now(), by_leave);
}

void supply_screen_set_hitec_held(bool held)
{
    const bool by_leave = port_word(&s.held_hitec, &s.left_hitec, held);

    port_held_apply(port_held_now(), by_leave);
}

void supply_screen_set_ports_own(bool own)
{
    /* The ports on their own supply let go of this screen with the
     * programmers' words still held: no LEAVE ends that. */
    s.ports_own = own;
    port_held_apply(port_held_now(), false);
}

void supply_screen_kst_leave_done(void)
{
    s.left_kst = true;
}

void supply_screen_hitec_leave_done(void)
{
    s.left_hitec = true;
}

bool supply_screen_port_held(void) { return s.port_held; }

void supply_screen_adopt(float v, float i)
{
    /* The operator's: the set points before the first port's, or after a
     * change made since the last port's were taken. */
    if (!s.lent || s.cv != s.lent_v || s.ci != s.lent_i) {
        s.own_v = s.cv;
        s.own_i = s.ci;
        s.lent  = true;
    }
    s.cv = supply_snap(v, s.eff.v_min, s.eff.v_max, s.eff.v_step);
    s.ci = supply_snap(i, s.eff.i_min, s.eff.i_max, s.eff.i_step);
    s.lent_v = s.cv;
    s.lent_i = s.ci;
    ui_slider_set(&s.v_slider, s.cv);
    ui_slider_set(&s.i_slider, s.ci);
    ++s.set_rev;
}

void supply_screen_port_done(bool free_off)
{
    /* Not under OUTPUT ON's hold either: an ON lands at the set points the
     * hold began on. */
    if (!s.lent || !free_off || s.port_held || s.on || s.on_asked
        || supply_screen_output_held()) {
        return;
    }
    s.lent = false;
    s.offer = false;
    /* Changed since the port's were taken: the change stands. */
    if (s.cv != s.lent_v || s.ci != s.lent_i) {
        return;
    }
    own_snapped(&s.cv, &s.ci);
    ui_slider_set(&s.v_slider, s.cv);
    ui_slider_set(&s.i_slider, s.ci);
    ++s.set_rev;
}

bool supply_screen_restore_offer(float *v, float *i)
{
    /* Only for the run the port left on: an OFF or a stop since ends it,
     * and so does a change of a set point. */
    if (s.offer && (s.stops != s.offer_stops || s.off_count != s.offer_offs
                    || !offer_fits())) {
        s.offer = false;
    }
    if (!s.offer) {
        return false;
    }
    if (v != NULL && i != NULL) {
        own_snapped(v, i);
    }
    return true;
}

void supply_screen_restore_taken(void) { s.offer = false; }

void supply_screen_put(float v, float i)
{
    if (s.port_held) {
        return;
    }
    s.cv = supply_snap(v, s.eff.v_min, s.eff.v_max, s.eff.v_step);
    s.ci = supply_snap(i, s.eff.i_min, s.eff.i_max, s.eff.i_step);
    /* The sliders show the set points.  This screen's own question is
     * closed whenever it is left, so none is open while another types. */
    ui_slider_set(&s.v_slider, s.cv);
    ui_slider_set(&s.i_slider, s.ci);
    ++s.set_rev;
}

bool supply_screen_output_on(void) { return s.on; }

bool supply_screen_output_live(void) { return s.on || s.on_asked; }

uint32_t supply_screen_off_count(void) { return s.off_count; }

uint32_t supply_screen_off_asks(void) { return s.off_asks; }

void supply_screen_ask_on(supply_by_t by) { sup_post_on(by); }

supply_by_t supply_screen_owner(void) { return s.owner; }

bool supply_screen_off_owed(void) { return s.off_owed; }

void supply_screen_claim(supply_by_t by)
{
    if (s.owner == SUPPLY_BY_NONE && s.on && !s.pending.off) {
        s.owner = by;
    }
}

void supply_screen_entered(supply_by_t by)
{
    if (s.owner == by) {
        s.off_owed = false;
    }
}

void supply_screen_left(supply_by_t by)
{
    if (by == SUPPLY_BY_NONE || s.owner != by) {
        return;
    }
    if (s.port_held) {
        /* An OFF now would end the port's session and cut a write on the
         * wire: owed until the port lets go, for an output on and for an
         * ON collected before the port held the supply, which may land
         * after this leave. */
        s.off_owed = s.on || s.on_asked || s.pending.on;
        return;
    }
    if (s.on || s.on_asked || s.pending.on) {
        sup_post_off();
    }
}

void supply_screen_ask_off(void) { sup_post_off(); }

bool supply_screen_off_pending(void) { return s.pending.off; }

bool supply_screen_output_held(void)
{
    return s.pressed == P_OUTPUT && !s.press_on;
}

void supply_screen_limits_changed(void)
{
    sup_refresh_limits();
}

void supply_screen_set_on_coming(bool coming)
{
    if (!coming && !s.pending.on) {
        s.on_asked = false;
    }
    sup_owner_settle();
}

void supply_screen_cancel_on(void)
{
    bool changed = false;
    /* Counted: the question about a live output does not outlive it. */
    ++s.stops;
    /* A stop drops an ON wherever it is, so none is waiting any more. */
    s.on_asked = false;
    if (s.pending.on) {
        s.pending.on = false;
        changed = true;
    }
    if (s.pressed == P_OUTPUT && !s.on) {
        sup_let_go();
        changed = true;
    }
    /* And an APPLY being pressed on the question: its release must not
     * change a set point after the stop. */
    if (s.pressed == P_APPLY) {
        sup_let_go();
        changed = true;
    }
    if (s.hold.down || s.hold.held_s > 0.0f) {
        changed = true;
    }
    /* PROG 5V ON's hold: the stop cuts the ports' 5V as well. */
    if (s.pressed == P_PROG5V || s.p5.hold.held_s > 0.0f) {
        prog5v_box_cancel(&s.p5);
        if (s.pressed == P_PROG5V) {
            sup_let_go();
        }
        changed = true;
    }
    if (changed) {
        ui_hold_reset(&s.hold);
        ++s.out_rev;
        ++s.ctrl_rev;
    }
    sup_owner_settle();
}

/*
 * Leaving switches off an output this screen switched on: a screen that is
 * not visible must not hold a supply live that no other screen names.  An
 * output another screen switched on stays as it is.  The keypads, the
 * question and a switch of task are this screen's own and are no leave.  The
 * press this screen was routing goes, for the reason MOTOR & ESC's leave()
 * gives, and so do the keypad and the question; the screen is entered on
 * LIVE (enter()).
 */
static void leave(void)
{
    supply_screen_left(SUPPLY_BY_SUPPLY);
    /* And the ports' 5V switched on here; a hold on PROG 5V ON drops. */
    prog5v_box_cancel(&s.p5);
    prog5v_box_left(PROG5V_BY_SUPPLY);
    /* A press on OUTPUT OFF when the screen is left -- a second finger on
     * HOME, which the band handles without asking this screen -- is the OFF
     * the operator was making, and it is sent, as cancel() sends one. */
    if (s.on && s.pressed == P_OUTPUT && s.press_on) {
        sup_post_off();
    }
    ui_slider_release(&s.v_slider);
    ui_slider_release(&s.i_slider);
    ui_hold_reset(&s.hold);
    ui_tabs_cancel(&s.tabs);
    ui_wb_strip_cancel(&s.wb);
    ++s.strip_rev;
    ui_keypad_close(&s.kp);
    s.kp_target     = KP_NONE;
    /* A change waiting for its question is dropped: nobody is there to
     * answer it. */
    s.confirm_open  = false;
    ui_slider_set(&s.v_slider, s.cv);
    ui_slider_set(&s.i_slider, s.ci);
    sup_let_go();
    ++s.ctrl_rev;
    ++s.out_rev;
    ++s.set_rev;
    supply_invalidate();
}

/* The menu's tile opens LIVE, as on every workbench.  Back on the screen
 * whose output it is, no OFF is owed for the leave. */
static void enter(void)
{
    ui_wb_strip_open(&s.wb);
    ++s.strip_rev;
    supply_invalidate();
    supply_screen_entered(SUPPLY_BY_SUPPLY);
}

int supply_screen_task(void) { return (int)ui_wb_strip_task(&s.wb); }

/*
 * Touch events were lost.  The press is dropped as sup_drop_press() drops it:
 * every gesture asks for nothing, except a tap on OUTPUT OFF, which is sent,
 * and an ON not yet collected is dropped; one collected stays asked.
 */
static void cancel(void)
{
    sup_drop_press();
}

/*
 * The home tag climbs one level: the question first, answered CANCEL, then
 * a keypad, closed with nothing typed kept.  The tasks are siblings, not
 * levels, as on MOTOR & ESC and SERVO: with neither open the screen is
 * left, from LIVE and from SETTINGS alike.  Nothing here touches the
 * output.
 */
static bool back(void)
{
    if (s.confirm_open) {
        sup_confirm_close(false);
    } else if (s.kp.open) {
        ui_keypad_close(&s.kp);
        s.kp_target = KP_NONE;
    } else {
        return false;
    }
    supply_invalidate();
    return true;
}

static const ui_screen_t k_screen = {
    .title  = "SUPPLY",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = sup_tick,
    .event  = sup_event,
    .cancel = cancel,
    .render = sup_render,
    .back   = back,
    .safe_at = sup_safe_at,
};

const ui_screen_t *supply_screen(void) { return &k_screen; }
