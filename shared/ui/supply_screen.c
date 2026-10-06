/*
 * The SUPPLY screen.  See supply_screen.h.
 *
 * MOTOR & ESC's frame and its rules for what repaints when: chrome once per
 * framebuffer, the numbers when a sample arrives, the plot when it moves,
 * and each control on its own counter.
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "bench_state.h"
#include "settings.h"
#include "ui_hero.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)   /* the router owns the band */

#define PAD       6
#define INNER     6
#define COL_GAP   6
#define LEFT_X    PAD
#define LEFT_W    546
#define RIGHT_X   (LEFT_X + LEFT_W + COL_GAP)
#define RIGHT_W   (W - PAD - RIGHT_X)

#define HDR_Y     2
#define HDR_H     18

#define UP_Y      24
#define UP_H      242
#define CARD_GAP  4
#define CARD_H    ((UP_H - 3 * CARD_GAP) / 4)

#define LO_Y      (UP_Y + UP_H + 6)
#define LO_H      (H - LO_Y - PAD)

#define TITLE_H   20
#define LEG_Y     (UP_Y + TITLE_H + 4)
#define LEG_H     30
#define PLOT_X    (LEFT_X + INNER)
#define PLOT_W    (LEFT_W - 2 * INNER)
#define PLOT_Y    (LEG_Y + LEG_H + 4)
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y)

/*
 * The set points: two rows in the lower left, each a label and its value
 * over a track with a fine step at each end.  The step buttons move by a
 * tenth of a volt or of an amp, which the slider is too coarse to place;
 * the slider covers the range, and both snap to what the supply takes.  The
 * value itself opens the keypad.
 */
#define SET_ROW_H   62
#define SET_Y0      (LO_Y + TITLE_H + 4)
#define SET_TEXT_H  20
#define SET_CTRL_DY 24
#define SET_CTRL_H  30
#define NUDGE_W     54
#define SET_TRACK_X (LEFT_X + INNER + NUDGE_W + 6)
#define SET_TRACK_W (LEFT_W - 2 * INNER - 2 * (NUDGE_W + 6))
#define NUDGE_V     0.1f
#define NUDGE_I     0.1f

#define BTN_H     36
#define OUT_Y     (LO_Y + TITLE_H + 8)
#define TOTALS_Y  (OUT_Y + BTN_H + 10)
#define RESET_Y   (LO_H + LO_Y - PAD - BTN_H)

#define TAB_Y     HDR_Y
#define TAB_H     HDR_H
#define TAB_W     124

/* SETTINGS, at the right end of the strip above both columns. */
#define SETB_W    96
#define SETB_X    (W - PAD - SETB_W)
#define SETB_Y    1
#define SETB_H    21

/*
 * The overlay: the settings and the keypad both cover the left column, plot
 * and set points, and leave the right column -- the readings and OUTPUT
 * OFF -- where it is and working.
 */
#define OV_X      LEFT_X
#define OV_Y      UP_Y
#define OV_W      LEFT_W
#define OV_H      (H - UP_Y - PAD)
#define OV_COL_W  (OV_W / 2 - 16)
#define OV_ROW_H  36
#define OV_VAL_W  120

/* The sample rate of the panel's loop, which is the plot's time base. */
#define SAMPLE_HZ 20.0f

/*
 * Three readings, and the two set points drawn dashed on their readings'
 * scales: a supply in CC (constant current) shows its voltage falling away
 * from the line it was set to.
 */
enum { S_VOLT = 0, S_CURR, S_POWER, S_READINGS, S_VSET = S_READINGS, S_ISET,
       S_COUNT };

static const ui_plot_series_t k_series[S_COUNT] = {
    { "VOLT", "V", 0, 2, 5.0f,  0 },
    { "CURR", "A", 0, 2, 1.0f,  0 },
    { "PWR",  "W", 0, 1, 10.0f, 0 },
    { "VSET", "V", 0, 2, 5.0f,  1 + S_VOLT },
    { "ISET", "A", 0, 2, 1.0f,  1 + S_CURR },
};

static const char *const k_tab_labels[] = { "PLOT", "TABLE" };

/* What a press is on.  One contact holds the screen at a time. */
enum { P_NONE = 0, P_OUTPUT, P_RESET, P_V_DOWN, P_V_UP, P_I_DOWN, P_I_UP,
       P_V_SLIDER, P_I_SLIDER, P_TABS, P_CARD_V, P_CARD_I, P_TEXT_V,
       P_TEXT_I, P_SETTINGS, P_CLOSE, P_ROW, P_KEYPAD, P_APPLY, P_DISCARD,
       P_MODRESET };

/* Where a set point change came from, for the question it may need. */
enum { FROM_SLIDER = 0, FROM_KEYPAD };

/* What the keypad is typing. */
enum { KP_NONE = 0, KP_SET_V, KP_SET_I, KP_SETTING };

/*
 * The settings the overlay edits, in two columns.  A start value is capped
 * like the set point it starts, and a trip of 0 is off.
 */
typedef struct {
    setting_id_t id;
    const char  *label;
    int          col;
    int          y;          /* from the overlay's top */
    int          decimals;
} limit_row_t;

static const limit_row_t k_rows[] = {
    { SET_SUPPLY_V_MAX,   "VOLTAGE MAX",   0, 78,  2 },
    { SET_SUPPLY_I_MAX,   "CURRENT MAX",   0, 120, 2 },
    { SET_SUPPLY_V_START, "START VOLTAGE", 0, 196, 2 },
    { SET_SUPPLY_I_START, "START CURRENT", 0, 238, 2 },
    { SET_SUPPLY_TRIP_I,  "CURRENT TRIP",  1, 78,  2 },
    { SET_SUPPLY_TRIP_V,  "VOLTAGE TRIP",  1, 120, 2 },
    { SET_SUPPLY_TRIP_MS, "TRIP TIME",     1, 162, 0 },
    { SET_SUPPLY_CONFIRM_SLIDE, "SLIDER AND STEPS", 1, 300, 0 },
    { SET_SUPPLY_CONFIRM_KEYS,  "KEYPAD",           1, 344, 0 },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

static struct {
    ui_plot_t       plot;
    ui_tabs_t       tabs;
    ui_slider_t     v_slider;
    ui_slider_t     i_slider;
    supply_caps_t   caps;     /* what the supply can be set to          */
    supply_limits_t lim;      /* what the operator allows               */
    supply_caps_t   eff;      /* the caps narrowed by the limits        */
    supply_state_t  sup;
    /*
     * The set points the supply is given.  The sliders show them, except
     * while a drag is under way or a change waits for its confirmation;
     * see propose().
     */
    float           cv, ci;
    /* An ON this screen has asked for and not yet seen applied: the output
     * is treated as live from the moment it is asked; see confirm_needed(). */
    bool            on_asked;
    bool            confirm_open;
    float           pend_v, pend_i;
    gfx_rect_t      apply_btn, discard_btn;
    bool            model;
    uint32_t        baud;          /* PD mini UART rate; 0: not shown */
    /* The output as this screen believes it: what the supply reported, and
     * an OFF this screen has asked for and not yet seen answered. */
    bool            on;
    /* The output as the supply reports it, which bounds a run. */
    bool            out_reported;
    supply_cmd_t    pending;
    unsigned        drawn_mask;
    uint32_t        drawn_push[2];
    uint32_t        drawn_plot[2];
    uint8_t         drawn_title[2];
    uint32_t        ctrl_rev;
    uint32_t        drawn_ctrl[2];
    uint32_t        set_rev;
    uint32_t        drawn_set[2];
    uint32_t        out_rev;
    uint32_t        drawn_out[2];
    uint32_t        ov_rev;
    uint32_t        drawn_ov[2];
    uint8_t         drawn_save[2];
    uint32_t        drawn_kp[2];
    ui_hold_t       hold;
    gfx_rect_t      out_rect;
    gfx_rect_t      reset_rect;
    gfx_rect_t      modreset_btn;   /* RESET PD MINI, in SETTINGS */
    gfx_rect_t      set_btn;
    gfx_rect_t      close_btn;
    gfx_rect_t      v_down, v_up, i_down, i_up;
    bool            settings_open;
    ui_keypad_t     kp;
    int             kp_target;
    setting_id_t    kp_setting;
    int             pressed;
    int             press_row;
    uint8_t         press_id;
    bool            have_press;
} s;

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
        s.drawn_title[b] = 0xFFu;
        s.drawn_save[b]  = 0xFFu;
    }
}

void supply_invalidate(void)
{
    forget_drawn();
}

static void bind_colours(void)
{
    s.plot.series[S_VOLT].color  = ui_theme_color(UI_C_VOLT);
    s.plot.series[S_CURR].color  = ui_theme_color(UI_C_CURR);
    s.plot.series[S_POWER].color = ui_theme_color(UI_C_POWER);
    s.plot.series[S_VSET].color  = ui_theme_color(UI_C_VOLT);
    s.plot.series[S_ISET].color  = ui_theme_color(UI_C_CURR);
    s.v_slider.color = ui_theme_color(UI_C_VOLT);
    s.i_slider.color = ui_theme_color(UI_C_CURR);
}

static gfx_rect_t set_track(int row)
{
    return (gfx_rect_t){ SET_TRACK_X,
                         (int16_t)(SET_Y0 + row * SET_ROW_H + SET_CTRL_DY),
                         SET_TRACK_W, SET_CTRL_H };
}

static gfx_rect_t nudge_rect(int row, bool up)
{
    const int16_t y = (int16_t)(SET_Y0 + row * SET_ROW_H + SET_CTRL_DY);
    return up ? (gfx_rect_t){ (int16_t)(SET_TRACK_X + SET_TRACK_W + 6), y,
                              NUDGE_W, SET_CTRL_H }
              : (gfx_rect_t){ (int16_t)(LEFT_X + INNER), y, NUDGE_W,
                              SET_CTRL_H };
}

/* A set point's value, at the right of its label line: tapped, the keypad. */
static gfx_rect_t set_text_rect(int row)
{
    const int half = (LEFT_W - 2 * INNER) / 2;
    return (gfx_rect_t){ (int16_t)(LEFT_X + INNER + half),
                         (int16_t)(SET_Y0 + row * SET_ROW_H - 2),
                         (int16_t)half, SET_TEXT_H + 2 };
}

static gfx_rect_t card_rect(int i)
{
    return (gfx_rect_t){ RIGHT_X, (int16_t)(UP_Y + i * (CARD_H + CARD_GAP)),
                         RIGHT_W, CARD_H };
}

static gfx_rect_t overlay_area(void)
{
    return (gfx_rect_t){ OV_X, OV_Y, OV_W, OV_H };
}

static gfx_rect_t row_rect(int i)
{
    const int x = (k_rows[i].col == 0) ? OV_X + 10 : OV_X + OV_W / 2 + 6;
    return (gfx_rect_t){ (int16_t)x, (int16_t)(OV_Y + k_rows[i].y),
                         OV_COL_W, OV_ROW_H };
}

static gfx_rect_t row_value_rect(int i)
{
    const gfx_rect_t r = row_rect(i);
    return (gfx_rect_t){ (int16_t)(r.x + r.w - OV_VAL_W), r.y, OV_VAL_W,
                         OV_ROW_H };
}

/* The sliders take the effective caps' ranges and steps; their values are
 * kept and snapped into them. */
static void apply_caps(void)
{
    s.cv = supply_snap(s.cv, s.eff.v_min, s.eff.v_max, s.eff.v_step);
    s.ci = supply_snap(s.ci, s.eff.i_min, s.eff.i_max, s.eff.i_step);
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
static void refresh_limits(void)
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

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    forget_drawn();
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    s.caps = caps;
    /* Nothing limited and no trip until the settings are read, which is
     * the settings' own default. */
    s.lim = (supply_limits_t){ caps.v_max, caps.i_max, 0.0f, 0.0f, 0.1f };
    s.eff = supply_caps_limited(&s.caps, &s.lim);
    ui_plot_init(&s.plot, k_series, S_COUNT, (float)PLOT_W / SAMPLE_HZ);
    ui_plot_set_running(&s.plot, false);
    ui_tabs_init(&s.tabs, k_tab_labels, SUPPLY_PANE_COUNT,
                 (gfx_rect_t){ LEFT_X, TAB_Y, TAB_W, TAB_H });
    /* Where the panel's model starts, so a screen and its model agree before
     * anything has been set. */
    s.cv = 6.0f;
    s.ci = 2.0f;
    apply_caps();
    s.v_down = nudge_rect(0, false);
    s.v_up   = nudge_rect(0, true);
    s.i_down = nudge_rect(1, false);
    s.i_up   = nudge_rect(1, true);
    s.out_rect   = (gfx_rect_t){ (int16_t)(RIGHT_X + INNER), OUT_Y,
                                 (int16_t)(RIGHT_W - 2 * INNER), BTN_H };
    s.reset_rect = (gfx_rect_t){ (int16_t)(RIGHT_X + INNER), RESET_Y,
                                 (int16_t)(RIGHT_W - 2 * INNER), BTN_H };
    s.set_btn    = (gfx_rect_t){ SETB_X, SETB_Y, SETB_W, SETB_H };
    s.close_btn  = (gfx_rect_t){ (int16_t)(OV_X + OV_W - 10 - 110),
                                 (int16_t)(OV_Y + 8), 110, 34 };
    s.modreset_btn = (gfx_rect_t){ (int16_t)(OV_X + 10), (int16_t)(OV_Y + 300),
                                   (int16_t)OV_COL_W, 36 };
    s.apply_btn   = (gfx_rect_t){ (int16_t)(OV_X + 40), (int16_t)(OV_Y + 290),
                                  210, 60 };
    s.discard_btn = (gfx_rect_t){ (int16_t)(OV_X + OV_W - 40 - 210),
                                  (int16_t)(OV_Y + 290), 210, 60 };
    s.pressed = P_NONE;
    s.kp.pressed = -1;
    bind_colours();
}

/* ---------------------------------------------------------------- commands */

static void post_on(void)
{
    /* An OFF already asked for stands: two touches in one frame must not
     * let an ON land on top of it. */
    if (!s.pending.off) {
        s.pending.on = true;
        s.on_asked   = true;
    }
}

static void post_off(void)
{
    s.pending.off = true;
    s.pending.on  = false;
    s.on_asked    = false;
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

/* The press is over, whatever it was on. */
static void let_go(void)
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
        if (on) {
            ui_hold_reached(&s.hold);
        } else if (ui_hold_left(&s.hold) && s.pressed == P_OUTPUT) {
            let_go();
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

void supply_screen_set_model(bool model)
{
    if (s.model != model) {
        s.model = model;
        ++s.ctrl_rev;
    }
}

float supply_screen_set_v(void) { return s.cv; }
float supply_screen_set_i(void) { return s.ci; }

void supply_screen_settings_loaded(void)
{
    refresh_limits();
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
static bool confirm_needed(int from)
{
    /* Live from the moment an ON is asked for: the first sample to show it
     * can be 50 ms behind, and a step button acts on its press. */
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
static void propose(int from)
{
    /* Snapped here and not on the slider: a slider written during a drag
     * re-anchors the drag on the written value while the press point stays
     * where it was, and every move then adds the whole distance again. */
    const float v = supply_snap(s.v_slider.value, s.eff.v_min, s.eff.v_max,
                                s.eff.v_step);
    const float i = supply_snap(s.i_slider.value, s.eff.i_min, s.eff.i_max,
                                s.eff.i_step);
    if (v == s.cv && i == s.ci) {
        return;
    }
    if (confirm_needed(from)) {
        s.pend_v = v;
        s.pend_i = i;
        ui_slider_set(&s.v_slider, s.cv);
        ui_slider_set(&s.i_slider, s.ci);
        s.confirm_open = true;
        ++s.set_rev;
        ++s.ov_rev;
        supply_invalidate();
        return;
    }
    s.cv = v;
    s.ci = i;
    ++s.set_rev;
}

/* The question answered: APPLY gives the supply the change, CANCEL drops it. */
static void confirm_close(bool apply)
{
    if (apply) {
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

void supply_screen_limits_changed(void)
{
    refresh_limits();
}

void supply_screen_set_on_coming(bool coming)
{
    if (!coming && !s.pending.on) {
        s.on_asked = false;
    }
}

void supply_screen_cancel_on(void)
{
    bool changed = false;
    /* A stop drops an ON wherever it is, so none is waiting any more. */
    s.on_asked = false;
    if (s.pending.on) {
        s.pending.on = false;
        changed = true;
    }
    if (s.pressed == P_OUTPUT && !s.on) {
        let_go();
        changed = true;
    }
    if (s.hold.down || s.hold.held_s > 0.0f) {
        changed = true;
    }
    if (changed) {
        ui_hold_reset(&s.hold);
        ++s.out_rev;
        ++s.ctrl_rev;
    }
}

/* ------------------------------------------------------------------ keypad */

static void open_keypad(int target, setting_id_t id)
{
    if (target == KP_SET_V) {
        ui_keypad_open(&s.kp, overlay_area(), "VOLTAGE", "V", s.cv,
                       s.eff.v_min, s.eff.v_max, 2);
    } else if (target == KP_SET_I) {
        ui_keypad_open(&s.kp, overlay_area(), "CURRENT LIMIT", "A", s.ci,
                       s.eff.i_min, s.eff.i_max, 2);
    } else {
        const setting_def_t *d = settings_def(id);
        float lo = d->min;
        float hi = d->max;
        /* A cap reaches what the supply can do; a start value what the caps
         * allow.  A trip goes down to 0, which is off. */
        if (id == SET_SUPPLY_V_MAX) {
            lo = s.caps.v_min;
            hi = s.caps.v_max;
        } else if (id == SET_SUPPLY_I_MAX) {
            lo = s.caps.i_min;
            hi = s.caps.i_max;
        } else if (id == SET_SUPPLY_V_START) {
            lo = s.eff.v_min;
            hi = s.eff.v_max;
        } else if (id == SET_SUPPLY_I_START) {
            lo = s.eff.i_min;
            hi = s.eff.i_max;
        }
        const char *label = d->label;
        int decimals = 2;
        for (int i = 0; i < ROW_COUNT; ++i) {
            if (k_rows[i].id == id) {
                label    = k_rows[i].label;
                decimals = k_rows[i].decimals;
            }
        }
        ui_keypad_open(&s.kp, overlay_area(), label, d->unit, settings_get(id),
                       lo, hi, decimals);
    }
    s.kp_target  = target;
    s.kp_setting = id;
    supply_invalidate();
}

static void keypad_done(ui_keypad_result_t r, float v)
{
    if (r == UI_KEYPAD_NONE) {
        if (s.kp.open) {
            return;
        }
    } else if (r == UI_KEYPAD_OK) {
        if (s.kp_target == KP_SET_V) {
            ui_slider_set(&s.v_slider, supply_snap(v, s.eff.v_min, s.eff.v_max,
                                                   s.eff.v_step));
            s.kp_target = KP_NONE;
            propose(FROM_KEYPAD);
        } else if (s.kp_target == KP_SET_I) {
            ui_slider_set(&s.i_slider, supply_snap(v, s.eff.i_min, s.eff.i_max,
                                                   s.eff.i_step));
            s.kp_target = KP_NONE;
            propose(FROM_KEYPAD);
        } else if (s.kp_target == KP_SETTING) {
            /* Onto the setting's own step the safe way: a cap rounds down,
             * so a typed 12.01 V allows no more than 12.00 V; a trip typed
             * above 0 stays a trip, never rounded to off. */
            const setting_def_t *d = settings_def(s.kp_setting);
            if (d->step > 0.0f && (s.kp_setting == SET_SUPPLY_V_MAX
                                   || s.kp_setting == SET_SUPPLY_I_MAX)) {
                v = d->min + floorf((v - d->min) / d->step + 1e-3f) * d->step;
            } else if (d->step > 0.0f && v > 0.0f && v < d->step
                       && (s.kp_setting == SET_SUPPLY_TRIP_I
                           || s.kp_setting == SET_SUPPLY_TRIP_V)) {
                v = d->step;
            }
            /* Kept as soon as the bench allows a flash write; see
             * settings_request_save().  An edit on SETUP that was not saved
             * is written with it. */
            settings_set(s.kp_setting, v);
            refresh_limits();
            settings_request_save();
        }
    }
    s.kp_target = KP_NONE;
    supply_invalidate();
}

/* ------------------------------------------------------------------ events */

/* A set point moved by @p dv volts and @p di amps, snapped to the caps. */
static void nudge(float dv, float di)
{
    ui_slider_set(&s.v_slider, supply_snap(s.v_slider.value + dv, s.eff.v_min,
                                           s.eff.v_max, s.eff.v_step));
    ui_slider_set(&s.i_slider, supply_snap(s.i_slider.value + di, s.eff.i_min,
                                           s.eff.i_max, s.eff.i_step));
    ++s.set_rev;
    propose(FROM_SLIDER);
}

static void take(const touch_event_t *evt, int code)
{
    s.have_press = true;
    s.press_id   = evt->point.id;
    s.pressed    = code;
    ++s.ctrl_rev;
    ++s.ov_rev;
}

static void down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;

    /* The output and the peaks work whatever covers the left column. */
    if (gfx_rect_contains(s.out_rect, x, y)) {
        take(evt, P_OUTPUT);
        if (!s.on) {
            ui_hold_begin(&s.hold);
        }
        ++s.out_rev;
        return;
    }
    if (gfx_rect_contains(s.reset_rect, x, y)) {
        take(evt, P_RESET);
        return;
    }
    /* A question being asked is answered before anything else is opened. */
    if (s.confirm_open) {
        if (gfx_rect_contains(s.apply_btn, x, y)) {
            take(evt, P_APPLY);
        } else if (gfx_rect_contains(s.discard_btn, x, y)) {
            take(evt, P_DISCARD);
        }
        return;
    }
    if (gfx_rect_contains(s.set_btn, x, y)) {
        take(evt, P_SETTINGS);
        return;
    }
    if (gfx_rect_contains(card_rect(0), x, y)) {
        take(evt, P_CARD_V);
        return;
    }
    if (gfx_rect_contains(card_rect(1), x, y)) {
        take(evt, P_CARD_I);
        return;
    }

    if (s.kp.open) {
        if (gfx_rect_contains(overlay_area(), x, y)) {
            take(evt, P_KEYPAD);
            (void)ui_keypad_event(&s.kp, evt, NULL);
        }
        return;
    }
    if (s.settings_open) {
        if (gfx_rect_contains(s.close_btn, x, y)) {
            take(evt, P_CLOSE);
            return;
        }
        if (!s.model && gfx_rect_contains(s.modreset_btn, x, y)) {
            take(evt, P_MODRESET);
            return;
        }
        for (int i = 0; i < ROW_COUNT; ++i) {
            if (gfx_rect_contains(row_rect(i), x, y)) {
                take(evt, P_ROW);
                s.press_row = i;
                return;
            }
        }
        return;
    }

    /* Nothing over the left column: its own controls. */
    if (gfx_rect_contains((gfx_rect_t){ LEFT_X, TAB_Y, TAB_W, TAB_H }, x, y)) {
        take(evt, P_TABS);
        (void)ui_tabs_event(&s.tabs, evt);
        return;
    }
    if (gfx_rect_contains(s.v_slider.track, x, y)
        || gfx_rect_contains(s.i_slider.track, x, y)) {
        const bool v = gfx_rect_contains(s.v_slider.track, x, y);
        take(evt, v ? P_V_SLIDER : P_I_SLIDER);
        if (ui_slider_event(v ? &s.v_slider : &s.i_slider, evt)) {
            ++s.set_rev;
            /* Live as it moves, unless the change needs a question: then
             * the release asks it. */
            if (!confirm_needed(FROM_SLIDER)) {
                propose(FROM_SLIDER);
            }
        }
        return;
    }
    const struct { gfx_rect_t r; int code; float dv, di; } k_steps[] = {
        { s.v_down, P_V_DOWN, -NUDGE_V, 0.0f },
        { s.v_up,   P_V_UP,    NUDGE_V, 0.0f },
        { s.i_down, P_I_DOWN,  0.0f, -NUDGE_I },
        { s.i_up,   P_I_UP,    0.0f,  NUDGE_I },
    };
    for (size_t k = 0; k < sizeof(k_steps) / sizeof(k_steps[0]); ++k) {
        if (gfx_rect_contains(k_steps[k].r, x, y)) {
            /* On the press: a fine step should feel immediate. */
            take(evt, k_steps[k].code);
            nudge(k_steps[k].dv, k_steps[k].di);
            return;
        }
    }
    if (gfx_rect_contains(set_text_rect(0), x, y)) {
        take(evt, P_TEXT_V);
    } else if (gfx_rect_contains(set_text_rect(1), x, y)) {
        take(evt, P_TEXT_I);
    }
}

/* The release of the press, over the control it began on. */
static void released(int was, int row, int x, int y)
{
    switch (was) {
    case P_RESET:
        if (gfx_rect_contains(s.reset_rect, x, y)) {
            s.pending.reset = true;
        }
        break;
    case P_CARD_V:
        if (gfx_rect_contains(card_rect(0), x, y)) {
            open_keypad(KP_SET_V, SETTING_COUNT);
        }
        break;
    case P_CARD_I:
        if (gfx_rect_contains(card_rect(1), x, y)) {
            open_keypad(KP_SET_I, SETTING_COUNT);
        }
        break;
    case P_TEXT_V:
        if (gfx_rect_contains(set_text_rect(0), x, y)) {
            open_keypad(KP_SET_V, SETTING_COUNT);
        }
        break;
    case P_TEXT_I:
        if (gfx_rect_contains(set_text_rect(1), x, y)) {
            open_keypad(KP_SET_I, SETTING_COUNT);
        }
        break;
    case P_SETTINGS:
        if (gfx_rect_contains(s.set_btn, x, y)) {
            /* SETTINGS opens the overlay and closes it, and takes a keypad
             * that was open with it, typed or not. */
            const bool was_open = s.settings_open || s.kp.open;
            ui_keypad_close(&s.kp);
            s.kp_target = KP_NONE;
            s.settings_open = !was_open;
            supply_invalidate();
        }
        break;
    case P_CLOSE:
        if (gfx_rect_contains(s.close_btn, x, y)) {
            s.settings_open = false;
            supply_invalidate();
        }
        break;
    case P_MODRESET:
        /* The module restarted: its output goes off first. */
        if (gfx_rect_contains(s.modreset_btn, x, y)) {
            s.pending.module_reset = true;
            s.pending.off          = true;
        }
        break;
    case P_ROW:
        if (row >= 0 && row < ROW_COUNT && gfx_rect_contains(row_rect(row), x, y)) {
            const setting_id_t id = k_rows[row].id;
            if (settings_def(id)->type == SET_TYPE_BOOL) {
                /* A switch flips on the tap; it has nothing to type. */
                settings_set(id, settings_get_bool(id) ? 0.0f : 1.0f);
                refresh_limits();
                settings_request_save();
            } else {
                open_keypad(KP_SETTING, id);
            }
        }
        break;
    case P_APPLY:
        if (gfx_rect_contains(s.apply_btn, x, y)) {
            confirm_close(true);
        }
        break;
    case P_DISCARD:
        if (gfx_rect_contains(s.discard_btn, x, y)) {
            confirm_close(false);
        }
        break;
    default:
        break;
    }
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    /*
     * One contact holds the screen at a time, and that is decided before
     * any widget sees the event: a second finger on a track while the first
     * holds OUTPUT OFF would otherwise move the set point of a live output,
     * and on a button it would take over the record of the first press and
     * lose its release.
     */
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (!s.have_press) {
            down(evt);
        }
        return;
    }
    if (!s.have_press || evt->point.id != s.press_id) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    const bool up = (evt->type == TOUCH_EVENT_UP);
    switch (s.pressed) {
    case P_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&s.kp, evt, &v);
        if (up) {
            let_go();
        }
        keypad_done(r, v);
        return;
    }
    case P_V_SLIDER:
    case P_I_SLIDER: {
        ui_slider_t *sl = (s.pressed == P_V_SLIDER) ? &s.v_slider : &s.i_slider;
        if (ui_slider_event(sl, evt)) {
            ++s.set_rev;
            if (!confirm_needed(FROM_SLIDER)) {
                propose(FROM_SLIDER);
            }
        }
        if (up) {
            let_go();
            propose(FROM_SLIDER);
            /* The drag is over, so the slider can be written: it shows the
             * set point it was snapped to, or, while a question is open,
             * the one the supply keeps until it is answered. */
            ui_slider_set(&s.v_slider, s.cv);
            ui_slider_set(&s.i_slider, s.ci);
            ++s.set_rev;
        }
        return;
    }
    case P_TABS:
        if (ui_tabs_event(&s.tabs, evt)) {
            supply_invalidate();
        }
        if (up) {
            let_go();
        }
        return;
    case P_OUTPUT:
        if (!up) {
            /* A finger that leaves the switch abandons the hold. */
            if (!s.on && !gfx_rect_contains(s.out_rect, x, y)
                && ui_hold_leave(&s.hold)) {
                let_go();
                ++s.out_rev;
            }
            return;
        }
        {
            const bool fired = ui_hold_end(&s.hold);
            ++s.out_rev;
            /* Off is a tap; on is a hold that has already sent its command
             * by the time the finger lifts. */
            if (s.on && !fired && gfx_rect_contains(s.out_rect, x, y)) {
                post_off();
            }
            let_go();
        }
        return;
    default:
        if (up) {
            const int was = s.pressed;
            const int row = s.press_row;
            let_go();
            released(was, row, x, y);
        }
        return;
    }
}

static void tick(float dt_s)
{
    if (s.pressed == P_OUTPUT && !s.on) {
        ++s.out_rev;
        if (ui_hold_tick(&s.hold, dt_s)) {
            post_on();
        }
    }
    if (s.hold.flash_left > 0) {
        ++s.out_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

static gfx_color_t out_fill(void)
{
    /* Off is the OK green the hold fades from; on is the danger red, so the
     * fade previews the colour the switch is about to hold. */
    gfx_color_t fill = s.on ? ui_theme_color(UI_C_DANGER)
                            : ui_theme_color(UI_C_OK);
    if (s.hold.flash_left > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), s.hold.flash_left);
    }
    if (!s.on && s.hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER), s.hold.held_s);
    }
    return fill;
}

static void out_flash_advance(void)
{
    if (s.hold.flash_left > 0) {
        ui_hold_flash_step(&s.hold);
        ++s.out_rev;
    }
}

static void draw_out_button(gfx_canvas_t *c)
{
    ui_button(c, s.out_rect, s.on ? "OUTPUT OFF" : "OUTPUT ON", out_fill(),
              s.pressed == P_OUTPUT, true);
    out_flash_advance();
}

static const char *mode_text(void)
{
    if (!s.sup.online) {
        return "--";
    }
    if (!s.sup.output && s.sup.trip != SUPPLY_TRIP_NONE) {
        return "TRIP";
    }
    switch (s.sup.mode) {
    case SUPPLY_MODE_CV: return "CV";
    case SUPPLY_MODE_CC: return "CC";
    case SUPPLY_MODE_OFF:
    default:             return "OFF";
    }
}

static const char *mode_line(void)
{
    if (!s.sup.online) {
        return "";
    }
    if (!s.sup.output && s.sup.trip == SUPPLY_TRIP_CURRENT) {
        return "current over trip";
    }
    if (!s.sup.output && s.sup.trip == SUPPLY_TRIP_VOLTAGE) {
        return "voltage over trip";
    }
    if (s.sup.mode == SUPPLY_MODE_CC) {
        return "at the limit";
    }
    return (s.sup.mode == SUPPLY_MODE_CV) ? "at the voltage" : "";
}

/* The strip above both columns: where the numbers come from, the range the
 * set points may take, and SETTINGS. */
static void draw_header(gfx_canvas_t *c)
{
    const int x0 = LEFT_X + TAB_W + 10;
    const int x1 = SETB_X - 8;
    gfx_fill_rect(c, x0, 0, W - x0, UP_Y - 1, ui_theme_color(UI_C_BG));
    char line[64];
    char rate[16] = "";
    if (!s.model && s.sup.online && s.baud != 0u) {
        snprintf(rate, sizeof(rate), " %lu", (unsigned long)s.baud);
    }
    snprintf(line, sizeof(line), "%s   %s%s",
             s.model ? "SUPPLY MODEL" : "PD MINI",
             s.sup.online ? "ONLINE" : "NOT ANSWERING", rate);
    gfx_text(c, x0, HDR_Y + 1, line, &gfx_font_8x16,
             s.sup.online ? ui_theme_color(UI_C_TEXT_DIM)
                          : ui_theme_color(UI_C_WARN), 1);
    char range[64];
    snprintf(range, sizeof(range), "%.1f-%.1f V  %.2f-%.2f A",
             (double)s.eff.v_min, (double)s.eff.v_max,
             (double)s.eff.i_min, (double)s.eff.i_max);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x0, HDR_Y + 1, (int16_t)(x1 - x0),
                                 16 },
                range, &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, s.set_btn, "SETTINGS",
              s.settings_open ? ui_theme_color(UI_C_ACCENT)
                              : ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_SETTINGS, true);
}

/* One card of the rail, as MOTOR & ESC draws them, with the set point in
 * brackets after the label when there is one. */
static void draw_card(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                      const char *set, const char *unit, gfx_color_t color,
                      int decimals, float value, const char *extreme,
                      float peak, bool pressed)
{
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_card(c, r, pressed ? ui_theme_color(UI_C_PANEL_HI)
                          : ui_theme_color(UI_C_PANEL));
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, color);
    gfx_fill_round_rect(c, r.x + 8, r.y + 7, 3, 10, 1, color);
    const int lw = gfx_text(c, r.x + 16, r.y + 6, label, &gfx_font_8x16,
                            ui_theme_color(UI_C_TEXT), 1);
    if (set != NULL) {
        gfx_text(c, r.x + 16 + lw + 8, r.y + 6, set, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    if (extreme != NULL && isfinite(peak)) {
        char pk[24], line[32];
        ui_fmt(pk, sizeof(pk), peak, decimals);
        snprintf(line, sizeof(line), "%s %s", extreme, pk);
        gfx_text(c, r.x + 8, r.y + 26, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    char number[24];
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    const gfx_seg_style_t seg = { .digit_w = 15, .digit_h = 24,
                                  .thickness = 3, .gap = 4, .slant = 2,
                                  .ghost = true };
    const int uw = gfx_text_width(&gfx_font_8x16, unit, 1);
    const int nw = gfx_seg_width(number, &seg);
    const int nx = r.x + r.w - 8 - uw - 6 - nw;
    gfx_seg_text(c, nx, r.y + 16, number, &seg, color,
                 gfx_lerp(ui_theme_color(UI_C_PANEL), color, 34));
    gfx_text(c, nx + nw + 6, r.y + 30, unit, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* The set points as the supply reports holding them, or as this screen holds
 * them while it does not answer. */
static float shown_set_v(void)
{
    return s.sup.online ? s.sup.set_v : s.cv;
}

static float shown_set_i(void)
{
    return s.sup.online ? s.sup.set_i : s.ci;
}

static void draw_cards(gfx_canvas_t *c)
{
    const bool v_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_CURRENT) != 0u;
    const bool run = s.sup.output || s.plot.filled > 0;
    char vset[16], iset[16];
    snprintf(vset, sizeof(vset), "(%.2f)", (double)shown_set_v());
    snprintf(iset, sizeof(iset), "(%.2f)", (double)shown_set_i());
    draw_card(c, card_rect(0), "VOLT", vset, "V", ui_theme_color(UI_C_VOLT), 2,
              v_ok ? s.sup.v : NAN, "min",
              (v_ok && run && s.sup.sag_seeded) ? s.sup.v_min : NAN,
              s.pressed == P_CARD_V);
    draw_card(c, card_rect(1), "CURR", iset, "A", ui_theme_color(UI_C_CURR), 2,
              i_ok ? s.sup.i : NAN, "pk", (i_ok && run) ? s.sup.i_max : NAN,
              s.pressed == P_CARD_I);
    draw_card(c, card_rect(2), "PWR", NULL, "W", ui_theme_color(UI_C_POWER), 1,
              (v_ok && i_ok) ? s.sup.p : NAN, "pk",
              (v_ok && i_ok && run) ? s.sup.p_max : NAN, false);

    /* The mode, in letters: which of the two set points the supply is
     * holding.  CC in the warning colour, because a supply at its limit is
     * not giving the load the voltage it was set to; a trip in the danger
     * colour until the output is switched on again. */
    const gfx_rect_t r = card_rect(3);
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_card(c, r, ui_theme_color(UI_C_PANEL));
    const bool tripped = s.sup.online && !s.sup.output
                         && s.sup.trip != SUPPLY_TRIP_NONE;
    const gfx_color_t mc = tripped ? ui_theme_color(UI_C_DANGER)
                           : (s.sup.online && s.sup.mode == SUPPLY_MODE_CC)
                               ? ui_theme_color(UI_C_WARN)
                               : ui_theme_color(UI_C_ACCENT);
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, mc);
    gfx_fill_round_rect(c, r.x + 8, r.y + 7, 3, 10, 1, mc);
    gfx_text(c, r.x + 16, r.y + 6, "MODE", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    gfx_text(c, r.x + 8, r.y + 26, mode_line(), &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 8), (int16_t)(r.y + 12),
                                 (int16_t)(r.w - 16), 32 },
                mode_text(), &gfx_font_8x16, mc, 2, GFX_ALIGN_RIGHT);
}

static void draw_table(gfx_canvas_t *c)
{
    gfx_text(c, PLOT_X, LEG_Y + 4, "CHANNEL        SET            NOW",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_rule(c, PLOT_X, LEG_Y + 24, PLOT_W, ui_theme_color(UI_C_EDGE));
    const bool v_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_CURRENT) != 0u;
    static const char *const rows[S_READINGS] = { "VOLTAGE", "CURRENT",
                                                  "POWER" };
    const float set[S_READINGS] = { shown_set_v(), shown_set_i(), NAN };
    const float now[S_READINGS] = { v_ok ? s.sup.v : NAN, i_ok ? s.sup.i : NAN,
                                    (v_ok && i_ok) ? s.sup.p : NAN };
    for (int i = 0; i < S_READINGS; ++i) {
        const int y = LEG_Y + 34 + i * 30;
        char a[24], b[24];
        if (isfinite(set[i])) {
            ui_fmt(a, sizeof(a), set[i], k_series[i].decimals);
        } else {
            snprintf(a, sizeof(a), "--");
        }
        if (isfinite(now[i])) {
            ui_fmt(b, sizeof(b), now[i], k_series[i].decimals);
        } else {
            snprintf(b, sizeof(b), "--");
        }
        gfx_text(c, PLOT_X, y, rows[i], &gfx_font_8x16,
                 s.plot.series[i].color, 1);
        gfx_text(c, PLOT_X + 128, y, a, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + 248, y, b, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + 348, y, k_series[i].unit, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

static void draw_totals(gfx_canvas_t *c)
{
    const gfx_rect_t box = { (int16_t)(RIGHT_X + INNER), TOTALS_Y,
                             (int16_t)(RIGHT_W - 2 * INNER), 16 };
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
    snprintf(line, sizeof(line), "%s mAh   %s Wh", mah, wh);
    gfx_text_in(c, box, line, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

/* The two set points: label and value, then the step buttons and the track. */
static void draw_setters(gfx_canvas_t *c)
{
    static const char *const label[2] = { "VOLTAGE", "CURRENT LIMIT" };
    const float value[2] = { s.v_slider.value, s.i_slider.value };
    const char *unit[2] = { "V", "A" };
    const gfx_color_t col[2] = { ui_theme_color(UI_C_VOLT),
                                 ui_theme_color(UI_C_CURR) };
    for (int row = 0; row < 2; ++row) {
        const int y = SET_Y0 + row * SET_ROW_H;
        gfx_fill_rect(c, LEFT_X + INNER, y - 2, LEFT_W - 2 * INNER,
                      SET_TEXT_H + 2, ui_theme_color(UI_C_PANEL));
        gfx_text(c, LEFT_X + INNER, y + 2, label[row], &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[24];
        snprintf(v, sizeof(v), "%.2f %s", (double)value[row], unit[row]);
        const bool pressed = s.pressed == ((row == 0) ? P_TEXT_V : P_TEXT_I);
        const gfx_rect_t tr = set_text_rect(row);
        if (pressed) {
            gfx_fill_rect(c, tr.x, tr.y, tr.w, tr.h,
                          ui_theme_color(UI_C_PANEL_HI));
        }
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(LEFT_X + INNER), (int16_t)y,
                                     (int16_t)(LEFT_W - 2 * INNER),
                                     SET_TEXT_H },
                    v, &gfx_font_8x16, col[row], 1, GFX_ALIGN_RIGHT);
        const ui_slider_t *sl = (row == 0) ? &s.v_slider : &s.i_slider;
        const gfx_rect_t pr = ui_slider_painted_rect(sl);
        gfx_fill_rect(c, pr.x, pr.y, pr.w, pr.h, ui_theme_color(UI_C_PANEL));
        ui_slider_render(sl, c);
    }
}

static void draw_steps(gfx_canvas_t *c)
{
    ui_button(c, s.v_down, "-0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_V_DOWN, true);
    ui_button(c, s.v_up, "+0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_V_UP, true);
    ui_button(c, s.i_down, "-0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_I_DOWN, true);
    ui_button(c, s.i_up, "+0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_I_UP, true);
}

/*
 * Whether the settings reached the medium: 0 saved, 1 waiting for a quiet
 * moment, 2 refused, 3 changed on SETUP and not asked to be saved -- which
 * nothing writes until SAVE there, or a change here, asks for it.
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

static void row_value_text(int i, char *buf, size_t n)
{
    const setting_def_t *d = settings_def(k_rows[i].id);
    const float v = settings_get(k_rows[i].id);
    const bool trip = (k_rows[i].id == SET_SUPPLY_TRIP_I
                       || k_rows[i].id == SET_SUPPLY_TRIP_V);
    if (d->type == SET_TYPE_BOOL) {
        snprintf(buf, n, "%s", (v != 0.0f) ? "ON" : "OFF");
    } else if (trip && !(v > 0.0f)) {
        snprintf(buf, n, "OFF");
    } else {
        snprintf(buf, n, "%.*f %s", k_rows[i].decimals, (double)v, d->unit);
    }
}

static void draw_settings(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    gfx_text(c, a.x + 10, a.y + 17, "SUPPLY SETTINGS", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    ui_button(c, s.close_btn, "CLOSE", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_CLOSE, true);
    if (!s.model) {
        /* A module in ERR -- a set point over its input -- comes back only
         * with a restart. */
        ui_button(c, s.modreset_btn, "RESET PD MINI",
                  ui_theme_color(UI_C_PANEL_SUNK), s.pressed == P_MODRESET,
                  true);
    }

    const int lx = a.x + 10;
    const int rx = a.x + a.w / 2 + 6;
    gfx_text(c, lx, a.y + 56, "CAPS", &gfx_font_8x16,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, lx, a.y + 174, "AFTER A RESTART", &gfx_font_8x16,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, rx, a.y + 56, "TRIPS", &gfx_font_8x16,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, rx, a.y + 278, "CONFIRM WHILE ON", &gfx_font_8x16,
             ui_theme_color(UI_C_ACCENT), 1);
    for (int i = 0; i < ROW_COUNT; ++i) {
        const gfx_rect_t r = row_rect(i);
        gfx_text(c, r.x, r.y + 10, k_rows[i].label, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[24];
        row_value_text(i, v, sizeof(v));
        ui_button(c, row_value_rect(i), v, ui_theme_color(UI_C_PANEL_SUNK),
                  s.pressed == P_ROW && s.press_row == i, true);
    }
    static const char *const help[] = {
        "A trip switches the output",
        "off once a reading has been",
        "over it for the trip time.",
    };
    for (size_t i = 0; i < sizeof(help) / sizeof(help[0]); ++i) {
        gfx_text(c, rx, a.y + 214 + (int)i * 18, help[i], &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    const uint8_t st = save_state();
    static const char *const k_save[] = { "SAVED", "SAVE WAITING", "NOT SAVED",
                                          "SETUP CHANGES NOT SAVED" };
    gfx_text(c, lx, a.y + a.h - 26, k_save[st], &gfx_font_8x16,
             (st == 2u) ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1);
}

/*
 * The question a change to a live output asks: what changes, from what to
 * what, and APPLY or CANCEL.  OUTPUT OFF and STOP stay where they are.
 */
static void draw_confirm(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_WARN));
    gfx_text(c, a.x + 20, a.y + 20, "OUTPUT IS ON", &gfx_font_8x16,
             ui_theme_color(UI_C_WARN), 2);
    gfx_text(c, a.x + 20, a.y + 70, "A new set point reaches the load at once.",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    int y = a.y + 120;
    const struct { const char *label; float was, now; const char *unit;
                   gfx_color_t col; } k[] = {
        { "VOLTAGE",       s.cv, s.pend_v, "V", ui_theme_color(UI_C_VOLT) },
        { "CURRENT LIMIT", s.ci, s.pend_i, "A", ui_theme_color(UI_C_CURR) },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (k[i].was == k[i].now) {
            continue;
        }
        gfx_text(c, a.x + 20, y + 8, k[i].label, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[48];
        snprintf(line, sizeof(line), "%.2f -> %.2f %s", (double)k[i].was,
                 (double)k[i].now, k[i].unit);
        gfx_text(c, a.x + 150, y, line, &gfx_font_8x16, k[i].col, 2);
        y += 50;
    }
    ui_button(c, s.apply_btn, "APPLY", ui_theme_color(UI_C_WARN),
              s.pressed == P_APPLY, true);
    ui_button(c, s.discard_btn, "CANCEL", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_DISCARD, true);
    gfx_text(c, a.x + 20, a.y + a.h - 26,
             "SETTINGS, CONFIRM WHILE ON, turns this off.", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

enum { TAG_LIVE = 0, TAG_HELD, TAG_IDLE };
static const char *const k_tag[] = { "LIVE OUTPUT", "OUTPUT HELD", "OUTPUT IDLE" };

/* The heading says what the output is doing, on either pane: a held run is
 * the plot's to show, so TABLE says IDLE where PLOT says HELD. */
static uint8_t output_tag(void)
{
    if (s.plot.running) {
        return (uint8_t)TAG_LIVE;
    }
    return (uint8_t)((s.tabs.selected == SUPPLY_PANE_PLOT && s.plot.filled > 0)
                         ? TAG_HELD : TAG_IDLE);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;
    bind_colours();

    /* The keypad, the question or the settings over the left column. */
    const bool covered = s.kp.open || s.confirm_open || s.settings_open;
    const uint8_t tag = output_tag();
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        if (!covered) {
            ui_panel(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H }, k_tag[tag],
                     ui_theme_color(UI_C_ACCENT));
            ui_panel(c, (gfx_rect_t){ LEFT_X, LO_Y, LEFT_W, LO_H },
                     "SET POINTS", ui_theme_color(UI_C_ACCENT));
        }
        ui_panel(c, (gfx_rect_t){ RIGHT_X, LO_Y, RIGHT_W, LO_H }, "CONTROL",
                 ui_theme_color(UI_C_ACCENT));
        s.drawn_mask |= bit;
        s.drawn_title[buf] = tag;
    }
    if (!covered && s.drawn_title[buf] != tag) {
        s.drawn_title[buf] = tag;
        ui_panel_header(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H },
                        k_tag[tag], ui_theme_color(UI_C_ACCENT));
    }
    const bool data_moved = (s.drawn_push[buf] != s.plot.pushes);
    const bool ctrl_moved = (s.drawn_ctrl[buf] != s.ctrl_rev);
    const bool set_moved  = (s.drawn_set[buf] != s.set_rev);
    const bool out_moved  = (s.drawn_out[buf] != s.out_rev);

    if (ctrl_moved || data_moved) {
        ui_tabs_render(&s.tabs, c);
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
    } else if (s.settings_open) {
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
                            "output not switched on yet", &gfx_font_8x16,
                            ui_theme_color(UI_C_TEXT_FAINT), 1,
                            GFX_ALIGN_CENTER);
            }
        }
    } else if (data_moved || set_moved) {
        gfx_fill_rect(c, LEFT_X + 1, UP_Y + TITLE_H, LEFT_W - 2,
                      UP_H - TITLE_H - 1 - UI_CHAMFER,
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
        ui_button(c, s.reset_rect, "RESET PEAKS",
                  ui_theme_color(UI_C_PANEL_SUNK), s.pressed == P_RESET, true);
    }
    if (ctrl_moved || out_moved) {
        draw_out_button(c);
    }
}

/*
 * Leaving keeps the output as it is.  A supply feeding a servo rail is what
 * the SERVO screen tests against, so walking there must not cut it; STOP
 * does, and so does a supply that stops answering.  The press this screen
 * was routing goes, for the reason MOTOR & ESC's leave() gives, and so do
 * the keypad and the settings: the screen comes back showing the plot.
 */
static void leave(void)
{
    /* A press on OUTPUT OFF when the screen is left -- a second finger on
     * HOME, which the band handles without asking this screen -- is the OFF
     * the operator was making, and it is sent, as cancel() sends one. */
    if (s.on && s.pressed == P_OUTPUT && !s.hold.fired) {
        post_off();
    }
    ui_slider_release(&s.v_slider);
    ui_slider_release(&s.i_slider);
    ui_hold_reset(&s.hold);
    ui_tabs_cancel(&s.tabs);
    ui_keypad_close(&s.kp);
    s.kp_target     = KP_NONE;
    s.settings_open = false;
    /* A change waiting for its question is dropped: nobody is there to
     * answer it. */
    s.confirm_open  = false;
    ui_slider_set(&s.v_slider, s.cv);
    ui_slider_set(&s.i_slider, s.ci);
    let_go();
    ++s.ctrl_rev;
    ++s.out_rev;
    ++s.set_rev;
    supply_invalidate();
}

/*
 * Touch events were lost.  Every gesture is dropped, which asks for nothing,
 * except a tap on OUTPUT OFF: switching off is a press, so a release lost is
 * an OFF the operator made, and it is sent.  An ON this screen has posted and
 * the application has not collected is dropped.  A key held on the keypad is
 * let go of; what was typed stays.
 */
static void cancel(void)
{
    if (s.on && s.pressed == P_OUTPUT && !s.hold.fired) {
        post_off();
    }
    s.pending.on = false;
    s.on_asked   = false;
    ui_slider_release(&s.v_slider);
    ui_slider_release(&s.i_slider);
    /* A drag whose release went missing changes nothing: the slider goes
     * back to the set point the supply has. */
    if (s.pressed == P_V_SLIDER || s.pressed == P_I_SLIDER) {
        ui_slider_set(&s.v_slider, s.cv);
        ui_slider_set(&s.i_slider, s.ci);
        ++s.set_rev;
    }
    ui_hold_reset(&s.hold);
    ui_tabs_cancel(&s.tabs);
    ui_keypad_cancel_press(&s.kp);
    let_go();
    ++s.ctrl_rev;
    ++s.out_rev;
}

static const ui_screen_t k_screen = {
    .title  = "SUPPLY",
    .reset  = reset,
    .enter  = NULL,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *supply_screen(void) { return &k_screen; }
