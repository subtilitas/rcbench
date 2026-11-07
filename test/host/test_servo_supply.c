/**
 * @file test_servo_supply.c
 * @brief What the servo screen does with the supply's output and set points when a
 * run ends, the screen is left or a hold is asked.
 *
 * Cases:
 * - leaving_servo_switches_off_an_on_a_run_or_the_hv_hold_asked
 * - a_run_ending_under_arms_steps_switches_nothing_on_past_6_v
 * - a_set_point_raised_past_6_v_under_arms_hold_switches_nothing_on
 * - the_set_points_go_back_whichever_screen_is_up
 * - the_set_points_wait_for_a_sample_after_the_off
 * - a_supply_reading_stamped_on_tick_zero_keeps_its_stamp
 * - a_hold_on_supplys_output_on_holds_the_restore_back
 * - settings_opens_nothing_under_arms_hold
 *
 * SPDX-License-Identifier: MIT
 */

/* LIVE's screen: no SET row and no OUTPUT ON; its READY rows' PD MINI is
 * switched on by ARM's steps, or past 6.0V by the HV warning a tap on the
 * row opens. */
#include "servo_screen_rig.h"
#include "servo_ready_int.h"
#include "supply_spots.h"

/* ------------------------------------------- the review's cases, as tests */

/* As bench_frames(), with ONs from the screen let through only while
 * @p accept_on, and the highest set point an ON was asked at kept. */
static void bench_frames_on(uint32_t ms, bool accept_on, float *on_v_max)
{
    for (uint32_t k = 0; k < ms; k += 20u) {
        bench.now += 20u;
        servo_screen_clock(bench.now);
        supply_cmd_t c;
        while (supply_screen_poll_cmd(&c)) {
            if (c.off) {
                bench.out = false;
            } else if (c.on) {
                if (supply_screen_set_v() > *on_v_max) {
                    *on_v_max = supply_screen_set_v();
                }
                if (accept_on) {
                    bench.out = true;
                }
            }
        }
        supply_screen_set_output(bench.out);
        servo_cmd_t sc;
        while (servo_screen_take(&sc)) { }
        if ((int32_t)(bench.now - bench.next) >= 0) {
            bench.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = true;
            st.ok     = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = bench.out;
            st.set_v  = supply_screen_set_v();
            st.set_i  = supply_screen_set_i();
            st.v      = bench.out ? st.set_v : 0.0f;
            st.mode   = bench.out ? SUPPLY_MODE_CV : SUPPLY_MODE_OFF;
            st.samples  = ++bench.samples;
            st.taken_ms = bench.now;
            st.stamped = true;
            servo_screen_supply(&st);
        }
        servo_screen_service();
        scr->tick(0.02f);
        bench_drain();
    }
}

/** @brief ARM on LIVE pressed, @p ms of bench frames under it with ONs let
 *         through, and lifted. */
static void arm_held_on(uint32_t ms, float *on_v)
{
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames_on(ms, true, on_v);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
}

/*
 * 8.4V set on SUPPLY with the output off; a run without HV steps starts
 * and asks 4.8V and its ON, which does not land.  The run ends as SERVO is
 * left, and its set points go back to 8.4V once a sample shows the output
 * off with no ON on its way.  LIVE shown again, ARM held there: the PD
 * MINI row reads past 6.0V, so no ON is ever asked above 6.0V and the
 * output is never on at a set point past 6.0V.
 */
TEST_CASE(a_run_ending_under_arms_steps_switches_nothing_on_past_6_v)
{
    bench_fresh();
    short_runs();
    supply_screen_put(8.4f, 1.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    float on_v = 0.0f;
    bench_frames_on(1500u, false, &on_v);          /* the ON does not land */
    on_v = 0.0f;
    scr->leave();                                  /* ends the run */
    CHECK(!servo_screen_testing());
    go(SERVO_FN_LIVE);
    scr->enter();
    arm_held_on(2800u, &on_v);
    CHECK(on_v <= 6.0f + 1e-4f);
    CHECK(!(bench.out && supply_screen_set_v() > 6.0f + 1e-4f));
    bench_frames_on(2000u, true, &on_v);
    CHECK(on_v <= 6.0f + 1e-4f);
    CHECK(!(bench.out && supply_screen_set_v() > 6.0f + 1e-4f));
    CHECK(fabsf(supply_screen_set_v() - 8.4f) < 1e-4f);
    char want[128], got[128];
    snprintf(want, sizeof(want), TR(RDY_BAND), "PD MINI", TR(SV_TAP_HV));
    const char *why = ready_refusal(got, sizeof(got));
    CHECK_STR_EQ(why != NULL ? why : "(null)", want);
}

/*
 * The ON a run asks for, and the ON the HV warning's hold sends from LIVE's
 * PD MINI row, belong to SERVO as ARM's steps' ON does: leaving SERVO
 * switches each off.
 */
TEST_CASE(leaving_servo_switches_off_an_on_a_run_or_the_hv_hold_asked)
{
    supply_cmd_t c;

    /* A run's ON. */
    bench_fresh();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    CHECK(supply_cmd(&c) && c.on);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_set_output(true);
    scr->leave();
    bool off = false;
    while (supply_cmd(&c)) {
        off = off || c.off;
    }
    CHECK(off);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);

    /* The HV warning's ON, opened by a tap on the red PD MINI row. */
    bench_fresh();
    supply_screen_put(7.4f, 1.0f);
    bench.out = false;
    bench_frames(200u);
    while (supply_cmd(&c)) { }
    tap(SV_RDY_X, SV_DRV_ROW_Y(1));
    CHECK(servo_screen_output_held());
    scr->tick(0.025f);
    hold_apply(2.3f);
    CHECK(supply_cmd(&c) && c.on);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_set_output(true);
    scr->leave();
    CHECK(supply_cmd(&c));
    CHECK(c.off && !c.on);
}

/* A set point raised past 6.0V on SUPPLY during ARM's hold, with the PD
 * mini off: the PD MINI row turns red under the finger, the hold is
 * dropped, and nothing goes on when it would have completed; the row names
 * the HV warning. */
TEST_CASE(a_set_point_raised_past_6_v_under_arms_hold_switches_nothing_on)
{
    bench_fresh();
    supply_screen_put(5.0f, 1.0f);
    bench.out = false;
    bench_frames(200u);
    servo_screen_set_armed(false);
    supply_cmd_t c;
    while (supply_cmd(&c)) { }
    CHECK(servo_screen_arm_switches());
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(0.025f);
    }
    supply_screen_put(8.4f, 1.0f);
    for (int i = 0; i < 80; ++i) {
        scr->tick(0.025f);
    }
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(0.025f);
    }
    CHECK(!supply_cmd(NULL));
    servo_cmd_t sc;
    bool armed = false;
    while (servo_screen_take(&sc)) {
        armed = armed || sc.kind == SERVO_CMD_ARM;
    }
    CHECK(!armed);
    char want[128], got[128];
    snprintf(want, sizeof(want), TR(RDY_BAND), "PD MINI", TR(SV_TAP_HV));
    const char *why = ready_refusal(got, sizeof(got));
    CHECK_STR_EQ(why != NULL ? why : "(null)", want);
}

/* A run ended by leaving SERVO puts the set points back from the per-frame
 * service, with SERVO not ticked; one set on SUPPLY after the run ended is
 * left as the operator set it. */
TEST_CASE(the_set_points_go_back_whichever_screen_is_up)
{
    for (int changed = 0; changed < 2; ++changed) {
        bench_fresh();
        short_runs();
        supply_screen_put(5.5f, 1.5f);
        hold_start(2.3f);
        bench_frames(3000u);
        CHECK(bench.out);
        scr->leave();
        CHECK(!servo_screen_testing());
        if (changed) {
            supply_screen_put(5.2f, 1.0f);              /* on SUPPLY */
        }
        float on_v = 0.0f;
        for (uint32_t k = 0; k < 2000u; k += 20u) {    /* SERVO not ticked */
            bench.now += 20u;
            servo_screen_clock(bench.now);
            supply_cmd_t c;
            while (supply_screen_poll_cmd(&c)) {
                bench.out = c.off ? false : (c.on ? true : bench.out);
            }
            supply_screen_set_output(bench.out);
            if ((int32_t)(bench.now - bench.next) >= 0) {
                bench.next += 100u;
                supply_state_t st;
                memset(&st, 0, sizeof(st));
                st.online = true;
                st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
                st.output = bench.out;
                st.set_v = supply_screen_set_v();
                st.samples = ++bench.samples;
                st.taken_ms = bench.now;
                st.stamped = true;
                servo_screen_supply(&st);
            }
            servo_screen_service();
            bench_drain();
        }
        CHECK(!bench.out);
        if (changed) {
            CHECK(fabsf(supply_screen_set_v() - 5.2f) < 1e-4f);
            CHECK(fabsf(supply_screen_set_i() - 1.0f) < 1e-4f);
        } else {
            CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
            CHECK(fabsf(supply_screen_set_i() - 1.5f) < 1e-4f);
        }
        /* Back on SERVO, nothing is put back over it later. */
        supply_screen_put(5.1f, 1.0f);
        bench_frames_on(1000u, true, &on_v);
        CHECK(fabsf(supply_screen_set_v() - 5.1f) < 1e-4f);
    }
}

/* The restore waits for a sample taken after the OFF went: one taken
 * before it, showing the output off while an ON is on its way, is not
 * enough. */
TEST_CASE(the_set_points_wait_for_a_sample_after_the_off)
{
    bench_fresh();
    short_runs();
    supply_screen_put(8.4f, 1.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    servo_screen_cancel_arm();                  /* STOP: OFF posted */
    CHECK(!servo_screen_testing());
    supply_cmd_t c;
    while (supply_screen_poll_cmd(&c)) { }       /* the OFF taken */
    servo_screen_service();
    /* A sample stamped before the next frame, showing off. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.set_v = supply_screen_set_v();
    st.samples = ++bench.samples;
    st.taken_ms = bench.now;
    st.stamped = true;
    servo_screen_supply(&st);
    servo_screen_service();
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);   /* not yet */
    /* The next frame and a sample of its own, but the module still reads
     * its output on: the panel asked it off and the PD mini has not yet
     * answered.  Not yet either. */
    bench.now += 20u;
    servo_screen_clock(bench.now);
    servo_screen_service();
    st.samples = ++bench.samples;
    st.taken_ms = bench.now;
    st.stamped = true;
    st.mode = SUPPLY_MODE_CV;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    /* Nor from a supply that has stopped answering. */
    st.samples = ++bench.samples;
    st.mode = SUPPLY_MODE_OFF;
    st.online = false;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    /* The module's own off: back. */
    st.samples = ++bench.samples;
    st.online = true;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 8.4f) < 1e-4f);
}

/* A reading stamped on tick 0 is stamped.  The restore compares the
 * stamp with the clock at the stage's start: a stamp of 0 older than that
 * is a sample from before the OFF and is not enough, and one newer, across
 * the 32-bit wrap, is. */
TEST_CASE(a_supply_reading_stamped_on_tick_zero_keeps_its_stamp)
{
    static const struct { uint32_t base; bool restores; } k_case[] = {
        { 0u,           false },    /* restore_ms 20: stamp 0 is older     */
        { 0xFFFFFFF0u,  false },    /* restore_ms 4, past the wrap         */
        { 0xFFFFFFE0u,  true },     /* restore_ms -12: stamp 0 is newer    */
    };
    for (size_t k = 0; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        bench_fresh();
        short_runs();
        supply_screen_put(8.4f, 1.0f);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        servo_screen_cancel_arm();
        supply_cmd_t c;
        while (supply_screen_poll_cmd(&c)) { }
        servo_screen_clock(k_case[k].base);
        servo_screen_service();                 /* OFF sent                */
        servo_screen_clock(k_case[k].base + 20u);
        servo_screen_service();                 /* waiting for a sample    */
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.online   = true;
        st.ok       = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        st.set_v    = supply_screen_set_v();
        st.mode     = SUPPLY_MODE_OFF;
        st.samples  = ++bench.samples;
        st.taken_ms = 0u;
        st.stamped  = true;
        servo_screen_supply(&st);
        servo_screen_service();
        servo_screen_service();
        const bool back = fabsf(supply_screen_set_v() - 8.4f) < 1e-4f;
        CHECK_EQ(back, k_case[k].restores);
    }
}

/* OUTPUT ON held on SUPPLY holds the restore back as a hold on SERVO does:
 * the ON lands at the set point shown at the press. */
TEST_CASE(a_hold_on_supplys_output_on_holds_the_restore_back)
{
    bench_fresh();
    short_runs();
    supply_screen_put(5.5f, 1.5f);
    hold_start(2.3f);
    bench_frames(3000u);
    scr->leave();                                /* ends the run */
    CHECK(!servo_screen_testing());
    const float v_end = supply_screen_set_v();
    /* The OFF goes; then, on SUPPLY, OUTPUT ON pressed (the CONTROL card's
     * foot, supply_spots.h) before a sample shows the output off. */
    supply_cmd_t c;
    while (supply_screen_poll_cmd(&c)) {
        bench.out = c.off ? false : (c.on ? true : bench.out);
    }
    supply_screen_set_output(bench.out);
    const ui_screen_t *sup = supply_screen();
    const touch_event_t down = { .type = TOUCH_EVENT_DOWN,
                                 .point = { .id = 1, .x = SU_OUT_X,
                                            .y = SU_OUT_Y, .strength = 40 } };
    sup->event(&down);
    CHECK(supply_screen_output_held());
    float on_v = 0.0f;
    for (uint32_t k = 0; k < 600u; k += 20u) {   /* samples, all off */
        bench.now += 20u;
        servo_screen_clock(bench.now);
        while (supply_screen_poll_cmd(&c)) {
            bench.out = c.off ? false : (c.on ? true : bench.out);
        }
        supply_screen_set_output(bench.out);
        if ((int32_t)(bench.now - bench.next) >= 0) {
            bench.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = true;
            st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = bench.out;
            st.set_v = supply_screen_set_v();
            st.samples = ++bench.samples;
            st.taken_ms = bench.now;
            st.stamped = true;
            servo_screen_supply(&st);
        }
        servo_screen_service();
        bench_drain();
    }
    CHECK(fabsf(supply_screen_set_v() - v_end) < 1e-4f);    /* held back */
    const touch_event_t up = { .type = TOUCH_EVENT_UP,
                               .point = { .id = 1, .x = SU_OUT_X,
                                          .y = SU_OUT_Y, .strength = 40 } };
    sup->event(&up);                             /* let go early: no ON */
    CHECK(!supply_screen_output_held());
    bench_frames_on(600u, true, &on_v);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
    CHECK_EQ(on_v, 0.0f);
}

/* A second finger on SETTINGS while ARM's 2s hold fills on SWEEP, whose
 * steps would switch the PD mini on: the list stays shut, so the hold's
 * own release still ends it and nothing goes on when the finger lifts
 * early. */
TEST_CASE(settings_opens_nothing_under_arms_hold)
{
    bench_fresh();
    supply_screen_put(5.0f, 1.0f);
    bench.out = false;
    bench_frames(200u);
    servo_screen_set_armed(false);
    go(SERVO_FN_SWEEP);
    scr->tick(0.025f);
    supply_cmd_t c;
    while (supply_cmd(&c)) { }
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 20; ++i) {
        scr->tick(0.025f);                      /* 0.5s held */
    }
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!servo_screen_list()->open);
    CHECK_EQ(servo_screen_run_settings(), 0);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.025f);                      /* 2.5s more */
    }
    bool on = false;
    while (supply_cmd(&c)) {
        on = on || c.on;
    }
    CHECK(!on);
    servo_cmd_t sc;
    bool armed = false;
    while (servo_screen_take(&sc)) {
        armed = armed || sc.kind == SERVO_CMD_ARM;
    }
    CHECK(!armed);
}

int main(void)
{
    RUN(leaving_servo_switches_off_an_on_a_run_or_the_hv_hold_asked);
    RUN(a_run_ending_under_arms_steps_switches_nothing_on_past_6_v);
    RUN(a_set_point_raised_past_6_v_under_arms_hold_switches_nothing_on);
    RUN(the_set_points_go_back_whichever_screen_is_up);
    RUN(the_set_points_wait_for_a_sample_after_the_off);
    RUN(a_supply_reading_stamped_on_tick_zero_keeps_its_stamp);
    RUN(a_hold_on_supplys_output_on_holds_the_restore_back);
    RUN(settings_opens_nothing_under_arms_hold);
    return test_summary("servo_supply");
}
