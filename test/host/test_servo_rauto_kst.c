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
 * travel 36°.  The servo's NEUTRAL reads 1500.2us.  The targets are 20°
 * with a 10% margin: 20° goals at 1000us and 2000us, the servo's pulses
 * 950.4 to 2049.6us, which the end points reach at about 92 counts; 950us
 * and 2050us then give 22°.
 *
 * Cases:
 * - auto_programs_both_end_points_to_the_goal_and_ends_safe
 * - auto_on_the_filtered_imu_waits_for_its_settle_and_passes
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
 * - on_the_carrier_the_lines_lead_to_a_start_from_safe
 * - on_the_carrier_the_lines_lead_to_a_start_from_armed
 * - a_session_holding_the_port_is_named_before_port_pwm
 * - auto_start_flashes_as_it_fires
 * - auto_enters_at_5v_whatever_the_kst_page_loaded
 * - writes_that_do_not_move_the_2000us_side_end_auto_after_two
 * - imu_yaw_alone_refuses_auto_in_either_algorithm
 * - servo_pulses_keep_or_1000_2000_leave_the_margins_pulses
 * - an_off_center_neutral_takes_the_margin_from_neutral
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "ui_setlist.h"
#include "servo_rudder_int.h"
#include "ui_widgets.h"

/* The run to its end on the rig: the margin's pulse range, 950.4 to
 * 2049.6us at 10%, written in the read session, the backup saved once
 * before it, both end points lowered from 150 counts to the 20° goal's
 * within the tolerance at 1000us and 2000us, 22° at 950us and 2050us, the
 * bench's PULSE MIN and PULSE MAX widened to 950-2050us, each
 * write its own session from a power-up, at most 8 writes a side; PASS in
 * RUDDRnnn's report, the CSV handed out first; and the end as every end:
 * the servo let go, DISARM asked and the supply's output off. */
TEST_CASE(auto_programs_both_end_points_to_the_goal_and_ends_safe)
{
    int32_t d = 0;
    char b[96], want[96];
    ra_setup(true, true);
    CHECK(rauto_blocked() == NULL);
    /* Nothing in the way: the first line gives the pulse range and its
     * ends, 20° x 1.10. */
    snprintf(want, sizeof(want), TR(RA_GOALS), 950u, 2050u, 22.0, 22.0);
    CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), want);
    CHECK_STR_EQ(b, "PULSES 950-2050us: 22.0°, 22.0°");
    settings_set(SET_RUDDER_MARGIN, 0.0f);
    settings_set(SET_RUDDER_DEFL_LO, 12.5f);
    snprintf(want, sizeof(want), TR(RA_GOALS), 1000u, 2000u, 12.5, 20.0);
    CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), want);
    uint16_t lo = 0u, hi = 0u;
    servo_screen_pulse_ends(&lo, &hi);
    CHECK_EQ(lo, 1000u);
    CHECK_EQ(hi, 2000u);
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
    CHECK_EQ(d, 9504);
    CHECK(kst_field_display(KST_F_PULSE_UPPER, servo(KST_F_PULSE_UPPER), &d));
    CHECK_EQ(d, 20496);
    /* 20° is 333.3us of the horn, at 1000us 91% of the way to PULSE LOWER:
     * 91.6 counts; 0.5° either way 8.3us.  950us and 2050us, past the
     * pulses driven, give 20° x 549.6 / 500, 22.0°. */
    for (int k = 0; k < 2; ++k) {
        const unsigned v = servo(k == 0 ? KST_F_LEFT_ANGLE
                                        : KST_F_RIGHT_ANGLE);
        CHECK(v >= 90u && v <= 94u);
        const float horn = sim_horn_us(g_rig.sim.acting,
                                       k == 0 ? 1000u : 2000u, 4.0f, false);
        CHECK(fabsf(fabsf(horn - 1500.0f) * DEG_PER_US - 20.0f) <= 0.5f);
        const float past = sim_horn_us(g_rig.sim.acting,
                                       k == 0 ? 950u : 2050u, 4.0f, false);
        CHECK(fabsf(fabsf(past - 1500.0f) * DEG_PER_US - 22.0f) <= 0.6f);
    }
    /* The search drove the margin's range, 950us to 2050us, never past
     * it; the rig's stops at 700us and 2300us leave both sides free. */
    CHECK_EQ(ra.cmd_lo, 950u);
    CHECK_EQ(ra.cmd_hi, 2050u);
    CHECK(strstr(ra.report, "Low end:        free to 950us") != NULL);
    CHECK(strstr(ra.report, "High end:       free to 2050us") != NULL);
    /* The bench's range widened to the margin's, named on the report. */
    servo_screen_pulse_ends(&lo, &hi);
    CHECK_EQ(lo, 950u);
    CHECK_EQ(hi, 2050u);
    CHECK(strstr(ra.report, "Bench pulses:   PULSE MIN/MAX 1000-2000 -> "
                            "950-2050us, for the session") != NULL);
    CHECK(strstr(ra.report, "Margin:         10% from NEUTRAL 1500.2us: "
                            "servo pulses 950.4-2049.6us; by the linear "
                            "model 950us reaches 22.0°, 2050us 22.0°; driven "
                            "in the search only") != NULL);
    CHECK_EQ(g_rig.fc.saves, 1u);
    /* The supply's OFF asked at the end decision, before the report's
     * first file opens. */
    CHECK(ra.open_seen);
    CHECK(ra.off_at_open);
    /* The read, two writes, and the LEAVE after each write: five
     * power-ups at least. */
    CHECK(g_rig.sim.power_ups >= 5u);
    CHECK_EQ(ra.dropped, 0u);
    CHECK(!ra.armed);
    CHECK(ra.disarms >= 1u);
    CHECK(!g_rig.sup_on);
    CHECK(ra.offs >= 1u);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)),
                 "REPORT RUDDR007.TXT, MIN/MAX 950-2050us");
    /* One row of the box's 400px in the label face, 10 x 20, in English
     * and German: 39 and 40 cells. */
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        char l3[96];
        (void)rauto_line(2, l3, sizeof(l3));
        CHECK(strlen(l3) <= 40u);
        CHECK_EQ(ui_text_wrap(NULL, gfx_rect_make(0, 0, RUD_SIDE_W, 20), 20,
                              l3, &gfx_font_10x20, 0), 1);
    }
    ui_text_set_language(UI_LANG_EN);
    /* The verify pass on the fifth line: each end from its goal, the
     * center from the zero. */
    CHECK(strncmp(rauto_line(4, b, sizeof(b)), "VERIFY ", 7) == 0);
    CHECK(strstr(b, ", CENTER ") != NULL);
    CHECK(strstr(b, "-0.00") == NULL);
    CHECK(strstr(ra.report, "VERIFY (pulse, measured, goal") != NULL);
    /* KST's raw values, as the KST page shows them, named raw. */
    snprintf(want, sizeof(want), TR(RA_SET), 150u,
             (unsigned)servo(KST_F_LEFT_ANGLE), 150u,
             (unsigned)servo(KST_F_RIGHT_ANGLE),
             ui_servo_str(SERVO_STR_RA_R_RAW));
    CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), want);
    /* The programmer's run is over: a second START is free once ARMED. */
    CHECK(kst_panel_run_ready());
    ra.armed = true;
    servo_screen_set_armed(true);
    ra_ms(100u);
    CHECK(rauto_blocked() == NULL);
}

/* The same run on the IMU at 10Hz through IMU FILTER 10: AUTO waits for
 * the filtered angle to settle, 3000ms plus the 78 frames at 150ms of the
 * allowance at the rate measured, plus SERVO_RAUTO_CREEP_MS and IMU
 * SETTLING TIME's default 3.0s, passes, and lands both end points on the
 * 20° goal within the tolerance; the settle the IMU gives does not reach
 * a run on the AS5600. */
TEST_CASE(auto_on_the_filtered_imu_waits_for_its_settle_and_passes)
{
    ra_setup_on(true, true, true);
    CHECK(rauto_blocked() == NULL);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    ra_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK(strstr(ra.report, "PASS") != NULL);
    CHECK(strstr(ra.report, "IMU X+") != NULL);
    char want[96];
    snprintf(want, sizeof(want), "within %ums of the command",
             (unsigned)(SERVO_RAUTO_MOVE_MS + 78u * 150u
                        + SERVO_RAUTO_CREEP_MS
                        + SERVO_SETTLE_WIN_DEFAULT_MS));
    CHECK(strstr(ra.report, want) != NULL);
    CHECK(strstr(ra.report, "its span at most 0.10° over 3.0s") != NULL);
    for (int k = 0; k < 2; ++k) {
        const float horn = sim_horn_us(g_rig.sim.acting,
                                       k == 0 ? 1000u : 2000u, 4.0f, false);
        CHECK(fabsf(fabsf(horn - 1500.0f) * DEG_PER_US - 20.0f) <= 0.5f);
    }
    /* On the AS5600 the IMU's settle is no part of the wait. */
    ra_setup(true, true);
    servo_screen_set_imu_settle(7000u);
    ra_hold_start();
    ra_run_out();
    CHECK(strstr(ra.report, "within 3000ms of the command") != NULL);
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
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    CHECK(!servo_screen_rauto_running());
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    ra_ms(3000u);
    CHECK(!ra.armed);
    CHECK(!g_rig.sup_on);
    CHECK(ra.offs >= 1u);
    CHECK(strstr(ra.report, "ABORTED") != NULL);
}

/* ZERO and RESET MAX tapped while AUTO's search holds the 1000us side at
 * about -36°: refused, the run measures from the zero it started with and
 * programs both end points alike. */
TEST_CASE(zero_and_reset_max_are_refused_while_auto_runs)
{
    ra_setup(true, true);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    bool there = false;
    for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
        ra_ms(1u);
        if (servo_screen_rauto_phase() == SERVO_RAUTO_PH_SEARCH
            && ra.sv.position_us < 1001.0f) {
            there = true;
            break;
        }
    }
    CHECK(there);
    screen_tap(ZERO_X, SV_RUD_ZERO_Y);
    screen_tap(RESET_X, SV_RUD_ZERO_Y);                  /* RESET MAX */
    CHECK(rauto_shown());
    CHECK(servo_screen_rauto_running());
    ra_run_out();
    CHECK(strstr(ra.report, "PASS") != NULL);
    const unsigned lo = servo(KST_F_LEFT_ANGLE);
    const unsigned hi = servo(KST_F_RIGHT_ANGLE);
    CHECK(lo >= 90u && lo <= 94u);
    CHECK(hi >= 90u && hi <= 94u);
    /* Idle again: RUDDER shows its drive buttons. */
    ra_ms(100u);
    go(SERVO_FN_RUDDER);
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
    char b[96], want[96];
    snprintf(want, sizeof(want), "%s, MIN/MAX 950-2050us",
             TR(SV_USER_LOGS_OFF));
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), want);
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
    snprintf(want, sizeof(want), TR(SV_LIM_RESTORE), TR(SVH_KST));
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

/**
 * @brief Does what AUTO's first line asks, up to 6 times, until START is
 *        free: ARM, the PROG 5V box ON, a DISARM; what the IOmcu reports
 *        after each.
 * @param seen receives the lines in their order, '|' between two
 * @param n its size
 * @return false on a line that asks for none of those
 */
static bool follow_the_lines(char *seen, size_t n)
{
    seen[0] = '\0';
    for (int k = 0; k < 6 && rauto_blocked() != NULL; ++k) {
        const char *why = rauto_blocked();
        const size_t at = strlen(seen);
        snprintf(seen + at, n - at, "%s%s", at > 0u ? "|" : "", why);
        if (why == ui_servo_str(SERVO_STR_START_NOT_ARMED)) {
            ra.armed = true;
        } else if (why == ui_servo_str(SERVO_STR_START_PROG_5V)) {
            prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
        } else if (why == TR(SV_LIM_PWM_SAFE)) {
            ra.armed = false;
        } else {
            return false;
        }
        servo_screen_set_armed(ra.armed);
        ra_ms(200u);
    }
    return rauto_blocked() == NULL;
}

/* On the rbmod carrier the port's pulse needs the PROG 5V and comes on
 * only while SAFE.  From SAFE with the box off the first line asks for the
 * PROG 5V before ARM; the pulse comes on in SAFE, ARM follows, and a hold
 * on START at its center starts AUTO. */
TEST_CASE(on_the_carrier_the_lines_lead_to_a_start_from_safe)
{
    char seen[256];
    carrier_port(false);
    CHECK(follow_the_lines(seen, sizeof(seen)));
    char want[256];
    snprintf(want, sizeof(want), "%s|%s",
             ui_servo_str(SERVO_STR_START_PROG_5V),
             ui_servo_str(SERVO_STR_START_NOT_ARMED));
    CHECK_STR_EQ(seen, want);
    const gfx_rect_t r = rauto_rect(RA_C_START);
    /* The hold lands on START's middle row, inside it. */
    CHECK(gfx_rect_contains(r, SV_RA_START_X, SV_RA_START_Y));
    CHECK_EQ(r.y + r.h / 2, SV_RA_START_Y);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
    ra_ms(2000u);
    carrier_done();
}

/* ARMED before the PROG 5V: the box switched on leaves the port's pulse
 * off until SAFE.  The line names PORT PWM ON and the DISARM it waits for
 * (EN and DE), not PORT PWM OFF; DISARM, ARM and a hold on START start
 * AUTO. */
TEST_CASE(on_the_carrier_the_lines_lead_to_a_start_from_armed)
{
    char seen[256];
    carrier_port(true);
    CHECK_STR_EQ(rauto_blocked(), ui_servo_str(SERVO_STR_START_PROG_5V));
    prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
    ra_ms(200u);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_STR_EQ(rauto_blocked(), TR(SV_LIM_PWM_SAFE));
        CHECK(strcmp(rauto_blocked(), TR(SV_LIM_PWM_OFF)) != 0);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK(follow_the_lines(seen, sizeof(seen)));
    char want[256];
    snprintf(want, sizeof(want), "%s|%s", TR(SV_LIM_PWM_SAFE),
             ui_servo_str(SERVO_STR_START_NOT_ARMED));
    CHECK_STR_EQ(seen, want);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
    ra_ms(2000u);
    carrier_done();
}

/* servo_limit_panel.c's start gate and its pulse line, declared in
 * servo_screen_int.h. */
const char *lim_blocked(void);
const char *lim_pwm_why(void);

/* A programmer session holding the port: AUTO names the session, not PORT
 * PWM OFF, whether the pulse reads off or a report still has it on; the
 * limit run's gate the same.  With the session gone AUTO starts. */
TEST_CASE(a_session_holding_the_port_is_named_before_port_pwm)
{
    ra_setup(true, true);
    CHECK(rauto_blocked() == NULL);
    /* SAFE, PROGRAM and CONNECT: the session holds the port. */
    ra.armed = false;
    servo_screen_set_armed(false);
    ra_ms(40u);
    program_open();
    press_port();
    settle();
    CHECK(kst_panel_holds_port());
    CHECK(prog_reg_servo_holds());
    /* The bench reports ARMED, the pulse off as the session has it. */
    ra.armed = true;
    servo_screen_set_armed(true);
    servo_screen_set_drive(1u + RIG_PORT, RIG_PIN, false);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_STR_EQ(rauto_blocked(), TR(OUT_PORT_HELD));
        CHECK_STR_EQ(lim_blocked(), TR(OUT_PORT_HELD));
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK(strcmp(rauto_blocked(), TR(SV_LIM_PWM_OFF_ARMED)) != 0);
    /* A report that still has the pulse on: the session first too. */
    servo_screen_set_drive(1u + RIG_PORT, RIG_PIN, true);
    CHECK_STR_EQ(rauto_blocked(), TR(OUT_PORT_HELD));
    CHECK_STR_EQ(lim_blocked(), TR(OUT_PORT_HELD));
    /* The setting off makes no difference while the session holds it. */
    settings_set(SET_SERVO_PORT_PWM, 0.0f);
    servo_screen_set_drive(1u + RIG_PORT, RIG_PIN, false);
    CHECK_STR_EQ(rauto_blocked(), TR(OUT_PORT_HELD));
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
}

/* The hold flash walk's case for START on AUTO: the run starts as the
 * hold fires, the label turns to STOP RUN, and the button flashes. */
static void ra_frame(float dt_s)
{
    (void)dt_s;
    ra_ms(17u);
}

static void ra_draw(void) { scr->render(&cv, 0); }

TEST_CASE(auto_start_flashes_as_it_fires)
{
    ra_setup(true, true);
    CHECK(rauto_blocked() == NULL);
    const gfx_rect_t b = rauto_rect(RA_C_START);
    const gfx_rect_t r = { (int16_t)(b.x + 12), (int16_t)(b.y + 4),
                           (int16_t)(b.w - 24), 6 };
    const hold_flash_rig_t rig = { ra_frame, ra_draw, fb, UI_PANEL_W };
    char seen[64];
    scr->render(&cv, 0);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    if (!hold_flash_walk(&rig, r, 2.6f, seen, sizeof(seen))) {
        T_FAIL("START on AUTO drew %s, no flash", seen);
    }
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(servo_screen_rauto_running());
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
}

/* The KST page loaded at 6.0V and at 4.8V, SET_SERVO_PROG_V and the
 * programmer alike: every ENTER of the run asks 5000mV, the PD mini takes
 * 5.0V, the run passes, and the PD mini's set point is 5.0V after it.  The
 * run over, the programmer holds the page's voltage again. */
TEST_CASE(auto_enters_at_5v_whatever_the_kst_page_loaded)
{
    static const uint16_t k_mv[] = { 6000u, 4800u };

    for (size_t i = 0; i < 2u; ++i) {
        ra_setup(true, true);
        settings_set(SET_SERVO_PROG_V, (float)k_mv[i] / 1000.0f);
        kst_prog_supply_set(M, k_mv[i], kst_prog_supply_ma(M));
        g_rig.enter_mv = 0u;
        ra_hold_start();
        CHECK(servo_screen_rauto_running());
        ra_run_out();
        CHECK(!servo_screen_rauto_running());
        CHECK(strstr(ra.report, "PASS") != NULL);
        CHECK_EQ(g_rig.enter_mv, (uint16_t)SERVO_LIMRUN_SUPPLY_MV);
        CHECK_EQ(g_rig.sup_mv, (uint16_t)SERVO_LIMRUN_SUPPLY_MV);
        CHECK(fabsf(supply_screen_set_v() - 5.0f) < 0.005f);
        CHECK_EQ(kst_prog_supply_mv(M), k_mv[i]);
    }
    settings_set(SET_SERVO_PROG_V, settings_def(SET_SERVO_PROG_V)->def);
}

/* The IMU on the surface, everything else ready: its AXIS Z is the one
 * thing that refuses the hold, `IMU YAW REFUSED: HINGE ON X OR Y`, in
 * 6-AXIS and in 9-AXIS, and a hold starts nothing in either; AXIS X takes
 * the refusal away. */
TEST_CASE(imu_yaw_alone_refuses_auto_in_either_algorithm)
{
    ra_setup_on(true, true, true);
    CHECK(rauto_blocked() == NULL);
    settings_set(SET_IMU_ALGO, 0.0f);
    servo_screen_set_imu_axis(2u, false);
    ra_ms(100u);
    CHECK(rauto_blocked() == TR(RA_IMU_YAW));
    rauto_row_t row = RAR_COUNT;
    CHECK(rauto_hold_blocked(&row) == TR(RA_IMU_YAW));
    CHECK_EQ(row, RAR_ANGLE);
    ra_hold_start();
    CHECK(!servo_screen_rauto_running());
    settings_set(SET_IMU_ALGO, 1.0f);
    ra_ms(100u);
    CHECK(rauto_blocked() == TR(RA_IMU_YAW));
    CHECK(rauto_hold_blocked(&row) == TR(RA_IMU_YAW));
    ra_hold_start();
    CHECK(!servo_screen_rauto_running());
    servo_screen_set_imu_axis(0u, false);
    ra_ms(100u);
    CHECK(rauto_blocked() != TR(RA_IMU_YAW));
    settings_set(SET_IMU_ALGO, 0.0f);
}

/* The bench's run 2 (RUDDR023) on KST: LEFT ANGLE at 92 counts gives the
 * 1000us side 22.1°, within at its first measure; the 2000us side's horn
 * is deaf to every write.  KST's registers are not known: RIGHT ANGLE is
 * tried for the 2000us side, then LEFT ANGLE with RIGHT ANGLE back at 150;
 * the side stays both times and the run ends FAIL after 2 writes, the
 * supply's output off, the bench SAFE and RESTORE offered. */
TEST_CASE(writes_that_do_not_move_the_2000us_side_end_auto_after_two)
{
    char b[96], want[96];
    ra_setup(true, true);
    kst_image_t img = rig_servo();
    CHECK(kst_field_set(&img, KST_F_LEFT_ANGLE, 92u));
    memcpy(g_rig.sim.reg, img.r, sizeof(g_rig.sim.reg));
    memcpy(g_rig.sim.acting, img.r, sizeof(g_rig.sim.acting));
    memcpy(ra.hi_reg, img.r, sizeof(ra.hi_reg));
    ra.hi_fixed = true;
    ra_ms(100u);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    ra_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK(strstr(ra.report, "FAIL - writes do not move the 2000us side")
          != NULL);
    CHECK(strstr(ra.report, "Writes:         1000us side 0, 2000us side 2,")
          != NULL);
    CHECK_EQ(servo(KST_F_RIGHT_ANGLE), 150u);
    CHECK(servo(KST_F_LEFT_ANGLE) < 92u);
    snprintf(want, sizeof(want), TR(RA_LAST), ui_servo_str(SERVO_STR_FAIL),
             ui_servo_str(SERVO_STR_RA_WHY_NO_EFFECT_HI));
    CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), want);
    snprintf(want, sizeof(want), "%s, MIN/MAX 950-2050us",
             TR(SV_LIM_RESTORE));
    char kst[96];
    snprintf(kst, sizeof(kst), want, TR(SVH_KST));
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), kst);
    CHECK(strstr(ra.report, "RESTORE") != NULL);
    CHECK(!ra.armed);
    CHECK(ra.disarms >= 1u);
    CHECK(!g_rig.sup_on);
    CHECK(ra.offs >= 1u);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    /* After a FAIL as after a PASS: the OFF before the report. */
    CHECK(ra.open_seen);
    CHECK(ra.off_at_open);
}

/* SERVO PULSES is LIMIT RUN's: AUTO writes the margin's pulses whatever
 * it reads.  At 1000-2000 and at KEEP, MARGIN 10% writes 950.4 to
 * 2049.6us and MARGIN 0% 1000.0 to 2000.0us. */
TEST_CASE(servo_pulses_keep_or_1000_2000_leave_the_margins_pulses)
{
    enum { PULSES = 5 };
    for (int k = 0; k < 4; ++k) {
        const bool std = (k & 1) != 0;
        const bool margin = k < 2;
        ra_setup(true, true);
        settings_set(SET_RUDDER_MARGIN, margin ? 10.0f : 0.0f);
        if (std != servo_screen_limit_pulses()) {
            /* OPTIONS on a SAFE bench; one step of SERVO PULSES. */
            ra.armed = false;
            servo_screen_set_armed(false);
            ra_ms(40u);
            screen_tap(SV_FOOT_SET_X, SV_FOOT_Y);
            ui_setlist_t *sl = (ui_setlist_t *)servo_screen_list();
            CHECK(sl->open);
            ui_setlist_scroll_to(sl, PULSES);
            const gfx_rect_t r =
                ui_setlist_row_part(sl, PULSES, UI_SL_PART_PLUS);
            screen_tap(r.x + r.w / 2, r.y + r.h / 2);
            CHECK_EQ(servo_screen_limit_pulses(), std);
            go(SERVO_FN_RAUTO);
            ra.armed = true;
            servo_screen_set_armed(true);
            ra_ms(100u);
        }
        CHECK_EQ(servo_screen_limit_pulses(), std);
        CHECK(rauto_blocked() == NULL);
        ra_hold_start();
        CHECK(servo_screen_rauto_running());
        ra_run_out();
        CHECK(strstr(ra.report, "PASS") != NULL);
        int32_t lo = 0, hi = 0;
        CHECK(kst_field_display(KST_F_PULSE_LOWER, servo(KST_F_PULSE_LOWER),
                                &lo));
        CHECK(kst_field_display(KST_F_PULSE_UPPER, servo(KST_F_PULSE_UPPER),
                                &hi));
        CHECK_EQ(lo, margin ? 9504 : 10000);
        CHECK_EQ(hi, margin ? 20496 : 20000);
    }
    settings_set(SET_RUDDER_MARGIN, 10.0f);
}

/* NEUTRAL 1640.0us on the KST at 10%: each side's span from NEUTRAL grows
 * by 10%, PULSE LOWER 936.0us and PULSE UPPER 2036.0us; the bench's PULSE
 * MIN and PULSE MAX widen to 936-2036us and the report names NEUTRAL. */
TEST_CASE(an_off_center_neutral_takes_the_margin_from_neutral)
{
    ra_setup(true, true);
    kst_image_t img = rig_servo();
    CHECK(kst_field_set(&img, KST_F_NEUTRAL, 4156u));
    int32_t d = 0;
    CHECK(kst_field_display(KST_F_NEUTRAL, kst_field_get(&img, KST_F_NEUTRAL),
                            &d));
    CHECK_EQ(d, 16400);
    memcpy(g_rig.sim.reg, img.r, sizeof(g_rig.sim.reg));
    memcpy(g_rig.sim.acting, img.r, sizeof(g_rig.sim.acting));
    ra_ms(100u);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    ra_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK(kst_field_display(KST_F_PULSE_LOWER, servo(KST_F_PULSE_LOWER), &d));
    CHECK_EQ(d, 9360);
    CHECK(kst_field_display(KST_F_PULSE_UPPER, servo(KST_F_PULSE_UPPER), &d));
    CHECK_EQ(d, 20360);
    CHECK(kst_field_display(KST_F_NEUTRAL, servo(KST_F_NEUTRAL), &d));
    CHECK_EQ(d, 16400);
    uint16_t lo = 0u, hi = 0u;
    servo_screen_pulse_ends(&lo, &hi);
    CHECK_EQ(lo, 936u);
    CHECK_EQ(hi, 2036u);
    CHECK(strstr(ra.report, "Margin:         10% from NEUTRAL 1640.0us: "
                            "servo pulses 936.0-2036.0us;") != NULL);
    CHECK(strstr(ra.report, "Bench pulses:   PULSE MIN/MAX 1000-2000 -> "
                            "936-2036us, for the session") != NULL);
}

int main(void)
{
    RUN(auto_programs_both_end_points_to_the_goal_and_ends_safe);
    RUN(auto_on_the_filtered_imu_waits_for_its_settle_and_passes);
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
    RUN(on_the_carrier_the_lines_lead_to_a_start_from_safe);
    RUN(on_the_carrier_the_lines_lead_to_a_start_from_armed);
    RUN(a_session_holding_the_port_is_named_before_port_pwm);
    RUN(auto_start_flashes_as_it_fires);
    RUN(auto_enters_at_5v_whatever_the_kst_page_loaded);
    RUN(writes_that_do_not_move_the_2000us_side_end_auto_after_two);
    RUN(imu_yaw_alone_refuses_auto_in_either_algorithm);
    RUN(servo_pulses_keep_or_1000_2000_leave_the_margins_pulses);
    RUN(an_off_center_neutral_takes_the_margin_from_neutral);
    return test_summary("servo_rauto_kst");
}
