/*
 * The SERVO screen's limit run on a Hitec servo (shared/ui/servo_limit_seam.c,
 * servo_limit_panel.c, the Hitec run seam hitec_panel_run.h): the LIMIT RUN
 * settings' SERVO row, the card's lines, the refusals before a start, a D
 * series run that programs 0xB2 and 0xB0 through the seam, and a STOP that
 * ends a run with every output off.  The bench is hitec_limit_panel_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_limit_panel_rig.h"

#include "ui_text.h"

/* The settings' rows, and what the card says, on a bench where both
 * programmers can run: the SERVO row offered on the programming port,
 * SERVO PULSES gone with HITEC. */
TEST_CASE(the_limit_run_card_offers_hitec_when_the_row_says_so_and_hides_servo_pulses)
{
    hl_fresh(true);
    hl_settings(false, false);
    /* KST/CHASERVO first: SERVO PULSES is there; HITEC takes it away, and a
     * tap where it stood changes nothing. */
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    tap(HL_SERVO_X, HL_ROW1_Y);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_D);
    CHECK(!servo_screen_limit_pulses());
    tap(HL_PULSE_X, HL_ROW1_Y);
    CHECK(!servo_screen_limit_pulses());
    /* The series is the Hitec programmer's: CLASSIC there, CLASSIC here. */
    settings_set(SET_HITEC_SERIES, (float)HITEC_CLASSIC);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_C);
    settings_set(SET_HITEC_SERIES, (float)HITEC_D);
    /* Back to KST/CHASERVO: SERVO PULSES taps again. */
    tap(HL_SERVO_X, HL_ROW1_Y);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    tap(HL_PULSE_X, HL_ROW1_Y);
    CHECK(servo_screen_limit_pulses());
    tap(HL_PULSE_X, HL_ROW1_Y);
    /* The programmer opened last takes the row with it: the Hitec one on
     * PROGRAM, then the KST one. */
    open_program();
    tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(1));
    hl_ms(100u);
    CHECK(hitec_panel_is_open());
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_D);
    hitec_panel_close();
    hl_ms(100u);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_D);
    open_kst();
    hl_ms(100u);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    close_kst();
}

/* The refusals a Hitec run meets before any hold: PULSE MIN and MAX short
 * of the model's full travel, and a classic servo with no PD mini. */
TEST_CASE(a_hitec_start_is_refused_before_a_drive_where_it_cannot_run)
{
    char want[80];

    hl_fresh(false);
    hl_settings(true, true);
    CHECK(servo_screen_limit_blocked() == NULL);
    choose_type(0);                          /* STANDARD PWM: 1000-2000us */
    hl_ms(100u);
    snprintf(want, sizeof(want), TR(SV_LIM_HITEC_RANGE), 850u, 2150u);
    CHECK(servo_screen_limit_blocked() != NULL);
    CHECK_STR_EQ(servo_screen_limit_blocked(), want);
    CHECK_STR_EQ(want, "HITEC: PULSE MIN/MAX MUST COVER 850-2150us");
    hl_hold_start();
    CHECK(!servo_screen_limit_running());
    CHECK_EQ(g_drv.d_writes, 0u);
    choose_type(2);
    hl_ms(100u);
    CHECK(servo_screen_limit_blocked() == NULL);
    /* CLASSIC: its entry needs a supply the bench switches. */
    settings_set(SET_HITEC_SERIES, (float)HITEC_CLASSIC);
    hl_ms(100u);
    CHECK_STR_EQ(servo_screen_limit_blocked(), TR(HP_NEED_PD_C));
    settings_set(SET_HITEC_SERIES, (float)HITEC_D);
}

/**
 * @brief PULSE MIN and MAX typed on the output settings, back to TEST.
 * @param min PULSE MIN, us, as typed
 * @param max PULSE MAX, us, as typed
 */
static void hl_pulses(const char *min, const char *max)
{
    open_settings();
    tap(ROW_L_X, ROW_Y(2));                  /* PULSE MIN */
    keys(min);
    tap(ROW_L_X, ROW_Y(3));                  /* PULSE MAX */
    keys(max);
    close_settings();
    hl_ms(100u);
}

/* The D series model's full travel at the exact limits: PULSE MIN 850us and
 * MAX 2150us take it; 851us or 2149us falls 1us short. */
TEST_CASE(pulse_min_850us_and_max_2150us_are_the_d_series_exact_limits)
{
    hl_fresh(false);
    hl_settings(true, true);
    hl_pulses("850", "2150");
    CHECK(servo_screen_limit_blocked() == NULL);
    hl_pulses("851", "2150");
    CHECK_STR_EQ(servo_screen_limit_blocked(),
                 "HITEC: PULSE MIN/MAX MUST COVER 850-2150us");
    hl_pulses("850", "2149");
    CHECK_STR_EQ(servo_screen_limit_blocked(),
                 "HITEC: PULSE MIN/MAX MUST COVER 850-2150us");
    hl_pulses("850", "2150");
    CHECK(servo_screen_limit_blocked() == NULL);
}

/* SEARCH + PROGRAM on the D485HW, no PD mini: the read session, then each
 * write a session of the Hitec programmer -- SAFE, CONNECT, the write with
 * its save and reboot, LEAVE, ARMED -- 0xB2 and 0xB0 lowered toward 0xC2
 * until neither end binds, one backup file, PASS. */
TEST_CASE(a_d_series_limit_run_programs_0xb2_and_0xb0_through_the_seam)
{
    hl_fresh(false);
    hl_settings(true, true);
    CHECK(servo_screen_limit_blocked() == NULL);
    hl_hold_start();
    CHECK(servo_screen_limit_running());
    hl_run_out();
    CHECK(!servo_screen_limit_running());
    CHECK(strstr(hl.report, "Result:         PASS\n") != NULL);
    CHECK(strstr(hl.report, "Hitec D SERIES") != NULL);
    CHECK(strstr(hl.report, "LEFT END (0xB2): 4841 before") != NULL);
    CHECK(strstr(hl.report, "RIGHT END (0xB0): 4811 before") != NULL);
    /* On the wire: both end points moved toward NEUTRAL, NEUTRAL stayed. */
    CHECK_EQ(g_drv.dreg[0xC2], 8192u);
    CHECK(g_drv.dreg[0xB2] > 3351u && g_drv.dreg[0xB2] < 8192u);
    CHECK(g_drv.dreg[0xB0] < 13003u && g_drv.dreg[0xB0] > 8192u);
    /* Each write sent saved and restarted the servo; each LEAVE restarted
     * it once more. */
    CHECK(strstr(hl.report, "writes sent 3,") != NULL);
    CHECK_EQ(g_drv.d_saves, 3u);
    CHECK(g_drv.d_reboots > g_drv.d_saves);
    /* The full travel inside both stops by the model's resolution: a push
     * of 3us past a stop draws less than the knee (test_servo_limrun.c). */
    hl.cmd = 850u;
    CHECK(hl_horn() >= 1150u - 3u);
    hl.cmd = 2150u;
    CHECK(hl_horn() <= 1880u + 3u);
    /* One backup file, the servo as the run found it. */
    CHECK_EQ(g_files_saves, 1u);
    CHECK(strstr(hl.report, "Backup:         HITEC001") != NULL);
    /* The bench back to ARMED after every session; the run's end gave the
     * write switch back. */
    CHECK(hl.armed);
    CHECK(hl.arms >= 2u);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_state(model()), HITEC_PROG_PWM);
}

/* A classic HS-5645MG at DIRECTION CCW: the short pulses are cell 0x13's,
 * 194 counts, 427us past the stop at 1150us; the long ones cell 0x14's,
 * 162 counts, 356us inside the stop at 1880us.  The run lowers 0x13 only
 * and names it LEFT END. */
TEST_CASE(a_classic_ccw_limit_run_lowers_cell_0x13_for_the_short_pulses)
{
    hl_fresh(true);
    servo_hs5645();
    hl_classic_ccw();
    settings_set(SET_HITEC_SERIES, (float)HITEC_CLASSIC);
    hl_settings(true, true);
    CHECK(servo_screen_limit_blocked() == NULL);
    hl_hold_start();
    CHECK(servo_screen_limit_running());
    hl_run_out();
    CHECK(!servo_screen_limit_running());
    CHECK(strstr(hl.report, "Result:         PASS\n") != NULL);
    CHECK(strstr(hl.report, "DIRECTION CCW") != NULL);
    CHECK(strstr(hl.report, "LEFT END (CELL 0x13): 194 before") != NULL);
    CHECK(strstr(hl.report, "RIGHT END (CELL 0x14): 162 before") != NULL);
    CHECK(g_drv.eeprom[0x13] < 194u);
    CHECK_EQ(g_drv.eeprom[0x14], 162u);
    CHECK_EQ(g_drv.eeprom[0x1B] & 1u, 0u);
}

/* A STOP while the run searches after its first write: the run ends
 * ABORTED, the servo let go, the supply's output off, the bench SAFE, and
 * the box names the backup file to load. */
TEST_CASE(a_stop_during_a_hitec_run_ends_it_with_every_output_off)
{
    hl_fresh(true);
    hl_settings(true, true);
    hl_hold_start();
    CHECK(servo_screen_limit_running());
    /* Until the first write landed and the bench is ARMED again. */
    for (int i = 0; i < 400000 && g_drv.d_saves == 0u; ++i) {
        hl_ms(1u);
    }
    CHECK(g_drv.d_saves == 1u);
    for (int i = 0; i < 20000 && !hl.armed; ++i) {
        hl_ms(1u);
    }
    CHECK(hl.armed);
    hl_ms(500u);
    CHECK(servo_screen_limit_running());
    const unsigned offs = hl.offs;
    const unsigned releases = hl.releases;
    press_stop();
    servo_screen_cancel_arm();
    hl.armed = false;
    servo_screen_set_armed(false);
    hl_ms(200u);
    CHECK(!servo_screen_limit_running());
    CHECK(hl.offs > offs);
    CHECK(!hl.pd_out);
    CHECK(hl.releases > releases);
    CHECK_EQ(hl.cmd, 1500u);
    CHECK(!hl.armed);
    hl_ms(1000u);
    CHECK(strstr(hl.report, "ABORTED") != NULL);
    CHECK(strstr(hl.report, "Restore:        load the backup file") != NULL);
    CHECK(!hitec_prog_write_on(model()));
    /* No ARM follows the STOP. */
    const unsigned arms = hl.arms;
    hl_ms(3000u);
    CHECK_EQ(hl.arms, arms);
}

int main(void)
{
    RUN(the_limit_run_card_offers_hitec_when_the_row_says_so_and_hides_servo_pulses);
    RUN(a_hitec_start_is_refused_before_a_drive_where_it_cannot_run);
    RUN(pulse_min_850us_and_max_2150us_are_the_d_series_exact_limits);
    RUN(a_d_series_limit_run_programs_0xb2_and_0xb0_through_the_seam);
    RUN(a_classic_ccw_limit_run_lowers_cell_0x13_for_the_short_pulses);
    RUN(a_stop_during_a_hitec_run_ends_it_with_every_output_off);
    return test_summary("servo_limit_hitec");
}
