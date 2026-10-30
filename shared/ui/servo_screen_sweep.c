/**
 * @file servo_screen_sweep.c
 * @brief The SERVO screen's sweep core and frame step.
 *
 * The SERVO screen's sweep core: the curve the IOmcu runs, drawn on the
 * screen's own clock; SWEEP and PAUSE and their acknowledgements; the far
 * end's curve drawn on after the screen stopped drawing it as its own;
 * and the frame step, which runs the holds, follows the supply and moves
 * the drawn output towards the command at SPEED.  The state and the seam
 * are servo_screen_int.h and servo_screen_core_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen.h"

#include <math.h>

#include "kst_panel.h"
#include "outputs.h"
#include "servo_limrun.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/* ------------------------------------------------------------------- sweep */

/**
 * @brief The sweep the SWEEP page describes, under the profile in force: CURVE,
 *        SPEED and DWELL as they are, and RANGE as a share of the travel the
 *        servo may make -- TRAVEL, and the nearer of PULSE MIN and MAX, so a sweep
 *        about the centre reaches neither end it may not.  In the command units of
 *        the range a command carries (cmd_range()), centred on PULSE CENTRE.  Trim
 *        is not applied: the sweep turns about the centre a release rests at.
 * @return the curve
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

/** @brief A command of the sweep, as the angle it puts the horn at. */
static float sweep_deg(uint16_t cmd)
{
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    const float us = (float)lo + (float)(hi - lo) * (float)cmd
                                 / (float)(2u * SWEEP_CENTRE);
    return us_to_deg((uint16_t)(us + 0.5f));
}

/**
 * @brief The sweep ended, running or paused.  A pause is ended by the command
 *        that follows; the button reads SWEEP from now.
 */
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
 * to, by the link's HOLD.  The IOmcu holds it there, because only it
 * knows where that is: SPEED can leave the output well behind the curve, and
 * without feedback the horn drawn here is an estimate.  The drawing stops
 * where it was.  The phase is kept, here and at the far end, for
 * resume_sweep().
 */
void svs_hold_sweep(void)
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
    s.dr.map          = svs_map_now();
    s.dr.out          = s.shown_cmd;
    /* With feedback the reading itself: tick() turns it into a command
     * only on its next pass. */
    s.pause_from_cmd  = s.have_feedback
                        ? svs_us_to_cmd(svs_deg_to_us_f(s.shown_deg))
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

/** @brief The start or resume just posted, as it will be sent. */
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

/**
 * @brief Start the sweep, or carry on with a changed one from its beginning, as the
 *        IOmcu does.  Only while the value is live (value_live()) and on an
 *        IOmcu that sweeps: the far end refuses one otherwise.  Drawn once
 *        the start is acknowledged (servo_screen_sweep_started()).
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
 * IOmcu starts it when it is written.  Until that start is
 * acknowledged the far end runs the old curve, so the old curve is drawn on
 * (the drain, as for a PAUSE on its way) at the SPEED it ran at, for at
 * most OUT_DEFAULT_TIMEOUT_MS.
 */
void svs_restart_sweep(void)
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
bool svs_speed_limits_sweep(void)
{
    const sweep_cfg_t cfg = sweep_cfg_now();
    return sweep_slew_limited(&cfg, svs_slew_of(s.speed_pct));
}

bool svs_same_sweep(const sweep_cfg_t *a, const sweep_cfg_t *b)
{
    return a->kind == b->kind && a->mhz == b->mhz
           && a->amplitude == b->amplitude && a->dwell_ms == b->dwell_ms;
}

/**
 * @brief A tap on PAUSED: the paused sweep carried on from where it was paused.
 *        The command asks the panel for a resume; an IOmcu older than 4.6, or
 *        one that refuses, starts the curve over instead.  Either way the far end
 *        stays held until then, so the horn does too, and the acknowledgement
 *        (servo_screen_sweep_started()) draws it from there.  A curve changed on
 *        the TEST page since the pause is a new sweep.
 */
static void resume_sweep(void)
{
    const sweep_cfg_t now = sweep_cfg_now();
    if (!value_live() || !s.sweep_able || !svs_same_sweep(&now, &s.sw.cfg)
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
void svs_toggle_sweep(void)
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
        svs_hold_sweep();
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

bool servo_screen_output_held(void)
{
    return (s.out_down && !s.out_press_on) || s.ask.open;
}

bool servo_screen_asks_restore(void)
{
    return s.ask.open && s.ask.purpose == ASK_RESTORE;
}

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
        s.commanded_deg = svs_rest_deg();
        s.at_rest       = true;
    }
    ++s.ctrl_rev;
}

/**
 * @brief The drawn output's slew at @p speed_pct, command units a millisecond, as
 *        the far end's: SPEED_FULL_SPAN_S times the share, and at 100% none --
 *        the command goes straight through (svs_slew_of()), which is negative here.
 */
static float slew_per_ms(int speed_pct)
{
    if (speed_pct >= 100) {
        return -1.0f;
    }
    return (float)svs_slew_of(speed_pct) / 1000.0f;
}

/**
 * @brief One step of the drawn output towards @p want, at most @p step command
 *        units: the slew the far end applies, as the drawing models it.  A
 *        negative step is no slew: there at once.
 */
static float chase_cmd(float from, float want, float step)
{
    const float d = want - from;
    if (step < 0.0f || fabsf(d) <= step) {
        return want;
    }
    return from + ((d > 0.0f) ? step : -step);
}

/**
 * @brief Where the horn was drawn @p ago_ms ago, from the trail; the oldest kept
 *        for anything older.
 */
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
     * curve is a new start (svs_restart_sweep()) -- so the start's curve
     * stands, at the SPEED of the command acknowledged.  An earlier start's
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
    s.shown_deg = us_to_deg_f(svs_cmd_to_us(s.shown_cmd));
}

/**
 * @brief The drawn output after @p ms more of the sweep @p w from @p phase_ms in,
 *        starting at @p from: each step the curve's command, chased as tick()
 *        chases it at @p speed_pct -- for a pause, the SPEED in force at the tap,
 *        the far end's slew until the HOLD reached it.  Steps of 4ms, under a
 *        frame, so a turn of the curve is followed as the drawing follows it.
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
        const float want =
            svs_us_to_cmd(svs_deg_to_us_f(clamp_travel(sweep_deg(cmd))));
        shown = chase_cmd(shown, want, per_ms * (float)dt);
    }
    return shown;
}

void servo_screen_sweep_held(uint16_t pause_seq, uint32_t kept_ms)
{
    s.toggle_live = false;      /* no undo across a notification */
    /*
     * The far end ran its curve on until the HOLD reached it, a queue and an
     * exchange after the tap; at 5Hz that is a visible share of a cycle.
     * The panel times the phase it kept from the acknowledgements, as it
     * times a start, so the two errors are the same and cancel.
     *
     * Without feedback the horn is the drawing's estimate of the output, so
     * it moves on to where the output had got by then: the curve from the
     * tap's phase to the acknowledged one, slewed at SPEED as the drawing
     * slews.  Worked out here rather than by drawing the curve on until the
     * acknowledgement, which would need an end for a HOLD that is never
     * acknowledged; without one, the tap's estimate stands.  At most 5s is
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
            s.resume_from_cmd = svs_us_to_cmd(svs_deg_to_us_f(s.shown_deg));
        } else {
            const uint32_t late = kept_ms - s.pause_tap_ms;
            const uint32_t ms = ((int32_t)late <= 0) ? 0u
                                : (late > 5000u) ? 5000u : late;
            s.resume_from_cmd = drawn_after(&s.sw, s.pause_tap_ms, ms,
                                            s.pause_from_cmd,
                                            s.pause_speed_pct);
            s.shown_cmd = s.resume_from_cmd;
            s.shown_deg = us_to_deg_f(svs_cmd_to_us(s.shown_cmd));
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
        s.shown_cmd = svs_us_to_cmd(svs_deg_to_us_f(s.shown_deg));
    } else {
        const uint32_t ms = ((int32_t)extra <= 0) ? 0u
                            : (extra > 5000u) ? 5000u : extra;
        s.shown_cmd = drawn_after(&s.sw, s.sw.paused_ms, ms,
                                  s.pause_from_cmd, s.pause_speed_pct);
        s.shown_deg = us_to_deg_f(svs_cmd_to_us(s.shown_cmd));
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
        /* The panel would go on repeating a sweep the IOmcu no longer
         * takes: what it holds is ended, and the surfaces rest. */
        stop_sweep();
        release();
    }
    stop_sweep();
    ++s.ctrl_rev;
}

/**
 * @brief The trail servo_screen_sweep_started() reads where a frozen output was.
 */
static void remember_shown(void)
{
    s.trail[s.trail_head].t   = s.clock_ms;
    s.trail[s.trail_head].cmd = s.shown_cmd;
    s.trail_head = (uint8_t)((s.trail_head + 1u) % 64u);
    if (s.trail_n < 64u) {
        ++s.trail_n;
    }
}

void svs_tick_body(float dt_s)
{
    /* PROGRAM's sessions: their holds and repeats, and the left column
     * while a port is in programming.  PWM and programming are exclusive
     * states of that port, so the way back is asked for there and nowhere
     * else.  The KST session's voltage follows HV SERVO. */
    kst_panel_set_hv(s.test_hv);
    prog_armed(bench_arming());
    prog_tick(dt_s);
    /* Not while a limit run holds the port: its box stays on the left
     * card, and the run gives the port back itself. */
    lim_leave_follow();
    if (!servo_limrun_running(&s.lim) && !s.lim_leaving) {
        prog_follow_hold();
    }
    if (prog_holds() != s.kst_held) {
        s.kst_held = prog_holds();
        ++s.arm_rev;
    }
    prog_page_follow();
    /* A PD mini the programmer's CONNECT switched on, through the
     * IOmcu rather than through SUPPLY, is this screen's. */
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
    if (prog_shown() != s.kst_shown) {
        s.kst_shown = prog_shown();
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
    /* The drag's filtered target, before the horn is moved towards it. */
    svs_drag_tick(dt_s);
    if (s.sweeping) {
        /* A sweep whose curve a setting or the profile changed starts over,
         * as the IOmcu's does when it is written. */
        const sweep_cfg_t now = sweep_cfg_now();
        if (!svs_same_sweep(&now, &s.sw.cfg)) {
            svs_restart_sweep();
        }
        uint16_t cmd = (uint16_t)SWEEP_CENTRE;
        if (s.sweeping && !s.awaiting
            && sweep_step(&s.sw, s.clock_ms, &cmd)) {
            s.commanded_deg = clamp_travel(sweep_deg(cmd));
            ++s.dial_rev;
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
                const float us = svs_map_cmd_to_us(m, s.dr.out);
                s.shown_deg = svs_map_us_to_deg(m, us);
                s.shown_cmd = svs_us_to_cmd(us);
            }
            s.commanded_deg = clamp_travel(s.shown_deg);
        }
        ++s.dial_rev;
    }
    if (s.arm_down && svs_arm_locked() && ui_hold_leave(&s.arm)) {
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
    if (s.arm.flash_ms > 0) {
        ui_hold_flash_step(&s.arm, dt_s);
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
            s.out_hold.flash_ms = 0;
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
    /* A programming port that let go of a live output: the operator's set
     * points offered back onto it. */
    ask_restore_follow();
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
    if (s.out_hold.flash_ms > 0) {
        ui_hold_flash_step(&s.out_hold, dt_s);
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
        s.shown_cmd = svs_us_to_cmd(svs_deg_to_us_f(s.shown_deg));
        remember_shown();
        return;
    }
    /*
     * In the units the far end slews in -- its command across the range a
     * command carries -- and at its rate, svs_slew_of(); at 100% there is none
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
    const float want = svs_us_to_cmd(svs_deg_to_us_f(s.commanded_deg));
    const float next = chase_cmd(s.shown_cmd, want, step);

    if (next == want) {
        s.shown_cmd = want;
        if (s.shown_deg != s.commanded_deg) {
            s.shown_deg = s.commanded_deg;
            ++s.dial_rev;
        }
        remember_shown();
        return;
    }
    s.shown_cmd = next;
    s.shown_deg = us_to_deg_f(svs_cmd_to_us(s.shown_cmd));
    ++s.dial_rev;
    remember_shown();
}
