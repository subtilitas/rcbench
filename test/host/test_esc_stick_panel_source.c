/**
 * @file test_esc_stick_panel_source.c
 * @brief ESC STICK's beep source: the phase tap while its capture runs, the
 * INA3221 channel in the ESC's supply line otherwise, RUN refused with
 * neither; the tap's beeps handed to the run from the readout, and the ends
 * a beep missing or a capture stopping give.
 *
 * Cases:
 * - run_is_refused_with_no_beep_source
 * - the_tap_counts_when_it_runs_and_the_meter_otherwise
 * - the_readouts_beeps_reach_the_run_oldest_first
 * - beeps_past_the_readout_end_the_run_beep_lost
 * - a_tap_that_stops_ends_the_run_and_draws_its_reason
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/** @brief The tap running, read, with @p n beeps numbered up to @p newest. */
static tone_readout_t tap_running(uint16_t newest, unsigned n)
{
    tone_readout_t t;
    memset(&t, 0, sizeof(t));
    t.state = TONE_STATE_RUNNING;
    t.read = true;
    t.n = (uint8_t)n;
    for (unsigned i = 0u; i < n && i < TONE_LINK_SHOWN; ++i) {
        t.beeps[i].seq = (uint16_t)(newest - i);
        t.beeps[i].start_ms = 10000u + 500u * (newest - i);
        t.beeps[i].len_dms = 1279u;
        t.beeps[i].freq_dhz = 20900u;
    }
    return t;
}

/** @brief The tap off, as SETUP has it by default. */
static void tap_off(void)
{
    tone_readout_t t;
    memset(&t, 0, sizeof(t));
    esc_stick_panel_tone(&t);
}

/** @brief Hobbywing's page with a change picked and the warning's hold held:
 *         a run, if the bench can carry one. */
static void hold_a_run(void)
{
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
}

TEST_CASE(run_is_refused_with_no_beep_source)
{
    fresh();
    tap_off();
    descend_to_hobbywing();
    pick_cutoff();
    /* The meter's last window 1500ms old, the supply read off just now. */
    for (uint32_t t = 1500u; t <= 1800u; t += 100u) {
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.samples = (uint16_t)(t / 100u);
        st.taken_ms = t;
        st.stamped = true;
        st.mode = SUPPLY_MODE_OFF;
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        esc_stick_panel_supply(&st);
    }
    esc_stick_panel_bench(1800u, false, 0u, 0u, false);
    scr->tick(0.02f);
    const uint32_t runs = esc_stick_panel_runs();
    tap(WRITE_X, BTN_CY);
    CHECK_STR_EQ(esc_stick_panel_note(), TR(SP_NO_SOURCE));
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), runs);
    CHECK(!esc_stick_running(esc_stick_panel_stick()));

    /* A window now: RUN is offered again. */
    esc_stick_panel_meter(RIG_METER_CH, 2u, 0, true);
    scr->tick(0.02f);
    CHECK(strcmp(esc_stick_panel_note(), TR(SP_NO_SOURCE)) != 0);
}

TEST_CASE(the_tap_counts_when_it_runs_and_the_meter_otherwise)
{
    fresh();
    tap_off();
    hold_a_run();
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    CHECK_EQ(esc_stick_panel_stick()->src, ESC_STICK_SRC_METER);
    static rig_t r;
    rig_start(&r);
    /* The tap coming up during the run changes nothing: the source is the
     * run's from its start. */
    tone_readout_t t = tap_running(5u, 4u);
    esc_stick_panel_tone(&t);
    for (int i = 0; i < 2000; ++i) {
        rig_step(&r);
    }
    CHECK_EQ(esc_stick_panel_stick()->src, ESC_STICK_SRC_METER);
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    rig_run(&r, 300000u);
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);

    /* Running at RUN: the tap counts. */
    fresh();
    esc_stick_panel_tone(&t);
    hold_a_run();
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    CHECK_EQ(esc_stick_panel_stick()->src, ESC_STICK_SRC_TAP);
    CHECK_EQ(esc_stick_panel_stick()->tap_seq, 5u);
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    (void)drain_cmds(MOTOR_CMD_ARM);
    tap_off();
}

TEST_CASE(the_readouts_beeps_reach_the_run_oldest_first)
{
    fresh();
    tone_readout_t t = tap_running(65534u, 2u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK_EQ(run->src, ESC_STICK_SRC_TAP);
    CHECK_EQ(run->tap_seq, 65534u);
    /* Three more, across the numbers' wrap: 65535, 1, 2. */
    t.n = 4u;
    t.beeps[0].seq = 2u;
    t.beeps[1].seq = 1u;
    t.beeps[2].seq = 65535u;
    t.beeps[3].seq = 65534u;
    t.beeps[0].start_ms = 13000u;
    t.beeps[1].start_ms = 12500u;
    t.beeps[2].start_ms = 12000u;
    t.beeps[3].start_ms = 11500u;
    esc_stick_panel_tone(&t);
    CHECK(esc_stick_running(run));
    CHECK_EQ(run->tap_seq, 2u);
    CHECK(run->tap_rx);
    CHECK_EQ(run->tap_end, 13000u + 128u);   /* 127.9ms, in whole ms */
    /* The same readout again hands over nothing. */
    esc_stick_panel_tone(&t);
    CHECK(esc_stick_running(run));
    CHECK_EQ(run->tap_seq, 2u);
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    (void)drain_cmds(MOTOR_CMD_ARM);
    tap_off();
}

/* Five beeps between two frames: the readout holds four, and the run never
 * gets the first of them. */
TEST_CASE(beeps_past_the_readout_end_the_run_beep_lost)
{
    fresh();
    tone_readout_t t = tap_running(10u, 4u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK_EQ(run->src, ESC_STICK_SRC_TAP);
    t = tap_running(15u, 4u);
    esc_stick_panel_tone(&t);
    CHECK(!esc_stick_running(run));
    CHECK_EQ(run->reason, ESC_STICK_R_TAP_LOST);
    CHECK(!esc_stick_out(run)->supply_on);
    CHECK(!esc_stick_out(run)->arm);

    /* The tap's own lost count moving ends it the same way. */
    fresh();
    t = tap_running(10u, 4u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    t.lost = 1u;
    esc_stick_panel_tone(&t);
    CHECK_EQ(run->reason, ESC_STICK_R_TAP_LOST);
    tap_off();
}

TEST_CASE(a_tap_that_stops_ends_the_run_and_draws_its_reason)
{
    fresh();
    tone_readout_t t = tap_running(3u, 3u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK(esc_stick_running(run));
    t.state = TONE_STATE_WAITING;            /* its reads gone stale */
    t.read = false;
    esc_stick_panel_tone(&t);
    CHECK(!esc_stick_running(run));
    CHECK_EQ(run->reason, ESC_STICK_R_TAP_STOP);
    scr->tick(0.02f);
    draws();
    bool red = false, green = true;
    esc_stick_panel_lights(&red, &green);
    CHECK(red && !green);
    tap_off();
}

int main(void)
{
    RUN(run_is_refused_with_no_beep_source);
    RUN(the_tap_counts_when_it_runs_and_the_meter_otherwise);
    RUN(the_readouts_beeps_reach_the_run_oldest_first);
    RUN(beeps_past_the_readout_end_the_run_beep_lost);
    RUN(a_tap_that_stops_ends_the_run_and_draws_its_reason);
    return test_summary("esc_stick_panel_source");
}
