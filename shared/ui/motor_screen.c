/**
 * @file
 * @brief The motor and ESC (electronic speed controller) bench screen, a
 * workbench (ui_workbench.h) with the tasks LIVE, PROGRAM and SETTINGS.
 *
 * This file is the screen and LIVE's page: the plot or the table, the
 * throttle, the readouts and the CONTROL card on the right.  It holds their
 * state and commands; motor_events.c takes their touch events and frames,
 * motor_draw.c draws them, and motor_live_int.h is what the three share.
 * The strip and the CONTROL card's TARGET row are motor_workbench.c's,
 * PROGRAM's list and its sessions motor_program_page.c's, SETTINGS' page
 * motor_settings_page.c's; the layout they share is motor_screen_int.h.
 * The rail on the right, the readouts and the CONTROL card with TARGET,
 * ARM and RESET PEAKS, is the same on every task, until a session on
 * PROGRAM opens: a full-width session takes the whole body, and the strip
 * row shows the output strip in place of the tabs and the card.
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_screen.h"

#include <stdint.h>
#include <string.h>

#include "ui_pinlist.h"
#include "ui_plot.h"
#include "link_pages.h"
#include "settings.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_value_gate.h"
#include "kst_panel.h"
#include "outputs_screen.h"
#include "port_view.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last but one: motor_screen_int.h's layout macros have short names, which
 * no header included after it may meet. */
#include "motor_screen_int.h"
#include "motor_live_int.h"

const ui_plot_series_t mo_series[S_COUNT] = {
    { "VOLT", "V",   0, 2, 5.0f,    0 },
    { "CURR", "A",   0, 1, 1.0f,    0 },
    { "PWR",  "W",   0, 0, 50.0f,   0 },
    { "RPM",  "RPM", 0, 0, 1000.0f, 0 },
};

/* Power has no flag of its own: it is voltage times current, and it is worth
 * no more than the weaker of the two. */
const uint16_t mo_ok[S_COUNT] = {
    LINK_BN_VOLTAGE_OK, LINK_BN_CURRENT_OK,
    LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK, LINK_BN_RPM_OK,
};

/* The channels' names, looked up where the colours are bound: both follow a
 * setting. */
const ui_text_id_t mo_names[S_COUNT] = { TX_MO_VOLT, TX_MO_CURR,
                                         TX_MO_PWR, TX_MO_RPM };

static const ui_text_id_t k_tab_labels[] = { TX_MO_TAB_PLOT,
                                              TX_MO_TAB_TABLE };

/* The screen's state; motor_live_int.h names it s. */
mo_state_t mo_s;

void motor_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn_push[0] = UINT32_MAX;
    s.drawn_push[1] = UINT32_MAX;
    s.drawn_ctrl[0] = UINT32_MAX;
    s.drawn_ctrl[1] = UINT32_MAX;
    s.drawn_thr[0]  = UINT32_MAX;
    s.drawn_thr[1]  = UINT32_MAX;
    s.drawn_arm[0]  = UINT32_MAX;
    s.drawn_arm[1]  = UINT32_MAX;
    s.drawn_plot[0] = UINT32_MAX;
    s.drawn_plot[1] = UINT32_MAX;
    s.drawn_title[0] = 0xFFu;
    s.drawn_title[1] = 0xFFu;
    s.drawn_strip[0] = UINT32_MAX;
    s.drawn_strip[1] = UINT32_MAX;
    s.drawn_page[0]  = UINT32_MAX;
    s.drawn_page[1]  = UINT32_MAX;
    s.drawn_sess[0]  = UINT32_MAX;
    s.drawn_sess[1]  = UINT32_MAX;
    s.drawn_ostrip[0] = UINT32_MAX;
    s.drawn_ostrip[1] = UINT32_MAX;
}

/* The palette is not a compile-time constant -- it changes with the theme --
 * so the series colours are bound here rather than in the table above. */
void mo_bind_colours(void)
{
    s.plot.series[S_VOLT].color  = ui_theme_color(UI_C_VOLT);
    s.plot.series[S_CURR].color  = ui_theme_color(UI_C_CURR);
    s.plot.series[S_POWER].color = ui_theme_color(UI_C_POWER);
    s.plot.series[S_RPM].color   = ui_theme_color(UI_C_RPM);
    s.slider.color = ui_theme_color(UI_C_ACCENT);
    for (int i = 0; i < S_COUNT; ++i) {
        s.plot.series[i].name = ui_tr(mo_names[i]);
    }
}

/**
 * @brief The screen laid out afresh: the strip, the pages, the plot, PLOT |
 *        TABLE, the throttle's track and step keys, ARM and RESET PEAKS.
 */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
    /* Not zero: a zeroed drawn_* must not read as "already drawn revision 0",
     * or the first frame after a reset skips what it is meant to paint. */
    motor_invalidate();
    mwb_reset();
    msp_reset();
    mpp_reset();
    ui_plot_init(&s.plot, mo_series, S_COUNT,
                 (float)PLOT_W / SAMPLE_HZ);
    /* A screen that has just reset has no run behind it and no bench in
     * front of it, so the trace does not advance until one arms. */
    ui_plot_set_running(&s.plot, false);
    ui_tabs_init_text(&s.tabs, k_tab_labels, MOTOR_PANE_COUNT,
                      (gfx_rect_t){ PLOT_X, SUB_Y, SUB_W, SUB_H });

    const gfx_rect_t track = { TRACK_X, TRACK_Y, TRACK_W, TRACK_H };
    ui_slider_init(&s.slider, track, 0.0f, 100.0f, 0);
    ui_slider_set_ticks(&s.slider, 4);   /* tick marks at the quarters */

    /*
     * A percentage point at each end of the track.  The presets the widget
     * can draw are not used: a row of them under the throttle is one more
     * thing to hit by accident on a bench that is about to spin something.
     */
    s.down_rect = (gfx_rect_t){ (int16_t)(LEFT_X + INNER), CTRL_Y,
                                NUDGE_W, NUDGE_H };
    s.up_rect   = (gfx_rect_t){ (int16_t)(TRACK_X + TRACK_W + 8), CTRL_Y,
                                NUDGE_W, NUDGE_H };

    s.arm_rect   = (gfx_rect_t){ RC_X, ARM_Y, RC_W, ARM_H };
    s.reset_rect = (gfx_rect_t){ RC_X, RESET_Y, RC_W, BTN_H };
    mo_bind_colours();
}

/*
 * Whether the throttle follows its controls: only in UI_VALUE_LIVE.  The one
 * question the slider, its step buttons, the knob and the drawing ask: not
 * on a disarmed bench, which they would otherwise load with a value for the
 * next arm, not between the ARM hold's ask and the bench's answer, and not
 * from a DISARM or a STOP until the bench reports itself disarmed.
 */
bool mo_value_live(void)
{
    return ui_value_live(&s.gate);
}

/* End a drag on the track.  The finger owned the slider in this frame, so
 * the knob stays out of the frame. */
void mo_end_drag(void)
{
    if (s.slider.dragging) {
        s.knob_finger = true;
    }
    ui_slider_release(&s.slider);
}

/*
 * What an edge of the value's state does, in one place.
 *
 * Out of LIVE: the owners of the value end -- a drag, and the knob's
 * command -- and a throttle not yet collected is not sent.  The value shown
 * stays until the bench reports disarmed, which returns it to 0 %.
 *
 * Into LIVE: 0 %, for an arm the hold did not ask for, as for a stick run
 * on PROGRAM.  The hold returned the slider to zero when it asked, and only
 * the application's hook can have set it since.
 */
static void value_edge(ui_value_edge_t edge, ui_value_state_t before)
{
    if (edge == UI_VALUE_EDGE_LEFT) {
        mo_end_drag();
        if (s.pending.kind == MOTOR_CMD_THROTTLE) {
            s.pending.kind = MOTOR_CMD_NONE;
        }
        s.knob_pending = false;
    } else if (edge == UI_VALUE_EDGE_LIVE) {
        if (before != UI_VALUE_ARMING) {
            mo_throttle_from_zero();
        }
    } else {
        return;
    }
    ++s.ctrl_rev;       /* the controls dim, or come back */
    ++s.thr_rev;
}

/*
 * One slot, coalescing.  The pending command is overwritten rather than
 * queued: the application drains it every frame, and the latest throttle
 * position is the one that matters.
 *
 * Exception: a pending DISARM survives everything.  Two taps inside one
 * drain must not let an ARM land on top of a DISARM and re-arm a bench that
 * was stopped a moment before.  A DISARM takes the value out of LIVE where
 * it is posted, collected or not.
 */
void mo_post(motor_cmd_kind_t kind, float value)
{
    s.knob_pending = false;
    if (kind == MOTOR_CMD_DISARM) {
        const ui_value_state_t before = ui_value_state(&s.gate);
        value_edge(ui_value_ask_leave(&s.gate), before);
    }
    if (s.pending.kind == MOTOR_CMD_DISARM && kind != MOTOR_CMD_DISARM) {
        return;
    }
    s.pending.kind  = kind;
    s.pending.value = value;
}

bool motor_screen_poll_cmd(motor_cmd_t *out)
{
    if (s.pending.kind == MOTOR_CMD_NONE) {
        return false;
    }
    if (out != NULL) {
        *out = s.pending;
    }
    s.pending.kind = MOTOR_CMD_NONE;
    s.knob_pending = false;
    return true;
}

void motor_screen_push(const bench_state_t *b)
{
    if (b == NULL) {
        return;
    }
    s.bench = *b;
    for (int i = 0; i < S_COUNT; ++i) {
        if (b->valid && (b->flags & mo_ok[i]) == mo_ok[i]) {
            s.pk_ok[i] = true;
        }
    }
    const float v[S_COUNT] = { b->voltage, b->current, b->power, b->rpm };
    ui_plot_push(&s.plot, v);
    ui_plot_update_scales(&s.plot, PLOT_W);
}

motor_plot_state_t motor_screen_plot_state(void)
{
    /* Read off the widget rather than off the arm state: what the operator
     * is looking at is what the plot holds, and the two part company for one
     * sample at either edge of a run. */
    if (s.plot.running) {
        return MOTOR_PLOT_RECORDING;
    }
    return (s.plot.filled > 0) ? MOTOR_PLOT_HELD : MOTOR_PLOT_EMPTY;
}

int motor_screen_plot_samples(void) { return s.plot.filled; }

bool motor_screen_peak_shown(int channel)
{
    return channel >= 0 && channel < S_COUNT && s.pk_ok[channel];
}

/* ARM takes no press while the servo programmer holds a port; DISARM
 * does. */
bool mo_arm_locked(void)
{
    return kst_panel_holds_port() && !s.gate.armed;
}

void motor_screen_cancel_arm(void)
{
    /* A stop latched; see the servo screen's own for why the command is
     * dealt with first and on its own account. */
    bool changed = false;
    /* The stop drops the arm that was asked for, and an armed bench is on
     * its way down from here. */
    const ui_value_state_t before = ui_value_state(&s.gate);
    value_edge(ui_value_ask_leave(&s.gate), before);
    if (s.pending.kind == MOTOR_CMD_ARM) {
        s.pending.kind = MOTOR_CMD_NONE;
        changed = true;
    }
    if (s.pressed == 1) {
        s.pressed = 0;
        ++s.ctrl_rev;
        changed = true;
    }
    if (s.arm.down || s.arm.held_s > 0.0f) {
        changed = true;
    }
    if (changed) {
        ui_hold_reset(&s.arm);
        ++s.arm_rev;
    }
}

/*
 * An arm starts from nothing and a disarm ends at nothing: the slider, the
 * readout and the step buttons, which all read the slider's value, are at
 * 0 %.  A throttle waiting to be collected was set on the other side of the
 * edge and goes with it, and so does what the knob would put back.  A drag
 * under way is released, so a finger resting on the track commands nothing
 * until it presses again: ui_slider_set() re-anchors a drag on the new value
 * and keeps its origin, and the next move would add the whole travel since
 * the press to 0 %.
 */
void mo_throttle_from_zero(void)
{
    mo_end_drag();
    ui_slider_set(&s.slider, 0.0f);
    if (s.pending.kind == MOTOR_CMD_THROTTLE) {
        s.pending.kind = MOTOR_CMD_NONE;
    }
    s.knob_pending = false;
    ++s.thr_rev;
}

void motor_screen_set_armed(bool armed)
{
    const bool was = s.gate.armed;
    const ui_value_state_t before = ui_value_state(&s.gate);
    value_edge(ui_value_report(&s.gate, armed), before);
    /*
     * The run boundary follows the bench, not the gate.  leave() reads the
     * bench as disarmed for a disarm this screen has only asked for, while
     * the application goes on reporting the bench as armed until its own
     * state catches up.  Hanging the clear on the gate would erase the run
     * at the moment of asking to end it.
     */
    if (s.bench_armed != armed) {
        s.bench_armed = armed;
        if (armed) {
            /* A run's trace is that run's.  Cleared at the arm rather than
             * at the disarm, so the last run stays readable until the next
             * one starts.  Its peaks start again too. */
            ui_plot_clear(&s.plot);
            memset(s.pk_ok, 0, sizeof(s.pk_ok));
        } else {
            /* The value shown on a disarmed bench is 0 %, and no control
             * moves it from there (mo_value_live()).  A throttle not yet
             * collected is not sent to the disarmed bench. */
            mo_throttle_from_zero();
        }
        ui_plot_set_running(&s.plot, armed);
    }
    if (was != armed) {
        if (armed) {
            ui_hold_reached(&s.arm);
        } else if (ui_hold_left(&s.arm)) {
            /* The bench disarmed under a finger still down on the button and
             * the hold ended with it, so this screen drops the press it was
             * routing.  Why both halves end together is in ui_hold_left(). */
            s.pressed = 0;
            ++s.ctrl_rev;
        }
        ++s.arm_rev;
        ++s.ctrl_rev;
    }
}

/*
 * The kV the connected ESC reports, or 0 when it reports none.  Preferred
 * over SET_MOTOR_KV: the part under test knows its own rating and a value
 * typed in months ago may belong to a different motor.
 *
 * Nothing calls this yet.  No ESC parameter set is wired to the link: the
 * OpenYGE cache is built and unconnected, and BLHeli_32's parameters are not
 * published.  Until one is, the field is whatever the operator entered.
 */
void motor_screen_set_esc_kv(int kv)
{
    if (kv < 0) {
        kv = 0;
    }
    if (s.esc_kv != kv) {
        s.esc_kv = kv;
        ++s.ctrl_rev;
    }
}

void motor_screen_set_throttle_pins(uint64_t gpio_mask, bool known)
{
    if (!known) {
        gpio_mask = 0u;
    }
    if (gpio_mask != s.thr_pins || known != s.thr_pins_known) {
        s.thr_pins       = gpio_mask;
        s.thr_pins_known = known;
        ++s.ctrl_rev;                   /* the TARGET row says it */
    }
}

void motor_screen_set_throttle_protocol(uint8_t proto)
{
    if (proto != s.thr_proto) {
        s.thr_proto = proto;
        ++s.ctrl_rev;
    }
}

/* Whether the bench is SAFE as this screen sees it: disarmed, and no ARM
 * or DISARM asked and not yet answered (ui_value_gate.h's OFF). */
bool mo_bench_safe(void)
{
    return ui_value_state(&s.gate) == UI_VALUE_OFF && !s.bench_armed;
}

/* Whether Idle pulse and Full pulse may change: SAFE, and ARM not held.  An
 * endpoint edit waits ENDPOINTS_SETTLE_MS on the panel before it goes to the
 * IOmcu, and the switch to ARMED sends no CHAN_CFG; an edit made under a
 * held ARM could be overtaken by the arm and run on the old range.  With ARM
 * down the keypad of an endpoint closes, and the 2 s hold leaves an edit
 * made before it the time to go out. */
bool mo_endpoints_free(void)
{
    return mo_bench_safe() && s.pressed != 1;
}

/* The TARGET row takes no press while the bench is not SAFE or ARM is held,
 * and while the servo programmer holds its port, which locks ARM too: what
 * the throttle drives changes only on a bench at rest. */
bool mo_target_locked(void)
{
    return !mo_bench_safe() || s.pressed == 1 || kst_panel_holds_port();
}

bool motor_screen_target_locked(void) { return mo_target_locked(); }

int motor_screen_task(void) { return (int)mwb_task(); }

bool motor_screen_session_shown(void) { return mpp_shown() != NULL; }

int motor_screen_pane(void) { return s.tabs.selected; }

float motor_screen_throttle(void) { return s.slider.value; }
void motor_screen_set_throttle(float pct)
{
    ui_slider_set(&s.slider, pct);
    ++s.thr_rev;
}

void motor_screen_knob_frame(void)
{
    s.knob_finger = false;
}

void motor_screen_knob_cancel(void)
{
    if (!s.knob_pending) {
        return;
    }
    s.knob_pending = false;
    ui_slider_set(&s.slider, s.knob_prev);
    ++s.thr_rev;
    if (s.knob_had_cmd) {
        s.pending.value = s.knob_prev;
    } else {
        s.pending.kind = MOTOR_CMD_NONE;
    }
}

void motor_screen_knob(float span_fraction)
{
    /*
     * The knob posts only when the slot is empty or holds a throttle, which
     * it may replace as the slider does.  Any other pending command -- an arm
     * that completed in this frame's tick, a disarm, a peak reset -- is not
     * the knob's to overwrite, and its delta is dropped, not held for later.
     * On SETTINGS the throttle is out of sight, and the knob moves nothing.
     */
    if (span_fraction == 0.0f || s.slider.dragging || s.knob_finger
        || mwb_task() != UI_WB_LIVE
        || (s.pending.kind != MOTOR_CMD_NONE
            && s.pending.kind != MOTOR_CMD_THROTTLE)) {
        return;
    }
    /* A turn while the value is not live moves nothing and is not kept: the
     * next turn after an arm starts from 0 %. */
    if (!mo_value_live()) {
        return;
    }
    const float before = s.slider.value;
    const bool  was_knob = s.knob_pending;
    const bool  had_cmd  = s.pending.kind == MOTOR_CMD_THROTTLE;
    motor_screen_set_throttle(before
                              + span_fraction * (s.slider.max - s.slider.min));
    if (s.slider.value != before) {
        mo_post(MOTOR_CMD_THROTTLE, s.slider.value);
        if (!was_knob) {
            s.knob_prev    = before;
            s.knob_had_cmd = had_cmd;
        }
        s.knob_pending = true;
    }
}

/*
 * Leaving disarms.  Navigating away from an armed bench must not leave a
 * propeller spinning behind a screen that is not visible, and the command
 * carries the disarm rather than relying on the application to infer it
 * from the navigation.
 */
static void leave(void)
{
    /* No release will arrive for a finger that is on the track as the screen
     * changes, and a latched drag outlives the gesture. */
    ui_slider_release(&s.slider);
    mo_post(MOTOR_CMD_DISARM, 0.0f);
    {
        const ui_value_state_t before = ui_value_state(&s.gate);
        value_edge(ui_value_hide(&s.gate), before);
    }
    /* Neither animation should still be running when the screen comes back. */
    ui_hold_reset(&s.arm);
    /*
     * And the press this screen was routing goes with it.  No release will
     * arrive for a finger that was on ARM as the screen changed -- the UP
     * belongs to whatever is up now -- and a press left recorded would own
     * the button for ever: the guard that keeps a hold with the contact that
     * began it refuses every later press while one is held.
     */
    s.pressed    = 0;
    s.have_press = false;
    /* SETTINGS' keypad closes, what it held typed with it; the task stays
     * for a way back from OUTPUTS (mwb_enter()).  PROGRAM's session closes,
     * a run in it ending. */
    msp_leave();
    mpp_leave();
    mwb_cancel();
    ++s.ctrl_rev;
    ++s.arm_rev;
}

/* A tile opens LIVE; the way back from OUTPUTS, which the TARGET row
 * opened, the task it was opened from. */
static void enter(void)
{
    mwb_enter();
    mpp_enter();
    motor_invalidate();
    ++s.ctrl_rev;
}

/*
 * The home tag: over SETTINGS' keypad it closes the keypad, as its CANCEL
 * does, and the page stays.  In PROGRAM's session it climbs one level: what
 * covers the page closes, a run ends, the profile's page goes back to the
 * list, the models to the makers, the makers out of the session to
 * PROGRAM's list.  The tasks are siblings, not levels: otherwise the tag
 * leaves the screen, which switches the bench to SAFE (leave()).
 */
static bool back(void)
{
    if (!msp_back() && !mpp_back()) {
        return false;
    }
    motor_invalidate();
    ++s.ctrl_rev;
    return true;
}

/*
 * Touch events were lost between two frames, so this screen's record of what
 * is on the glass cannot be trusted.  Drop the gesture rather than let a
 * hold that completes on a timer finish on a contact that may have gone.
 * Nothing is commanded here: a gesture abandoned part way asks for nothing,
 * which is what letting go early already does.
 */
static void cancel(void)
{
    /*
     * Abandoning a gesture asks for nothing, and on an armed bench that is
     * the wrong direction for one of them.  Disarming is a press: the
     * release is the whole command, so a release that went missing is a
     * DISARM the operator made and the bench never saw, and dropping it
     * silently leaves the outputs driving until somebody notices.  Arming
     * has already sent its command by the time the finger lifts, so an arm
     * gesture cancelled part way asks for nothing, which is correct.
     */
    if (s.gate.armed && s.pressed == 1 && !s.arm.fired) {
        mo_post(MOTOR_CMD_DISARM, 0.0f);
    }
    /*
     * And an arm this screen has posted but the application has not yet
     * collected.  A command is forwarded on the frame after the one that
     * posted it, and the frame that observes a loss cancels before that
     * forwarding, so an arm completed by a hold whose contact had already
     * gone is dropped here rather than reaching the bench.  An arm already
     * collected is past this screen's reach; it carries the loss count it
     * was posted under, and the control task drops one whose count has
     * moved.  A disarm is kept: it is the direction that fails safe.
     */
    if (s.pending.kind == MOTOR_CMD_ARM) {
        s.pending.kind = MOTOR_CMD_NONE;
    }
    /* Collected or not, the arm asked for is dropped across a loss, so an
     * arm that comes later is not this hold's. */
    ui_value_drop_arm(&s.gate);
    ui_slider_release(&s.slider);
    ui_hold_reset(&s.arm);
    /* And the tab row: a press it kept would take a later contact's release
     * for its own and switch the pane. */
    ui_tabs_cancel(&s.tabs);
    /* And the strip's, the TARGET row's and SETTINGS' presses, for the
     * same reason. */
    mwb_cancel();
    msp_cancel();
    mpp_cancel();
    s.pressed    = 0;
    s.have_press = false;
    ++s.ctrl_rev;
    ++s.arm_rev;
}

/* The title keeps its 11 characters, the one title over 10 (owner's
 * decision): its home tag ends at x 226, over the band's chip row. */
static const ui_screen_t k_screen = {
    .title  = "MOTOR & ESC",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = mo_tick,
    .event  = mo_event,
    .cancel = cancel,
    .render = mo_render,
    .back   = back,
    .safe_at = mo_safe_at,
};

const ui_screen_t *motor_screen(void) { return &k_screen; }
