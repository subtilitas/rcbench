/*
 * The servo bench screen.
 *
 * The horn is the control: dragging the drawn arm commands the servo, and
 * the arm is drawn at the measured position, so a servo that is slow, stuck
 * or fighting a linkage lags the finger by that much.
 *
 * SETTINGS, top right, opens the servo's settings over the left card: its
 * type, frame rate and pulse widths, the automatic test, its limits and the
 * device under test.  A type or a frame rate that can destroy a servo not
 * made for it -- a heli profile, anything above 60 Hz -- is applied only
 * after a warning held for two seconds, and is never kept past a restart.
 * The overlay is servo_settings_panel.c; the state and the layout the
 * files share are servo_screen_int.h.
 *
 * START TEST, on the TEST page, runs the automatic test (servo_test.h): the
 * screen hands it the supply's samples and the clock, and does what it asks
 * -- the servo's position, SUPPLY's set points and output -- through the
 * same paths a finger uses.  Its progress and result are on the left card.
 * The run's code is servo_test_panel.c.
 *
 * START LIMIT RUN, on the LIMITS page, runs the limit run
 * (servo_limrun.h) the same way; its code is servo_limit_panel.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kst_panel.h"
#include "kst_port.h"
#include "link_pages.h"
#include "log_name.h"
#include "outputs.h"
#include "rcbench_version.h"
#include "servo_limrun.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_pinlist.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/* The output shaft, which everything on the left is drawn around.  It sits
 * on the top face of the case, where a servo's output is. */
#define SHAFT_X 300
/* Integer division on purpose: this is a pixel row, and H is even. */
/* NOLINTNEXTLINE(bugprone-integer-division) */
#define SHAFT_Y ((H) / 2)          /* centred in the card, not under it */
#define ARC_R   140
#define HORN_L  92
#define BODY_W  260
#define BODY_H  88
/*
 * The mounting flanges run the full width of the case end, as on a servo.
 *
 * The three clearances are the chosen numbers.  The flange width is what
 * holds a bore with TAB_MARGIN of material outside it and TAB_INNER between
 * it and the case, and the hole positions follow from the same margin top
 * and bottom.
 */
#define TAB_HOLE_R  12
#define TAB_MARGIN  6                  /* material outside the bore     */
#define TAB_INNER   8                  /* between the bore and the case */
#define TAB_OVER    6                  /* how far the case laps over it */
#define TAB_W       (TAB_MARGIN + 2 * TAB_HOLE_R + TAB_INNER + TAB_OVER)
#define TAB_H       BODY_H
#define TAB_HOLE_DY (BODY_H / 2 - TAB_MARGIN - TAB_HOLE_R)

/*
 * Screen angle from servo angle.
 *
 * The case lies along the card with its output at the right-hand end; zero
 * is the arm pointing straight out, and positive is counter-clockwise, the
 * convention every other angle on the bench uses.
 */
#define PHI(a) (a)

/* The sample rate of the panel's loop, which is the power plot's time base. */
#define SAMPLE_HZ 20.0f

/*
 * The profiles the bench drives.  STANDARD PWM is the safe one and the one
 * every restart starts at.  The heli profiles are Rotorflight's for digital
 * cyclic and narrow-band tail servos: 1520 us centre, +/-700 us, up to
 * 333 Hz; and 760 us centre, +/-350 us, up to 560 Hz.
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

/* The overlay's pages' names. */
static const ui_text_id_t k_pages[PG_COUNT] = {
    TX_SV_PG_OUTPUT, TX_SV_PG_TEST, TX_SV_PG_LIMITS, TX_SV_PG_DUT,
};

/* The power plot's series: the servo rail's voltage, current and power. */
enum { PS_V = 0, PS_A, PS_W, PS_COUNT };
static const ui_plot_series_t k_power[PS_COUNT] = {
    { "V", "V", 0, 2, 5.0f, 0 },
    { "A", "A", 0, 2, 0.5f, 0 },
    { "W", "W", 0, 1, 5.0f, 0 },
};

/*
 * The widths of the three readings on the line over the plot, in px of 8 px
 * cells.  Beside PD MINI 68 each.  Beside INA3221 CH1 60, 64 and 56, which
 * hold the widest reading of the part's registers: `65.54 V`, `-32.77 A`,
 * and a power of 7 cells, `-99.9 W` or `-2147 W`.
 */
static const uint8_t k_line_w[2][PS_COUNT] = {
    { 68, 68, 68 },
    { 60, 64, 56 },
};

/* The state, its type in servo_screen_int.h. */
servo_screen_state_t s;

/* ------------------------------------------------------------- conversions */

/*
 * Each side of centre runs to its own end: MIN, CENTRE and MAX are set one
 * by one, so -90 deg is MIN and +90 deg is MAX however far each lies from
 * the centre.
 */
static float half_travel_us(bool below)
{
    return below ? (float)s.centre_us - (float)s.min_us
                 : (float)s.max_us - (float)s.centre_us;
}

static float deg_to_us_f(float deg)
{
    const float d = s.reverse ? -deg : deg;
    float us = (float)s.centre_us + (float)s.trim_us
               + d / 90.0f * half_travel_us(d < 0.0f);
    if (us < (float)s.min_us) { us = (float)s.min_us; }
    if (us > (float)s.max_us) { us = (float)s.max_us; }
    return us;
}

static uint16_t deg_to_us(float deg)
{
    return (uint16_t)(deg_to_us_f(deg) + 0.5f);
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
static servo_map_t map_now(void)
{
    const servo_map_t m = { s.min_us, s.centre_us, s.max_us, s.trim_us,
                            s.reverse, s.travel_deg };
    return m;
}

/* A command of @p m's range as a pulse, and a pulse as @p m's angle: what
 * cmd_to_us() and us_to_deg_f() are for the mapping in force. */
static float map_cmd_to_us(const servo_map_t *m, float cmd)
{
    const unsigned below = (unsigned)m->centre_us - (unsigned)m->min_us;
    const unsigned above = (unsigned)m->max_us - (unsigned)m->centre_us;
    const float half = (float)((below > above) ? below : above);
    return (float)m->centre_us - half + 2.0f * half * cmd / (float)OUT_SPAN;
}

static float map_us_to_deg(const servo_map_t *m, float us)
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
static inline float us_to_cmd(float us)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (hi > lo) ? (us - (float)lo) * (float)OUT_SPAN / (float)(hi - lo)
                     : (float)OUT_SPAN / 2.0f;
}

static float cmd_to_us(float cmd)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    return (float)lo + (float)(hi - lo) * cmd / (float)OUT_SPAN;
}

/*
 * The angle the pin drives on an armed bench while nothing commands it: the
 * outputs layer's rest for a surface, in the range a command carries.  The
 * midpoint of that range, so PULSE CENTRE without the trim: 1500 us for
 * STANDARD PWM, 760 us for NARROW 760.
 */
static float rest_deg(void)
{
    return us_to_deg_f(cmd_to_us((float)outputs_role_rest(OUT_ROLE_SURFACE)));
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
        s.commanded_deg = rest_deg();
        s.at_rest       = true;
    }
    ++s.ctrl_rev;
}

/*
 * SPEED as the bench's slew, in channel-span units a second.
 *
 * The horn's full travel is 180 degrees and a channel's span covers it, so
 * SPEED_FULL_SPAN_S, two spans a second, is 360 degrees a second.  Below
 * 100% the bench ramps the command at that fraction of it, and a servo
 * asked for 30% takes three times as long to cross as one asked for 90%.
 *
 * 100% is immediate rather than two spans a second, at the far end and in
 * the drawing (slew_per_ms()).  It is the value the screen starts at, so
 * anybody who never touches the slider gets the servo at its own rate,
 * with nothing in front of it.
 */
#define SPEED_FULL_SPAN_S (2u * OUT_SPAN)

static uint16_t slew_of(int pct)
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
        s.posted_map = map_now();
    }
    if (kind == SERVO_CMD_ARM) {
        s.arm_profile_rev = s.profile_rev;
        ui_value_ask_arm(&s.gate);
    } else if (kind == SERVO_CMD_DISARM) {
        ui_value_drop_arm(&s.gate);
    }
    /* The range travels with the pulse: the panel configures the channel
     * from it, and a narrow servo's 760 us centre is below a standard
     * servo's floor.  The frame rate goes with it. */
    cmd_range(&s.pending.min_us, &s.pending.max_us);
    s.pending.frame_hz = s.frame_hz;
    s.pending.slew_per_s = slew_of(s.speed_pct);
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

/* ------------------------------------------------------------------- sweep */

/*
 * The sweep the TEST page describes, under the profile in force: CURVE,
 * SPEED and DWELL as they are, and RANGE as a share of the travel the
 * servo may make -- TRAVEL, and the nearer of PULSE MIN and MAX, so a sweep
 * about the centre reaches neither end it may not.  In the command units of
 * the range a command carries (cmd_range()), centred on PULSE CENTRE.  Trim
 * is not applied: the sweep turns about the centre a release rests at.
 */
sweep_cfg_t sweep_cfg_now(void)
{
    sweep_cfg_t c;
    c.kind = (sweep_kind_t)(settings_get_int(SET_SERVO_CURVE) + 1);
    float mhz = settings_get(SET_SERVO_TEST_HZ) * 1000.0f;
    if (mhz < (float)SWEEP_MHZ_MIN) { mhz = (float)SWEEP_MHZ_MIN; }
    if (mhz > (float)SWEEP_MHZ_MAX) { mhz = (float)SWEEP_MHZ_MAX; }
    c.mhz = (uint16_t)(mhz + 0.5f);
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    const float half = (float)(hi - lo) * 0.5f;
    const float below = (float)s.centre_us - (float)s.min_us;
    const float above = (float)s.max_us - (float)s.centre_us;
    const float nearer = (below < above) ? below : above;
    float amp = 0.0f;
    if (half > 0.0f) {
        amp = settings_get(SET_SERVO_TEST_RANGE) / 100.0f
              * (s.travel_deg / 90.0f) * (nearer / half)
              * (float)SWEEP_AMPLITUDE_MAX;
    }
    if (amp > (float)SWEEP_AMPLITUDE_MAX) { amp = (float)SWEEP_AMPLITUDE_MAX; }
    c.amplitude = (uint16_t)(amp + 0.5f);
    int dwell = settings_get_int(SET_SERVO_DWELL_MS);
    if (dwell > (int)SWEEP_DWELL_MAX_MS) { dwell = (int)SWEEP_DWELL_MAX_MS; }
    c.dwell_ms = (uint16_t)((dwell > 0) ? dwell : 0);
    c.moves = 0u;     /* until it is stopped */
    return c;
}

/* A command of the sweep, as the angle it puts the horn at. */
static float sweep_deg(uint16_t cmd)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    const float us = (float)lo + (float)(hi - lo) * (float)cmd
                                 / (float)(2u * SWEEP_CENTRE);
    return us_to_deg((uint16_t)(us + 0.5f));
}

/* The sweep ended, running or paused.  A pause is ended by the command
 * that follows; the button reads SWEEP from now. */
void stop_sweep(void)
{
    s.lineage         = 0u;
    s.resume_from_set = false;
    s.awaiting       = false;
    s.dr.on          = false;
    s.start_rec.live = false;
    if (s.sweeping) {
        s.sweeping    = false;
        s.sweep_ended = true;
        ++s.ctrl_rev;
    }
    if (s.paused) {
        s.paused = false;
        ++s.ctrl_rev;
    }
}

/*
 * PAUSE: the sweep stops and every surface stays where its output had got
 * to, by the link's HOLD.  The coprocessor holds it there, because only it
 * knows where that is: SPEED can leave the output well behind the curve, and
 * without feedback the horn drawn here is an estimate.  The drawing stops
 * where it was.  The phase is kept, here and at the far end, for
 * resume_sweep().
 */
static void hold_sweep(void)
{
    /* A PAUSE before the resume of an earlier pause is acknowledged holds
     * what that pause left, and stays on it. */
    const uint16_t root = s.lineage;
    const bool moving = !s.awaiting;
    if (!moving) {
        /* Not yet moving at the far end as far as is known here: paused
         * where it would have begun, and the HOLD's acknowledgement says
         * how far it got. */
        s.sw.running   = false;
        s.sw.paused    = true;
        s.sw.paused_ms = s.await_phase_ms;
    } else {
        s.dr.sw = s.sw;
        sweep_pause(&s.sw, s.clock_ms);
    }
    s.pause_tap_ms = s.sw.paused_ms;
    s.pause_speed_pct = s.speed_pct;
    s.dr.speed_pct    = s.speed_pct;
    s.dr.map          = map_now();
    s.dr.out          = s.shown_cmd;
    /* With feedback the reading itself: tick() turns it into a command
     * only on its next pass. */
    s.pause_from_cmd  = s.have_feedback ? us_to_cmd(deg_to_us_f(s.shown_deg))
                                        : s.shown_cmd;
    stop_sweep();
    /* Drawn on along the curve until the HOLD is acknowledged, as the far
     * end runs it; held where it is if it was not yet moving. */
    s.dr.on      = moving;
    s.dr.from_ms = s.clock_ms;
    s.commanded_deg = clamp_travel(s.shown_deg);
    ++s.pause_seq;
    if (s.pause_seq == 0u) {
        s.pause_seq = 1u;       /* 0 is no pause */
    }
    s.lineage = root;
    post(SERVO_CMD_HOLD, 0);
    if (s.pending.kind == SERVO_CMD_HOLD) {
        s.pending.pause_seq = s.pause_seq;
    }
    s.lineage = (root != 0u) ? root : s.pause_seq;
    ++s.ctrl_rev;
}

/* The start or resume just posted, as it will be sent. */
static void record_start(void)
{
    if (s.pending.kind != SERVO_CMD_SWEEP) {
        return;
    }
    s.start_rec.live      = true;
    s.start_rec.seq       = s.pending.start_seq;
    s.start_rec.cfg       = s.sw.cfg;
    s.start_rec.speed_pct = s.speed_pct;
}

/*
 * Start the sweep, or carry on with a changed one from its beginning, as the
 * coprocessor does.  Only while the value is live (value_live()) and on a
 * coprocessor that sweeps: the far end refuses one otherwise.  Drawn once
 * the start is acknowledged (servo_screen_sweep_started()).
 */
static void start_sweep(void)
{
    if (!value_live() || !s.sweep_able || !s.surfaces || !s.link_up) {
        return;
    }
    const sweep_cfg_t cfg = sweep_cfg_now();
    if (!sweep_start(&s.sw, &cfg, s.clock_ms)) {
        return;
    }
    s.sweeping       = true;
    s.awaiting       = true;
    s.await_phase_ms = 0u;
    s.lineage        = 0u;      /* a new origin */
    post(SERVO_CMD_SWEEP, 0);
    s.pending.resume = false;   /* a start, whatever it replaced */
    record_start();
    ++s.ctrl_rev;
}

/*
 * A running sweep's curve changed: the new one from its beginning, as the
 * coprocessor starts it when it is written.  Until that start is
 * acknowledged the far end runs the old curve, so the old curve is drawn on
 * (the drain, as for a PAUSE on its way) at the SPEED it ran at, for at
 * most OUT_DEFAULT_TIMEOUT_MS.
 */
static void restart_sweep(void)
{
    const bool was_drawn = s.sweeping && !s.awaiting;
    const sweep_drain_t old = {
        .on = true, .sw = s.sw, .from_ms = s.clock_ms,
        .speed_pct = s.speed_pct, .map = s.posted_map, .out = s.shown_cmd,
    };
    stop_sweep();
    start_sweep();
    if (s.sweeping && was_drawn) {
        s.dr = old;
    }
}

/*
 * Whether SPEED is slower than the fastest change the TEST page's curve asks
 * for, so the slew and not the curve shapes the sweep.  From the settings,
 * so it shows before SWEEP is pressed as well as while one runs: a running
 * sweep is this curve, started over when a setting changes.  The sweep's
 * amplitude and SPEED's slew are in the same units, the command across the
 * range a command carries.
 */
static bool speed_limits_sweep(void)
{
    const sweep_cfg_t cfg = sweep_cfg_now();
    return sweep_slew_limited(&cfg, slew_of(s.speed_pct));
}

static bool same_sweep(const sweep_cfg_t *a, const sweep_cfg_t *b)
{
    return a->kind == b->kind && a->mhz == b->mhz
           && a->amplitude == b->amplitude && a->dwell_ms == b->dwell_ms;
}

/*
 * A tap on PAUSED: the paused sweep carried on from where it was paused.
 * The command asks the panel for a resume; a coprocessor older than 4.6, or
 * one that refuses, starts the curve over instead.  Either way the far end
 * stays held until then, so the horn does too, and the acknowledgement
 * (servo_screen_sweep_started()) draws it from there.  A curve changed on
 * the TEST page since the pause is a new sweep.
 */
static void resume_sweep(void)
{
    const sweep_cfg_t now = sweep_cfg_now();
    if (!value_live() || !s.sweep_able || !same_sweep(&now, &s.sw.cfg)
        || !sweep_resume(&s.sw, s.clock_ms)) {
        start_sweep();
        return;
    }
    s.sweeping       = true;
    s.awaiting       = true;
    s.await_phase_ms = s.sw.paused_ms;
    /* A HOLD not yet acknowledged: the curve stops being drawn on here, and
     * the resume's acknowledgement times it from the far end's phase. */
    s.dr.on       = false;
    s.commanded_deg  = clamp_travel(s.shown_deg);
    s.resume_from_set  = true;
    s.resume_from_cmd  = s.shown_cmd;
    s.resume_pause_seq = s.pause_seq;
    post(SERVO_CMD_SWEEP, 0);
    record_start();
    s.pending.resume = s.pending.kind == SERVO_CMD_SWEEP;
    ++s.ctrl_rev;
}

/*
 * The sweep button: PAUSE pauses a running sweep, PAUSED carries it on, and
 * SWEEP starts the curve.  A tap whose own command has not left yet -- two
 * taps drained in one pass after a late frame -- is undone with it instead:
 * the far end heard neither, so nothing is sent and nothing here moves,
 * the curve's clock included.
 */
static void toggle_sweep(void)
{
    if (s.toggle_live && s.post_count == s.toggle_post
        && s.pending.kind == s.toggle_kind) {
        s.pending        = s.undo.pending;
        s.sw             = s.undo.sw;
        s.sweeping       = s.undo.sweeping;
        s.paused         = s.undo.paused;
        s.awaiting       = s.undo.awaiting;
        s.dr             = s.undo.dr;
        s.start_rec      = s.undo.start_rec;
        s.await_phase_ms = s.undo.await_phase_ms;
        s.sweep_ended    = s.undo.sweep_ended;
        s.driving        = s.undo.driving;
        s.commanded_deg  = s.undo.commanded_deg;
        s.pause_seq        = s.undo.pause_seq;
        s.pause_tap_ms     = s.undo.pause_tap_ms;
        s.pause_from_cmd   = s.undo.pause_from_cmd;
        s.pause_speed_pct  = s.undo.pause_speed_pct;
        s.resume_from_set  = s.undo.resume_from_set;
        s.resume_from_cmd  = s.undo.resume_from_cmd;
        s.resume_pause_seq = s.undo.resume_pause_seq;
        s.lineage          = s.undo.lineage;
        s.toggle_live    = false;
        ++s.ctrl_rev;
        return;
    }
    s.undo.pending        = s.pending;
    s.undo.sw             = s.sw;
    s.undo.sweeping       = s.sweeping;
    s.undo.paused         = s.paused;
    s.undo.awaiting       = s.awaiting;
    s.undo.dr             = s.dr;
    s.undo.start_rec      = s.start_rec;
    s.undo.await_phase_ms = s.await_phase_ms;
    s.undo.sweep_ended    = s.sweep_ended;
    s.undo.driving        = s.driving;
    s.undo.commanded_deg  = s.commanded_deg;
    s.undo.pause_seq        = s.pause_seq;
    s.undo.pause_tap_ms     = s.pause_tap_ms;
    s.undo.pause_from_cmd   = s.pause_from_cmd;
    s.undo.pause_speed_pct  = s.pause_speed_pct;
    s.undo.resume_from_set  = s.resume_from_set;
    s.undo.resume_from_cmd  = s.resume_from_cmd;
    s.undo.resume_pause_seq = s.resume_pause_seq;
    s.undo.lineage          = s.lineage;
    const uint32_t before = s.post_count;
    if (s.sweeping) {
        hold_sweep();
    } else if (s.paused) {
        resume_sweep();
    } else {
        start_sweep();
    }
    s.toggle_live = s.post_count != before;
    s.toggle_kind = s.pending.kind;
    s.toggle_post = s.post_count;
}

bool servo_screen_sweeping(void) { return s.sweeping; }

bool servo_screen_paused(void) { return s.paused; }

uint32_t servo_screen_curve_ms(void) { return s.clock_ms - s.sw.start_ms; }

void servo_screen_released(uint16_t pause_seq)
{
    /*
     * Nothing held any more, and the horn goes to the centre the surfaces
     * were released to -- while the screen is still on that pause.  The
     * panel dropped everything it asked since (its lineage), so the
     * surfaces are at rest.  Left since, by a drag, CENTRE, a new pause or
     * a new start, the screen's newer command was sent and is what the
     * surfaces do, so this changes nothing.
     */
    s.toggle_live = false;      /* no undo across a notification */
    if (pause_seq == 0u || pause_seq != s.lineage) {
        return;
    }
    stop_sweep();
    s.driving       = false;
    s.paused        = false;
    s.dr.on      = false;
    /* Where the pins follow; behind a disarm the value last driven stays. */
    if (value_live()) {
        s.commanded_deg = rest_deg();
        s.at_rest       = true;
    }
    ++s.ctrl_rev;
}

/*
 * The drawn output's slew at @p speed_pct, command units a millisecond, as
 * the far end's: SPEED_FULL_SPAN_S times the share, and at 100 % none --
 * the command goes straight through (slew_of()), which is negative here.
 */
static float slew_per_ms(int speed_pct)
{
    if (speed_pct >= 100) {
        return -1.0f;
    }
    return (float)slew_of(speed_pct) / 1000.0f;
}

/* One step of the drawn output towards @p want, at most @p step command
 * units: the slew the far end applies, as the drawing models it.  A
 * negative step is no slew: there at once. */
static float chase_cmd(float from, float want, float step)
{
    const float d = want - from;
    if (step < 0.0f || fabsf(d) <= step) {
        return want;
    }
    return from + ((d > 0.0f) ? step : -step);
}

/* Where the horn was drawn @p ago_ms ago, from the trail; the oldest kept
 * for anything older. */
static float shown_cmd_ago(uint32_t ago_ms)
{
    const uint32_t when = s.clock_ms - ago_ms;
    float best = s.shown_cmd;
    for (unsigned k = 0; k < s.trail_n; ++k) {
        const unsigned i = (s.trail_head + 64u - 1u - k) % 64u;
        best = s.trail[i].cmd;
        if ((int32_t)(s.trail[i].t - when) <= 0) {
            break;
        }
    }
    return best;
}

static float drawn_after(const sweep_t *w, uint32_t phase_ms, uint32_t ms,
                         float from, int speed_pct);

void servo_screen_sweep_started(uint16_t start_seq, uint32_t age_ms,
                                servo_sweep_from_t from, uint32_t since_ms)
{
    s.toggle_live = false;      /* no undo across a notification */
    /*
     * The far end's curve has phase 0 age_ms ago: this one is timed from
     * then, rather than from the tap that asked for it a queue and a few
     * transactions earlier.  Until now the far end was not known to move,
     * so the drawn output went on as it was; with nothing measuring it, it
     * is now worked on from where it was drawn when the far end began to
     * move -- age_ms ago, or since_ms ago for a resume -- along the curve
     * at SPEED, which the sweep's command carries.  For a sweep that froze
     * unrepeated, from where it froze, since_ms ago.
     */
    if (!s.sweeping) {
        return;
    }
    /*
     * Waiting for a start or a resume, the wait ends with the
     * acknowledgement of that command or of any sweep command posted after
     * it: a write of the start can fail and a later repeat start the sweep
     * at the far end instead.  A repeat never changes the curve -- a changed
     * curve is a new start (restart_sweep()) -- so the start's curve stands,
     * at the SPEED of the command acknowledged.  An earlier start's
     * acknowledgement describes a curve since replaced and is not taken.
     * A sweep already drawn takes any start the far end makes -- one that
     * froze unrepeated, or a link that came back -- at the SPEED in force.
     */
    int speed = s.speed_pct;
    if (s.awaiting) {
        const uint16_t after  = (uint16_t)(start_seq - s.start_rec.seq);
        const uint16_t posted = (uint16_t)(s.start_seq - s.start_rec.seq);
        if (!s.start_rec.live || after > posted) {
            return;
        }
        s.sw.cfg = s.start_rec.cfg;
        speed    = s.start_rec.speed_pct;
        if (s.sent_speed[start_seq % 8u].seq == start_seq) {
            speed = s.sent_speed[start_seq % 8u].speed_pct;
        }
    }
    s.start_rec.live = false;
    const uint32_t moving = (from == SERVO_SWEEP_RESUMED && since_ms < age_ms)
                            ? since_ms : age_ms;
    s.sw.start_ms = s.clock_ms - age_ms;
    s.sw.running  = true;
    s.sw.paused   = false;
    s.awaiting    = false;
    s.lineage     = 0u;         /* the far end moves on its own now */
    s.dr.on    = false;
    ++s.ctrl_rev;
    /* A resume's origin is for this acknowledgement only. */
    const bool from_resume = from == SERVO_SWEEP_RESUMED && s.resume_from_set;
    s.resume_from_set = false;
    if (s.have_feedback) {
        return;
    }
    /* A resume from where the far end held the output, which a late HOLD
     * acknowledgement can have said since the tap. */
    const float from_cmd = (from == SERVO_SWEEP_FROM_FROZEN)
                           ? shown_cmd_ago(since_ms)
                           : from_resume ? s.resume_from_cmd
                                         : shown_cmd_ago(moving);
    const uint32_t ms = (moving > 5000u) ? 5000u : moving;
    s.shown_cmd = drawn_after(&s.sw, age_ms - moving, ms, from_cmd, speed);
    s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
}

/*
 * The drawn output after @p ms more of the sweep @p w from @p phase_ms in,
 * starting at @p from: each step the curve's command, chased as tick()
 * chases it at @p speed_pct -- for a pause, the SPEED in force at the tap,
 * the far end's slew until the HOLD reached it.  Steps of 4 ms, under a
 * frame, so a turn of the curve is followed as the drawing follows it.
 */
static float drawn_after(const sweep_t *w, uint32_t phase_ms, uint32_t ms,
                         float from, int speed_pct)
{
    sweep_t run = *w;
    run.running  = true;
    run.paused   = false;
    run.start_ms = 0u;
    const float per_ms = slew_per_ms(speed_pct);
    float shown = from;
    for (uint32_t done = 0u; done < ms;) {
        const uint32_t dt = (ms - done < 4u) ? ms - done : 4u;
        done += dt;
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        (void)sweep_step(&run, phase_ms + done, &cmd);
        const float want = us_to_cmd(deg_to_us_f(clamp_travel(sweep_deg(cmd))));
        shown = chase_cmd(shown, want, per_ms * (float)dt);
    }
    return shown;
}

void servo_screen_sweep_held(uint16_t pause_seq, uint32_t kept_ms)
{
    s.toggle_live = false;      /* no undo across a notification */
    /*
     * The far end ran its curve on until the HOLD reached it, a queue and an
     * exchange after the tap; at 5 Hz that is a visible share of a cycle.
     * The panel times the phase it kept from the acknowledgements, as it
     * times a start, so the two errors are the same and cancel.
     *
     * Without feedback the horn is the drawing's estimate of the output, so
     * it moves on to where the output had got by then: the curve from the
     * tap's phase to the acknowledged one, slewed at SPEED as the drawing
     * slews.  Worked out here rather than by drawing the curve on until the
     * acknowledgement, which would need an end for a HOLD that is never
     * acknowledged; without one, the tap's estimate stands.  At most 5 s is
     * worked through, 1250 steps.
     */
    if (pause_seq != s.pause_seq) {
        return;
    }
    if (!s.paused && s.awaiting && s.resume_from_set
        && s.resume_pause_seq == pause_seq) {
        /*
         * PAUSED was tapped before this arrived: the far end ran on from
         * the tap and froze here, and stays frozen until the RESUME reaches
         * it.  Without feedback the output is drawn there now, and the
         * resume's acknowledgement replays from it rather than from the
         * tap's estimate.
         */
        if (s.have_feedback) {
            /* The reading, kept for the resume's replay in case feedback
             * goes before the resume is acknowledged. */
            s.resume_from_cmd = us_to_cmd(deg_to_us_f(s.shown_deg));
        } else {
            const uint32_t late = kept_ms - s.pause_tap_ms;
            const uint32_t ms = ((int32_t)late <= 0) ? 0u
                                : (late > 5000u) ? 5000u : late;
            s.resume_from_cmd = drawn_after(&s.sw, s.pause_tap_ms, ms,
                                            s.pause_from_cmd,
                                            s.pause_speed_pct);
            s.shown_cmd = s.resume_from_cmd;
            s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
            s.commanded_deg = clamp_travel(s.shown_deg);
            ++s.ctrl_rev;
        }
        return;
    }
    if (!s.paused || !s.sw.paused) {
        return;
    }
    /* Behind the tap's phase by this screen's own timing error: nothing to
     * replay, and the kept phase stands. */
    const uint32_t extra = kept_ms - s.sw.paused_ms;
    if (s.have_feedback) {
        /* The servo is where it reports, and is held there. */
        s.shown_cmd = us_to_cmd(deg_to_us_f(s.shown_deg));
    } else {
        const uint32_t ms = ((int32_t)extra <= 0) ? 0u
                            : (extra > 5000u) ? 5000u : extra;
        s.shown_cmd = drawn_after(&s.sw, s.sw.paused_ms, ms,
                                  s.pause_from_cmd, s.pause_speed_pct);
        s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
    }
    /* Either way the held angle is the one a changed profile says again
     * (reissue()), and the curve is no longer drawn on. */
    s.dr.on = false;
    s.commanded_deg = clamp_travel(s.shown_deg);
    s.sw.paused_ms = kept_ms;
    ++s.ctrl_rev;
}

void servo_screen_set_sweep(bool able)
{
    if (able == s.sweep_able) {
        return;
    }
    s.sweep_able = able;
    if (!able && (s.sweeping || s.paused)) {
        /* The panel would go on repeating a sweep the coprocessor no longer
         * takes: what it holds is ended, and the surfaces rest. */
        stop_sweep();
        release();
    }
    stop_sweep();
    ++s.ctrl_rev;
}

/*
 * Say the position again under a mapping that has just changed.
 *
 * The pulse a command carries is the angle put through the profile, the
 * trim, the reverse and the travel; change any of them while an output is
 * held and the pulse on the pin belongs to the old one.  Switching a held
 * servo from STANDARD PWM to NARROW 760 would otherwise leave 1500 us on a
 * servo whose maximum is 860 while the screen shows the new range.
 */
static void ask_disarm(void);

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
        s.pending.slew_per_s = slew_of(s.speed_pct);
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
        if (same_sweep(&now, &s.sw.cfg)) {
            post(SERVO_CMD_SWEEP, 0);
        } else {
            /* The curve changed with the range -- MIN, CENTRE, MAX or
             * TRAVEL move its amplitude -- so it starts over now, rather
             * than going out once with the old amplitude under the new
             * range before tick() catches it. */
            restart_sweep();
        }
        return;
    }
    if (s.driving) {
        /* Paused with the curve still drawn on: held where it is drawn. */
        if (s.dr.on) {
            s.dr.on = false;
            s.commanded_deg = clamp_travel(s.shown_deg);
        }
        post(SERVO_CMD_POSITION, deg_to_us(s.commanded_deg));
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
static bool command(float deg)
{
    if (!value_live()) {
        return false;
    }
    s.commanded_deg = clamp_travel(deg);
    post(SERVO_CMD_POSITION, deg_to_us(s.commanded_deg));
    ++s.ctrl_rev;
    return true;
}

static void knob_withdraw(void);

/*
 * A motion command still waiting to be taken as the bench stops driving --
 * a disarm, or a stop -- is not sent: the pins carry no pulse, and a
 * position written to a disarmed far end is a value nobody watches change.
 * The knob's is withdrawn with the value it replaced; any other is dropped.
 * A release, an arm and a disarm stay.
 */
static void drop_motion(void)
{
    knob_withdraw();
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

/*
 * What an edge of the value's state does, in one place.
 *
 * Into LIVE: the value is the rest.
 *
 * Out of LIVE, to LEAVING or to OFF: everything that drives the servo by
 * itself ends -- a run, ended as @p why, a sweep, a pause, a drag, and a
 * held position, which a change of SPEED or of the profile would otherwise
 * say again -- and a motion command not yet taken is not sent.  The value
 * shown stays where it was last driven: the release a run ends with moves
 * none, because the value is no longer live (release()).
 */
void value_edge(ui_value_edge_t edge, servo_test_abort_t why)
{
    if (edge == UI_VALUE_EDGE_LEFT) {
        test_end_now(why);
        drop_motion();
        stop_sweep();
        s.dragging         = false;
        s.driving          = false;
        s.knob_had_driving = false;
        s.at_rest          = false;
        ++s.ctrl_rev;
    }
    if (edge == UI_VALUE_EDGE_LIVE) {
        /*
         * The bench armed: the pins drive every surface at its rest, the
         * release the arm pays having put them there, whatever this screen
         * commanded before.  The value shown is that rest, and the first
         * input starts from it.  Without feedback the horn is drawn there
         * at once, as the output is: nothing slews to a rest it is
         * already at.
         */
        s.commanded_deg = rest_deg();
        s.at_rest       = true;
        if (!s.have_feedback) {
            s.shown_cmd = (float)outputs_role_rest(OUT_ROLE_SURFACE);
            s.shown_deg = s.commanded_deg;
            /* And where it was drawn before the arm is no part of where
             * a sweep started after it begins. */
            s.trail_n = 0u;
        }
        ++s.ctrl_rev;
    }
}

void servo_screen_set_armed(bool armed)
{
    const bool was = s.gate.armed;
    value_edge(ui_value_report(&s.gate, armed), SERVO_TEST_AB_DISARMED);
    if (was == armed) {
        return;
    }
    /* A finger on the dial does not carry its drag across the edge: after
     * an arm the first input is a new press, and a disarmed dial takes
     * none. */
    s.dragging = false;
    /*
     * Nothing is being held across this edge, in either direction.  An arm
     * starts from nothing -- the panel drops the position and the slot on the
     * way through -- and a disarm holds nothing by definition.  A screen that
     * went on believing it was driving would say a discarded position again
     * on the next change of profile, trim or travel, onto a bench that is
     * now armed.
     */
    s.driving = false;
    /* Nor is what the knob found before the edge: a withdrawal after it
     * puts back no drive from the other side. */
    s.knob_had_driving = false;
    stop_sweep();
    if (armed) {
        ui_hold_reached(&s.arm);
        /*
         * The profile changed after ARM was asked for and nothing answered
         * it yet: the rest is restated under the profile in force, its
         * range and rate in the panel's order.  reissue() answers a change
         * while the arm is on its way, so this is the backstop.
         */
        if (s.profile_rev != s.arm_profile_rev) {
            post(SERVO_CMD_RELEASE, 0);
            s.arm_profile_rev = s.profile_rev;
        }
    } else {
        /*
         * Disarmed, however it happened -- this screen's button, a STOP, a
         * dead touch, or the far end.  Nothing is being held any more: the
         * rings must stop pulsing, and a change to the profile, the trim or
         * the travel must not say a position again and rebuild a command
         * the stop had just released.
         */
        s.at_rest = false;
        drop_motion();
        /* The flash says the bench armed; disarmed, it is over, so both
         * buffers end on ARM's own colour. */
        s.arm.flash_left = 0;
        if (ui_hold_left(&s.arm)) {
            /* The bench disarmed under a finger still down on the button, and
             * the hold ended with it; see ui_hold_left(). */
            s.arm_down = false;
        }
    }
    ++s.arm_rev;
    ++s.ctrl_rev;
}

void servo_screen_cancel_arm(void)
{
    /*
     * A stop latched, so a hold under way is abandoned and an arm it has
     * already produced is dropped.
     *
     * The command is dealt with first and on its own account.  A hold that
     * completed and whose finger has since lifted leaves nothing held and
     * nothing counting -- the release cleared both -- while its arm is still
     * waiting to be read, and that is precisely the one that would be
     * forwarded a frame later and clear the latch the stop had just set.
     */
    bool changed = false;
    /* Counted: a question about the live output stands only while this
     * count is the one it was asked under (ask_stands()). */
    ++s.stops;
    /* The stop ends the arm on its way too, and an armed bench is on its
     * way down from here.  A run ends, and START TEST's hold with it; the
     * value shown stays where the run last drove the servo, as after any
     * disarm, rather than at the rest a run's end on a live bench releases
     * to. */
    value_edge(ui_value_ask_leave(&s.gate), SERVO_TEST_AB_STOP);
    test_end_now(SERVO_TEST_AB_STOP);
    if (s.test_down || s.test_hold.held_s > 0.0f
        || s.lim_down || s.lim_hold.held_s > 0.0f) {
        ui_hold_reset(&s.test_hold);
        s.test_down = false;
        ui_hold_reset(&s.lim_hold);
        s.lim_down = false;
        ++s.test_rev;
    }
    if (s.pending.kind == SERVO_CMD_ARM) {
        s.pending.kind = SERVO_CMD_NONE;
        changed = true;
    }
    if (s.arm_down || s.arm.held_s > 0.0f) {
        ui_hold_reset(&s.arm);
        s.arm_down = false;
        changed = true;
    }
    if (changed) {
        ++s.arm_rev;
    }
    /* OUTPUT ON's hold too: the stop cuts the output, and a hold that ran
     * on would switch it back on.  An ON already posted is SUPPLY's to drop
     * (supply_screen_cancel_on()). */
    if (s.out_down || s.out_hold.held_s > 0.0f) {
        ui_hold_reset(&s.out_hold);
        s.out_down = false;
        ++s.sup_rev;
    }
    /* And the two warnings' holds: a voltage past a standard servo's rating
     * or a profile that can destroy one is not applied by a hold the stop
     * interrupted.  The warnings stay open; a new hold applies them. */
    /* An APPLY being pressed -- a tap on the question as well as a hold --
     * is let go of: its release must not apply a set point after the stop. */
    if (s.ov_have && (s.ov_pressed == OP_ASK_APPLY
                      || s.ov_pressed == OP_WARN_APPLY
                      || s.ov_pressed == OP_TEST_START
                      || s.ov_pressed == OP_LIM_START)) {
        s.ov_have    = false;           /* the press is over, as ov_let_go() */
        s.ov_pressed = OP_NONE;
        ++s.ctrl_rev;
    }
    if (s.ask.down || s.warn.down) {
        ui_hold_reset(&s.ask.hold);
        ui_hold_reset(&s.warn.hold);
        s.ask.down  = false;
        s.warn.down = false;
        ++s.ask.rev;
        ++s.warn.rev;
    }
    /*
     * And nothing is being held any more.  The armed state need not have
     * moved -- a bench that was not armed is stopped just the same, and the
     * panel centres the surface either way -- so this cannot wait for that
     * edge: the rings would go on pulsing, and the next change of profile,
     * trim or travel would say the released position again.
     */
    if (s.driving) {
        s.driving = false;
        ++s.ctrl_rev;
    }
    /* Nor is what the knob found before the stop: a withdrawal after it
     * puts back no drive, while the screen still reads the bench as armed. */
    s.knob_had_driving = false;
    /* A position not yet taken would be sent after the stop. */
    drop_motion();
    s.dragging = false;
    stop_sweep();
}

bool servo_screen_take(servo_cmd_t *out)
{
    if (out == NULL || s.pending.kind == SERVO_CMD_NONE) {
        return false;
    }
    *out = s.pending;
    s.pending.kind = SERVO_CMD_NONE;
    s.knob_live = false;
    return true;
}

void servo_screen_feedback(uint16_t position_us, float current_a, bool valid)
{
    const float deg = us_to_deg(position_us);

    /*
     * A reading counts as new only if it is drawn differently: compare at
     * the precision shown, tenths of a degree.  Feedback arrives at the
     * poll rate whether or not the servo moved, and a revision bump per
     * reading would repaint the 488x418 card at that rate and never reach
     * the grip's clipped repaint.  The current is not drawn from here: the
     * CURRENT row reads the servo rail's meter (rail_now()).
     */
    (void)current_a;
    const int q_deg = (int)(deg * 10.0f + (deg >= 0.0f ? 0.5f : -0.5f));
    const bool same = valid && s.have_feedback && q_deg == s.shown_q_deg;

    s.measured_deg  = deg;
    s.have_feedback = valid;
    /*
     * The arm is drawn where the servo reports it, with no easing: easing
     * would add the screen's lag to the servo's, and the two are
     * indistinguishable when drawn.  The smoothing in tick() applies only
     * while nothing is reporting.
     */
    if (valid) {
        s.shown_deg = deg;
        /*
         * Paused, the servo is held where it is, and it goes on moving while
         * the HOLD is on its way and while it settles on the output held:
         * the angle a changed profile says again (reissue()) is the one it
         * reports, not the one it had at the tap.
         */
        if (s.paused) {
            const uint16_t was = deg_to_us(s.commanded_deg);
            s.commanded_deg = clamp_travel(deg);
            if (deg_to_us(s.commanded_deg) != was) {
                ++s.ctrl_rev;       /* COMMANDED shows it */
            }
        }
    }
    if (!same) {
        s.shown_q_deg = q_deg;
        ++s.ctrl_rev;
    }
}

void servo_screen_encoder(const servo_test_enc_t *e)
{
    if (e == NULL) {
        return;
    }
    /* A reading that was queued before the link went down is not the
     * angle now. */
    const servo_test_enc_t none = { .valid = false };
    if (!s.link_up) {
        e = &none;
    }
    s.enc_valid     = e->valid;
    s.enc_no_magnet = !e->valid && e->no_magnet;
    if (e->valid) {
        s.enc_raw = e->raw;
    }
    servo_test_encoder(&s.test, e);
    /* Redrawn only when the digits change: tenths of a degree. */
    const int q = e->valid ? (int)lroundf(enc_deg_now() * 10.0f) : 0x7FFF;
    if (q != s.shown_q_enc) {
        s.shown_q_enc = q;
        ++s.ctrl_rev;
    }
}

/* What the servo rail's meter reads, at the precision it is drawn with. */
typedef struct {
    bool    v_ok, i_ok;
    float   v;        /**< V                                              */
    float   i;        /**< A, signed                                      */
    float   p;        /**< W                                              */
    int32_t q_a;      /**< the current in hundredths of an amp, signed    */
    bool    clipped;  /**< a sample read an end of the meter's range      */
} rail_t;

/* @p x in units of 1/@p per, rounded half away from zero, for the digits of
 * a reading; held to +-10^8 so the count fits whatever the float holds. */
static int32_t fixed_of(float x, float per)
{
    const float q = x * per;
    if (!(q > -1.0e8f)) {
        return -100000000;
    }
    if (!(q < 1.0e8f)) {
        return 100000000;
    }
    return (int32_t)lroundf(q);
}

/*
 * The servo rail as its meter reads it.  With the INA3221 as the meter, its
 * last CH1 window: the mean bus voltage at the load side of the shunt, the
 * mean current and their product.  Otherwise the supply's last sample, which
 * is the PD mini's reading or the model's.  Only what arrived: a quantity
 * with no reading is not ok and is drawn as dashes and as a gap.
 */
static rail_t rail_now(void)
{
    rail_t r;
    memset(&r, 0, sizeof(r));
    if (s.source == SERVO_SOURCE_INA3221) {
        if (!s.have_win || !s.link_up) {
            return r;
        }
        r.v_ok    = s.win.voltage;
        r.i_ok    = s.win.current;
        r.v       = (float)s.win.mean_mv / 1000.0f;
        r.i       = (float)s.win.mean_ma / 1000.0f;
        r.p       = r.v * r.i;
        r.clipped = r.i_ok && (s.win.clip_hi || s.win.clip_lo);
        const int32_t ma = s.win.mean_ma;
        r.q_a     = (ma >= 0) ? (ma + 5) / 10 : -((5 - ma) / 10);
        return r;
    }
    if (!s.have_sup || !s.sup.online) {
        return r;
    }
    /* A reading that is not a number is no reading. */
    r.v_ok = (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u && isfinite(s.sup.v);
    r.i_ok = (s.sup.ok & SUPPLY_OK_CURRENT) != 0u && isfinite(s.sup.i);
    r.v    = s.sup.v;
    r.i    = s.sup.i;
    r.p    = s.sup.p;
    r.q_a  = r.i_ok ? fixed_of(s.sup.i, 100.0f) : 0;
    return r;
}

/* What the CURRENT row shows: the dashes, or the digits and their colour. */
#define CUR_KEY_NONE INT32_MIN
static int32_t cur_key_of(const rail_t *r)
{
    return r->i_ok ? r->q_a * 2 + (r->clipped ? 1 : 0) : CUR_KEY_NONE;
}

/* The CURRENT row after its meter or the meter's reading changed: redrawn
 * only when what it shows did, hundredths of an amp and the colour. */
static void cur_follow(void)
{
    const rail_t r = rail_now();
    const int32_t key = cur_key_of(&r);
    if (key != s.cur_key) {
        s.cur_key = key;
        ++s.cur_rev;
    }
}

/* One point of the plot from the meter's reading: a quantity that did not
 * arrive is a gap, not a zero. */
static void rail_plot(void)
{
    const rail_t r = rail_now();
    const float v[PS_COUNT] = {
        r.v_ok ? r.v : NAN,
        r.i_ok ? r.i : NAN,
        (r.v_ok && r.i_ok) ? r.p : NAN,
    };
    ui_plot_push(&s.power, v);
    ui_plot_update_scales(&s.power, RC_W);
    ++s.power_rev;
}

servo_test_reading_t test_reading_of(const supply_state_t *st)
{
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v        = st->v;
    r.i        = st->i;
    r.set_v    = st->set_v;
    r.set_i    = st->set_i;
    r.mode     = (uint8_t)st->mode;
    r.output   = st->output;
    r.online   = st->online;
    r.ok       = (st->ok & (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT))
                 == (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    r.trip     = st->trip;
    r.samples  = st->samples;
    /* Stamped where the panel had it; a sample without a stamp at its
     * arrival. */
    r.taken_ms = st->stamped ? st->taken_ms : test_now();
    return r;
}

/* One sample of the supply.  @p in_force: it was taken under the meter in
 * force, and not under one replaced since. */
static void supply_sample(const supply_state_t *st, bool in_force)
{
    s.sup = *st;
    s.have_sup = true;
    /* A restore waits for a sample taken after the run's OFF went in which
     * the supply itself reports the output off.  The panel's own flag is
     * its request: the PD mini switches off a link exchange and a module
     * transaction behind it, and a set point put back before then reaches
     * an output that is still on. */
    if (s.test_restore && s.restore_stage == RS_SAMPLE && !st->output
        && st->online && st->mode == SUPPLY_MODE_OFF
        && (int32_t)(test_reading_of(st).taken_ms - s.restore_ms) >= 0) {
        s.restore_stage = RS_OFF_SEEN;
    }
    /* The run's reading, with the horn's position where it is measured;
     * the supply's state alone in a run on the INA3221. */
    const servo_test_reading_t r = test_reading_of(st);
    servo_test_reading(&s.test, &r,
                       s.have_feedback ? deg_to_us(s.measured_deg) : 0u);
    servo_limrun_supply(&s.lim, &r, test_now());
    test_tell();
    /* The line, the plot and the CURRENT row are the supply's while it is
     * the rail's meter; with the INA3221 as the meter they are fed by its
     * windows (servo_screen_window()). */
    if (s.source == SERVO_SOURCE_INA3221) {
        return;
    }
    if (in_force) {
        rail_plot();
    } else {
        /* The supply's last reading all the same, on the line and in the
         * row; no point of a trace that began after it was taken. */
        ++s.power_rev;
    }
    cur_follow();
}

/*
 * The meter @p id as decided at change count @p changes, unless the meter in
 * force was decided at a later one: the snapshot, the windows and the
 * supply's samples reach the render task on three paths, and each can be
 * the oldest.  Returns whether it is the meter in force.
 */
static bool source_take(servo_source_id_t id, uint32_t changes)
{
    const int32_t ahead = s.have_changes ? (int32_t)(changes - s.changes) : 0;
    if (ahead < 0) {
        return false;
    }
    test_meter_follow(id, changes);
    lim_meter_follow(id, changes);
    s.have_changes = true;
    s.changes      = changes;
    /* More than one change ahead: changes went by unseen, and another meter
     * may have been the rail's in between, whatever the meter is called on
     * both sides of them. */
    const bool skipped = ahead > 1;
    if (id == s.source && !skipped) {
        return true;
    }
    const bool was_ina = (s.source == SERVO_SOURCE_INA3221);
    const bool now_ina = (id == SERVO_SOURCE_INA3221);
    s.source = id;
    if (was_ina != now_ina && s.ov_open) {
        ++s.ctrl_rev;           /* the line under START TEST names a range */
    }
    if (was_ina != now_ina || skipped) {
        /* The two meters do not agree, and the label names one of them:
         * the trace starts again with the meter it is labelled with. */
        ui_plot_clear(&s.power);
        ++s.power_rev;
    }
    if (skipped) {
        /* And a window kept from before them is not this meter's. */
        s.have_win = false;
    }
    cur_follow();
    return true;
}

void servo_screen_source(servo_source_id_t id, uint32_t changes)
{
    (void)source_take(id, changes);
}

void servo_screen_source_why(servo_source_why_t why)
{
    s.src_why = why;
}

void servo_screen_source_dropped(servo_source_why_t why, uint32_t at)
{
    if (why == SERVO_SOURCE_WHY_NONE) {
        return;
    }
    if (!s.have_drop || (int32_t)(at - s.drop_at) > 0) {
        s.have_drop = true;
        s.drop_why  = why;
        s.drop_at   = at;
    }
}

void servo_screen_supply(const supply_state_t *st)
{
    if (st != NULL) {
        supply_sample(st, true);
    }
}

void servo_screen_supply_at(const supply_state_t *st, servo_source_id_t id,
                            uint32_t changes)
{
    if (st != NULL) {
        supply_sample(st, source_take(id, changes));
    }
}

void servo_screen_window(const sense_link_win_t *w, servo_source_id_t id,
                         uint32_t changes)
{
    /* A window that was queued before the link went down is not the rail's
     * reading now, and neither is one taken under a meter that has been
     * replaced since. */
    if (w == NULL || !s.link_up || !source_take(id, changes)) {
        return;
    }
    s.win      = *w;
    s.have_win = true;
    if (s.source != SERVO_SOURCE_INA3221) {
        return;
    }
    {
        /* The runs' reading, stamped where the panel had it. */
        const servo_test_win_t tw = {
            .number   = w->number,
            .current  = w->current,
            .voltage  = w->voltage,
            .clipped  = w->clipped,
            .mean_ma  = w->mean_ma,
            .max_ma   = w->max_ma,
            .min_ma   = w->min_ma,
            .mean_mv  = w->mean_mv,
            .min_mv   = w->min_mv,
            .taken_ms = w->stamped ? w->taken_ms : test_now(),
        };
        if (s.test_on_ina) {
            servo_test_window(&s.test, &tw,
                              s.have_feedback ? deg_to_us(s.measured_deg)
                                              : 0u);
        }
        servo_limrun_window(&s.lim, &tw);
        test_tell();
    }
    rail_plot();
    cur_follow();
}

int servo_screen_power_points(void) { return s.power.filled; }

float servo_screen_power_sample(int series, int back)
{
    return ui_plot_sample(&s.power, series, back);
}

uint16_t servo_screen_commanded(void) { return deg_to_us(s.commanded_deg); }

const char *servo_screen_commanded_text(void)
{
    static char text[16];

    if (kst_panel_holds_port()) {
        /* The programmer holds the port: no pulse is on it. */
        return "---";
    }
    snprintf(text, sizeof(text), "%u us",
             (unsigned)deg_to_us(s.commanded_deg));
    return text;
}

uint16_t servo_screen_drawn(void) { return deg_to_us(s.shown_deg); }

uint16_t servo_screen_frame_hz(void) { return s.frame_hz; }

const char *servo_screen_type_name(void) { return type()->name; }

servo_rate_state_t servo_rate_in_force(uint16_t minor)
{
    return (minor >= LINK_MINOR_SERVO_TARGET_RATE)
               ? SERVO_RATE_IN_FORCE
               : SERVO_RATE_IN_FORCE_SURFACES;
}

void servo_screen_rate(servo_rate_state_t st, uint16_t hz)
{
    if (st == s.rate_st && hz == s.rate_hz) {
        return;
    }
    s.rate_st = st;
    s.rate_hz = hz;
    if (s.ov_open) {
        ++s.ctrl_rev;   /* the OUTPUT page's note says it */
    }
}

void servo_screen_set_commanded(float deg)
{
    (void)command(deg);
}

/* ------------------------------------------------------------- the profile */

/*
 * A profile goes into force: a type with its own pulse widths when it is a
 * different type, and a frame rate no faster than its pulses allow.
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

/*
 * A profile asked for: one that can destroy a servo not made for it opens
 * the warning, and goes into force only once that has been held; any other
 * goes into force at once.
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

static gfx_rect_t sup_row_rect(void)
{
    return (gfx_rect_t){ RC_X, SUP_Y, RC_W, SUP_H };
}

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
    /* STANDARD PWM at 50 Hz: what every restart starts at, whatever the
     * session before it used. */
    s.type      = 0;
    s.min_us    = k_types[0].min_us;
    s.centre_us = k_types[0].centre_us;
    s.max_us    = k_types[0].max_us;
    s.frame_hz  = k_types[0].default_hz;

    ui_slider_init(&s.speed, (gfx_rect_t){ RC_X, 296, RC_W, 22 },
                   10.0f, 100.0f, 0);
    s.speed.value = 100.0f;
    ui_slider_set_ticks(&s.speed, 0);
    /* A sweep speed commands nothing on its own, and the track is 22 px: a
     * tap is how it is used.  The throttle's rule is the throttle's. */
    ui_slider_set_tap_to_set(&s.speed, true);

    s.centre_btn  = (gfx_rect_t){ RC_X, 350, (int16_t)(RC_W / 3 - 4), 32 };
    s.sweep_btn   = (gfx_rect_t){ (int16_t)(RC_X + RC_W / 3 + 2), 350,
                                  (int16_t)(RC_W / 3 - 4), 32 };
    s.release_btn = (gfx_rect_t){ (int16_t)(RC_X + 2 * (RC_W / 3) + 4), 350,
                                  (int16_t)(RC_W - 2 * (RC_W / 3) - 4), 32 };
    /* Full width and last, under the three that only shape what is commanded:
     * this is the one that decides whether anything is driven at all. */
    s.arm_btn     = (gfx_rect_t){ RC_X, 388, RC_W, 32 };
    s.out_btn     = (gfx_rect_t){ SUP_OUT_X, SUP_Y,
                                  (int16_t)(RC_X + RC_W - SUP_OUT_X), SUP_H };
    s.set_btn     = (gfx_rect_t){ (int16_t)(RCARD_X + RCARD_W - 12 - SETB_W),
                                  12, SETB_W, 24 };
    s.prog_btn    = (gfx_rect_t){ (int16_t)(s.set_btn.x - 6 - PROGB_W),
                                  12, PROGB_W, 24 };
    kst_panel_init(overlay_area());
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
static void ask_disarm(void)
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

void servo_screen_set_link(bool up)
{
    if (s.link_up && !up) {
        test_end_now(SERVO_TEST_AB_LINK);
        /* The encoder's last angle is not the horn's now: the dashes, and
         * no ENC CENTRE from it, until a reading arrives with the link. */
        const servo_test_enc_t none = { .valid = false };
        s.enc_valid     = false;
        s.enc_no_magnet = false;
        servo_test_encoder(&s.test, &none);
        if (s.shown_q_enc != 0x7FFF) {
            s.shown_q_enc = 0x7FFF;
            ++s.ctrl_rev;
        }
        /* And the INA3221's last window is not the rail's reading now: the
         * dashes in the row and on the line while it is the meter. */
        s.have_win = false;
        if (s.source == SERVO_SOURCE_INA3221) {
            ++s.power_rev;
        }
        cur_follow();
        /*
         * A sweep or a pause: the far end stops a sweep and lets a hold go
         * 500 ms after the last write it heard, and the surfaces rest.  The
         * screen ends them as well and draws the output at rest, so nothing
         * stays PAUSED over an output that has gone, and the next tap starts
         * a sweep rather than resuming one.
         */
        /* And one not yet taken is not sent once the link is back. */
        if (!servo_cmd_survives_link_loss(&s.pending)) {
            s.pending.kind = SERVO_CMD_NONE;
            s.toggle_live  = false;
        }
        if (s.sweeping || s.paused || s.awaiting || s.dr.on) {
            stop_sweep();
            s.paused        = false;
            s.driving       = false;
            s.toggle_live   = false;
            s.sw.running    = false;
            s.sw.paused     = false;
            if (!s.have_feedback) {
                s.shown_cmd = (float)OUT_SPAN / 2.0f;
                s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
            }
            s.commanded_deg = clamp_travel(s.shown_deg);
            ++s.ctrl_rev;
        }
    }
    s.link_up = up;
}

void servo_screen_set_enc_held(bool held)
{
    s.enc_held = held;
}

void servo_screen_set_drive(uint8_t target, uint8_t gpio, bool pwm)
{
    if (target == 0u || target == SERVO_SCREEN_TARGET_UNKNOWN
        || target == SERVO_SCREEN_TARGET_NO_LINK) {
        gpio = SERVO_SCREEN_NO_PIN;
        pwm  = false;
    }
    if (target != s.drive_target || gpio != s.drive_pin
        || pwm != s.drive_pwm) {
        s.drive_target = target;
        s.drive_pin    = gpio;
        s.drive_pwm    = pwm;
        ++s.ctrl_rev;
    }
}

void servo_screen_set_pins(uint64_t gpio_mask, bool known)
{
    if (gpio_mask != s.pins || known != s.pins_known) {
        s.pins = gpio_mask;
        s.pins_known = known;
        ++s.ctrl_rev;
    }
}

void servo_screen_set_surfaces(bool any)
{
    if (any == s.surfaces) {
        return;
    }
    s.surfaces = any;
    ++s.ctrl_rev;                       /* SWEEP greys or comes back */
}

void servo_screen_sweep_refused(void)
{
    s.toggle_live = false;      /* no undo across a notification */
    /* Nothing bound to sweep: no start comes, nothing moves, and nothing
     * is driven for a change of profile to say again. */
    if (s.sweeping || s.paused) {
        stop_sweep();
        s.paused  = false;
        s.driving = false;
        s.commanded_deg = clamp_travel(s.shown_deg);
        ++s.ctrl_rev;
    }
}

uint16_t servo_cmd_pause_root(const servo_cmd_t *c)
{
    if (c == NULL) {
        return 0u;
    }
    return (c->from_pause != 0u) ? c->from_pause : c->pause_seq;
}

bool servo_cmd_stale(servo_pause_end_t *e, const servo_cmd_t *c)
{
    if (e == NULL || c == NULL || !e->on) {
        return false;
    }
    const bool drive = c->kind == SERVO_CMD_POSITION
                       || c->kind == SERVO_CMD_CENTRE
                       || c->kind == SERVO_CMD_SWEEP
                       || c->kind == SERVO_CMD_HOLD;
    if (!drive) {
        return false;
    }
    if (c->from_pause != 0u && c->from_pause == e->pause_seq) {
        return true;
    }
    e->on = false;              /* from after it: nothing of it is queued */
    return false;
}

bool servo_cmd_survives_link_loss(const servo_cmd_t *c)
{
    return c != NULL && c->kind != SERVO_CMD_SWEEP
           && c->kind != SERVO_CMD_HOLD;
}

void servo_screen_set_bench_armed(bool armed)
{
    s.bench_armed = armed;
}

/* ARM takes no press while the programmer holds the port: programming and
 * an armed bench are exclusive.  DISARM is always taken. */
static bool arm_locked(void)
{
    return (kst_panel_holds_port() || servo_limrun_running(&s.lim))
           && !s.gate.armed;
}

/* ------------------------------------------------------------------ events */

/* Whether a touch is close enough to the horn's sweep to mean the horn. */
static bool on_the_dial(int px, int py, float *deg)
{
    const float dx = (float)(px - SHAFT_X);
    const float dy = (float)(SHAFT_Y - py);
    const float r  = sqrtf(dx * dx + dy * dy);
    /* Generous inside, bounded outside: a finger landing short of the horn
     * still means the horn, and one landing well past the arc means the
     * card behind it. */
    if (r < 30.0f || r > (float)ARC_R + 34.0f) {
        return false;
    }
    if (dx < -30.0f) {
        return false;      /* left of the shaft is the case, not the sweep */
    }
    const float phi = atan2f(dy, dx) * 180.0f / 3.14159265358979f;
    if (phi < -104.0f || phi > 104.0f) {
        return false;
    }
    *deg = phi;
    return true;
}

/* Latches that something other than the knob holds the horn. */
static void knob_note_owner(void)
{
    if (s.ov_open || kst_panel_is_open() || servo_test_running(&s.test)
        || s.sweeping || s.paused) {
        s.knob_owned = true;
    }
}

void servo_screen_knob_frame(void)
{
    s.knob_finger = false;
    s.knob_owned  = false;
    knob_note_owner();
}

static void knob_withdraw(void)
{
    if (!s.knob_live) {
        return;
    }
    s.knob_live = false;
    if (s.post_count != s.knob_post || s.pending.kind != SERVO_CMD_POSITION) {
        return;     /* something else was posted over it since */
    }
    s.commanded_deg = s.knob_prev_deg;
    /* Held as before the knob only on an armed bench: a stop or a disarm
     * since the knob's post holds nothing, and a screen that read itself as
     * driving would say the position again on the next change of SPEED. */
    s.driving       = s.knob_had_driving && value_live();
    ++s.ctrl_rev;
    if (s.knob_had_cmd) {
        s.pending.value_us = deg_to_us(s.commanded_deg);
    } else {
        s.pending.kind = SERVO_CMD_NONE;
    }
}

void servo_screen_knob_cancel(void)
{
    knob_withdraw();
}

void servo_screen_knob(float span_fraction)
{
    knob_note_owner();
    if (span_fraction == 0.0f || s.dragging || s.knob_finger
        || s.knob_owned) {
        return;
    }
    /* A turn while the value is not live moves nothing and is not kept:
     * the next turn after an arm starts from the value the arm set. */
    if (!value_live()) {
        return;
    }
    /*
     * Posts only into an empty slot or over a position, which it may replace
     * as a drag does.  A release, an arm, a disarm, a hold or a sweep waiting
     * to be taken is not the knob's to overwrite; its delta is dropped.
     */
    if (s.pending.kind != SERVO_CMD_NONE
        && s.pending.kind != SERVO_CMD_POSITION) {
        return;
    }
    const float before = s.commanded_deg;
    const bool  was_knob = s.knob_live && s.post_count == s.knob_post;
    const bool  had_cmd  = s.pending.kind == SERVO_CMD_POSITION;
    const bool  was_driving = s.driving;
    const uint32_t posts = s.post_count;
    (void)command(before + span_fraction * 2.0f * s.travel_deg);
    if (s.post_count != posts) {
        if (!was_knob) {
            s.knob_prev_deg = before;
            s.knob_had_cmd  = had_cmd;
            s.knob_had_driving = was_driving;
        }
        s.knob_live = true;
        s.knob_post = s.post_count;
    }
}

static void event_body(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    kst_panel_bench_armed(bench_arming());
    const int px = evt->point.x, py = evt->point.y;

    /* The programmer's own press first, then the overlay's. */
    if (kst_panel_has_press(evt->point.id)
        && evt->type != TOUCH_EVENT_DOWN) {
        kst_panel_event(evt);
        return;
    }
    /* The overlay's own press: its moves and its release are its. */
    if (s.ov_have && evt->point.id == s.ov_id
        && evt->type != TOUCH_EVENT_DOWN) {
        ov_rest(evt);
        return;
    }

    if (evt->type == TOUCH_EVENT_DOWN) {
        /*
         * Any press ends a drag on the dial or on SPEED that is still
         * latched, wherever the press lands; ui_slider_event() gives the
         * reason.  A release can go missing, and a later contact reusing
         * the track id would command a position, or a rate, with no press
         * on the control.  A finger that was on the dial still owned it in
         * this frame, so the knob stays out of the frame.
         */
        if (s.dragging) {
            s.knob_finger = true;
        }
        s.dragging = false;
        ui_slider_release(&s.speed);
        if (s.ov_open && gfx_rect_contains(overlay_area(), px, py)) {
            /* One press at a time in the overlay. */
            if (!s.ov_have) {
                ov_down(evt);
            }
            return;
        }
        if (kst_panel_is_open() && gfx_rect_contains(overlay_area(), px, py)) {
            if (!s.ov_have) {
                kst_panel_event(evt);
            }
            return;
        }
        if (gfx_rect_contains(s.prog_btn, px, py)) {
            if (!s.ov_have) {
                ov_take(evt, OP_PROGRAM, -1);
            }
            return;
        }
        if (gfx_rect_contains(s.set_btn, px, py)) {
            if (!s.ov_have) {
                ov_take(evt, OP_SETTINGS, -1);
            }
            return;
        }
        if (gfx_rect_contains(s.out_btn, px, py)) {
            /* A programming port holds the supply: off is taken, on is
             * not. */
            if (!supply_screen_output_on() && supply_screen_port_held()) {
                return;
            }
            /* Off, with a set point past a standard servo's rating: the HV
             * warning, whose hold switches it on.  One press in the
             * overlay at a time. */
            if (!s.out_down && !supply_screen_output_on()
                && hv_set_point()) {
                if (!s.ov_have) {
                    ask_hv_on();
                }
                return;
            }
            /* One contact owns the switch, as on ARM. */
            if (!s.out_down) {
                s.out_down     = true;
                s.out_id       = evt->point.id;
                s.out_press_on = supply_screen_output_on();
                if (!s.out_press_on) {
                    ui_hold_begin(&s.out_hold);
                }
                ++s.sup_rev;
            }
            return;
        }
        for (int k = 0; k < 2; ++k) {
            if (gfx_rect_contains(sup_val_rect(k), px, py)) {
                /* No set point moves under a programming port. */
                if (supply_screen_port_held()) {
                    return;
                }
                /* The run owns the set points while it runs. */
                test_end_now(SERVO_TEST_AB_OPERATOR);
                if (!s.ov_have) {
                    ov_take(evt, (k == 0) ? OP_SUP_V : OP_SUP_I, -1);
                }
                return;
            }
        }
        if (s.lim_box && !s.ov_open && !kst_panel_is_open()
            && gfx_rect_contains(test_btn_rect(), px, py)) {
            /* STOP RUN ends the limit run on the press; CLOSE puts the
             * result away. */
            if (servo_limrun_running(&s.lim)) {
                lim_end_now(SERVO_TEST_AB_OPERATOR);
            } else {
                s.lim_box = false;
            }
            servo_invalidate();
            ++s.ctrl_rev;
            return;
        }
        if (s.test_box && !s.ov_open && !kst_panel_is_open()
            && gfx_rect_contains(test_btn_rect(), px, py)) {
            /* STOP TEST ends the run on the press, as a stop does; CLOSE
             * puts the result away. */
            if (servo_test_running(&s.test)) {
                test_end_now(SERVO_TEST_AB_OPERATOR);
            } else {
                s.test_box = false;
            }
            ++s.ctrl_rev;
            return;
        }
        float deg;
        if (!s.ov_open && !kst_panel_is_open() && on_the_dial(px, py, &deg)) {
            /* Refused while the value is not live: the press is the dial's
             * and takes no drag, so a bench armed under it moves nothing
             * until the next press. */
            if (!value_live()) {
                return;
            }
            /* A finger on the dial takes the horn from a sweep, and from a
             * run. */
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            s.dragging = true;
            s.knob_finger = true;
            s.drag_id  = evt->point.id;
            (void)command(deg);
            return;
        }
        if (gfx_rect_contains(s.centre_btn, px, py)) {
            /* A position like any other, and refused like one. */
            if (!value_live()) {
                return;
            }
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            s.commanded_deg = 0.0f;
            post(SERVO_CMD_CENTRE, deg_to_us(0.0f));
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.sweep_btn, px, py)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
            toggle_sweep();
        } else if (gfx_rect_contains(s.release_btn, px, py)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            release();
        } else if (gfx_rect_contains(s.arm_btn, px, py)) {
            if (arm_locked()) {
                /* Programming and an armed bench are exclusive. */
                return;
            }
            if (s.arm_down) {
                /* The gesture belongs to the contact that began it.  A second
                 * finger, or a palm, taking it over would leave the first
                 * one's release ignored and arm the bench from a contact
                 * nobody made deliberately. */
                return;
            }
            s.arm_down = true;
            s.arm_id   = evt->point.id;
            ui_hold_begin(&s.arm);
            ++s.arm_rev;
        }
    }

    if (s.out_down && evt->point.id == s.out_id) {
        const bool on = supply_screen_output_on();
        if (evt->type == TOUCH_EVENT_MOVE) {
            /* Off the switch abandons the hold; a press that began on a
             * live output is an OFF, whose release is checked against the
             * switch. */
            if (!s.out_press_on && !gfx_rect_contains(s.out_btn, px, py)
                && ui_hold_leave(&s.out_hold)) {
                s.out_down = false;
                ++s.sup_rev;
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            const bool fired = ui_hold_end(&s.out_hold);
            s.out_down = false;
            ++s.sup_rev;
            /* Off is a tap; on is a hold that has already asked by the time
             * the finger lifts.  Which one is the press's: an earlier ON
             * reported during a second hold does not make its release an
             * OFF. */
            if (s.out_press_on && on && !fired
                && gfx_rect_contains(s.out_btn, px, py)) {
                supply_screen_ask_off();
            }
            return;
        }
    }

    if (s.arm_down && evt->point.id == s.arm_id) {
        if (evt->type == TOUCH_EVENT_MOVE) {
            /* A finger that leaves ARM abandons the hold -- see
             * ui_hold_leave().  While the bench is armed the same press is a
             * disarm, whose release is checked against the rectangle. */
            if (!s.gate.armed && !gfx_rect_contains(s.arm_btn, px, py)
                && ui_hold_leave(&s.arm)) {
                s.arm_down = false;
                ++s.arm_rev;
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            const bool fired = ui_hold_end(&s.arm);
            s.arm_down = false;
            ++s.arm_rev;
            /*
             * Disarming is a press; arming is a hold that has already sent
             * its command by the time the finger lifts.
             */
            if (s.gate.armed && !fired && gfx_rect_contains(s.arm_btn, px, py)) {
                ask_disarm();
            }
            return;
        }
    }

    if (s.dragging && evt->point.id == s.drag_id) {
        s.knob_finger = true;
        if (evt->type == TOUCH_EVENT_MOVE) {
            float deg;
            if (on_the_dial(px, py, &deg)) {
                (void)command(deg);
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            s.dragging = false;
            return;
        }
    }

    if (ui_slider_event(&s.speed, evt)) {
        s.speed_pct = (int)(s.speed.value + 0.5f);
        /* The rate is part of the command, so a held output takes the new
         * one rather than waiting for the next drag.  Not a paused sweep:
         * the hold moves nothing, and the resume carries the new rate. */
        if (!s.paused) {
            reissue();
        }
        ++s.ctrl_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

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

static void draw_horn(gfx_canvas_t *c, float deg, gfx_color_t col)
{
    int tx, ty;
    at(deg, HORN_L, &tx, &ty);

    /*
     * The unlit rings are drawn first and whole, so the arm is drawn over
     * them and they pass behind it.
     */
    const gfx_color_t ghost =
        gfx_lerp(ui_theme_color(UI_C_PANEL), col, 46);
    gfx_arc(c, tx, ty, 25, 3, 0.0f, 360.0f, ghost);
    gfx_arc(c, tx, ty, 33, 2, 0.0f, 360.0f, ghost);

    gfx_capsule_aa(c, SHAFT_X, SHAFT_Y, tx, ty, 26, col);
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 25, col);

    /* Holes along the arm, spaced from the tip inwards so the outermost stays
     * on the arm whatever HORN_L is; a hole drawn past the tip reads as a
     * bite out of the card. */
    for (int i = 1; i <= 3; ++i) {
        int hx, hy;
        at(deg, HORN_L - 12 - (3 - i) * 14, &hx, &hy);
        gfx_fill_circle_aa(c, hx, hy, 5, ui_theme_color(UI_C_PANEL));
    }
    /* And the outermost one, which is the hole the ring is pointing at. */
    gfx_fill_circle_aa(c, tx, ty, 5, ui_theme_color(UI_C_PANEL));

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
    gfx_arc_fade(c, tx, ty, 25, 3, deg + 180.0f + g_in,
                 deg + 540.0f - g_in, grip, ghost, 78.0f);
    gfx_arc_fade(c, tx, ty, 33, 2, deg + 180.0f + g_out,
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
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(lx - 24), (int16_t)(ly - 8),
                                     48, 16 },
                    lbl, UI_FONT_LABEL, dim, 1, GFX_ALIGN_CENTER);
    }
}

/*
 * @p text in the 8x16 face from (x, y) down, @p pitch px a line, in lines
 * of at most @p cells cells: each broken at the last double space that
 * keeps the line within them, the gap between two fields, or else at the
 * last single space.  A word longer than a line stays whole.  With @p c
 * NULL nothing is drawn.  Returns the lines it takes.
 */
int wrap_text(gfx_canvas_t *c, int x, int y, int pitch,
                     const char *text, int cells, gfx_color_t ink)
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
            gfx_text(c, x, y + lines * pitch, buf, &gfx_font_8x16, ink, 1);
        }
        ++lines;
        rest += len;
        while (*rest == ' ') {
            ++rest;
        }
    }
    return lines;
}

/* Characters the TARGET row's value holds: 188 px of the 8x16 face. */
#define TARGET_CELLS 23u

bool servo_target_line(char *out, size_t size, uint8_t target, uint8_t gpio,
                       bool pwm, uint64_t pins, bool known)
{
    if (out == NULL || size == 0u) {
        return false;
    }
    /* No link, or the coprocessor's page not known: the tester sends
     * nothing, and where it would go is not known. */
    if (target == SERVO_SCREEN_TARGET_NO_LINK) {
        snprintf(out, size, "%s", TR(SV_TARGET_NO_LINK));
        return true;
    }
    if (target == SERVO_SCREEN_TARGET_UNKNOWN) {
        snprintf(out, size, "%s", TR(SV_TARGET_UNKNOWN));
        return true;
    }
    /* The target the drive uses, not the setting: a change waits for a
     * quiet bench.  A port target is the port whether or not its pin is
     * known: the commands go there and do not reach the SERVO PWM outputs.
     * With its pulse off the port renders none, and the line says so. */
    if (target != 0u) {
        if (gpio == SERVO_SCREEN_NO_PIN) {
            snprintf(out, size, "%s",
                     pwm ? TR(SV_TARGET_PORT) : TR(SV_TARGET_PORT_OFF));
        } else {
            snprintf(out, size,
                     pwm ? TR(SV_TARGET_PORT_PIN) : TR(SV_TARGET_PORT_PIN_OFF),
                     (unsigned)gpio);
        }
        return true;
    }
    if (!known) {
        snprintf(out, size, "%s", TR(SV_TARGET_UNREAD));
        return true;
    }
    if (pins == 0u) {
        snprintf(out, size, "%s", TR(SV_TARGET_NONE));
        return true;
    }
    /* As many pins as the value holds, then "+N" (ui_pinlist()). */
    char list[UI_PINLIST_PINS * 5u];
    ui_pinlist(list, sizeof(list), pins, TARGET_CELLS);
    snprintf(out, size, "%s", list);
    return false;
}

static void draw_left(gfx_canvas_t *c)
{
    /* Everything that moves lives in this rectangle, so this is what gets
     * cleared -- the case and the dial below it are chrome. */
    gfx_fill_rect(c, PAD + 1, PAD + 1, LCARD_W - 2, H - 2 * PAD - 2,
                  ui_theme_color(UI_C_PANEL));
    draw_dial(c);
    /* The case first: the horn bolts to the top of it and must be drawn over
     * it, not under. */
    draw_body(c);

    /* The commanded position behind the measured one, so a lag is visible as
     * two arms rather than as a number that disagrees with a picture. */
    if (fabsf(s.shown_deg - s.commanded_deg) > 1.0f) {
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

    char deg[16];
    snprintf(deg, sizeof(deg), "%+.1f", (double)s.shown_deg);
    const gfx_seg_style_t seg = ui_seg_hero();
    const int dw = gfx_seg_width(deg, &seg);
    gfx_seg_text(c, PAD + 46, H - 84, deg, &seg,
                 ui_theme_color(UI_C_ACCENT),
                 gfx_lerp(ui_theme_color(UI_C_PANEL),
                          ui_theme_color(UI_C_ACCENT), 30));
    gfx_text(c, PAD + 52 + dw, H - 70, "DEG",
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    /* Why the dial takes no press, in the words START TEST's line uses. */
    if (!value_live()) {
        gfx_text(c, PAD + 46, H - 36,
                 ui_servo_str(SERVO_STR_START_NOT_ARMED), UI_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
    if (s.test_box) {
        draw_test_box(c);
    } else if (s.lim_box) {
        draw_lim_box(c);
    }
}

static inline void row_in(gfx_canvas_t *c, int y, const char *label,
                   gfx_color_t label_col, const char *value)
{
    gfx_text(c, RC_X, y + 5, label, UI_FONT_LABEL, label_col, 1);
    if (value != NULL) {
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(RC_X + 80), (int16_t)(y + 5),
                                     (int16_t)(RC_W - 80), 16 },
                    value, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                    GFX_ALIGN_RIGHT);
    }
}

static void row(gfx_canvas_t *c, int y, const char *label, const char *value)
{
    row_in(c, y, label, ui_theme_color(UI_C_TEXT_DIM), value);
}

/*
 * The same fill as MOTOR & ESC's ARM, from the same widget: armed is the
 * danger red the press fades towards, and the flash is the whole button.
 */
static gfx_color_t arm_fill(void)
{
    gfx_color_t fill = s.gate.armed ? ui_theme_color(UI_C_DANGER)
                               : ui_theme_color(UI_C_OK);
    if (s.arm.flash_left > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), s.arm.flash_left);
    }
    if (!s.gate.armed && s.arm.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER), s.arm.held_s);
    }
    return fill;
}

static void draw_arm(gfx_canvas_t *c)
{
    ui_button(c, s.arm_btn, s.gate.armed ? "DISARM" : "ARM", arm_fill(),
              s.arm_down, !arm_locked());
}

static gfx_rect_t power_rect(void)
{
    return (gfx_rect_t){ RC_X, (int16_t)(PWR_TXT_Y - 2), RC_W,
                         (int16_t)(PWR_Y + PWR_H - PWR_TXT_Y + 2) };
}

/* @p q, a signed count of 10^-@p places of @p unit, as text: the sign kept,
 * and a reading that rounds to nothing drawn without one. */
static void fixed_text(char *out, size_t n, int32_t q, int places,
                       const char *unit)
{
    const int32_t  scale = (places == 2) ? 100 : 10;
    const uint32_t mag   = (q < 0) ? (uint32_t)(-(int64_t)q) : (uint32_t)q;
    snprintf(out, n, "%s%u.%0*u %s", (q < 0) ? "-" : "",
             (unsigned)(mag / (uint32_t)scale), places,
             (unsigned)(mag % (uint32_t)scale), unit);
}

/* The servo rail's voltage, current and power, read and plotted, under the
 * name of its meter: the supply, or INA3221 CH1 while that is the meter. */
static void draw_power(gfx_canvas_t *c)
{
    const gfx_rect_t r = power_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    const bool ina = (s.source == SERVO_SOURCE_INA3221);
    /* The supply is the PD mini, named so in both languages, also while
     * the panel's model stands in for it; the model carries the pill
     * below. */
    gfx_text(c, RC_X, PWR_TXT_Y, ina ? TR(SV_SOURCE_INA) : "PD MINI",
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    const rail_t m = rail_now();
    char v[20], a[20], w[20];
    if (m.v_ok) { snprintf(v, sizeof(v), "%.2f V", (double)m.v); }
    else        { snprintf(v, sizeof(v), "-- V"); }
    if (m.i_ok) { fixed_text(a, sizeof(a), m.q_a, 2, "A"); }
    else        { snprintf(a, sizeof(a), "-- A"); }
    const uint8_t *fw = k_line_w[ina ? 1 : 0];
    if (m.v_ok && m.i_ok) {
        /* Tenths of a watt, and whole watts where those do not fit. */
        const float p = isfinite(m.p) ? m.p : 0.0f;
        fixed_text(w, sizeof(w), fixed_of(p, 10.0f), 1, "W");
        if (gfx_text_width(UI_FONT_LABEL, w, 1) > fw[PS_W]) {
            snprintf(w, sizeof(w), "%ld W", (long)fixed_of(p, 1.0f));
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
        gfx_text_in(c, (gfx_rect_t){ (int16_t)x, PWR_TXT_Y, fw[k], 16 },
                    txt[k], UI_FONT_LABEL, tc, 1, GFX_ALIGN_RIGHT);
        x += fw[k];
    }
    ui_plot_render(&s.power, c, (gfx_rect_t){ RC_X, PWR_Y, RC_W, PWR_H });
    /*
     * The readings of the panel's model are no measurement: the overview's
     * pill, 88 px or as wide as a longer word and 20 px high, on the plot's
     * top-left corner under the name, as the line beside the name is full.
     */
    if (!ina && supply_screen_model()) {
        int mw = 24 + gfx_text_width(UI_FONT_LABEL, TR(SV_MODELLED), 1);
        mw = (mw < 88) ? 88 : mw;
        ui_pill(c, (gfx_rect_t){ RC_X + 2, PWR_Y + 2, (int16_t)mw, 20 },
                TR(SV_MODELLED), ui_theme_color(UI_C_WARN),
                ui_theme_color(UI_C_PANEL_SUNK));
    }
}

/* The value of the CURRENT row, where row() draws a row's value. */
static gfx_rect_t cur_val_rect(void)
{
    return (gfx_rect_t){ (int16_t)(RC_X + 80), (int16_t)(CUR_Y + 5),
                         (int16_t)(RC_W - 80), 16 };
}

/* The rail's current as its meter reads it, signed, in hundredths of an
 * amp; in the warning colour from a clipped window. */
static void cur_value(gfx_canvas_t *c)
{
    const rail_t m = rail_now();
    char buf[20];
    if (m.i_ok) { fixed_text(buf, sizeof(buf), m.q_a, 2, "A"); }
    else        { snprintf(buf, sizeof(buf), "---"); }
    gfx_text_in(c, cur_val_rect(), buf, UI_FONT_LABEL,
                m.clipped ? ui_theme_color(UI_C_WARN)
                          : ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_RIGHT);
}

/* The same over its own background: the row's repaint between two repaints
 * of the card. */
static void draw_cur_value(gfx_canvas_t *c)
{
    const gfx_rect_t r = cur_val_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    cur_value(c);
}

/* OUTPUT ON and OFF in SUPPLY's colours: off is the green the hold fades
 * from, on the danger red it fades to. */
static gfx_color_t out_fill(void)
{
    if (s.out_hold.flash_left > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER),
                             s.out_hold.flash_left);
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
    gfx_text(c, RC_X, SUP_Y + 5, TR(SV_SET), UI_FONT_LABEL,
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
        gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, 4,
                            down ? ui_theme_color(UI_C_PANEL_HI)
                                 : ui_theme_color(UI_C_PANEL_SUNK));
        gfx_text_in(c, b, k[i].txt, UI_FONT_LABEL, k[i].col, 1,
                    GFX_ALIGN_CENTER);
    }
    ui_button(c, s.out_btn, s.out_on ? TR(SUP_OUTPUT_OFF) : TR(SUP_OUTPUT_ON),
              out_fill(),
              s.out_down, s.out_on || !held);
    if (s.out_hold.flash_left > 0) {
        ui_hold_flash_step(&s.out_hold);
    }
}

static gfx_rect_t tag_rect(void)
{
    return (gfx_rect_t){ RC_X, TAG_Y, RC_W, TAG_H };
}

/* The profile in force, its frame rate and its pulse range in
 * microseconds, in the danger colour when it is one that can destroy a
 * servo not made for it.  The longest, a 13-character type, 560 Hz and two
 * 4-digit ends, is 32 of the row's 33 cells. */
static void draw_tag(gfx_canvas_t *c)
{
    const gfx_rect_t r = tag_rect();
    const bool danger = in_force_dangerous();
    const gfx_color_t fill = danger ? ui_theme_color(UI_C_DANGER)
                                    : ui_theme_color(UI_C_PANEL_SUNK);
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, 4, fill);
    char line[40];
    snprintf(line, sizeof(line), "%s  %u Hz  %u-%u", type()->name,
             (unsigned)s.frame_hz, (unsigned)s.min_us, (unsigned)s.max_us);
    gfx_text_in(c, r, line, UI_FONT_LABEL,
                danger ? GFX_WHITE : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
}

/*
 * What the tester drives, on the right card, which the settings, their
 * keypads and the servo programmer leave in sight.  The programming port is
 * the rare case and takes the warning colour: the tester's commands then do
 * not reach the SERVO PWM outputs, which an armed bank holds at their rest.
 * No pin, a binding not read, a target not known and no link take it too.
 */
static void draw_target_row(gfx_canvas_t *c)
{
    char line[32];
    const bool warn = servo_target_line(line, sizeof(line), s.drive_target,
                                        s.drive_pin, s.drive_pwm, s.pins,
                                        s.pins_known);
    const gfx_color_t col = warn ? ui_theme_color(UI_C_WARN)
                                 : ui_theme_color(UI_C_TEXT_DIM);
    gfx_text(c, RC_X, TARGET_Y + 5, TR(SV_TARGET), UI_FONT_LABEL, col, 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(RC_X + 80), (int16_t)(TARGET_Y + 5),
                                 (int16_t)(RC_W - 80), 16 },
                line, UI_FONT_LABEL,
                warn ? ui_theme_color(UI_C_WARN) : ui_theme_color(UI_C_TEXT),
                1, GFX_ALIGN_RIGHT);
}

/*
 * The right card.  The power plot only when @p power: a drag of the horn
 * repaints this card every frame for the COMMANDED line, and the plot
 * beside it has not moved -- its own samples repaint it, clipped.
 */
static void draw_right(gfx_canvas_t *c, bool power)
{
    const gfx_rect_t pr = power_rect();
    if (power) {
        gfx_fill_rect(c, RCARD_X + 1, PAD + 1, RCARD_W - 2, H - 2 * PAD - 2,
                      ui_theme_color(UI_C_PANEL));
    } else {
        /* Around the plot, which keeps what it shows. */
        gfx_fill_rect(c, RCARD_X + 1, PAD + 1, RCARD_W - 2,
                      pr.y - PAD - 1, ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, RCARD_X + 1, pr.y + pr.h, RCARD_W - 2,
                      H - PAD - 1 - (pr.y + pr.h), ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, RCARD_X + 1, pr.y, pr.x - RCARD_X - 1, pr.h,
                      ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, pr.x + pr.w, pr.y, RCARD_X + RCARD_W - 1 - (pr.x + pr.w),
                      pr.h, ui_theme_color(UI_C_PANEL));
    }

    gfx_text(c, RC_X, 18, "SERVO", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    ui_button(c, s.set_btn, TR(SUP_SETTINGS),
              s.ov_open ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_SETTINGS,
              !kst_panel_holds_port());
    ui_button(c, s.prog_btn, TR(KP_PROGRAM),
              kst_panel_is_open() ? ui_theme_color(UI_C_ACCENT)
                                  : ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_PROGRAM, true);

    char buf[24];
    row(c, 44, TR(SV_COMMANDED), servo_screen_commanded_text());
    /* The horn's angle from the encoder where one reads.  With the encoder
     * off in SETUP, from the feedback; with it on and not reading, the
     * dashes: the pulse position is not the horn's angle then. */
    if (enc_shown()) {
        snprintf(buf, sizeof(buf), "%+.1f deg", (double)enc_deg_now());
        row(c, 68, TR(SV_MEASURED), buf);
    } else if (s.have_feedback && !settings_get_bool(SET_ENC_EN)) {
        snprintf(buf, sizeof(buf), "%+.1f deg", (double)s.measured_deg);
        row(c, 68, TR(SV_MEASURED), buf);
    } else {
        row(c, 68, TR(SV_MEASURED), "---");
    }
    row(c, CUR_Y, TR(SV_CURRENT), NULL);
    cur_value(c);

    draw_tag(c);
    if (power) {
        draw_power(c);
    }
    draw_sup_row(c);

    /* SPEED's row says when SPEED, not CURVE, decides how a sweep moves:
     * the curves then look alike. */
    snprintf(buf, sizeof(buf), "%d %%", s.speed_pct);
    if (speed_limits_sweep()) {
        row_in(c, 264, TR(SV_SPEED_LIMITS), ui_theme_color(UI_C_WARN), buf);
    } else {
        row(c, 264, TR(SV_SPEED), buf);
    }
    s.speed.color = ui_theme_color(UI_C_ACCENT);
    ui_slider_render(&s.speed, c);

    draw_target_row(c);

    /* CENTRE sets a position, so it is dimmed with SWEEP while the bench
     * is disarmed. */
    ui_button(c, s.centre_btn, TR(SV_CENTRE_BTN), ui_theme_color(UI_C_ACCENT),
              false, value_live());
    /* PAUSE while a sweep runs, in the accent; PAUSED (PAUSIERT) while it
     * is paused, filled in the warning colour, so the two read apart by
     * fill as well as by the word. */
    const gfx_color_t sweep_fill = s.sweeping ? ui_theme_color(UI_C_ACCENT)
                                   : s.paused ? ui_theme_color(UI_C_WARN)
                                              : ui_theme_color(UI_C_PANEL_HI);
    ui_button(c, s.sweep_btn,
              s.paused ? TR(SV_PAUSED) : s.sweeping ? TR(SV_PAUSE) : "SWEEP", sweep_fill,
              false, s.sweeping || s.paused
                     || (value_live() && s.sweep_able && s.surfaces
                         && s.link_up));
    ui_button(c, s.release_btn, TR(SV_RELEASE), ui_theme_color(UI_C_PANEL_HI),
              false, true);
    draw_arm(c);
}

/* The trail servo_screen_sweep_started() reads where a frozen output was. */
static void remember_shown(void)
{
    s.trail[s.trail_head].t   = s.clock_ms;
    s.trail[s.trail_head].cmd = s.shown_cmd;
    s.trail_head = (uint8_t)((s.trail_head + 1u) % 64u);
    if (s.trail_n < 64u) {
        ++s.trail_n;
    }
}

static void tick_body(float dt_s)
{
    /* The programmer: its holds and repeats, and the left card while the
     * port is in programming.  PWM and programming are exclusive states of
     * that port, so the way back is asked for there and nowhere else. */
    kst_panel_set_hv(s.test_hv);
    kst_panel_bench_armed(bench_arming());
    kst_panel_tick(dt_s);
    /* Not while a limit run holds the port: its box stays on the left
     * card, and the run gives the port back itself. */
    lim_leave_follow();
    if (kst_panel_holds_port() && !kst_panel_is_open()
        && !servo_limrun_running(&s.lim) && !s.lim_leaving) {
        kst_panel_open();
    }
    if (kst_panel_holds_port() != s.kst_held) {
        s.kst_held = kst_panel_holds_port();
        ++s.arm_rev;
    }
    /* A PD mini the programmer's CONNECT switched on, through the
     * coprocessor rather than through SUPPLY, is this screen's. */
    if (supply_screen_port_held()) {
        supply_screen_claim(SUPPLY_BY_SERVO);
    }
    if (supply_screen_port_held() != s.sup_held) {
        s.sup_held = supply_screen_port_held();
        /* A hold on OUTPUT ON that was under way ends with nothing asked. */
        if (s.sup_held && s.out_down && !s.out_press_on) {
            ui_hold_reset(&s.out_hold);
            s.out_down = false;
        }
        ++s.sup_rev;
    }
    /* The supply's model switched on or off: the pill on the plot. */
    if (supply_screen_model() != s.model_shown) {
        s.model_shown = supply_screen_model();
        ++s.power_rev;
    }
    if (kst_panel_is_open() != s.kst_shown) {
        s.kst_shown = kst_panel_is_open();
        servo_invalidate();
        ++s.ctrl_rev;
    }
    /* The screen's own clock, for the sweep it draws. */
    if (dt_s > 0.0f) {
        s.clock_frac_ms += dt_s * 1000.0f;
        const uint32_t whole = (uint32_t)s.clock_frac_ms;
        s.clock_ms += whole;
        s.clock_frac_ms -= (float)whole;
    }
    if (s.sweeping) {
        /* A sweep whose curve a setting or the profile changed starts over,
         * as the coprocessor's does when it is written. */
        const sweep_cfg_t now = sweep_cfg_now();
        if (!same_sweep(&now, &s.sw.cfg)) {
            restart_sweep();
        }
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        if (s.sweeping && !s.awaiting
            && sweep_step(&s.sw, s.clock_ms, &cmd)) {
            s.commanded_deg = clamp_travel(sweep_deg(cmd));
            ++s.ctrl_rev;
        }
    }
    if (s.dr.on) {
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        if ((uint32_t)(s.clock_ms - s.dr.from_ms) > OUT_DEFAULT_TIMEOUT_MS
            || !sweep_step(&s.dr.sw, s.clock_ms, &cmd)) {
            /* Unheard for that long the far end has stopped too. */
            s.dr.on = false;
            s.commanded_deg = clamp_travel(s.shown_deg);
        } else {
            /*
             * In the mapping it was sent under, and slewed in its units at
             * its SPEED, as the far end renders it.  The curve's target
             * stays here: the held angle (commanded_deg) is what is drawn,
             * the reading or the estimate, which a change of profile while
             * paused says again (reissue()) -- never a point the curve was
             * still heading for.
             */
            const servo_map_t *m = &s.dr.map;
            if (!s.have_feedback) {
                const float per_ms = slew_per_ms(s.dr.speed_pct);
                s.dr.out = chase_cmd(s.dr.out, (float)cmd,
                                     (per_ms < 0.0f) ? -1.0f
                                                     : per_ms * dt_s * 1000.0f);
                const float us = map_cmd_to_us(m, s.dr.out);
                s.shown_deg = map_us_to_deg(m, us);
                s.shown_cmd = us_to_cmd(us);
            }
            s.commanded_deg = clamp_travel(s.shown_deg);
        }
        ++s.ctrl_rev;
    }
    if (s.arm_down && arm_locked() && ui_hold_leave(&s.arm)) {
        /* The port was found held under a hold of ARM: no arm is sent. */
        s.arm_down = false;
        ++s.arm_rev;
    }
    if (s.arm_down && !s.gate.armed) {
        ++s.arm_rev;
        if (ui_hold_tick(&s.arm, dt_s)) {
            post(SERVO_CMD_ARM, 0);
        }
    }
    if (s.arm.flash_left > 0) {
        ++s.arm_rev;   /* keep the frames coming while it flashes */
    }

    /* The supply as SUPPLY holds it: its output and its set points. */
    const bool on = supply_screen_output_on();
    if (on != s.out_on) {
        s.out_on = on;
        if (on) {
            ui_hold_reached(&s.out_hold);
        } else {
            /* The flash says the output came on; off, it is over, so both
             * buffers end on OUTPUT ON's own colour. */
            s.out_hold.flash_left = 0;
            if (ui_hold_left(&s.out_hold) && s.out_down) {
                s.out_down = false;
            }
        }
        if (s.ask.open && s.ask.hv) {
            ++s.ctrl_rev;   /* the warning says whether the output is on */
        }
        ++s.sup_rev;
    }
    /* The question is about a live output: once there is none -- off, and
     * no ON on its way, which a STOP or a lost touch can drop -- it goes,
     * unanswered, with the change it held, as on SUPPLY.  The HV warning
     * is about the servo, and stays. */
    /* A cap that came down while the question stands takes the waiting
     * value down with it, for good, as SUPPLY does with its own: one that
     * recovers before APPLY does not bring the old value back. */
    /* Not the run's warning: it names the run's top step, which the run
     * either reaches or refuses, never a value snapped under it. */
    if (s.ask.open && s.ask.purpose != ASK_TEST) {
        const supply_caps_t caps = supply_screen_caps();
        const float v = supply_snap(s.ask.v, caps.v_min, caps.v_max,
                                    caps.v_step);
        const float i = supply_snap(s.ask.i, caps.i_min, caps.i_max,
                                    caps.i_step);
        if (v != s.ask.v || i != s.ask.i) {
            s.ask.v = v;
            s.ask.i = i;
            ++s.ctrl_rev;               /* the question shows the value */
        }
    }
    if (s.ask.open && !s.ask.hv && !ask_stands()) {
        s.ask.open = false;
        if (s.ov_pressed == OP_ASK_APPLY || s.ov_pressed == OP_ASK_CANCEL) {
            ov_let_go();                /* its buttons have gone with it */
        }
        close_alone();
        servo_invalidate();
    }
    if (s.out_down && !s.out_press_on && !on) {
        ++s.sup_rev;
        if (ui_hold_tick(&s.out_hold, dt_s)) {
            /* The set point is looked at again as the hold completes: it
             * can have risen past a standard servo's rating since the press
             * -- a run's end putting it back, or SUPPLY -- and then the ON
             * goes through the HV warning instead. */
            if (hv_set_point()) {
                ui_hold_reset(&s.out_hold);
                s.out_down = false;
                if (!s.ov_have) {
                    ask_hv_on();
                }
            } else {
                supply_screen_ask_on(SUPPLY_BY_SERVO);
            }
        }
    }
    if (s.out_hold.flash_left > 0) {
        ++s.sup_rev;
    }
    if (supply_screen_set_v() != s.sup_v || supply_screen_set_i() != s.sup_i) {
        s.sup_v = supply_screen_set_v();
        s.sup_i = supply_screen_set_i();
        ++s.sup_rev;
    }

    /* The HV warning's hold: the voltage goes to the supply when it
     * completes. */
    if (s.ask.open && s.ask.down) {
        ++s.ask.rev;
        if (ui_hold_tick(&s.ask.hold, dt_s)) {
            ov_let_go();
            ask_apply();
        }
    }

    /* The warning's hold: the profile goes into force when it completes. */
    if (s.warn.open && s.warn.down) {
        ++s.warn.rev;
        if (ui_hold_tick(&s.warn.hold, dt_s)) {
            s.warn.open = false;
            s.warn.down = false;
            ov_let_go();
            apply_profile(s.warn.type, s.warn.hz);
        }
    }

    /* START TEST's hold: the run starts when it completes. */
    if (s.test_down) {
        ++s.test_rev;
        if (ui_hold_tick(&s.test_hold, dt_s)) {
            s.test_down = false;
            ov_let_go();
            test_begin();
        }
    }
    test_tick();

    /* START LIMIT RUN's hold, then the run. */
    if (s.lim_down) {
        ++s.test_rev;
        if (ui_hold_tick(&s.lim_hold, dt_s)) {
            s.lim_down = false;
            ov_let_go();
            lim_begin();
        }
    }
    lim_tick();

    if (s.driving) {
        s.pulse += dt_s * 3.6f;         /* a little under two seconds a cycle */
        if (s.pulse > 6.28318f) {
            s.pulse -= 6.28318f;
        }
    }

    /*
     * With nothing reporting, the arm still must not arrive before a servo
     * could: it chases the command at the configured speed instead of
     * snapping to it, which is the only thing standing in for travel time
     * when there is no hardware to measure.  With feedback this does nothing,
     * because the measurement has already placed the arm.
     */
    if (s.have_feedback) {
        s.shown_cmd = us_to_cmd(deg_to_us_f(s.shown_deg));
        remember_shown();
        return;
    }
    /*
     * In the units the far end slews in -- its command across the range a
     * command carries -- and at its rate, slew_of(); at 100% there is none
     * and the output is drawn at the command at once.  An angle a second
     * would not be: with CENTRE off the middle a degree is a different share
     * of the command on either side, and PAUSE, which keeps the servo where
     * the horn is drawn, would keep it somewhere else.
     */
    if (s.dr.on) {
        remember_shown();       /* drawn above, in its own mapping */
        return;
    }
    const float per_ms = slew_per_ms(s.speed_pct);
    const float step = (per_ms < 0.0f) ? -1.0f : per_ms * dt_s * 1000.0f;
    const float want = us_to_cmd(deg_to_us_f(s.commanded_deg));
    const float next = chase_cmd(s.shown_cmd, want, step);

    if (next == want) {
        s.shown_cmd = want;
        if (s.shown_deg != s.commanded_deg) {
            s.shown_deg = s.commanded_deg;
            ++s.ctrl_rev;
        }
        remember_shown();
        return;
    }
    s.shown_cmd = next;
    s.shown_deg = us_to_deg_f(cmd_to_us(s.shown_cmd));
    ++s.ctrl_rev;
    remember_shown();
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

static bool page_shown(void)
{
    return s.ov_open && !s.warn.open && !s.kp.open && !s.tk.open && !s.ch.open
           && !s.ask.open;
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_card(c, (gfx_rect_t){ PAD, PAD, LCARD_W, (int16_t)(H - 2 * PAD) },
                ui_theme_color(UI_C_PANEL));
        ui_card(c, (gfx_rect_t){ RCARD_X, PAD, RCARD_W,
                                 (int16_t)(H - 2 * PAD) },
                ui_theme_color(UI_C_PANEL));
        s.drawn_mask |= bit;
        s.drawn_ctrl[buf]  = UINT32_MAX;
        s.drawn_power[buf] = UINT32_MAX;
    }

    /* The keypad and the keyboard keep their own counts of what changed. */
    const uint32_t ov_rev = s.ctrl_rev + s.kp.revision + s.tk.revision;
    if (s.drawn_ctrl[buf] != ov_rev) {
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
        s.drawn_kst[buf]   = kst_panel_revision();
        s.drawn_kst_hold[buf] = kst_panel_hold_revision();
        if (s.ov_open) {
            draw_overlay(c);
        } else if (kst_panel_is_open()) {
            kst_panel_render(c);
        } else {
            draw_left(c);
        }
        draw_right(c, power);
        if (s.arm.flash_left > 0) {
            ui_hold_flash_step(&s.arm);
        }
        return;
    }

    /*
     * Otherwise only what moved on its own, each clipped to itself: ARM's
     * fade, the warning's hold, the power plot, the CURRENT row, the save
     * line and the grip.  The right card is 292 x 420 and the fades run for two seconds:
     * asking for whole cards every frame would spend most of the panel's
     * bandwidth on a button.
     */
    if (s.drawn_arm[buf] != s.arm_rev) {
        s.drawn_arm[buf] = s.arm_rev;
        clipped(c, s.arm_btn, draw_arm);
        if (s.arm.flash_left > 0) {
            ui_hold_flash_step(&s.arm);
        }
    }
    if (s.drawn_power[buf] != s.power_rev) {
        s.drawn_power[buf] = s.power_rev;
        clipped(c, power_rect(), draw_power);
    }
    if (s.drawn_cur[buf] != s.cur_rev) {
        s.drawn_cur[buf] = s.cur_rev;
        clipped(c, cur_val_rect(), draw_cur_value);
    }
    if (s.drawn_sup[buf] != s.sup_rev) {
        s.drawn_sup[buf] = s.sup_rev;
        clipped(c, sup_row_rect(), draw_sup_row);
    }
    if (s.ov_open && s.warn.open && s.drawn_warn[buf] != s.warn.rev) {
        s.drawn_warn[buf] = s.warn.rev;
        clipped(c, warn_apply_rect(), draw_warn_apply);
    }
    if (s.ov_open && s.ask.open && s.ask.hv
        && s.drawn_ask[buf] != s.ask.rev) {
        s.drawn_ask[buf] = s.ask.rev;
        clipped(c, warn_apply_rect(), draw_ask_apply);
    }
    if (page_shown() && s.tabs.selected == PG_TEST
        && s.drawn_test[buf] != s.test_rev) {
        s.drawn_test[buf] = s.test_rev;
        clipped(c, test_start_rect(), draw_test_start);
    }
    if (page_shown() && s.tabs.selected == PG_LIMITS
        && s.drawn_test[buf] != s.test_rev) {
        s.drawn_test[buf] = s.test_rev;
        clipped(c, test_start_rect(), draw_lim_start);
    }
    if (page_shown() && s.drawn_save[buf] != save_state()) {
        s.drawn_save[buf] = save_state();
        clipped(c, save_line_rect(), draw_save_line);
    }
    if (s.ov_open) {
        return;
    }
    if (kst_panel_is_open()) {
        /* The programmer, when it changed; of a hold only its button. */
        if (s.drawn_kst[buf] != kst_panel_revision()) {
            s.drawn_kst[buf] = kst_panel_revision();
            s.drawn_kst_hold[buf] = kst_panel_hold_revision();
            clipped(c, overlay_area(), kst_panel_render);
        } else if (s.drawn_kst_hold[buf] != kst_panel_hold_revision()) {
            s.drawn_kst_hold[buf] = kst_panel_hold_revision();
            clipped(c, kst_panel_apply_rect(), kst_panel_render);
        }
        return;
    }

    /*
     * The grip, only while it breathes.  Clipped to the tip: repainting the
     * 488x418 card at the frame rate to animate a ring of 36 px radius would
     * cost most of the panel's bandwidth.
     */
    const int grip_r = 36;
    const int step = (int)(s.pulse * 8.0f);
    if (!s.driving || s.drawn_pulse[buf] == step) {
        return;
    }
    s.drawn_pulse[buf] = step;
    int tx, ty;
    at(s.shown_deg, HORN_L, &tx, &ty);
    const gfx_rect_t box = { (int16_t)(tx - grip_r), (int16_t)(ty - grip_r),
                             (int16_t)(grip_r * 2), (int16_t)(grip_r * 2) };
    clipped(c, box, draw_left);
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
    s.dragging = false;
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
    s.ov_open = false;
    s.kp_alone = false;
    close_panels();
    ui_tabs_cancel(&s.tabs);
    s.ov_have = false;
    s.ov_pressed = OP_NONE;
    /* The programmer's presses and pages go; the port stays as it is, and
     * the screen shows it again while it holds the port. */
    kst_panel_leave();
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
    kst_panel_screen_entry();
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
    /* Disarming is a press, so its release is the whole command: one that
     * went missing is a DISARM the operator made and the bench never saw.
     * Arming has already sent its command by the time the finger lifts, so
     * cancelling one part way asks for nothing, which is correct. */
    if (s.gate.armed && s.arm_down && !s.arm.fired) {
        ask_disarm();
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
        hold_sweep();
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
     * on an armed bench moves the servo.
     */
    s.dragging = false;
    /* And the overlay's press: the warning's hold above all, which must not
     * complete on a contact that may have gone. */
    kst_panel_cancel();
    ui_keypad_cancel_press(&s.kp);
    ui_textkey_cancel_press(&s.tk);
    ui_tabs_cancel(&s.tabs);
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
    tick_body(dt_s);
    knob_note_owner();
}

static void event(const touch_event_t *evt)
{
    event_body(evt);
    knob_note_owner();
}

static const ui_screen_t k_screen = {
    .title  = "SERVO",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *servo_screen(void) { return &k_screen; }
