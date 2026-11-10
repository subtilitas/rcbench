/*
 * AUTO on RUDDER on a Hitec servo (shared/ui/servo_rudder_auto_panel.c
 * through servo_limit_seam.c and the Hitec run seam hitec_panel_run.h):
 * a D series servo landed within the tolerance, a D series side whose
 * writes do not move it ending the run after 2 writes, a classic servo's
 * 8-bit cells kept within what the servo takes, and the D series save and
 * reboot inside a write's session, READY's SERVO and SUPPLY rows on
 * both families, and the 5.0V every session asks.  The bench is
 * hitec_limit_panel_rig.h,
 * the surface on the AS5600 at 0.06° a us of the horn, the stops at 700
 * and 2300us outside the travel; the targets 20° with a 10% margin, 22°
 * goals.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_limit_panel_rig.h"

#include "servo_rudder_auto.h"
#include "servo_ready_int.h"
#include "servo_rudder_auto_int.h"
#include "servo_str.h"

/** @brief RUDDER's ZERO (servo_spots.h). */
#define ZERO_X 98

/* The surface's deflection at the end pulse @p us, as the horn model has
 * it. */
static float defl_at(uint16_t us)
{
    const uint16_t keep = hl.cmd;
    hl.cmd = us;
    const float d = fabsf((float)hl_horn() - 1500.0f) * HL_DEG_PER_US;
    hl.cmd = keep;
    return d;
}

/**
 * @brief The Hitec bench with the AS5600 on the surface, RUDDER AUTO's
 *        SERVO at HITEC D SERIES or CLASSIC, RUDDER zeroed and AUTO's box
 *        shown.
 * @param classic the HS-5645MG and CLASSIC, else the D485HW
 */
static void ha_setup(bool classic)
{
    servo_sim_cfg_t sc;
    hl_fresh(classic);
    if (classic) {
        servo_hs5645();
    }
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 700u;
    sc.stop_hi_us = 2300u;
    servo_sim_init(&hl.sv, &sc);
    hl.sv.position_us = 1500.0f;
    hl.last_raw = -100;
    hl.enc = true;
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTER, (float)HL_CENTER_COUNT);
    /* RUDDER AUTO's own SERVO; the HITEC page stays at D SERIES and LIMIT
     * RUN at KST/CHASERVO. */
    hl_rauto_family(classic ? SERVO_RAUTO_FAM_HITEC_C : SERVO_RAUTO_FAM_HITEC_D);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    (void)home_tag();
    hl_ms(200u);
    go(SERVO_FN_RAUTO);
    tap(ZERO_X, SV_RUD_ZERO_Y);
    CHECK(rauto_shown());
    hl_ms(100u);
}

/* HOLD 2s TO START held for 2.3s. */
static void ha_hold_start(void)
{
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    hl_ms(2300u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    hl_ms(20u);
}

static void ha_run_out(void)
{
    for (int i = 0; i < 1200 && servo_screen_rauto_running(); ++i) {
        hl_ms(500u);
    }
    hl_ms(2000u);
}

TEST_CASE(auto_lands_a_d_series_servo_within_tolerance)
{
    char b[96];
    ha_setup(false);
    CHECK(rauto_blocked() == NULL);
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "SERVO HITEC D SERIES");
    /* 1000us takes the horn 76.9% of the way to 0xB2: 33.5°, past the
     * goal; 2000us the same to 0xB0. */
    CHECK(defl_at(1000u) > 30.0f && defl_at(2000u) > 30.0f);
    ha_hold_start();
    CHECK(servo_screen_rauto_running());
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "SERVO HITEC D SERIES");
    ha_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK(strstr(hl.report, "PASS") != NULL);
    CHECK(strstr(hl.report, "Hitec D") != NULL);
    /* The end points as the HITEC page shows them: positions in counts,
     * 8192 the center (4841 and 4811 from NEUTRAL). */
    CHECK(strstr(hl.report, "LEFT END (0xB2): 3351 before") != NULL);
    CHECK(strstr(hl.report, "RIGHT END (0xB0): 13003 before") != NULL);
    CHECK(strstr(hl.report, "after (counts); moves the 1000us side") != NULL);
    {
        char line[96];
        snprintf(line, sizeof(line), "%s: 3351>%u, 13003>%u counts",
                 ui_servo_str(SERVO_STR_PASS), (unsigned)g_drv.dreg[0xB2],
                 (unsigned)g_drv.dreg[0xB0]);
        CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), line);
    }
    CHECK(strstr(hl.report, "Pulse write") == NULL);
    /* Both ends within 0.5° of 22°, moved toward NEUTRAL in counts. */
    CHECK(fabsf(defl_at(1000u) - 22.0f) <= 0.5f);
    CHECK(fabsf(defl_at(2000u) - 22.0f) <= 0.5f);
    CHECK(g_drv.dreg[0xB2] > 3351u && g_drv.dreg[0xB2] < 8192u);
    CHECK(g_drv.dreg[0xB0] < 13003u && g_drv.dreg[0xB0] > 8192u);
    CHECK_EQ(g_drv.dreg[0xC2], 8192u);
    /* One backup file before the first write; the end as every end. */
    CHECK_EQ(g_files_saves, 1u);
    CHECK(!hl.armed);
    CHECK(hl.disarms >= 1u);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_state(model()), HITEC_PROG_PWM);
}

TEST_CASE(auto_on_a_classic_servo_writes_8_bit_cells_within_lo_hi)
{
    char b[96];
    ha_setup(true);
    CHECK(rauto_blocked() == NULL);
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "SERVO HITEC CLASSIC");
    ha_hold_start();
    CHECK(servo_screen_rauto_running());
    ha_run_out();
    CHECK(strstr(hl.report, "PASS") != NULL);
    CHECK(strstr(hl.report, "LEFT END (CELL 0x14): 162 before") != NULL);
    CHECK(strstr(hl.report, "RIGHT END (CELL 0x13): 194 before") != NULL);
    CHECK(fabsf(defl_at(1000u) - 22.0f) <= 0.5f);
    CHECK(fabsf(defl_at(2000u) - 22.0f) <= 0.5f);
    /* The left cell had to rise to reach its goal; both stay 8-bit. */
    CHECK(g_drv.eeprom[0x14] > 162u);
    /* A goal past the travel the cells reach: 45° needs a cell above 255.
     * The run ends FAIL LIMIT; no write goes past 255. */
    ha_setup(true);
    settings_set(SET_RUDDER_DEFL_LO, 45.0f);
    hl_ms(100u);
    ha_hold_start();
    CHECK(servo_screen_rauto_running());
    ha_run_out();
    CHECK(strstr(hl.report, "FAIL") != NULL);
    CHECK(strstr(hl.report, servo_str(SERVO_STR_RA_WHY_LIMIT)) != NULL);
    CHECK(g_drv.eeprom[0x14] <= 255u);
    CHECK(!hl.armed);
    /* The cells written stay: the third line names the backup to load. */
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "LOAD HITEC001 IN FILES");
}

/* The HS-5645MG at DIRECTION CCW: 1000us is cell 0x13's side (194 counts,
 * 21.3°), 2000us cell 0x14's (162 counts, 17.8°); AUTO raises each toward
 * 22° and names them by the servo's direction. */
TEST_CASE(auto_on_a_classic_ccw_servo_writes_cell_0x13_for_the_short_pulses)
{
    ha_setup(true);
    hl_classic_ccw();
    hl_ms(100u);
    CHECK(rauto_blocked() == NULL);
    ha_hold_start();
    CHECK(servo_screen_rauto_running());
    ha_run_out();
    CHECK(strstr(hl.report, "PASS") != NULL);
    CHECK(strstr(hl.report, "LEFT END (CELL 0x13): 194 before") != NULL);
    CHECK(strstr(hl.report, "RIGHT END (CELL 0x14): 162 before") != NULL);
    CHECK(fabsf(defl_at(1000u) - 22.0f) <= 0.5f);
    CHECK(fabsf(defl_at(2000u) - 22.0f) <= 0.5f);
    CHECK(g_drv.eeprom[0x13] > 194u);
    CHECK(g_drv.eeprom[0x14] > 162u);
    CHECK_EQ(g_drv.eeprom[0x1B] & 1u, 0u);
}

TEST_CASE(a_d_series_write_counts_the_save_and_reboot_time_in_the_session_cap)
{
    ha_setup(false);
    ha_hold_start();
    CHECK(servo_screen_rauto_running());
    /* The first write: from WRITE to LEAVE, the save, 100ms, the restart,
     * 1000ms and the read-back lie inside the phase. */
    uint32_t at = 0u, len = 0u;
    for (int i = 0; i < 400000 && servo_screen_rauto_running(); ++i) {
        hl_ms(1u);
        const int ph = servo_screen_rauto_phase();
        if (ph == SERVO_RAUTO_PH_WRITE && at == 0u) {
            at = g_ms;
        } else if (ph != SERVO_RAUTO_PH_WRITE && at != 0u) {
            len = g_ms - at;
            break;
        }
    }
    CHECK(len >= 1100u);
    CHECK(len < SERVO_LIMRUN_PROG_MS);
    CHECK_EQ(g_drv.d_saves, 1u);
    ha_run_out();
    CHECK(strstr(hl.report, "PASS") != NULL);

    /* A write whose commands the control task never takes: the
     * programmer's own answer timeout ends it within 5s, long before the
     * run's cap; ABORTED, the bench left SAFE, the servo let go. */
    ha_setup(false);
    ha_hold_start();
    for (int i = 0; i < 400000 && servo_screen_rauto_phase()
                                      != SERVO_RAUTO_PH_WRITE; ++i) {
        hl_ms(1u);
    }
    CHECK_EQ(servo_screen_rauto_phase(), SERVO_RAUTO_PH_WRITE);
    g_q_hold = true;
    const uint32_t t0 = g_ms;
    for (int i = 0; i < 200000 && servo_screen_rauto_running(); ++i) {
        hl_ms(1u);
    }
    CHECK(!servo_screen_rauto_running());
    CHECK(g_ms - t0 < 5000u);
    hl_ms(1000u);
    CHECK(strstr(hl.report, "ABORTED") != NULL);
    CHECK(strstr(hl.report, servo_str(SERVO_STR_L_WHY_WRITE)) != NULL);
    CHECK(!hl.armed);
    CHECK_EQ(hl.cmd, 1500u);
    /* The write handed over may have landed: RESTORE is offered, with no
     * file to load since the write's session never saved one. */
    {
        char b[96];
        CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "NO BACKUP FILE");
    }
    g_q_hold = false;
}

/* A STOP after the first write's session saved HITEC001 and before the
 * write's answer: AUTO counts the write as landed, offers RESTORE with the
 * file and names it in the report. */
TEST_CASE(a_stop_with_the_first_hitec_write_on_its_way_names_the_backup)
{
    char b[96];
    ha_setup(false);
    ha_hold_start();
    CHECK(servo_screen_rauto_running());
    for (int i = 0; i < 400000 && g_files_saves == 0u; ++i) {
        hl_ms(1u);
    }
    CHECK_EQ(g_files_saves, 1u);
    CHECK_EQ(g_drv.d_saves, 0u);
    CHECK(servo_screen_rauto_running());
    /* The run holds the screen: SERVO takes no pick. */
    CHECK(servo_screen_family_refused() != NULL);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), -1);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_D);
    press_stop();
    servo_screen_cancel_arm();
    hl.armed = false;
    servo_screen_set_armed(false);
    hl_ms(200u);
    CHECK(!servo_screen_rauto_running());
    hl_ms(10000u);
    CHECK(strstr(hl.report, "ABORTED") != NULL);
    CHECK(strstr(hl.report, "Backup:         HITEC001") != NULL);
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "LOAD HITEC001 IN FILES");
    CHECK(!hl.armed);
    CHECK(!hitec_prog_write_on(model()));
}
/* The bench's run 2 (RUDDR023) on the D485HW: LEFT END at 3178 counts
 * gives the 1000us side 22.0°, within at its first measure; the 2000us
 * side at 33.3° and its horn deaf to RIGHT END.  RIGHT END is the 2000us
 * side's from the read (CW); its 2 writes leave the side where it was and
 * the run ends FAIL, LEFT END untouched, the bench SAFE and the backup to
 * load named on the third line.  A read on the HITEC page that finds the
 * values the run left keeps that line; one that finds RIGHT END changed
 * by another write clears it. */
TEST_CASE(a_d_series_side_whose_writes_do_not_move_it_fails_after_two)
{
    char b[96], want[96];
    ha_setup(false);
    g_drv.dreg[0xB2] = 8192u - 3178u;
    hl.b0_fixed = g_drv.dreg[0xB0];
    hl_ms(100u);
    CHECK(fabsf(defl_at(1000u) - 22.0f) <= 0.5f);
    CHECK(fabsf(defl_at(2000u) - 33.3f) <= 0.1f);
    ha_hold_start();
    CHECK(servo_screen_rauto_running());
    ha_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK(strstr(hl.report, "FAIL - writes do not move the 2000us side")
          != NULL);
    CHECK(strstr(hl.report, "LEFT END (0xB2): 5014 before, 5014 after "
                            "(counts); moves the 1000us side") != NULL);
    CHECK(strstr(hl.report, "Writes:         1000us side 0, 2000us side 2,")
          != NULL);
    CHECK_EQ(g_drv.dreg[0xB2], 8192u - 3178u);
    CHECK(g_drv.dreg[0xB0] < hl.b0_fixed);
    snprintf(want, sizeof(want), TR(RA_LAST), ui_servo_str(SERVO_STR_FAIL),
             ui_servo_str(SERVO_STR_RA_WHY_NO_EFFECT_HI));
    CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), want);
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "LOAD HITEC001 IN FILES");
    /* SAFE: the IOmcu drives no pulse; the rail is fed from elsewhere. */
    CHECK(!hl.armed);
    CHECK(hl.disarms >= 1u);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_state(model()), HITEC_PROG_PWM);

    /* The HITEC page reads the servo as the run left it: the hint stays. */
    const uint16_t left_b0 = g_drv.dreg[0xB0];
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    for (int i = 0; i < 60000 && hitec_prog_state(model()) != HITEC_PROG_HELD;
         ++i) {
        hl_ms(1u);
    }
    CHECK_EQ(hitec_prog_state(model()), HITEC_PROG_HELD);
    hl_ms(100u);
    CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "LOAD HITEC001 IN FILES");
    /* Another file written to the servo, read back: values the run did not
     * leave, and the hint is gone. */
    g_drv.dreg[0xB0] = (uint16_t)(left_b0 + 100u);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    for (int i = 0; i < 60000 && hitec_prog_state(model()) != HITEC_PROG_HELD;
         ++i) {
        hl_ms(1u);
    }
    hl_ms(100u);
    /* The third line names the report again. */
    const char *rep = TR(RA_FILE);
    const size_t head = (size_t)(strchr(rep, '%') - rep);
    CHECK(strncmp(rauto_line(2, b, sizeof(b)), rep, head) == 0);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    hl_ms(3000u);
}

/* A D series servo needs no power cycle, so a rail fed from elsewhere
 * passes the gate: READY reads SERVO as HITEC D SERIES, SUPPLY green with
 * the 6.00V the INA3221 measures and nothing live through it, and the hold
 * is offered with nothing for it to switch on.  On the classic servo's bench
 * the PD mini feeds it: SERVO reads HITEC CLASSIC, PD MINI the run's 5.0V
 * and SUPPLY's SET A, in red as an output that is live through it.  The
 * external rail's row has no SET A: no run sets its current limit. */
TEST_CASE(a_d_series_servo_on_an_external_rail_reads_the_rail_voltage)
{
    rdy_row_t r;
    ha_setup(false);
    CHECK(rauto_blocked() == NULL);
    CHECK(rauto_ready_row(RAR_SERVO, &r));
    CHECK_STR_EQ(r.value, "HITEC D SERIES");
    CHECK(rauto_ready_row(RAR_SUPPLY, &r));
    CHECK_STR_EQ(r.label, TR(RDY_SUPPLY));
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(r.value, "6.00V");
    CHECK(r.why == NULL);
    CHECK(rauto_hold_blocked(NULL) == NULL);

    ha_setup(true);
    CHECK(rauto_ready_row(RAR_SERVO, &r));
    CHECK_STR_EQ(r.value, "HITEC CLASSIC");
    CHECK(rauto_ready_row(RAR_SUPPLY, &r));
    CHECK_STR_EQ(r.label, "PD MINI");
    CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
    char want[24];
    snprintf(want, sizeof(want), "5.0V %.2fA", (double)supply_screen_set_i());
    CHECK_STR_EQ(r.value, want);
}

/* SET_SERVO_PROG_V at 6.0V and at 4.8V: AUTO's sessions ask 5000mV, on
 * the D series servo's external rail and on the classic servo behind the
 * PD mini, which is at 5.0V after the run.  The run over, the HITEC page's
 * programmer holds the setting's voltage again. */
TEST_CASE(auto_programs_at_5v_whatever_prog_v_holds)
{
    static const float k_v[] = { 6.0f, 4.8f };

    for (int cls = 0; cls < 2; ++cls) {
        for (size_t i = 0; i < 2u; ++i) {
            ha_setup(cls != 0);
            settings_set(SET_SERVO_PROG_V, k_v[i]);
            g_step_mv = 0u;
            ha_hold_start();
            CHECK(servo_screen_rauto_running());
            ha_run_out();
            CHECK(!servo_screen_rauto_running());
            CHECK(strstr(hl.report, "PASS") != NULL);
            CHECK_EQ(g_step_mv, (uint16_t)SERVO_LIMRUN_SUPPLY_MV);
            if (cls != 0) {
                CHECK(fabsf(supply_screen_set_v() - 5.0f) < 0.005f);
            }
            CHECK_EQ(hitec_prog_mv(model()),
                     (uint16_t)lroundf(k_v[i] * 1000.0f));
        }
    }
    settings_set(SET_SERVO_PROG_V, settings_def(SET_SERVO_PROG_V)->def);
}

int main(void)
{
    RUN(auto_lands_a_d_series_servo_within_tolerance);
    RUN(auto_on_a_classic_servo_writes_8_bit_cells_within_lo_hi);
    RUN(auto_on_a_classic_ccw_servo_writes_cell_0x13_for_the_short_pulses);
    RUN(a_d_series_write_counts_the_save_and_reboot_time_in_the_session_cap);
    RUN(a_stop_with_the_first_hitec_write_on_its_way_names_the_backup);
    RUN(a_d_series_side_whose_writes_do_not_move_it_fails_after_two);
    RUN(a_d_series_servo_on_an_external_rail_reads_the_rail_voltage);
    RUN(auto_programs_at_5v_whatever_prog_v_holds);
    return test_summary("servo_rauto_hitec");
}
