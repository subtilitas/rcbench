/**
 * @file
 * @brief The SERVO screen's automatic test: START TEST, on TEST's AUTOMATIC TEST
 * card, runs it (servo_test.h).  This file hands the run the steps, the
 * servo's profile and ends, the meter and the supply's caps; does what the
 * run asks -- the servo's position, SUPPLY's set points and output --
 * through the same paths a finger uses; puts SUPPLY's set points back after
 * it; and draws its box on LIVE's card and its button and lines on the
 * AUTOMATIC TEST card.  It also serves the card writer the outbox of both runs, the
 * automatic test's first, then the limit run's.
 *
 * The screen is servo_screen.c; the state and the layout both share are
 * servo_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "log_name.h"
#include "rcbench_version.h"
#include "sensors_rows.h"
#include "servo_limrun.h"
#include "settings.h"
#include "settings_sensors.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

static void test_apply(const servo_test_do_t *d);

/*
 * The run against the meter @p id decided at change count @p changes: a
 * run on the INA3221 ends when the meter changed under it
 * (servo_test_meter_now()), and what that asks -- the output off, the servo
 * let go -- is done before the window or the sample that brought the
 * answer is looked at.
 */
void test_meter_follow(servo_source_id_t id, uint32_t changes)
{
    if (!servo_test_running(&s.test)) {
        return;
    }
    const servo_test_meter_now_t m = {
        .ina3221    = id == SERVO_SOURCE_INA3221,
        .changes    = changes,
        .dropped    = s.have_drop ? test_abort_of(s.drop_why)
                                  : SERVO_TEST_AB_NONE,
        .dropped_at = s.drop_at,
    };
    servo_test_meter_now(&s.test, &m, test_now());
    test_tell();
    if (servo_test_running(&s.test)) {
        return;
    }
    const servo_test_in_t in = { value_live(), supply_screen_caps().v_max };
    servo_test_do_t d;
    servo_test_step(&s.test, test_now(), &in, &d);
    test_apply(&d);
    test_ended();
}

/* --------------------------------------------------------- the automatic test */

/* The steps the run's settings name: 7.4 and 8.4V only while HV SERVO is
 * on. */
static uint8_t test_steps(float *v)
{
    static const struct { setting_id_t id; float v; bool hv; } k[] = {
        { SET_SERVO_STEP_48, 4.8f, false },
        { SET_SERVO_STEP_60, 6.0f, false },
        { SET_SERVO_STEP_74, 7.4f, true  },
        { SET_SERVO_STEP_84, 8.4f, true  },
    };
    uint8_t n = 0u;
    for (size_t j = 0; j < sizeof(k) / sizeof(k[0]); ++j) {
        if (settings_get_bool(k[j].id) && (!k[j].hv || s.test_hv)) {
            if (v != NULL) {
                v[n] = k[j].v;
            }
            ++n;
        }
    }
    return n;
}

/* The highest voltage a run would ask for: its top step, or the brown-out's
 * start. */
float test_top_v(void)
{
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t n = test_steps(v);
    float top = settings_get_bool(SET_SERVO_BROWNOUT)
                    ? SERVO_TEST_BROWNOUT_START_V : 0.0f;
    for (uint8_t k = 0; k < n; ++k) {
        if (v[k] > top) {
            top = v[k];
        }
    }
    return top;
}

/* What the run is told: its settings and SWEEP's, the servo's profile, and
 * its ends -- the sweep's, RANGE of the travel either side of PULSE CENTRE. */
static void test_ends(uint16_t *end_lo, uint16_t *end_hi)
{
    const sweep_cfg_t sw = sweep_cfg_now();
    uint16_t lo, hi;
    cmd_range(&lo, &hi);
    const float span = (float)(hi - lo);
    const float k = 2.0f * (float)SWEEP_CENTRE;
    *end_lo = (uint16_t)lroundf(
        (float)lo + span * (float)(SWEEP_CENTRE - sw.amplitude) / k);
    *end_hi = (uint16_t)lroundf(
        (float)lo + span * (float)(SWEEP_CENTRE + sw.amplitude) / k);
}

/* Whether a run started now reads the INA3221: it is the rail's meter,
 * and the supply it measures is not the panel's model. */
static bool test_on_ina_now(void)
{
    return s.link_up && s.source == SERVO_SOURCE_INA3221
           && !supply_screen_model();
}

/* Why the INA3221 is not the meter, as a run's report states it. */
static servo_test_ina_t test_ina_why(void)
{
    switch (s.src_why) {
    case SERVO_SOURCE_WHY_OLD:       return SERVO_TEST_INA_OLD;
    case SERVO_SOURCE_WHY_NOT_HELD:  return SERVO_TEST_INA_NOT_HELD;
    case SERVO_SOURCE_WHY_SILENT:    return SERVO_TEST_INA_SILENT;
    case SERVO_SOURCE_WHY_NO_WINDOW: return SERVO_TEST_INA_NO_WINDOW;
    case SERVO_SOURCE_WHY_RESET:     return SERVO_TEST_INA_RESET;
    case SERVO_SOURCE_WHY_SETTLING:  return SERVO_TEST_INA_SETTLING;
    default:                         return SERVO_TEST_INA_NONE;
    }
}

static void test_cfg(servo_test_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->step_count = test_steps(c->steps_v);
    c->brownout   = settings_get_bool(SET_SERVO_BROWNOUT);
    c->i_limit    = supply_screen_set_i();
    c->centre_us = s.centre_us;
    test_ends(&c->end_lo_us, &c->end_hi_us);
    c->settle_ms     = (uint16_t)settings_get_int(SET_SERVO_SETTLE_MS);
    c->dwell_ms      = (uint16_t)settings_get_int(SET_SERVO_DWELL_MS);
    c->by_moves      = settings_get_int(SET_SERVO_LEN_BY) != 0;
    c->moves         = (uint16_t)settings_get_int(SET_SERVO_LEN_MOVES);
    c->time_s        = (uint16_t)settings_get_int(SET_SERVO_LEN_S);
    c->idle_max_a    = settings_get(SET_SERVO_IDLE_MAX);
    c->hold_max_a    = settings_get(SET_SERVO_HOLD_MAX);
    c->travel_max_ms = (uint16_t)settings_get_int(SET_SERVO_TRAVEL_MAX_MS);
    c->stall_a       = settings_get(SET_SERVO_STALL_A);
    c->report        = settings_get_bool(SET_SERVO_REPORT);
    snprintf(c->dut, sizeof(c->dut), "%s", settings_text(SET_TEXT_DUT_NAME));
    snprintf(c->type, sizeof(c->type), "%s", type()->name);
    if (in_force_dangerous()) {
        snprintf(c->danger, sizeof(c->danger), "%s %uHz", type()->name,
                 (unsigned)s.frame_hz);
    }
    c->hv         = s.test_hv;
    c->min_us     = s.min_us;
    c->max_us     = s.max_us;
    c->frame_hz   = s.frame_hz;
    c->trim_us    = s.trim_us;
    c->reverse    = s.reverse;
    c->travel_deg = (uint8_t)s.travel_deg;
    c->range_pct  = (uint8_t)settings_get_int(SET_SERVO_TEST_RANGE);
    c->model      = supply_screen_model();
    /* Only an encoder the IOmcu holds on reads angles: a set-up it
     * has not taken -- written only while disarmed -- gives a run none. */
    c->enc_on     = settings_get_bool(SET_ENC_EN) && s.enc_held;
    c->enc_centre = (uint16_t)settings_get_int(SET_ENC_CENTRE);
    c->enc_cmd_deg[0] = us_to_deg(c->end_lo_us);
    c->enc_cmd_deg[1] = us_to_deg(c->end_hi_us);
    /* The meter the run reads to its end: the servo rail's as the panel
     * last said it.  The INA3221 channel's windows while it is the meter of a
     * measured supply; else the PD mini's own readings, whose travel times
     * are an upper bound; and with the supply modelled, the model, which
     * stands in for the PD mini with no lag of its own. */
    if (test_on_ina_now()) {
        servo_test_meter_ina3221(&c->meter, settings_sensors_meter_dmohm());
        (void)sensors_rows_meter(c->meter.name, sizeof(c->meter.name));
        c->meter_changes = s.changes;
    } else if (c->model) {
        servo_test_meter_model(&c->meter);
    } else {
        servo_test_meter_pdmini(&c->meter);
    }
    /* And why not the INA3221, where SETUP > SENSORS has a part fitted. */
    if (c->meter.kind != (uint8_t)SERVO_TEST_METER_INA3221
        && settings_sensors_fitted() != 0u) {
        c->ina_why = c->model ? (uint8_t)SERVO_TEST_INA_MODEL
                              : (uint8_t)test_ina_why();
    }
    snprintf(c->firmware, sizeof(c->firmware), "%s", RCBENCH_VERSION_STRING);
    /* The report in the language showing at the start; the CSV in English
     * whatever it is. */
    c->text = ui_servo_table();
}

/* What the run asked for, done the way a finger does it here. */
static void test_apply(const servo_test_do_t *d)
{
    if (d->set) {
        supply_screen_put(d->set_v, d->set_i);
    }
    if (d->on) {
        supply_screen_ask_on(SUPPLY_BY_SERVO);
    }
    /* A step is a position like any other: only while the value is live.
     * The run itself ends on the edge out of it (value_edge()). */
    if (d->command && value_live()) {
        s.commanded_deg = us_to_deg(d->cmd_us);
        post(SERVO_CMD_POSITION, d->cmd_us);
        /* A step: the servo's own travel is what is timed, not SPEED's. */
        s.pending.slew_per_s = 0u;
        ++s.ctrl_rev;
    }
    if (d->off) {
        supply_screen_ask_off();
    }
    if (d->release) {
        /* The value goes to the rest only where the pin follows: a run a
         * disarm ended leaves it where the run last drove it. */
        release();
    }
}

/* The set points back to what they were before the run, in stages; see
 * test_restore.  Dropped if they were set since the run ended. */
void test_restore_service(void)
{
    if (!s.test_restore) {
        return;
    }
    if (supply_screen_set_v() != s.test_v1
        || supply_screen_set_i() != s.test_i1) {
        s.test_restore = false;         /* the operator's, now */
        return;
    }
    switch (s.restore_stage) {
    case RS_OFF_POSTED:
        if (!supply_screen_off_pending()) {
            s.restore_stage = RS_OFF_SENT;
            s.restore_ms    = test_now();
        }
        break;
    case RS_OFF_SENT:
        /* The frame after the one the OFF went in: a sample stamped from
         * here on was taken after it was sent. */
        if (test_now() != s.restore_ms) {
            s.restore_stage = RS_SAMPLE;
            s.restore_ms    = test_now();
        }
        break;
    case RS_OFF_SEEN:
        /* Not under OUTPUT ON's hold, here or on SUPPLY, nor with an ON
         * on its way: the set point an ON lands at is the one the operator
         * held for. */
        if (!s.out_down && !supply_screen_output_held()
            && !supply_screen_output_live()) {
            s.test_restore = false;
            supply_screen_put(s.test_v0, s.test_i0);
        }
        break;
    default:
        break;                          /* RS_SAMPLE: servo_screen_supply() */
    }
}

/* End a run now, and do what that asks -- the output off, the servo let
 * go -- before whatever ended it acts. */
void test_end_now(servo_test_abort_t why)
{
    lim_end_now(why);
    if (!servo_test_running(&s.test)) {
        return;
    }
    servo_test_abort(&s.test, why, test_now());
    test_tell();
    const servo_test_in_t in = { value_live(), supply_screen_caps().v_max };
    servo_test_do_t d;
    servo_test_step(&s.test, test_now(), &in, &d);
    test_apply(&d);
    test_ended();
}

/* Why a START cannot run now, before any hold: SERVO_STR_*, or 0. */
int test_blocked(void)
{
    if (!value_live()) {
        return SERVO_STR_START_NOT_ARMED;
    }
    if (!servo_test_drained(&s.test) || servo_limrun_running(&s.lim)
        || !servo_limrun_drained(&s.lim) || s.logger_busy) {
        return SERVO_STR_START_BUSY;
    }
    /* What the engine would refuse, said live: a refusal stays on the
     * line only while it holds, and goes with the edit that answers it. */
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t n = test_steps(v);
    if (n == 0u && !settings_get_bool(SET_SERVO_BROWNOUT)) {
        return SERVO_STR_START_NO_STEPS;
    }
    if (!s.have_sup || !s.sup.online) {
        return SERVO_STR_START_NO_SUPPLY;
    }
    uint16_t lo, hi;
    test_ends(&lo, &hi);
    if (!(lo < s.centre_us && s.centre_us < hi)) {
        return SERVO_STR_START_BAD_ENDS;
    }
    /* A step the supply's caps do not reach: refused here, before a
     * warning names a voltage the run would not use. */
    const supply_caps_t caps = supply_screen_caps();
    for (uint8_t k = 0; k < n; ++k) {
        if (v[k] > caps.v_max + 0.001f || v[k] < caps.v_min - 0.001f) {
            return SERVO_STR_START_ABOVE_CAP;
        }
    }
    return 0;
}

/* START TEST's hold, or the HV warning's, completed: the run starts.  A
 * panel open closes; TEST's card and LIVE's box show the run. */
void test_begin(void)
{
    /* The hold that started it is spent, whatever comes of it: a press
     * the overlay let go of never reaches ui_hold_end(). */
    ui_hold_reset(&s.test_hold);
    s.test_down = false;
    ++s.test_rev;
    s.test_note = 0;
    if (test_blocked() != 0) {
        ++s.ctrl_rev;                   /* the line under it says why */
        return;
    }
    servo_test_cfg_t c;
    test_cfg(&c);
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    if (s.have_sup) {
        last = test_reading_of(&s.sup);
    }
    const supply_caps_t caps = supply_screen_caps();
    const float v0 = supply_screen_set_v();
    const float i0 = supply_screen_set_i();
    const servo_test_start_t why = servo_test_start(
        &s.test, &c, test_now(), &last, caps.v_min, caps.v_max);
    test_tell();
    if (why != SERVO_TEST_START_OK) {
        s.test_note = (int)SERVO_STR_START_OK + (int)why;
        ++s.ctrl_rev;
        return;
    }
    /* "No magnet" arrives once, when it begins: a run that starts in it is
     * told so, and its report gives the reason for its missing angles. */
    if (s.enc_no_magnet) {
        const servo_test_enc_t none = { .valid = false, .no_magnet = true };
        servo_test_encoder(&s.test, &none);
    }
    stop_sweep();
    s.test_v0      = v0;
    s.test_i0      = i0;
    s.test_restore = false;
    s.test_seen    = true;
    s.test_on_ina  = c.meter.kind == (uint8_t)SERVO_TEST_METER_INA3221;
    s.test_file    = 0;
    s.test_report  = false;
    /* USER LOGS on SETUP > SD CARD, as it stands at the start: the run
     * writes its files or none, never half of them. */
    s.test_off     = !settings_get_bool(SET_LOG_USER);
    s.test_box     = true;
    s.lim_box      = false;
    close_panels();
    servo_invalidate();
    ++s.ctrl_rev;
}

/* The HV warning, for a run with a step past a standard servo's rating. */
void ask_hv_test(void)
{
    close_panels();
    s.ask.open      = true;
    s.ask.hv        = true;
    s.ask.purpose   = ASK_TEST;
    s.ask.target    = KT_NONE;
    s.ask.off_count = supply_screen_off_count();
    s.ask.stops     = s.stops;
    s.ask.v         = test_top_v();
    s.ask.i         = supply_screen_set_i();
    s.ask.down      = false;
    ui_hold_reset(&s.ask.hold);
    servo_invalidate();
}

bool servo_screen_testing(void) { return servo_test_running(&s.test); }

/*
 * The automatic test's outbox first, then the limit run's: one of the two
 * runs at a time, and each drained before the other starts.  A run started
 * with USER LOGS off hands the logger nothing: its lines are taken here and
 * dropped, so no OPEN reaches the logger, no number is taken and no card
 * alert can follow.
 */
servo_test_out_t servo_screen_test_peek(const char **text)
{
    for (;;) {
        servo_test_out_t k = servo_test_peek(&s.test, text);
        bool lim = false;
        if (k == SERVO_TEST_OUT_NONE) {
            k = servo_limrun_peek(&s.lim, text);
            lim = k != SERVO_TEST_OUT_NONE;
        }
        if (k != SERVO_TEST_OUT_NONE && (lim ? s.lim_off : s.test_off)) {
            if (lim) {
                servo_limrun_pop(&s.lim);
            } else {
                servo_test_pop(&s.test);
            }
            continue;
        }
        s.peek_lim  = lim;
        s.peek_kind = k;
        return k;
    }
}

void servo_screen_test_pop(void)
{
    if (s.peek_kind == SERVO_TEST_OUT_OPEN
        || s.peek_kind == SERVO_TEST_OUT_OPEN_TXT) {
        s.files_lim = s.peek_lim;
    }
    if (s.peek_lim) {
        servo_limrun_pop(&s.lim);
    } else {
        servo_test_pop(&s.test);
    }
    s.peek_kind = SERVO_TEST_OUT_NONE;
    s.peek_lim  = false;
}

void servo_screen_test_files(int number, bool report)
{
    /* The last OPEN's answer, which is not the answer for a run whose
     * OPEN never went. */
    if (s.files_lim ? s.lim_off : s.test_off) {
        return;
    }
    int *file = s.files_lim ? &s.lim_file : &s.test_file;
    bool *whole = s.files_lim ? &s.lim_report : &s.test_report;
    if (number != *file || report != *whole) {
        *file  = number;
        *whole = report;
        ++s.ctrl_rev;
    }
}

void servo_screen_test_logger_busy(bool busy)
{
    if (busy != s.logger_busy) {
        s.logger_busy = busy;
        ++s.ctrl_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

/* A file's name for the run's number: ATESTnnn.CSV. */
static void test_file_name(char *b, size_t n)
{
    if (s.test_off) {
        snprintf(b, n, "%s", TR(SV_USER_LOGS_OFF));
    } else if (s.test_file > 0) {
        log_name_build(b, n, LOG_PREFIX_ATEST, s.test_file, false);
    } else {
        snprintf(b, n, "%s", (s.test_file < 0) ? TR(SV_NOT_RECORDED)
                                                : TR(SV_WRITING));
    }
}

/**
 * @brief The run on LIVE's card: the step and phase while it runs, with
 *        STOP TEST; PASS, FAIL or ABORTED and why, and the files, after it,
 *        with CLOSE.
 * @param c the canvas
 */
void draw_test_box(gfx_canvas_t *c)
{
    const gfx_rect_t b = test_box_rect();
    const servo_test_t *t = &s.test;
    const bool running = servo_test_running(t);
    gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, 6,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, b.x, b.y, b.w, b.h, 6, ui_theme_color(UI_C_EDGE));
    char l1[64], l2[64], l3[64];
    gfx_color_t head = ui_theme_color(UI_C_ACCENT);
    if (running) {
        snprintf(l1, sizeof(l1), "%s", TR(SV_AUTO_TEST));
        const servo_test_step_t *st = &t->steps[t->step];
        snprintf(l2, sizeof(l2), TR(SV_STEP_OF),
                 ui_servo_str(st->brownout ? SERVO_STR_TEST_BROWNOUT
                                           : SERVO_STR_TEST_STEP),
                 servo_test_step_now(t), servo_test_steps_planned(t),
                 (double)st->set_v);
        const bool moving = t->phase == SERVO_TEST_PH_MOVE
                            || t->phase == SERVO_TEST_PH_HOLD;
        if (moving && t->counted && !st->brownout && t->cfg.by_moves) {
            snprintf(l3, sizeof(l3), TR(SV_PHASE_OF),
                     ui_servo_str(servo_test_phase_str(t->phase)),
                     (unsigned)t->moves_done + 1u, (unsigned)t->cfg.moves);
        } else if (moving && t->counted) {
            snprintf(l3, sizeof(l3), "%s %u",
                     ui_servo_str(servo_test_phase_str(t->phase)),
                     (unsigned)t->moves_done + 1u);
        } else {
            snprintf(l3, sizeof(l3), "%s",
                     ui_servo_str(servo_test_phase_str(t->phase)));
        }
    } else {
        const servo_test_verdict_t v = servo_test_verdict(t);
        head = (v == SERVO_TEST_PASS)   ? ui_theme_color(UI_C_OK)
               : (v == SERVO_TEST_FAIL) ? ui_theme_color(UI_C_DANGER)
                                        : ui_theme_color(UI_C_WARN);
        snprintf(l1, sizeof(l1), TR(SV_TEST_VERDICT),
                 ui_servo_str(servo_test_verdict_str(v)));
        uint32_t ms = 0u;
        float hold = 0.0f;
        if (v == SERVO_TEST_ABORTED) {
            snprintf(l2, sizeof(l2), "%s",
                     ui_servo_str(servo_test_abort_str(t->why)));
        } else if (servo_test_max_travel(t, &ms) && servo_test_max_hold(t, &hold)) {
            snprintf(l2, sizeof(l2), TR(SV_TRAVEL_HOLD),
                     (unsigned long)ms, (double)hold);
        } else {
            l2[0] = '\0';
        }
        char f[LOG_RUN_NAME_MAX + 4];
        test_file_name(f, sizeof(f));
        snprintf(l3, sizeof(l3), "%s%s", f,
                 (s.test_file > 0 && s.test_report) ? " + .TXT" : "");
    }
    gfx_text(c, b.x + 10, b.y + 10, l1, SV_FONT_LABEL, head, 1);
    /* The second line takes the box's third as well where it is longer
     * than the box is wide; the third moves down to the fourth. */
    const int n2 = wrap_text(c, b.x + 10, b.y + 10 + BOX_PITCH, BOX_PITCH,
                             l2, BOX_CELLS, ui_theme_color(UI_C_TEXT),
                             SV_FONT_LABEL);
    gfx_text(c, b.x + 10, b.y + 10 + BOX_PITCH * (1 + ((n2 > 1) ? n2 : 1)),
             l3, SV_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_button_font(c, test_btn_rect(),
                   running ? TR(SV_STOP_TEST) : TR(SUP_CLOSE),
                   running ? ui_theme_color(UI_C_DANGER)
                           : ui_theme_color(UI_C_PANEL_HI), false, true,
                   SV_FONT_LABEL);
    /* The meter the run reads or read, beside the button, flush right. */
    const char *meter =
        (t->cfg.meter.kind == (uint8_t)SERVO_TEST_METER_MODEL)
            ? TR(SV_METER_MODEL) : t->cfg.meter.name;
    /* An INA3221 channel as over the plot, "INA 0x41 CH2": the whole
     * name passes the button. */
    char shortname[sizeof(t->cfg.meter.name)];
    if (t->cfg.meter.kind == (uint8_t)SERVO_TEST_METER_INA3221
        && strncmp(meter, "INA3221 0x", 10u) == 0) {
        snprintf(shortname, sizeof(shortname), "INA %s", meter + 8);
        meter = shortname;
    }
    const gfx_rect_t btn = test_btn_rect();
    const int mw = gfx_text_width(SV_FONT_LABEL, meter, 1);
    gfx_text(c, b.x + b.w - 10 - mw, btn.y + 14, meter, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

/**
 * @brief START TEST: green, fading to the danger red across its hold, as
 *        ARM; STOP TEST in red while a run is under way.
 * @param c the canvas
 */
void draw_test_start(gfx_canvas_t *c)
{
    const bool running = servo_test_running(&s.test);
    gfx_color_t fill = ui_theme_color(UI_C_OK);
    if (running) {
        fill = ui_theme_color(UI_C_DANGER);
    } else if (s.test_hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER),
                            s.test_hold.held_s);
    }
    ui_button_font(c, test_start_rect(),
                   running ? TR(SV_STOP_TEST) : TR(SV_START_TEST), fill,
                   s.ov_have && s.ov_pressed == OP_TEST_START,
                   running || test_blocked() == 0, SV_FONT_LABEL);
}

/* The steps a run would take, as the card says them: "4.8 6.0V
 * BROWN-OUT", or NO STEP CHOSEN. */
static void test_plan(char *plan, size_t size)
{
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t n = test_steps(v);
    if (n == 0u && !settings_get_bool(SET_SERVO_BROWNOUT)) {
        snprintf(plan, size, "%s", ui_servo_str(SERVO_STR_START_NO_STEPS));
        return;
    }
    plan[0] = '\0';
    int k = 0;
    for (uint8_t j = 0; j < n && k >= 0 && (size_t)k < size; ++j) {
        k += snprintf(plan + k, size - (size_t)k, "%.1f ", (double)v[j]);
    }
    if (k >= 0 && (size_t)k < size) {
        snprintf(plan + k, size - (size_t)k, "%s%s",
                 (n > 0u) ? "V" : "",
                 settings_get_bool(SET_SERVO_BROWNOUT)
                     ? ((n > 0u) ? " BROWN-OUT" : "BROWN-OUT") : "");
    }
}

/**
 * @brief A STALL AT the run's readings cannot pass, in the warning colour:
 *        the current limit first, then the meter's range.  Nothing when it
 *        can.
 * @param c the canvas
 * @param x the line's left edge, px
 * @param y its top, px
 */
void draw_test_stall(gfx_canvas_t *c, int x, int y)
{
    servo_test_cfg_t tc;
    test_cfg(&tc);
    char note[96];
    if (servo_test_stall_note(&tc, note, sizeof(note))) {
        gfx_text(c, x, y, note, SV_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
    }
}

/**
 * @brief The AUTOMATIC TEST card's lines: over START TEST the steps and
 *        the length of each, and under it why a run cannot start or what it
 *        does now, the last result and its file, and a STALL AT no reading
 *        can pass.
 * @param c the canvas
 */
void draw_test_lines(gfx_canvas_t *c)
{
    const gfx_rect_t k = run_card_rect(0);
    const int x = k.x + 10;
    char plan[40];
    test_plan(plan, sizeof(plan));
    char line[96];
    if (settings_get_int(SET_SERVO_LEN_BY) != 0) {
        snprintf(line, sizeof(line), TR(SV_SUM_MOVES), plan,
                 (unsigned)settings_get_int(SET_SERVO_LEN_MOVES));
    } else {
        snprintf(line, sizeof(line), TR(SV_SUM_TIME), plan,
                 (unsigned)settings_get_int(SET_SERVO_LEN_S));
    }
    gfx_text(c, x, k.y + RUN_SUM_DY, line, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    const int y = k.y + RUN_LINE_DY;
    gfx_color_t col = ui_theme_color(UI_C_TEXT_FAINT);
    const int blocked = servo_test_running(&s.test) ? 0 : test_blocked();
    if (blocked != 0 || s.test_note != 0) {
        snprintf(line, sizeof(line), "%s",
                 ui_servo_str((servo_str_t)((blocked != 0) ? blocked
                                                           : s.test_note)));
        col = ui_theme_color(UI_C_WARN);
    } else if (servo_test_running(&s.test)) {
        snprintf(line, sizeof(line), TR(SV_RUNNING),
                 servo_test_step_now(&s.test),
                 servo_test_steps_planned(&s.test));
    } else {
        snprintf(line, sizeof(line), "%s",
                 test_needs_hv() ? TR(SV_TAP_HV) : TR(SV_HOLD_START));
    }
    gfx_text(c, x, y, line, SV_FONT_LABEL, col, 1);

    if (s.test.state == SERVO_TEST_DONE && !servo_test_running(&s.test)) {
        char f[LOG_RUN_NAME_MAX + 4];
        test_file_name(f, sizeof(f));
        char name[LOG_RUN_NAME_MAX + 12];
        snprintf(name, sizeof(name), "%s%s", f,
                 (s.test_file > 0 && s.test_report) ? " + .TXT" : "");
        snprintf(line, sizeof(line), TR(SV_CARD_LAST),
                 ui_servo_str(servo_test_verdict_str(
                     servo_test_verdict(&s.test))), name);
        gfx_text(c, x, y + RUN_LINE_P, line, SV_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    draw_test_stall(c, x, y + 2 * RUN_LINE_P);
}

/* What the box and the AUTOMATIC TEST card show of the run, as one number. */
uint32_t test_signature(void)
{
    const servo_test_t *t = &s.test;
    const uint32_t f[] = {
        (uint32_t)t->state, (uint32_t)t->phase, (uint32_t)t->step,
        (uint32_t)t->moves_done, (uint32_t)(s.test_file + 2),
        s.test_report ? 1u : 0u, s.test_off ? 1u : 0u,
        (uint32_t)test_blocked(),
        (uint32_t)s.test_note,
    };
    uint32_t h = 2166136261u;           /* FNV-1a over the fields */
    for (size_t k = 0; k < sizeof(f) / sizeof(f[0]); ++k) {
        h = (h ^ f[k]) * 16777619u;
    }
    return h;
}

/*
 * The run, every frame: its timers against the panel's clock and the cap in
 * force, and what it asks done.  Once it is over and the output reads off,
 * SUPPLY's set points go back to what they were before it.
 */
void test_tick(void)
{
    const servo_test_in_t in = { value_live(), supply_screen_caps().v_max };
    servo_test_do_t d;
    servo_test_step(&s.test, test_now(), &in, &d);
    test_tell();
    test_apply(&d);
    test_ended();
    test_restore_service();
    /* A refusal the engine gave goes once what it was about changes: the
     * bench armed or disarmed, a report taken, a cap moved, the supply
     * answering or not. */
    const int blocked = test_blocked();
    const bool online = s.have_sup && s.sup.online;
    if (blocked != s.test_blocked_seen || online != s.test_online_seen) {
        s.test_blocked_seen = blocked;
        s.test_online_seen  = online;
        s.test_note = 0;
        ++s.test_rev;
    }
    /* The box and the card say the step, the phase and the move. */
    const uint32_t sig = test_signature();
    if (sig != s.test_sig) {
        s.test_sig = sig;
        ++s.ctrl_rev;
    }
}
