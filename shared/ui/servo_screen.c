/**
 * @file
 * @brief The servo bench screen.
 *
 * The horn is the control: dragging the drawn arm commands the servo, and
 * the arm is drawn at the measured position, so a servo that is slow, stuck
 * or fighting a linkage lags the finger by that much.
 *
 * A workbench (ui_workbench.h): the strip under the band shows LIVE, the
 * dial; TEST, the run cards; SETTINGS, the servo's type, frame rate and
 * pulse widths, the sweep and the device under test.  The right card is
 * the output card on every task.  The strip, the TEST page and the TARGET
 * row are servo_workbench.c; the pages of rows and the panels they open
 * are servo_settings_panel.c; the state and the layout the files share are
 * servo_screen_int.h.  This file is the state, the profiles, the command
 * and the screen's hooks; the sweep and the frame step are
 * servo_screen_sweep.c, what the panel reports servo_screen_link.c, the
 * presses servo_screen_touch.c and the drawing servo_screen_draw.c, with
 * servo_screen_core_int.h between them.
 *
 * A type or a frame rate that can destroy a servo not made for it -- a
 * heli profile, anything above 60Hz -- is applied only after a warning
 * held for two seconds, and is never kept past a restart.
 *
 * START TEST, on TEST's AUTOMATIC TEST card, runs the automatic test
 * (servo_test.h): the screen hands it the supply's samples and the clock,
 * and does what it asks -- the servo's position, SUPPLY's set points and
 * output -- through the same paths a finger uses.  The run's code is
 * servo_test_panel.c.  START LIMIT RUN, on the LIMIT RUN card, runs the
 * limit run (servo_limrun.h) the same way; its code is
 * servo_limit_panel.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen.h"

#include <string.h>

#include "kst_panel.h"
#include "outputs.h"
#include "servo_limrun.h"
#include "servo_sweep.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_text.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/* The sample rate of the panel's loop, which is the power plot's time base. */
#define SAMPLE_HZ 20.0f

/*
 * The profiles the bench drives.  STANDARD PWM is the safe one and the one
 * every restart starts at.  The heli profiles are Rotorflight's for digital
 * cyclic and narrow-band tail servos: 1520us centre, +/-700us, up to
 * 333Hz; and 760us centre, +/-350us, up to 560Hz.
 */
static const uint16_t k_std_rates[]  = { 50, 60, 100, 150, 200, 250, 300, 333 };
static const uint16_t k_cyc_rates[]  = { 50, 120, 200, 333 };
static const uint16_t k_tail_rates[] = { 200, 333, 560 };

#define RATES(a) (a), (uint8_t)(sizeof(a) / sizeof((a)[0]))
const servo_type_t k_types[] = {
    { "STANDARD PWM",  1000, 1500, 2000, RATES(k_std_rates),  50,  0,   false },
    { "NARROW 760",    660,  760,  860,  RATES(k_std_rates),  50,  0,   false },
    { "WIDE",          800,  1500, 2200, RATES(k_std_rates),  50,  0,   false },
    { "HELI CYCLIC",   820,  1520, 2220, RATES(k_cyc_rates),  333, 333, true  },
    { "HELI TAIL 760", 410,  760,  1110, RATES(k_tail_rates), 560, 560, true  },
};
_Static_assert(sizeof(k_types) / sizeof(k_types[0]) == TYPE_COUNT,
               "TYPE_COUNT in servo_screen_int.h counts k_types");

/* SETTINGS' pages' names. */
static const ui_text_id_t k_pages[PG_COUNT] = {
    TX_SV_PG_OUTPUT, TX_SV_PG_SWEEP, TX_SV_PG_DUT,
};

/* The power plot's series (PS_*, servo_screen_core_int.h). */
static const ui_plot_series_t k_power[PS_COUNT] = {
    { "V", "V", 0, 2, 5.0f, 0 },
    { "A", "A", 0, 2, 0.5f, 0 },
    { "W", "W", 0, 1, 5.0f, 0 },
};

/* The state, its type in servo_screen_int.h. */
servo_screen_state_t s;

/* ------------------------------------------------------------- conversions */

/*
 * Each side of centre runs to its own end: MIN, CENTRE and MAX are set one
 * by one, so -90deg is MIN and +90deg is MAX however far each lies from
 * the centre.
 */
static float half_travel_us(bool below)
{
    return below ? (float)s.centre_us - (float)s.min_us
                 : (float)s.max_us - (float)s.centre_us;
}

float svs_deg_to_us_f(float deg)
{
    const float d = s.reverse ? -deg : deg;
    float us = (float)s.centre_us + (float)s.trim_us
               + d / 90.0f * half_travel_us(d < 0.0f);
    if (us < (float)s.min_us) { us = (float)s.min_us; }
    if (us > (float)s.max_us) { us = (float)s.max_us; }
    return us;
}

uint16_t svs_deg_to_us(float deg)
{
    return (uint16_t)(svs_deg_to_us_f(deg) + 0.5f);
}

float us_to_deg_f(float us)
{
    const float off = us - (float)s.centre_us - (float)s.trim_us;
    const float half = half_travel_us(off < 0.0f);
    if (half <= 0.0f) {
        return 0.0f;
    }
    const float d = off * 90.0f / half;
    return s.reverse ? -d : d;
}

/* The mapping in force now. */
servo_map_t svs_map_now(void)
{
    const servo_map_t m = { s.min_us, s.centre_us, s.max_us, s.trim_us,
                            s.reverse, s.travel_deg };
    return m;
}

/* A command of @p m's range as a pulse, and a pulse as @p m's angle: what
 * svs_cmd_to_us() and us_to_deg_f() are for the mapping in force. */
float svs_map_cmd_to_us(const servo_map_t *m, float cmd)
{
    const unsigned below = (unsigned)m->centre_us - (unsigned)m->min_us;
    const unsigned above = (unsigned)m->max_us - (unsigned)m->centre_us;
    const float half = (float)((below > above) ? below : above);
    return (float)m->centre_us - half + 2.0f * half * cmd / (float)OUT_SPAN;
}

float svs_map_us_to_deg(const servo_map_t *m, float us)
{
    const float off = us - (float)m->centre_us - (float)m->trim_us;
    const float half = (off < 0.0f)
                       ? (float)m->centre_us - (float)m->min_us
                       : (float)m->max_us - (float)m->centre_us;
    if (half <= 0.0f) {
        return 0.0f;
    }
    float d = off * 90.0f / half;
    if (m->reverse) {
        d = -d;
    }
    if (d < -m->travel_deg) { d = -m->travel_deg; }
    if (d >  m->travel_deg) { d =  m->travel_deg; }
    return d;
}

/* A pulse as the far end's command, 0..OUT_SPAN of the range a command
 * carries, and back: the units it slews in. */
float svs_us_to_cmd(float us)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (hi > lo) ? (us - (float)lo) * (float)OUT_SPAN / (float)(hi - lo)
                     : (float)OUT_SPAN / 2.0f;
}

float svs_cmd_to_us(float cmd)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (float)lo + (float)(hi - lo) * cmd / (float)OUT_SPAN;
}

/*
 * The angle the pin drives on an armed bench while nothing commands it: the
 * outputs layer's rest for a surface, in the range a command carries.  The
 * midpoint of that range, so PULSE CENTRE without the trim: 1500us for
 * STANDARD PWM, 760us for NARROW 760.
 */
float svs_rest_deg(void)
{
    return us_to_deg_f(
        svs_cmd_to_us((float)outputs_role_rest(OUT_ROLE_SURFACE)));
}


/*
 * RELEASE posted.  On an armed bench the pins go to every surface's rest,
 * so the value shown is that rest, as at an arm: a drag under way ends, the
 * value follows the rest through a change of profile (at_rest), and the
 * next input starts from it.  The horn is drawn towards it at SPEED.  While
 * the value is not live, and behind an arm or a disarm waiting to be taken,
 * which the release does not replace (post()), the value stays.
 */
void release(void)
{
    post(SERVO_CMD_RELEASE, 0);
    if (value_live() && s.pending.kind == SERVO_CMD_RELEASE) {
        s.dragging      = false;
        s.commanded_deg = svs_rest_deg();
        s.at_rest       = true;
    }
    ++s.ctrl_rev;
}

uint16_t svs_slew_of(int pct)
{
    if (pct >= 100) {
        return 0u;
    }
    if (pct < 1) {
        pct = 1;
    }
    return (uint16_t)((unsigned)SPEED_FULL_SPAN_S * (unsigned)pct / 100u);
}

void post(servo_cmd_kind_t kind, uint16_t us)
{
    /*
     * A pending disarm survives everything.  One command is held at a time,
     * so a position landing on top of a disarm would drive a bench somebody
     * has just asked to stop.
     */
    if (s.pending.kind == SERVO_CMD_DISARM && kind != SERVO_CMD_DISARM) {
        return;
    }
    /*
     * And an arm that has completed survives everything but a disarm.  The
     * application drains these between frames, so a hold that finishes in
     * tick() waits a frame to be read; a touch landing in that gap would
     * throw away two seconds of gesture and leave the bench unarmed with
     * nothing on screen to say why.
     */
    if (s.pending.kind == SERVO_CMD_ARM && kind != SERVO_CMD_DISARM
        && kind != SERVO_CMD_ARM) {
        return;
    }
    /* Any other command takes the horn from the drag's filter. */
    svs_drag_on_post(kind, us);
    /* A resume not yet taken stays one when the sweep is said again. */
    const bool resume = kind == SERVO_CMD_SWEEP
                        && s.pending.kind == SERVO_CMD_SWEEP
                        && s.pending.resume;
    /* And a start not yet taken stays the start, as it will be sent. */
    const bool carry = kind == SERVO_CMD_SWEEP
                       && s.pending.kind == SERVO_CMD_SWEEP
                       && s.start_rec.live
                       && s.pending.start_seq == s.start_rec.seq;
    /* Any command but the HOLD replaces the curve at the far end: what is
     * drawn stops being drawn on. */
    if (kind != SERVO_CMD_HOLD) {
        s.dr.on = false;
    }
    s.pending.kind       = kind;
    s.pending.value_us   = us;
    s.pending.resume     = resume;
    s.pending.from_pause = s.lineage;
    ++s.post_count;
    if (kind == SERVO_CMD_SWEEP) {
        ++s.start_seq;
        s.pending.start_seq = s.start_seq;
        s.sent_speed[s.start_seq % 8u].seq       = s.start_seq;
        s.sent_speed[s.start_seq % 8u].speed_pct = (int16_t)s.speed_pct;
        if (carry) {
            s.start_rec.seq       = s.start_seq;
            s.start_rec.cfg       = s.sw.cfg;
            s.start_rec.speed_pct = s.speed_pct;
        }
        s.posted_map = svs_map_now();
    }
    if (kind == SERVO_CMD_ARM) {
        s.arm_profile_rev = s.profile_rev;
        ui_value_ask_arm(&s.gate);
    } else if (kind == SERVO_CMD_DISARM) {
        ui_value_drop_arm(&s.gate);
    }
    /* The range travels with the pulse: the panel configures the channel
     * from it, and a narrow servo's 760us centre is below a standard
     * servo's floor.  The frame rate goes with it. */
    cmd_range(&s.pending.min_us, &s.pending.max_us);
    s.pending.frame_hz = s.frame_hz;
    s.pending.slew_per_s = svs_slew_of(s.speed_pct);
    /* The grip only breathes while something is actually being held, so this
     * has to follow the command rather than the screen being open. */
    s.driving = (kind == SERVO_CMD_POSITION || kind == SERVO_CMD_CENTRE
                 || kind == SERVO_CMD_SWEEP || kind == SERVO_CMD_HOLD);
    if (s.driving) {
        s.at_rest = false;
    }
    /* Paused while the hold is the command in force: anything sent after it
     * replaces it at the far end, and the button goes with it. */
    if (s.paused != (kind == SERVO_CMD_HOLD)) {
        s.paused = (kind == SERVO_CMD_HOLD);
        ++s.ctrl_rev;
    }
    const bool sw = kind == SERVO_CMD_SWEEP;
    s.pending.ends_sweep     = s.sweep_ended && !sw;
    if (!sw) {
        s.sweep_ended = false;
    }
    s.pending.sweep_kind     = sw ? (uint16_t)s.sw.cfg.kind : 0u;
    s.pending.sweep_mhz      = sw ? s.sw.cfg.mhz : 0u;
    s.pending.sweep_span     = sw ? s.sw.cfg.amplitude : 0u;
    s.pending.sweep_dwell_ms = sw ? s.sw.cfg.dwell_ms : 0u;
}

/*
 * Say the position again under a mapping that has just changed.
 *
 * The pulse a command carries is the angle put through the profile, the
 * trim, the reverse and the travel; change any of them while an output is
 * held and the pulse on the pin belongs to the old one.  Switching a held
 * servo from STANDARD PWM to NARROW 760 would otherwise leave 1500us on a
 * servo whose maximum is 860 while the screen shows the new range.
 */

void reissue(void)
{
    /* A run times a servo driven as it was when it started. */
    test_end_now(SERVO_TEST_AB_SETTINGS);
    ++s.profile_rev;
    if (s.pending.kind == SERVO_CMD_ARM) {
        /*
         * An arm asked for and not yet collected carries the profile it was
         * asked under, and the panel centres the surfaces under it before it
         * arms: it carries the one in force instead.  One already collected
         * is answered when the arm lands; see servo_screen_set_armed().
         */
        cmd_range(&s.pending.min_us, &s.pending.max_us);
        s.pending.frame_hz   = s.frame_hz;
        s.pending.slew_per_s = svs_slew_of(s.speed_pct);
        s.arm_profile_rev    = s.profile_rev;
        return;
    }
    if (!value_live()) {
        /* Nothing that drives is said again while the value is not live.
         * An arm on its way is answered with the rest, as below, and so is
         * a bench on its way down. */
        if (s.gate.armed || s.gate.arm_asked) {
            release();
            s.arm_profile_rev = s.profile_rev;
        }
        return;
    }
    if (s.sweeping) {
        /*
         * Said again under the profile now in force -- its range and its
         * frame rate, which a changed type or rate moves even where the
         * curve in command units stays the same.  A curve that did change
         * is started over by tick(); one that did not carries on, here and
         * at the far end.
         */
        const sweep_cfg_t now = sweep_cfg_now();
        if (svs_same_sweep(&now, &s.sw.cfg)) {
            post(SERVO_CMD_SWEEP, 0);
        } else {
            /* The curve changed with the range -- MIN, CENTRE, MAX or
             * TRAVEL move its amplitude -- so it starts over now, rather
             * than going out once with the old amplitude under the new
             * range before tick() catches it. */
            svs_restart_sweep();
        }
        return;
    }
    if (s.driving) {
        /* Paused with the curve still drawn on: held where it is drawn. */
        if (s.dr.on) {
            s.dr.on = false;
            s.commanded_deg = clamp_travel(s.shown_deg);
        }
        /* The held position said again under the new rate or range: the
         * drag's filter, settling, goes on (svs_drag_on_post()). */
        s.df.restating = true;
        post(SERVO_CMD_POSITION, svs_deg_to_us(s.commanded_deg));
        s.df.restating = false;
    } else if (s.gate.armed || s.gate.arm_asked) {
        /*
         * Resting on an armed bench: the rest restated under the profile now
         * in force, its range and its frame rate, which the panel orders so
         * a faster rate never meets wider pulses.  Left alone, a narrow
         * servo would rest at a standard servo's centre, past its stop, and
         * a servo would go on running at a heli rate after STANDARD PWM was
         * chosen.
         *
         * An arm on its way is answered the same way, at once: the panel
         * takes commands in order and arms only once the release it owes is
         * written, so a release that reaches it before the arm lands is the
         * profile the pins arm under.
         */
        release();
        s.arm_profile_rev = s.profile_rev;
    }
}

/* A position from an input.  Refused, with nothing changed and nothing
 * posted, while the value is not live (value_live()). */
bool svs_command(float deg)
{
    if (!value_live()) {
        return false;
    }
    s.commanded_deg = clamp_travel(deg);
    post(SERVO_CMD_POSITION, svs_deg_to_us(s.commanded_deg));
    ++s.dial_rev;
    return true;
}

/*
 * A motion command still waiting to be taken as the bench stops driving --
 * a disarm, or a stop -- is not sent: the pins carry no pulse, and a
 * position written to a disarmed far end is a value nobody watches change.
 * The knob's is withdrawn with the value it replaced; any other is dropped.
 * A release, an arm and a disarm stay.
 */
void svs_drop_motion(void)
{
    svs_knob_withdraw();
    svs_drag_filter_end();      /* and the drag's filter commands no more */
    const servo_cmd_kind_t k = s.pending.kind;
    if (k == SERVO_CMD_POSITION || k == SERVO_CMD_CENTRE
        || k == SERVO_CMD_SWEEP || k == SERVO_CMD_HOLD) {
        /* What it was to end at the far end, the next command still ends. */
        s.sweep_ended  = s.sweep_ended || s.pending.ends_sweep;
        s.pending.kind = SERVO_CMD_NONE;
    }
    /* And no undo of a tap whose command went with it. */
    s.toggle_live = false;
}

/* ------------------------------------------------------------- the profile */

/**
 * @brief A profile goes into force: a type with its own pulse widths when it is a
 *        different type, and a frame rate no faster than its pulses allow.
 * @param t the profile's index in k_types
 * @param hz the frame rate asked for
 */
void apply_profile(int t, uint16_t hz)
{
    if (t != s.type) {
        s.type      = t;
        s.min_us    = k_types[t].min_us;
        s.centre_us = k_types[t].centre_us;
        s.max_us    = k_types[t].max_us;
    }
    const uint16_t top = max_rate_for(s.type, cmd_top());
    s.frame_hz = (hz > top) ? top : hz;
    reissue();
    ++s.ctrl_rev;
}

/**
 * @brief A profile asked for: one that can destroy a servo not made for it opens
 *        the warning, and goes into force only once that has been held; any other
 *        goes into force at once.
 * @param t the profile's index in k_types
 * @param hz the frame rate asked for
 */
void ask_profile(int t, uint16_t hz)
{
    const uint16_t pulses = (t != s.type) ? k_types[t].max_us : cmd_top();
    const uint16_t top = max_rate_for(t, pulses);
    if (hz > top) {
        hz = top;
    }
    if (dangerous(t, hz)) {
        s.warn.open = true;
        s.warn.type = t;
        s.warn.hz   = hz;
        s.warn.down = false;
        ui_hold_reset(&s.warn.hold);
        ++s.warn.rev;
        ++s.ctrl_rev;
        return;
    }
    apply_profile(t, hz);
}

/* ------------------------------------------------------------------ layout */

void servo_invalidate(void)
{
    s.drawn_mask = 0;
    for (int b = 0; b < 2; ++b) {
        s.drawn_ctrl[b]  = UINT32_MAX;
        s.drawn_arm[b]   = UINT32_MAX;
        s.drawn_power[b] = UINT32_MAX;
        s.drawn_cur[b]   = UINT32_MAX;
        s.drawn_sup[b]   = UINT32_MAX;
        s.drawn_warn[b]  = UINT32_MAX;
        s.drawn_ask[b]   = UINT32_MAX;
        s.drawn_test[b]  = UINT32_MAX;
        s.drawn_save[b]  = 0xFFu;
        /* No step the arm can be drawn at, so the next frame draws it. */
        s.drawn_pulse[b] = -1;
    }
}

/**
 * @brief The screen at its start: the state cleared, STANDARD PWM at 50Hz,
 *        and the output card's controls, the strip, the sessions, the plot
 *        and SETTINGS' tabs laid out.
 */
static void reset(void)
{
    if (s.lim_end_due) {
        kst_panel_run_end();
    }
    memset(&s, 0, sizeof(s));
    s.drive_pin   = SERVO_SCREEN_NO_PIN;
    servo_test_init(&s.test);
    test_tell();                /* a run the reset took with it is over */
    s.test_sig = test_signature();
    servo_invalidate();
    s.drawn_mask    = 0;
    s.travel_deg    = 90.0f;
    s.speed_pct     = 100;
    s.surfaces      = true;
    s.link_up       = true;     /* until the panel says otherwise */
    s.source        = SERVO_SOURCE_MODEL;   /* likewise */
    s.cur_key       = CUR_KEY_NONE;
    s.shown_cmd     = (float)OUT_SPAN / 2.0f;
    /* STANDARD PWM at 50Hz: what every restart starts at, whatever the
     * session before it used. */
    s.type      = 0;
    s.min_us    = k_types[0].min_us;
    s.centre_us = k_types[0].centre_us;
    s.max_us    = k_types[0].max_us;
    s.frame_hz  = k_types[0].default_hz;

    ui_slider_init(&s.speed, (gfx_rect_t){ RC_X, SLIDER_Y, RC_W, SLIDER_H },
                   10.0f, 100.0f, 0);
    s.speed.value = 100.0f;
    ui_slider_set_ticks(&s.speed, 0);
    /* A sweep speed commands nothing on its own: a tap is how it is used.
     * The throttle's rule is the throttle's. */
    ui_slider_set_tap_to_set(&s.speed, true);

    /* CENTRE, SWEEP and RELEASE, a third of the column each. */
    const int bw = (RC_W - 2 * BTN_GAP) / 3;
    s.centre_btn  = (gfx_rect_t){ RC_X, BTN_Y, (int16_t)bw, BTN_H };
    s.sweep_btn   = (gfx_rect_t){ (int16_t)(RC_X + bw + BTN_GAP), BTN_Y,
                                  (int16_t)bw, BTN_H };
    s.release_btn = (gfx_rect_t){ (int16_t)(RC_X + 2 * (bw + BTN_GAP)), BTN_Y,
                                  (int16_t)(RC_W - 2 * (bw + BTN_GAP)),
                                  BTN_H };
    /* Full width and last, under the three that only shape what is commanded:
     * this is the one that decides whether anything is driven at all. */
    s.arm_btn     = (gfx_rect_t){ RC_X, ARM_Y, RC_W, ARM_H };
    s.out_btn     = (gfx_rect_t){ SUP_OUT_X, SUP_Y,
                                  (int16_t)(RC_X + RC_W - SUP_OUT_X), SUP_H };
    wb_init();
    prog_init();
    servo_limrun_init(&s.lim);
    s.lim_sig = lim_signature();

    ui_plot_init(&s.power, k_power, PS_COUNT, (float)RC_W / SAMPLE_HZ);
    ui_tabs_init_text(&s.tabs, k_pages, PG_COUNT,
                 (gfx_rect_t){ (int16_t)(OV_X + 10), OV_TAB_Y, OV_TAB_W,
                               OV_TAB_H });
    s.kp.pressed = -1;
    s.tk.pressed = -1;
    s.ch.pressed = -1;
}

/* --------------------------------------------------------- the automatic test */

/* The run is servo_test_panel.c.  What both runs and the screen use stays
 * here: the panel told whether a run is under way, and DISARM asked. */

/*
 * Whether a run is under way, told to the panel where it changes
 * (servo_screen_on_testing()): after every call that can start or end the
 * engine's run.  Kept outside the screen's state, which a reset clears.
 */
static void (*s_testing_told)(bool running);
static bool s_testing_last;

void test_tell(void)
{
    const bool running = servo_test_running(&s.test)
                         || servo_limrun_running(&s.lim);
    if (running != s_testing_last) {
        s_testing_last = running;
        if (s_testing_told != NULL) {
            s_testing_told(running);
        }
    }
}

void servo_screen_on_testing(void (*told)(bool running))
{
    s_testing_told = told;
}

/*
 * DISARM asked of an armed bench: the value leaves LIVE here and not at the
 * bench's answer (value_edge()).  The release a run ends with is replaced
 * by the disarm, which lets go of the output.
 */
void svs_ask_disarm(void)
{
    value_edge(ui_value_ask_leave(&s.gate), SERVO_TEST_AB_DISARMED);
    post(SERVO_CMD_DISARM, 0);
}

void servo_screen_service(void)
{
    lim_leave_follow();
    test_ended();
    test_restore_service();
}

void servo_screen_clock(uint32_t now_ms)
{
    s.now_ms   = now_ms;
    s.have_now = true;
}

/*
 * Leaving releases the output: a screen that is not visible must not hold
 * the servo somewhere, the same rule as the motor bench's disarm on leave.
 * The overlay closes with it, and a warning not held is a profile not
 * applied.  A supply this screen switched on -- OUTPUT ON, a test, the
 * programmer's CONNECT -- goes off with it; the programmer's overlay, the
 * keypads and the questions are this screen's own and are no leave.
 */
static void leave(void)
{
    /* No release arrives for a finger on the speed slider or on the dial as
     * the screen changes, and a latched drag outlives the gesture. */
    ui_slider_release(&s.speed);
    svs_drag_end();
    /* Disarm rather than release: navigating away from an armed bench must
     * not leave it armed behind a screen that is not visible, and the
     * disarm lets go of the output on its way. */
    value_edge(ui_value_ask_leave(&s.gate), SERVO_TEST_AB_LEFT);
    test_end_now(SERVO_TEST_AB_LEFT);
    ui_hold_reset(&s.test_hold);
    s.test_down = false;
    ui_hold_reset(&s.lim_hold);
    s.lim_down = false;
    post(SERVO_CMD_DISARM, 0);
    /* The button reads ARM when the screen comes back; a DISARM asked of an
     * armed bench stands until the bench answers it. */
    (void)ui_value_hide(&s.gate);
    s.at_rest = false;
    ui_hold_reset(&s.arm);
    s.arm_down = false;
    /* A press on OUTPUT OFF as the screen goes is the OFF being made. */
    if (s.out_down && s.out_press_on) {
        supply_screen_ask_off();
    }
    ui_hold_reset(&s.out_hold);
    s.out_down = false;
    ++s.sup_rev;
    /* The sweep and everything kept about it: a reopened screen draws no
     * curve, drain or acknowledgement of this one. */
    stop_sweep();
    s.paused      = false;
    s.toggle_live = false;
    s.sw.running  = false;
    s.sw.paused   = false;
    close_panels();
    ui_tabs_cancel(&s.tabs);
    wb_leave();
    s.ov_have = false;
    s.ov_pressed = OP_NONE;
    /* The sessions' presses and pages go; a port stays as it is, and the
     * screen shows its session again while it holds the port. */
    prog_leave();
    /* And a supply this screen switched on goes off.  While the port holds
     * it, the OFF waits for the port to let go: an OFF now would end the
     * session and cut a write on the wire. */
    supply_screen_left(SUPPLY_BY_SERVO);
    ++s.arm_rev;
    ++s.ctrl_rev;
}

/* ENABLE WRITE TO SERVO goes off whenever the screen is entered, and an
 * OFF owed for leaving with the port held is dropped: the supply is this
 * screen's again. */
static void enter(void)
{
    wb_enter();
    prog_entry();
    supply_screen_entered(SUPPLY_BY_SERVO);
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
    /* DISARM acts on its press and takes no hold.  A press of ARM still
     * down on a bench that reads armed without its hold having fired is a
     * finger on DISARM all the same, and switches to SAFE.  Arming has
     * already sent its command by the time the finger lifts, so cancelling
     * one part way asks for nothing, which is correct. */
    if (s.gate.armed && s.arm_down && !s.arm.fired) {
        svs_ask_disarm();
    }
    /* And an arm posted but not yet collected: a command is forwarded on the
     * frame after the one that posted it, and the frame that observes a loss
     * cancels before that forwarding.  A disarm is kept. */
    if (s.pending.kind == SERVO_CMD_ARM) {
        s.pending.kind  = SERVO_CMD_NONE;
        ui_value_drop_arm(&s.gate);
    }
    /* And a sweep: the event that went missing may be the PAUSE that was to
     * stop it, and the panel would go on repeating it.  Paused where the
     * output has got to, as PAUSE pauses it. */
    if (s.sweeping) {
        svs_hold_sweep();
    }
    /* And a run, for the same reason: the lost event may be STOP TEST.
     * START TEST's hold goes with the other holds. */
    test_end_now(SERVO_TEST_AB_TOUCH);
    ui_hold_reset(&s.test_hold);
    s.test_down = false;
    ui_hold_reset(&s.lim_hold);
    s.lim_down = false;
    ++s.test_rev;
    ui_slider_release(&s.speed);
    ui_hold_reset(&s.arm);
    s.arm_down = false;
    /* OUTPUT OFF is a press too, and its lost release an OFF made; OUTPUT
     * ON's hold is dropped, and SUPPLY drops an ON not yet collected. */
    if (s.out_down && s.out_press_on) {
        supply_screen_ask_off();
    }
    ui_hold_reset(&s.out_hold);
    s.out_down = false;
    ++s.sup_rev;
    /*
     * And the dial.  A drag left latched owns its track id, and the GT911
     * reuses ids: a later contact that began somewhere else would satisfy
     * the drag path and command a position with no press on the dial, which
     * on an armed bench moves the servo.  Its filter stops where it has
     * got to: the target it was settling on is the abandoned gesture's.
     */
    svs_drag_end();
    /* And the overlay's press: the warning's hold above all, which must not
     * complete on a contact that may have gone. */
    prog_cancel();
    ui_keypad_cancel_press(&s.kp);
    ui_textkey_cancel_press(&s.tk);
    ui_tabs_cancel(&s.tabs);
    wb_cancel();
    ui_hold_reset(&s.warn.hold);
    s.warn.down = false;
    ++s.warn.rev;
    ui_hold_reset(&s.ask.hold);
    s.ask.down = false;
    ++s.ask.rev;
    s.ov_have = false;
    s.ov_pressed = OP_NONE;
    ++s.arm_rev;
    ++s.ctrl_rev;
}

/* The owners the knob yields to can end inside a frame: each tick and each
 * event latches them, so a turn applied after the frame sees them. */
static void tick(float dt_s)
{
    svs_tick_body(dt_s);
    svs_knob_note_owner();
}

static void event(const touch_event_t *evt)
{
    svs_event_body(evt);
    svs_knob_note_owner();
}

/* The home tag: over a panel it closes the panel, from a run's settings it
 * climbs to TEST; otherwise it leaves, switching to SAFE. */
static bool back(void)
{
    return wb_back();
}

static const ui_screen_t k_screen = {
    .title  = "SERVO",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = svs_render,
    .back   = back,
    .safe_at = svs_safe_at,
};

const ui_screen_t *servo_screen(void) { return &k_screen; }
