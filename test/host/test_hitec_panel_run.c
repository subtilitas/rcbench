/*
 * The Hitec programmer's run seam on the overlay (shared/ui/
 * hitec_panel_run.c) on the loopback of hitec_panel_rig.h: CONNECT, the
 * write and LEAVE polled in order for a D series servo (the save, 100ms,
 * the reboot, 1000ms, the read-back) and for a classic one (the cells and
 * the balance); a CONNECT that finds no servo; a CONNECT on an ARMED
 * bench; ready while the overlay holds the port; the backup file kept over
 * LEAVE; the operator's supply back at the run's end; a write the servo
 * does not take.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_run.h"

#include "hitec_panel_rig.h"

/* Poll until the step ends; 60s is the bound. */
static hitec_panel_run_t settle(void)
{
    hitec_panel_run_t r = hitec_panel_run_poll();

    for (unsigned i = 0; i < 60000u && r == HITEC_PANEL_RUN_BUSY; ++i) {
        pms();
        r = hitec_panel_run_poll();
    }
    return r;
}

/* A run begun and connected on the overlay's bench. */
static void connect_ok(void)
{
    hitec_panel_run_begin();
    CHECK(hitec_panel_run_ready());
    CHECK_EQ(hitec_panel_run_connect(true, 5000u, 1000u),
             HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_panel_run_poll(), HITEC_PANEL_RUN_BUSY);
    CHECK(!hitec_panel_run_ready());
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    /* Taken once. */
    CHECK_EQ(hitec_panel_run_poll(), HITEC_PANEL_RUN_NONE);
}

TEST_CASE(connect_write_leave_poll_in_order_for_the_d_series)
{
    hitec_run_servo_t sv;
    uint16_t v[2] = { 0u, 0u };
    uint32_t save_ms = 0, reboot_ms = 0;
    unsigned saves = 0, reboots = 0;
    hitec_panel_run_t r;

    open_en();
    servo_d485();
    g_card = true;
    connect_ok();
    CHECK(hitec_panel_run_servo(&sv));
    CHECK_EQ(sv.series, HITEC_D);
    CHECK_EQ(sv.value[0], 8192u - 3351u);
    CHECK(!hitec_panel_run_values(v));
    CHECK_EQ(hitec_panel_run_write(4000u, 4500u), HITEC_PROG_MSG_NONE);
    /* Step by step: the save, 100ms, the reboot, 1000ms, the read-back. */
    r = hitec_panel_run_poll();
    for (unsigned i = 0; i < 60000u && r == HITEC_PANEL_RUN_BUSY; ++i) {
        pms();
        if (g_drv.d_saves != saves) {
            saves = g_drv.d_saves;
            save_ms = g_ms;
        }
        if (g_drv.d_reboots != reboots) {
            reboots = g_drv.d_reboots;
            reboot_ms = g_ms;
        }
        r = hitec_panel_run_poll();
    }
    CHECK_EQ(r, HITEC_PANEL_RUN_OK);
    CHECK_EQ(saves, 1u);
    CHECK_EQ(reboots, 1u);
    CHECK(reboot_ms - save_ms >= 100u);
    CHECK(g_ms - reboot_ms >= 1000u);
    CHECK_EQ(g_drv.dreg[0xB2], 8192u - 4000u);
    CHECK_EQ(g_drv.dreg[0xB0], 8192u + 4500u);
    CHECK(hitec_panel_run_values(v));
    CHECK_EQ(v[0], 4000u);
    CHECK_EQ(v[1], 4500u);
    /* The write's end switches the write off. */
    CHECK(!hitec_prog_write_on(model()));
    CHECK_STR_EQ(hitec_panel_run_backup(), "HITEC001");
    /* LEAVE: back in PWM, the backup's name kept. */
    CHECK_EQ(hitec_panel_run_leave(), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    CHECK(hitec_panel_run_ready());
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "");
    CHECK_STR_EQ(hitec_panel_run_backup(), "HITEC001");
    hitec_panel_run_end();
    CHECK(!model()->run);
}

TEST_CASE(the_same_for_a_classic_servo_with_the_balance)
{
    hitec_run_servo_t sv;
    uint16_t v[2];
    unsigned sum = 0;

    open_en();
    settings_set(SET_HITEC_SERIES, (float)HITEC_CLASSIC);
    servo_hs5645();
    connect_ok();
    CHECK_EQ(hitec_prog_series(model()), HITEC_CLASSIC);
    CHECK(hitec_panel_run_servo(&sv));
    CHECK_EQ(sv.value[0], 0xA2u);
    CHECK_EQ(sv.value[1], 0xC2u);
    CHECK_EQ(hitec_panel_run_write(0x90u, 0xB0u), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    CHECK_EQ(g_drv.eeprom[0x14], 0x90u);
    CHECK_EQ(g_drv.eeprom[0x13], 0xB0u);
    /* The balance: cells 0x00 to 0x1C sum to 0. */
    for (unsigned c = 0; c <= 0x1Cu; ++c) {
        sum += g_drv.eeprom[c];
    }
    CHECK_EQ(sum & 0xFFu, 0u);
    CHECK(hitec_panel_run_values(v));
    CHECK_EQ(v[0], 0x90u);
    CHECK_EQ(v[1], 0xB0u);
    CHECK_EQ(hitec_panel_run_leave(), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    /* No card: no backup file. */
    CHECK_STR_EQ(hitec_panel_run_backup(), "");
    hitec_panel_run_end();
}

TEST_CASE(no_servo_is_reported_after_a_connect_that_found_none)
{
    open_at(SUPPLY_OFFLINE, UI_LANG_EN);
    servo_d485();
    hitec_panel_run_begin();
    CHECK(!hitec_panel_run_no_servo());
    CHECK_EQ(hitec_panel_run_connect(false, 0u, 0u), HITEC_PROG_MSG_NONE);
    CHECK(!hitec_panel_run_no_servo());
    CHECK_EQ(settle(), HITEC_PANEL_RUN_FAILED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_NO_D);
    CHECK(hitec_panel_run_no_servo());
    CHECK(!hitec_panel_run_servo(&(hitec_run_servo_t){ 0 }));
    /* LEAVE of a servo that never entered: CLOSE at once. */
    CHECK_EQ(hitec_panel_run_leave(), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    CHECK(!hitec_panel_run_no_servo());
    /* A NO_D said before the next CONNECT does not count for it. */
    CHECK_EQ(hitec_panel_run_connect(false, 0u, 0u), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_FAILED);
    CHECK(hitec_panel_run_no_servo());
    hitec_panel_run_end();
}

TEST_CASE(a_connect_on_an_armed_bench_is_refused)
{
    open_en();
    servo_d485();
    CHECK(arm());
    hitec_panel_bench_armed(true);
    prun(50u);
    hitec_panel_run_begin();
    CHECK_EQ(hitec_panel_run_connect(true, 5000u, 1000u),
             HITEC_PROG_MSG_NEED_DISARM);
    CHECK_EQ(hitec_panel_run_poll(), HITEC_PANEL_RUN_NONE);
    prun(200u);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    CHECK(!hitec_prog_holds_port(model()));
    /* SAFE again: taken. */
    disarm();
    hitec_panel_bench_armed(false);
    prun(50u);
    CHECK_EQ(hitec_panel_run_connect(true, 5000u, 1000u),
             HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    /* ARMED asked under the held port: the write is refused, and switched
     * off. */
    hitec_panel_bench_armed(true);
    CHECK_EQ(hitec_panel_run_write(4000u, 4500u), HITEC_PROG_MSG_NEED_DISARM);
    CHECK_EQ(hitec_panel_run_poll(), HITEC_PANEL_RUN_NONE);
    CHECK(!hitec_prog_write_on(model()));
    hitec_panel_bench_armed(false);
    hitec_panel_run_end();
}

TEST_CASE(ready_is_false_while_the_overlay_holds_the_port)
{
    open_en();
    servo_d485();
    CHECK(hitec_panel_run_ready());
    /* The operator's CONNECT on the overlay. */
    tap_connect();
    CHECK(!hitec_panel_run_ready());
    hitec_panel_run_begin();
    CHECK_EQ(hitec_panel_run_connect(true, 5000u, 1000u),
             HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_panel_run_poll(), HITEC_PANEL_RUN_NONE);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK(hitec_panel_run_ready());
    hitec_panel_run_end();
}

TEST_CASE(the_run_end_gives_the_operator_his_supply_back)
{
    open_en();
    servo_d485();
    const uint16_t mv = model()->supply_mv;
    const uint16_t ma = model()->supply_ma;

    hitec_panel_run_begin();
    CHECK_EQ(hitec_panel_run_connect(true, 5500u, 1500u),
             HITEC_PROG_MSG_NONE);
    CHECK_EQ(model()->supply_mv, 5500u);
    CHECK_EQ(model()->supply_ma, 1500u);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    CHECK_EQ(hitec_panel_run_leave(), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    /* A second session of the run keeps the first values kept. */
    CHECK_EQ(hitec_panel_run_connect(true, 5200u, 1200u),
             HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    CHECK_EQ(hitec_panel_run_leave(), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    hitec_panel_run_end();
    CHECK_EQ(model()->supply_mv, mv);
    CHECK_EQ(model()->supply_ma, ma);
    /* A run without a CONNECT keeps nothing to give back. */
    hitec_panel_run_begin();
    hitec_panel_run_end();
    CHECK_EQ(model()->supply_mv, mv);
}

TEST_CASE(a_write_the_servo_does_not_take_fails_and_switches_write_off)
{
    uint16_t v[2];

    open_en();
    servo_d485();
    connect_ok();
    g_drv.d_drop = true;
    CHECK_EQ(hitec_panel_run_write(4000u, 4500u), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_FAILED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_VERIFY_FAIL);
    CHECK(!hitec_prog_write_on(model()));
    /* The values the servo holds, from the read-back. */
    CHECK(hitec_panel_run_values(v));
    CHECK_EQ(v[0], 8192u - 3351u);
    /* A write outside the held port: refused, nothing asked. */
    CHECK_EQ(hitec_panel_run_leave(), HITEC_PROG_MSG_NONE);
    CHECK_EQ(settle(), HITEC_PANEL_RUN_OK);
    CHECK(hitec_panel_run_write(4000u, 4500u) != HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_panel_run_poll(), HITEC_PANEL_RUN_NONE);
    /* A LEAVE with no port held: refused, nothing asked. */
    CHECK(hitec_panel_run_leave() != HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_panel_run_poll(), HITEC_PANEL_RUN_NONE);
    /* The step's end counts the link: a CONNECT the link loses fails. */
    CHECK_EQ(hitec_panel_run_connect(true, 5000u, 1000u),
             HITEC_PROG_MSG_NONE);
    link_silent();
    CHECK_EQ(settle(), HITEC_PANEL_RUN_FAILED);
    hitec_panel_run_end();
}

int main(void)
{
    RUN(connect_write_leave_poll_in_order_for_the_d_series);
    RUN(the_same_for_a_classic_servo_with_the_balance);
    RUN(no_servo_is_reported_after_a_connect_that_found_none);
    RUN(a_connect_on_an_armed_bench_is_refused);
    RUN(ready_is_false_while_the_overlay_holds_the_port);
    RUN(the_run_end_gives_the_operator_his_supply_back);
    RUN(a_write_the_servo_does_not_take_fails_and_switches_write_off);
    free(fb);
    return test_summary("hitec_panel_run");
}
