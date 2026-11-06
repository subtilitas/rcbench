/*
 * AUTO on RUDDER through the SERVO screen and the programmer
 * (shared/ui/servo_rudder_auto_panel.c, shared/servo/servo_rudder_auto.c):
 * a run on a KST servo on the programming port that programs both end
 * points to the target, the pulse write and the backup before the first
 * write, one session a write, a STOP ending it with every output off, and
 * the refusals before a drive.  The bench is kst_panel_rig.h.
 *
 * The servo is servo_sim's behind the KST horn model (kst_sim.h
 * sim_horn_us(): LEFT ANGLE and RIGHT ANGLE the travel at 4us a count),
 * its stops at 700 and 2300us, outside the bench image's 150 counts.  The
 * AS5600 sits on the surface: 0.06° a microsecond of the horn, 600us of
 * travel 36°.  The targets are 20° with a 10% margin: 22° goals, which
 * the end points reach at about 92 counts.
 *
 * Cases:
 * - auto_programs_both_end_points_to_the_goal_and_ends_safe
 * - a_stop_during_auto_ends_it_with_every_output_off
 * - auto_without_a_pd_mini_or_a_zero_is_refused_and_says_why
 * - a_tap_on_stop_run_ends_auto_at_once
 * - zero_and_reset_max_are_refused_while_auto_runs
 * - a_supply_set_anew_outside_a_session_ends_auto
 * - with_user_logs_off_auto_hands_the_logger_nothing
 * - another_angle_source_drops_the_zero_and_ends_auto
 * - the_knob_moves_nothing_while_auto_runs
 * - a_stop_during_the_pulse_write_offers_restore_and_names_the_backup
 * - on_the_carrier_auto_off_the_port_names_the_port
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>

#include "kst_panel_rig.h"
#include "log_name.h"
#include "servo_rudder_auto.h"
#include "servo_rudder_auto_int.h"
#include "prog5v_box.h"
#include "prog_choice.h"
#include "settings_sensors.h"
#include "ui_board.h"

/** @brief The AS5600's centre count. */
#define CENTRE_COUNT 2048
/** @brief The surface's degrees per microsecond of the horn. */
#define DEG_PER_US 0.06f
/** @brief RUDDER's ZERO, AUTO and START (servo_spots.h). */
#define ZERO_X 98

static struct {
    servo_sim_t sv;
    uint16_t cmd;
    bool armed;
    unsigned offs;          /* the supply's OFF taken */
    uint32_t win_end, win_no;
    float sum;
    unsigned n;
    unsigned disarms, arms, dropped;
    unsigned opens_csv, opens_txt, ends;
    int prefix;
    char report[16384];
    size_t len;
    /* The encoder's still time. */
    int last_raw;
    uint32_t still_from;
    bool enc;               /* readings arrive */
} ra;

static void ra_drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t o = servo_screen_test_peek(&text);
        if (o == SERVO_TEST_OUT_NONE) {
            return;
        }
        ra.prefix = servo_screen_test_prefix();
        if (o == SERVO_TEST_OUT_OPEN) {
            ra.opens_csv++;
        } else if (o == SERVO_TEST_OUT_OPEN_TXT) {
            ra.opens_txt++;
        } else if (o == SERVO_TEST_OUT_END) {
            ra.ends++;
        } else if (o == SERVO_TEST_OUT_TXT) {
            const size_t n = strlen(text);
            if (ra.len + n + 2u < sizeof(ra.report)) {
                memcpy(ra.report + ra.len, text, n);
                ra.len += n;
                ra.report[ra.len++] = '\n';
                ra.report[ra.len] = '\0';
            }
        }
        servo_screen_test_pop();
        if (o == SERVO_TEST_OUT_OPEN) {
            servo_screen_test_files(7, true);   /* the card's answer */
        }
    }
}

/* What the panel does with the screen's commands. */
static void ra_commands(void)
{
    servo_cmd_t c;
    while (servo_screen_take(&c)) {
        if (c.kind == SERVO_CMD_POSITION) {
            ra.cmd = c.value_us;
        } else if (c.kind == SERVO_CMD_RELEASE) {
            ra.cmd = 1500u;
        } else if (c.kind == SERVO_CMD_DISARM) {
            /* The IOmcu drives no pulse while SAFE. */
            ra.disarms++;
            ra.armed = false;
            ra.cmd = 1500u;
        } else if (c.kind == SERVO_CMD_ARM) {
            if (kst_panel_holds_port()
                || kst_prog_state(M) != KST_PROG_PWM) {
                ra.dropped++;
            } else {
                ra.arms++;
                ra.armed = true;
            }
        }
    }
    g_rig.safe = !ra.armed;
    servo_screen_set_armed(ra.armed);
    supply_cmd_t sc;
    while (supply_screen_poll_cmd(&sc)) {
        if (sc.off) {
            ra.offs++;
            g_rig.sup_on = false;
        }
    }
}

/* The surface's angle as the AS5600 reads it, with its still time. */
static void ra_encoder(void)
{
    const float deg = (ra.sv.position_us - 1500.0f) * DEG_PER_US;
    const int raw = (CENTRE_COUNT + (int)lroundf(deg * 4096.0f / 360.0f))
                    & 4095;
    if (abs(raw - ra.last_raw) > 1) {
        ra.last_raw = raw;
        ra.still_from = g_rig.ms;
    }
    const uint32_t still = g_rig.ms - ra.still_from;
    const servo_test_enc_t e = {
        .valid = true, .raw = (uint16_t)raw,
        .still_ms = (uint16_t)(still > 60000u ? 60000u : still),
        .taken_ms = g_rig.ms,
    };
    servo_screen_encoder(&e);
}

static void ra_ms(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        rig_frame();
        const float amps = servo_sim_step(
            &ra.sv,
            sim_horn_cmd(g_rig.on_rail ? g_rig.sim.acting : g_rig.sim.reg,
                         ra.cmd, 4.0f, false),
            g_rig.ms);
        ra.sum += amps;
        ra.n++;
        if ((int32_t)(g_rig.ms - ra.win_end) >= 0) {
            const int ma = (int)(ra.sum / (float)ra.n * 1000.0f + 0.5f);
            const sense_link_win_t w = {
                .number = (uint16_t)++ra.win_no, .current = true,
                .voltage = true, .mean_ma = (int16_t)ma,
                .max_ma = (int16_t)ma, .min_ma = (int16_t)ma,
                .mean_mv = 6000u, .min_mv = 6000u,
                .taken_ms = g_rig.ms, .stamped = true,
            };
            ra.win_end += 50u;
            ra.sum = 0.0f;
            ra.n = 0u;
            servo_screen_window(&w, SERVO_SOURCE_INA3221, 2u);
        }
        if (ra.enc && g_rig.ms % 40u == 0u) {
            ra_encoder();
        }
        if (g_rig.ms % 20u == 0u) {
            servo_screen_clock(g_rig.ms);
            servo_screen_set_drive(1u + RIG_PORT, RIG_PIN,
                                   kst_prog_state(M) == KST_PROG_PWM
                                       && !kst_panel_holds_port());
            ra_commands();
            scr->tick(0.02f);
            servo_screen_service();
            ra_drain();
        }
    }
}

/* One reading of a PD mini that answers, its output on or off. */
static void ra_pd_reading(bool on)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = on;
    st.set_v = supply_screen_set_v();
    st.set_i = supply_screen_set_i();
    st.v = on ? st.set_v : 0.0f;
    st.samples = 1u;
    st.taken_ms = g_rig.ms;
    st.stamped = true;
    servo_screen_supply(&st);
}

/**
 * @brief The SERVO screen on the rig, armed, the INA3221 the rail's meter,
 *        the port in PWM with the servo on it, the AS5600 reading the
 *        surface; RUDDER with its zero taken and AUTO's box shown.
 * @param pd a PD mini answers with its output on behind the servo
 * @param zero ZERO tapped at the centre
 */
static void ra_setup(bool pd, bool zero)
{
    servo_sim_cfg_t sc;
    fresh_screen();
    memset(&ra, 0, sizeof(ra));
    ra.last_raw = -100;
    memcpy(g_rig.sim.reg, rig_servo().r, sizeof(g_rig.sim.reg));
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 700u;
    sc.stop_hi_us = 2300u;
    servo_sim_init(&ra.sv, &sc);
    ra.sv.position_us = 1500.0f;
    ra.cmd = 1500u;
    ra.armed = true;
    ra.enc = true;
    settings_set(SET_INA_FIT(0), 1.0f);
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, (float)CENTRE_COUNT);
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    if (pd) {
        g_rig.rail = true;
        g_rig.on_rail = true;
        g_rig.sup_on = true;
        g_rig.sup_mv = (uint16_t)lroundf(supply_screen_set_v() * 1000.0f);
        g_rig.sup_ma = (uint16_t)lroundf(supply_screen_set_i() * 1000.0f);
        g_rig.sim.needs_power_up = true;
    }
    link_up();
    ra.win_end = g_rig.ms + 50u;
    if (pd) {
        ra_pd_reading(true);
    }
    ra_ms(400u);
    screen_tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    if (zero) {
        screen_tap(ZERO_X, SV_RUD_ZERO_Y);
    }
    screen_tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    CHECK(rauto_shown());
    ra_ms(100u);
}

/* HOLD 2s TO START held for 2.3s. */
static void ra_hold_start(void)
{
    ev(SV_RUD_LEN_X, SV_RA_ROW_Y(2), TOUCH_EVENT_DOWN, 1);
    ra_ms(2300u);
    ev(SV_RUD_LEN_X, SV_RA_ROW_Y(2), TOUCH_EVENT_UP, 1);
    ra_ms(20u);
}

static void ra_run_out(void)
{
    for (int i = 0; i < 1200 && servo_screen_rauto_running(); ++i) {
        ra_ms(500u);
    }
    ra_ms(2000u);
}

/* The run to its end on the rig: the pulse range 1000 to 2000us written
 * in the read session, the backup saved once before it, both end points
 * lowered from 150 counts to the 22° goal's within the tolerance, each
 * write its own session from a power-up, at most 8 writes a side; PASS in
 * RUDDRnnn's report, the CSV handed out first; and the end as every end:
 * the servo let go, DISARM asked and the supply's output off. */
TEST_CASE(auto_programs_both_end_points_to_the_goal_and_ends_safe)
{
    int32_t d = 0;
    char b[96], want[96];
    ra_setup(true, true);
    CHECK(rauto_blocked() == NULL);
    /* Nothing in the way: the first line gives the goals, 20° x 1.10. */
    snprintf(want, sizeof(want), TR(RA_GOALS), 22.0, 22.0);
    CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), want);
    settings_set(SET_RUDDER_MARGIN, 0.0f);
    settings_set(SET_RUDDER_DEFL_LO, 12.5f);
    snprintf(want, sizeof(want), TR(RA_GOALS), 12.5, 20.0);
    CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), want);
    settings_set(SET_RUDDER_MARGIN, 10.0f);
    settings_set(SET_RUDDER_DEFL_LO, 20.0f);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    ra_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK(strstr(ra.report, "RCBENCH RUDDER AUTO REPORT\n") == ra.report);
    CHECK(strstr(ra.report, "PASS") != NULL);
    CHECK_EQ(ra.prefix, (int)LOG_PREFIX_RUDDR);
    CHECK_EQ(ra.opens_csv, 1u);
    CHECK_EQ(ra.ends, 1u);
    CHECK(kst_field_display(KST_F_PULSE_LOWER, servo(KST_F_PULSE_LOWER), &d));
    CHECK_EQ(d, 10000);
    CHECK(kst_field_display(KST_F_PULSE_UPPER, servo(KST_F_PULSE_UPPER), &d));
    CHECK_EQ(d, 20000);
    /* 22° is 366.7us of the horn, 91.7 counts; 0.5° either way 8.3us. */
    for (int k = 0; k < 2; ++k) {
        const unsigned v = servo(k == 0 ? KST_F_LEFT_ANGLE
                                        : KST_F_RIGHT_ANGLE);
        CHECK(v >= 90u && v <= 94u);
        const float horn = sim_horn_us(g_rig.sim.acting,
                                       k == 0 ? 1000u : 2000u, 4.0f, false);
        CHECK(fabsf(fabsf(horn - 1500.0f) * DEG_PER_US - 22.0f) <= 0.5f);
    }
    CHECK_EQ(g_rig.fc.saves, 1u);
    /* The read, two writes, and the LEAVE after each write: five
     * power-ups at least. */
    CHECK(g_rig.sim.power_ups >= 5u);
    CHECK_EQ(ra.dropped, 0u);
    CHECK(!ra.armed);
    CHECK(ra.disarms >= 1u);
    CHECK(!g_rig.sup_on);
    CHECK(ra.offs >= 1u);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "REPORT RUDDR007.TXT");
    snprintf(want, sizeof(want), TR(RA_SET), 150u,
             (unsigned)servo(KST_F_LEFT_ANGLE), 150u,
             (unsigned)servo(KST_F_RIGHT_ANGLE));
    CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), want);
    /* The programmer's run is over: a second START is free once ARMED. */
    CHECK(kst_panel_run_ready());
    ra.armed = true;
    servo_screen_set_armed(true);
    ra_ms(100u);
    CHECK(rauto_blocked() == NULL);
}

/* A STOP during AUTO (servo_screen_cancel_arm(), the panel's STOP stamp
 * path) in a measure and inside a write session: the run ends ABORTED,
 * the servo let go, the bench SAFE and the supply's output off; no ARM
 * follows, and the report names RESTORE after a write. */
TEST_CASE(a_stop_during_auto_ends_it_with_every_output_off)
{
    for (int where = 0; where < 2; ++where) {
        ra_setup(true, true);
        ra_hold_start();
        CHECK(servo_screen_rauto_running());
        bool there = false, settled = false;
        for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
            ra_ms(1u);
            const int ph = servo_screen_rauto_phase();
            settled = settled || ph == SERVO_RAUTO_PH_SETTLE;
            /* In a measure; or in the first end point write, after one. */
            if ((where == 0 && settled)
                || (where == 1 && settled && ph == SERVO_RAUTO_PH_WRITE)) {
                there = true;
                break;
            }
        }
        CHECK(there);
        rig_stop(true);
        servo_screen_cancel_arm();
        CHECK(!servo_screen_rauto_running());
        const unsigned arms = ra.arms;
        ra_ms(3000u);
            CHECK(!ra.armed);
        CHECK_EQ(ra.arms, arms);
        CHECK(!g_rig.sup_on);
        CHECK(ra.offs >= 1u);
    CHECK(ra.offs >= 1u);
        CHECK(strstr(ra.report, "ABORTED") != NULL);
        if (where == 1) {
            CHECK(strstr(ra.report, "RESTORE") != NULL);
        }
        rig_stop(false);
        CHECK(rig_settle());
    }
}

/* Refused before a drive, the reason on the box's first line: no PD mini
 * to power-cycle the servo; no zero taken; and no angle reading. */
TEST_CASE(auto_without_a_pd_mini_or_a_zero_is_refused_and_says_why)
{
    char b[96];
    ra_setup(true, false);
    CHECK_STR_EQ(rauto_blocked(), TR(RA_NO_ZERO));
    ra_hold_start();
    CHECK(!servo_screen_rauto_running());
    CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), TR(RA_NO_ZERO));
    CHECK(ra.cmd == 1500u);

    ra_setup(false, true);
    CHECK(rauto_blocked() != NULL);
    ra_hold_start();
    CHECK(!servo_screen_rauto_running());
    CHECK_EQ(g_rig.fc.saves, 0u);

    ra_setup(true, true);
    ra.enc = false;
    const servo_test_enc_t none = { .valid = false, .taken_ms = g_rig.ms };
    servo_screen_encoder(&none);
    ra_ms(100u);
    CHECK_STR_EQ(rauto_blocked(), TR(RA_NO_ANGLE));
}

/* STOP RUN in START's place ends the run on the DOWN: ABORTED, the outputs
 * off as after a STOP. */
TEST_CASE(a_tap_on_stop_run_ends_auto_at_once)
{
    ra_setup(true, true);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    ra_ms(200u);
    ev(SV_RUD_LEN_X, SV_RA_ROW_Y(2), TOUCH_EVENT_DOWN, 1);
    CHECK(!servo_screen_rauto_running());
    ev(SV_RUD_LEN_X, SV_RA_ROW_Y(2), TOUCH_EVENT_UP, 1);
    ra_ms(3000u);
    CHECK(!ra.armed);
    CHECK(!g_rig.sup_on);
    CHECK(ra.offs >= 1u);
    CHECK(strstr(ra.report, "ABORTED") != NULL);
}

/* ZERO and RESET MAX tapped while AUTO holds the 1000us side at about
 * -36°: refused, the run measures from the zero it started with and
 * programs both end points alike. */
TEST_CASE(zero_and_reset_max_are_refused_while_auto_runs)
{
    ra_setup(true, true);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    bool there = false;
    for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
        ra_ms(1u);
        if (servo_screen_rauto_phase() == SERVO_RAUTO_PH_SETTLE
            && ra.sv.position_us < 1100.0f) {
            there = true;
            break;
        }
    }
    CHECK(there);
    screen_tap(ZERO_X, SV_RUD_ZERO_Y);
    screen_tap(SV_RUD_AUTO_X - 170, SV_RUD_ZERO_Y);      /* RESET MAX */
    screen_tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);            /* MANUAL */
    CHECK(rauto_shown());
    CHECK(servo_screen_rauto_running());
    ra_run_out();
    CHECK(strstr(ra.report, "PASS") != NULL);
    const unsigned lo = servo(KST_F_LEFT_ANGLE);
    const unsigned hi = servo(KST_F_RIGHT_ANGLE);
    CHECK(lo >= 90u && lo <= 94u);
    CHECK(hi >= 90u && hi <= 94u);
    /* Idle again: MANUAL is taken. */
    ra_ms(100u);
    screen_tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    CHECK(!rauto_shown());
}

/* The PD mini's voltage set point moved in a measure, outside the
 * programmer's sessions: ABORTED, `supply or rail voltage changed`, every
 * output off; the box's second line names it. */
TEST_CASE(a_supply_set_anew_outside_a_session_ends_auto)
{
    ra_setup(true, true);
    ra_hold_start();
    bool there = false;
    for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
        ra_ms(1u);
        if (servo_screen_rauto_phase() == SERVO_RAUTO_PH_SETTLE) {
            there = true;
            break;
        }
    }
    CHECK(there);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = true;
    st.set_v = supply_screen_set_v() + 0.1f;
    st.set_i = supply_screen_set_i();
    st.v = st.set_v;
    st.samples = 1u;
    st.taken_ms = g_rig.ms;
    st.stamped = true;
    servo_screen_supply(&st);
    CHECK(!servo_screen_rauto_running());
    ra_ms(3000u);
    CHECK(!ra.armed);
    CHECK(!g_rig.sup_on);
    CHECK(strstr(ra.report, "ABORTED - supply or rail voltage changed")
          != NULL);
    char b[96], want[96];
    snprintf(want, sizeof(want), TR(RA_LAST),
             ui_servo_str(SERVO_STR_ABORTED),
             ui_servo_str(SERVO_STR_L_WHY_SUPPLY));
    CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), want);
}

/* USER LOGS off on SETUP > SD CARD: the run hands out no OPEN and no line,
 * and the box's last line reads USER LOGS OFF. */
TEST_CASE(with_user_logs_off_auto_hands_the_logger_nothing)
{
    ra_setup(true, true);
    settings_set(SET_LOG_USER, 0.0f);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    ra_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK_EQ(ra.opens_csv, 0u);
    CHECK_EQ(ra.ends, 0u);
    CHECK_EQ(ra.len, 0u);
    char b[96];
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), TR(SV_USER_LOGS_OFF));
}

/* ANGLE SOURCE switched to the IMU in a measure: the zero AUTO measures
 * from is dropped, and the run ends ABORTED at once with every output
 * off, before any write lands on the new source's angle; a START then
 * asks for ZERO.  Switched before a START, the same refusal. */
TEST_CASE(another_angle_source_drops_the_zero_and_ends_auto)
{
    ra_setup(true, true);
    ra_hold_start();
    bool there = false;
    for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
        ra_ms(1u);
        if (servo_screen_rauto_phase() == SERVO_RAUTO_PH_SETTLE) {
            there = true;
            break;
        }
    }
    CHECK(there);
    const unsigned left = servo(KST_F_LEFT_ANGLE);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    ra_ms(20u);
    CHECK(!servo_screen_rauto_running());
    ra_ms(3000u);
    CHECK(!ra.armed);
    CHECK(!g_rig.sup_on);
    CHECK_EQ(servo(KST_F_LEFT_ANGLE), left);
    CHECK(strstr(ra.report, "ABORTED") != NULL);
    CHECK(strstr(ra.report, "PASS") == NULL);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_AS5600);
    ra.armed = true;
    servo_screen_set_armed(true);
    ra_ms(100u);
    CHECK_STR_EQ(rauto_blocked(), TR(RA_NO_ZERO));

    /* Before a START: the zero taken on the AS5600 does not carry over. */
    ra_setup(true, true);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    ra_ms(100u);
    CHECK(rauto_blocked() != NULL);
    ra_hold_start();
    CHECK(!servo_screen_rauto_running());
}

/* The rotary knob turned both ways while AUTO holds an end pulse, on
 * RUDDER with the box shown: no POSITION but AUTO's own, the end pulse
 * stays, and the run passes. */
TEST_CASE(the_knob_moves_nothing_while_auto_runs)
{
    ra_setup(true, true);
    ra_hold_start();
    bool there = false;
    for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
        ra_ms(1u);
        if (servo_screen_rauto_phase() == SERVO_RAUTO_PH_SETTLE
            && (ra.cmd == 1000u || ra.cmd == 2000u)) {
            there = true;
            break;
        }
    }
    CHECK(there);
    CHECK(rauto_shown());
    const uint16_t end = ra.cmd;
    for (int k = 0; k < 10; ++k) {
        servo_screen_knob(k % 2 == 0 ? 0.05f : -0.05f);
        servo_cmd_t c;
        while (servo_screen_take(&c)) {
            CHECK(c.kind != SERVO_CMD_POSITION || c.value_us == end);
        }
        ra_ms(20u);
        CHECK_EQ(ra.cmd, end);
    }
    ra_run_out();
    CHECK(strstr(ra.report, "PASS") != NULL);
}

/* A STOP while the read session's pulse write is on its way, before its
 * read-back: the write may have landed, so the report names the backup
 * file and RESTORE, and the box's last line asks for RESTORE. */
TEST_CASE(a_stop_during_the_pulse_write_offers_restore_and_names_the_backup)
{
    ra_setup(true, true);
    const uint16_t lower0 = servo(KST_F_PULSE_LOWER);
    ra_hold_start();
    bool there = false;
    for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
        ra_ms(1u);
        /* The servo's PULSE LOWER changed, the run still waits for the
         * write's answer. */
        if (servo_screen_rauto_phase() == SERVO_RAUTO_PH_WRITE
            && servo(KST_F_PULSE_LOWER) != lower0) {
            there = true;
            break;
        }
    }
    CHECK(there);
    rig_stop(true);
    servo_screen_cancel_arm();
    CHECK(!servo_screen_rauto_running());
    ra_ms(3000u);
    CHECK(!ra.armed);
    CHECK(!g_rig.sup_on);
    CHECK(strstr(ra.report, "ABORTED") != NULL);
    CHECK(strstr(ra.report, "Backup:         KST") != NULL);
    CHECK(strstr(ra.report, "Restore:        RESTORE") != NULL);
    char b[96], want[96];
    snprintf(want, sizeof(want), TR(SV_LIM_RESTORE), TR(KP_PROGRAM));
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), want);
    rig_stop(false);
    CHECK(rig_settle());
}

/* On the rbmod carrier: the servo on PROGP01 reads CH3, PROG 5V, and AUTO
 * may start.  TARGET on the bench's outputs with no output bound as SERVO
 * PWM leaves no channel watching the target; the line names the
 * programming port AUTO needs, not an INA3221 that is not set up. */
TEST_CASE(on_the_carrier_auto_off_the_port_names_the_port)
{
    ra_setup(false, true);
    g_rig.fixed = true;
    ui_board_set(OUTBIND_BOARD_RBMOD);
    (void)ui_board_apply_pins(NULL);
    prog_choice_set_far_minor(LINK_MINOR_CARRIER_PWM);
    prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
    settings_set(SET_SERVO_TARGET, (float)(1u + RIG_PORT));
    ra_ms(200u);
    CHECK_EQ(settings_sensors_fast(), 2u);
    CHECK(rauto_blocked() == NULL);
    /* The bench's outputs, none bound: what the panel does with the
     * binding read (panel_frame.c). */
    settings_set(SET_SERVO_TARGET, 0.0f);
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, true);
    servo_screen_set_pins(0u, true);
    (void)settings_sensors_set_tester_rail(ui_board_tester_rail(0u));
    CHECK_EQ(settings_sensors_fast(), LINK_SM_FAST_NONE);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_STR_EQ(rauto_blocked(), TR(RA_PROG_PORT));
    }
    ui_text_set_language(UI_LANG_EN);
    g_rig.fixed = false;
    prog5v_box_reset();
    prog_choice_reset();
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    (void)ui_board_apply_pins(NULL);
}

int main(void)
{
    RUN(auto_programs_both_end_points_to_the_goal_and_ends_safe);
    RUN(a_stop_during_auto_ends_it_with_every_output_off);
    RUN(auto_without_a_pd_mini_or_a_zero_is_refused_and_says_why);
    RUN(a_tap_on_stop_run_ends_auto_at_once);
    RUN(zero_and_reset_max_are_refused_while_auto_runs);
    RUN(a_supply_set_anew_outside_a_session_ends_auto);
    RUN(with_user_logs_off_auto_hands_the_logger_nothing);
    RUN(another_angle_source_drops_the_zero_and_ends_auto);
    RUN(the_knob_moves_nothing_while_auto_runs);
    RUN(a_stop_during_the_pulse_write_offers_restore_and_names_the_backup);
    RUN(on_the_carrier_auto_off_the_port_names_the_port);
    return test_summary("servo_rauto_kst");
}
