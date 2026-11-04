/*
 * AUTO on RUDDER on a Hitec servo (shared/ui/servo_rudder_auto_panel.c
 * through servo_limit_seam.c and the Hitec run seam hitec_panel_run.h):
 * a D series servo landed within the tolerance, a classic servo's 8-bit
 * cells kept within what the servo takes, and the D series save and
 * reboot inside a write's session.  The bench is hitec_limit_panel_rig.h,
 * the surface on the AS5600 at 0.06° a us of the horn, the stops at 700
 * and 2300us outside the travel; the targets 20° with a 10% margin, 22°
 * goals.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_limit_panel_rig.h"

#include "servo_rudder_auto.h"
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
 * @brief The Hitec bench with the AS5600 on the surface, the SERVO row at
 *        HITEC, RUDDER zeroed and AUTO's box shown.
 * @param classic the HS-5645MG and CLASSIC, else the D485HW
 */
static void ha_setup(bool classic)
{
    servo_sim_cfg_t sc;
    hl_fresh(classic);
    if (classic) {
        servo_hs5645();
        settings_set(SET_HITEC_SERIES, (float)HITEC_CLASSIC);
    }
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 700u;
    sc.stop_hi_us = 2300u;
    servo_sim_init(&hl.sv, &sc);
    hl.sv.position_us = 1500.0f;
    hl.last_raw = -100;
    hl.enc = true;
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, (float)HL_CENTRE_COUNT);
    hl_settings(true, false);                /* SERVO: HITEC */
    (void)home_tag();
    hl_ms(200u);
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    tap(ZERO_X, SV_RUD_ZERO_Y);
    tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    CHECK(rauto_shown());
    hl_ms(100u);
}

/* HOLD 2s TO START held for 2.3s. */
static void ha_hold_start(void)
{
    ev(SV_RUD_LEN_X, SV_RA_ROW_Y(2), TOUCH_EVENT_DOWN, 1);
    hl_ms(2300u);
    ev(SV_RUD_LEN_X, SV_RA_ROW_Y(2), TOUCH_EVENT_UP, 1);
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
    CHECK(strstr(hl.report, "LEFT END (0xB2): 4841 before") != NULL);
    CHECK(strstr(hl.report, "RIGHT END (0xB0): 4811 before") != NULL);
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

int main(void)
{
    RUN(auto_lands_a_d_series_servo_within_tolerance);
    RUN(auto_on_a_classic_servo_writes_8_bit_cells_within_lo_hi);
    RUN(auto_on_a_classic_ccw_servo_writes_cell_0x13_for_the_short_pulses);
    RUN(a_d_series_write_counts_the_save_and_reboot_time_in_the_session_cap);
    RUN(a_stop_with_the_first_hitec_write_on_its_way_names_the_backup);
    return test_summary("servo_rauto_hitec");
}
