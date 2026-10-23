/**
 * @file test_esc_stick_panel_phases.c
 * @brief ESC STICK's timing page and the drawing of a run: every phase, every end
 * and every refusal, and what a stop or a lost touch takes away.
 *
 * Cases:
 * - timing_steps_the_settings_and_closing_asks_for_a_save
 * - an_item_steps_from_keep_and_stops_at_its_ends
 * - every_phase_of_a_run_draws
 * - every_end_draws_its_reason
 * - a_run_that_cannot_start_says_why
 * - pages_step_back_and_leaving_closes_timing
 * - a_stop_or_a_lost_touch_takes_a_queued_arm_away
 * - the_screen_passes_the_supplys_own_state
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

TEST_CASE(timing_steps_the_settings_and_closing_asks_for_a_save)
{
    fresh();
    descend_to_hobbywing();
    tap(TIMING_X, TIMING_Y);
    tap(STEP_UP_X, STEP_CY(2));              /* BEEP MIN */
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    tap(STEP_UP_X, STEP_CY(0));              /* VOLTAGE off FROM PROFILE */
    CHECK_NEAR(settings_get(SET_STICK_V), 0.1f, 0.001f);
    for (int i = 0; i < 10; ++i) {
        tap(PAGE_DN_X, PAGE_CY);
    }
    tap(STEP_DN_X, STEP_CY(ST_PARM_ROWS - 1)); /* the last: HYSTERESIS */
    CHECK_EQ(settings_get_int(SET_STICK_HYST), 30);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    tap(READ_X, BTN_CY);                     /* DEFAULTS */
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 200);
    CHECK_EQ(settings_get_int(SET_STICK_HYST), 40);
    /* The defaults are the values loaded at start: nothing to write. */
    CHECK(!settings_dirty());
    tap(STEP_DN_X, STEP_CY(ST_PARM_ROWS - 1)); /* HYSTERESIS again */
    CHECK_EQ(settings_get_int(SET_STICK_HYST), 30);
    tap(WRITE_X, BTN_CY);                    /* CLOSE */
    CHECK(settings_save_asked());
    settings_cancel_save();
}

/* An item steps from KEEP through its values and stops at either end. */
TEST_CASE(an_item_steps_from_keep_and_stops_at_its_ends)
{
    fresh();
    descend_to_hobbywing();
    for (int i = 0; i < 5; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* brake: off, on */
    }
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK_EQ(run->n, 1);
    CHECK_EQ(run->ch[0].item, 0);
    CHECK_EQ(run->ch[0].value, 1);           /* the last value, "on" */
    scr->leave();

    fresh();
    descend_to_hobbywing();
    tap(STEP_UP_X, STEP_CY(0));
    for (int i = 0; i < 5; ++i) {
        tap(STEP_DN_X, STEP_CY(0));          /* back to KEEP, and no lower */
    }
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    /* Paging reaches item 8, and BACK climbs to the list. */
    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_DN_X, PAGE_CY);                 /* items 3 to 8 */
    tap(STEP_UP_X, STEP_CY(ST_PARM_ROWS - 1));
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 5000);
}

/* Run to the end, drawing the page at every phase it passes through;
 * returns the phases seen, one bit each. */
static unsigned run_drawing(rig_t *r, uint32_t ms)
{
    unsigned seen = 1u << esc_stick_panel_stick()->phase;
    int last = (int)esc_stick_panel_stick()->phase;
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    for (uint32_t i = 0; i < ms; ++i) {
        rig_step(r);
        const esc_stick_t *run = esc_stick_panel_stick();
        if ((int)run->phase != last) {
            last = (int)run->phase;
            seen |= 1u << run->phase;
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, (int)(i & 1u));
            if (lit() < 20000) {
                T_FAIL("phase %s drew only %d pixels",
                       esc_stick_phase_text(run->phase), lit());
            }
        }
        if (!esc_stick_running(run)) {
            break;
        }
    }
    return seen;
}

/* Every phase a run passes through draws: a two-stage menu's items and
 * values, and a one-stage menu's power cycle between its two changes. */
TEST_CASE(every_phase_of_a_run_draws)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    unsigned seen = run_drawing(&r, 300000u);
    const unsigned two = (1u << ESC_STICK_ARMING) | (1u << ESC_STICK_SIGNAL)
                         | (1u << ESC_STICK_POWER) | (1u << ESC_STICK_ENTRY)
                         | (1u << ESC_STICK_ITEMS) | (1u << ESC_STICK_VALUES)
                         | (1u << ESC_STICK_STORE) | (1u << ESC_STICK_DONE);
    CHECK_EQ(seen & two, two);

    /* sunrise-pro: two changes, one a power-up. */
    fresh();
    open_profile("sunrise-pro");
    CHECK_STR_EQ(esc_stick_panel_page()->id, "sunrise-pro");
    tap(STEP_UP_X, STEP_CY(0));
    tap(STEP_UP_X, STEP_CY(0));
    tap(STEP_UP_X, STEP_CY(0));              /* battery: LiFe */
    tap(STEP_UP_X, STEP_CY(1));              /* timing: automatic */
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    rig_start(&r);
    seen = run_drawing(&r, 300000u);
    CHECK(seen & (1u << ESC_STICK_CYCLE));
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 4);
}

/* Each way a run can end draws its result with its reason. */
TEST_CASE(every_end_draws_its_reason)
{
    static const esc_stick_reason_t want[] = {
        ESC_STICK_R_NOT_ARMED, ESC_STICK_R_NO_POWER, ESC_STICK_R_STALE,
        ESC_STICK_R_RATE, ESC_STICK_R_TIMEOUT, ESC_STICK_R_NO_BEEPS,
        ESC_STICK_R_HIGH, ESC_STICK_R_SUPPLY_OFF, ESC_STICK_R_SUPPLY_LOST,
        ESC_STICK_R_DISARMED, ESC_STICK_R_LINK,
    };
    for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); ++k) {
        fresh();
        settings_set(SET_STICK_TIMEOUT, 20000.0f);
        descend_to_hobbywing();
        if (want[k] == ESC_STICK_R_TIMEOUT) {
            /* Brake is item 1, reached only by the wrap from the item
             * the profile ends on; one item more and it never is. */
            tap(STEP_UP_X, STEP_CY(0));
        } else {
            pick_cutoff();
        }
        tap(WRITE_X, BTN_CY);
        static rig_t r;
        memset(&r, 0, sizeof(r));
        /* The link answers at the start for LINK LOST, and only then. */
        esc_stick_panel_bench(0u, false, 0u, 0u, want[k] == ESC_STICK_R_LINK);
        hold_for(2.25f);
        esc_sim_init(&r.sim, esc_stick_panel_stick()->p, NULL);
        switch (want[k]) {
        case ESC_STICK_R_NOT_ARMED: r.no_arm = true;          break;
        case ESC_STICK_R_NO_POWER:  r.no_power = true;        break;
        case ESC_STICK_R_TIMEOUT:   r.sim.c.extra = 1u;       break;
        case ESC_STICK_R_NO_BEEPS:  r.sim.c.mute = true;      break;
        case ESC_STICK_R_RATE:      r.every = 400u;           break;
        default: break;
        }
        for (int i = 0; i < 12000; ++i) {
            rig_step(&r);
        }
        switch (want[k]) {
        case ESC_STICK_R_STALE:       r.silent = true;          break;
        case ESC_STICK_R_HIGH:        r.extra_ma = 2000;        break;
        case ESC_STICK_R_SUPPLY_OFF:  r.report_off = true;      break;
        case ESC_STICK_R_SUPPLY_LOST: r.offline = true;         break;
        case ESC_STICK_R_DISARMED:    r.no_arm = true; r.armed = false; break;
        case ESC_STICK_R_LINK:        r.link_lost = true;       break;
        default: break;
        }
        rig_run(&r, 200000u);
        const esc_stick_t *run = esc_stick_panel_stick();
        if (run->reason != want[k]) {
            T_FAIL("case %u ended %s", (unsigned)k,
                   esc_stick_reason_text(run->reason));
        }
        CHECK(!r.on);
        /* Every one of these is an end nobody chose: red, until OK. */
        bool red = false, green = true;
        esc_stick_panel_lights(&red, &green);
        CHECK(red && !green);
        scr->tick(0.25f);
        esc_stick_panel_lights(&red, NULL);
        CHECK(red);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);
        CHECK(lit() > 20000);
        /* BACK from the result goes to the list. */
        tap(BACK_X, BACK_Y);
        esc_stick_panel_lights(&red, NULL);
        CHECK(!red);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 1);
        CHECK(lit() > 20000);
    }
}

/* What RUN says when it cannot start: a model the data vouches no voltage
 * for, a voltage over the cap, and a supply switched on in the middle of
 * the warning. */
TEST_CASE(a_run_that_cannot_start_says_why)
{
    /* No cell count and no voltage rating: the row opens with a warning;
     * RUN asks for VOLTAGE, and with one it runs, the warning on the
     * page. */
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Dualsky");
    int row = -1;
    for (int i = 0; i < esc_stick_panel_listed(NULL); ++i) {
        const esc_profile_t *q = esc_stick_panel_row(i, NULL);
        row = (q != NULL && strcmp(q->id, "dualsky-xcontroller") == 0)
                  ? i : row;
    }
    CHECK(esc_stick_panel_row_why(row) == NULL);
    const char *warn = esc_stick_panel_row_warn(row);
    CHECK(warn != NULL && strcmp(warn, "ESC voltage rating unknown.") == 0);
    open_model("dualsky-xcontroller", NULL);
    CHECK(esc_stick_panel_page() != NULL);
    tap(STEP_UP_X, STEP_CY(0));
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    CHECK_STR_EQ(esc_stick_panel_note(),
                 "Profile has no cell count. Set VOLTAGE on the TIMING page.");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    settings_set(SET_STICK_V, 7.4f);
    scr->tick(0.02f);
    CHECK_STR_EQ(esc_stick_panel_note(),
                 "VOLTAGE 7.4 V: check the ESC's voltage rating. It is unknown.");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    scr->leave();
    settings_set(SET_STICK_V, 0.0f);

    /* VOLTAGE over the cap, set where a profile ran: refused on RUN. */
    fresh();
    descend_to_hobbywing();
    tap(STEP_UP_X, STEP_CY(0));
    settings_set(SET_STICK_V, 20.0f);
    settings_set(SET_SUPPLY_V_MAX, 12.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    /* And the list pages back as well as forward. */
    tap(BACK_X, BACK_Y);
    tap(LIST_DN_X, LIST_CY);
    tap(LIST_DN_X, LIST_CY);
    tap(LIST_UP_X, LIST_CY);                 /* ^: back a page */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);

    /* The supply switched on while the warning is up: the hold completes
     * and the run is still refused. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    supply_screen_ask_on(SUPPLY_BY_SERVO);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
}

/* The pages step back as well as forward, and leaving with TIMING open
 * closes it and asks for the save. */
TEST_CASE(pages_step_back_and_leaving_closes_timing)
{
    fresh();
    descend_to_hobbywing();
    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_UP_X, PAGE_CY);
    tap(TIMING_X, TIMING_Y);
    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_UP_X, PAGE_CY);
    tap(80, STEP_CY(1));                     /* a row's name: its help */
    tap(STEP_UP_X, STEP_CY(1));
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
    scr->leave();
    CHECK(settings_save_asked());
    settings_cancel_save();
    /* Leaving closed the session; opened again it starts on the makers,
     * and the profile's page comes back with TIMING closed. */
    CHECK_EQ(esc_stick_panel_stage(), 0);
    scr->enter();
    open_stick();
    CHECK_EQ(esc_stick_panel_level(), 0);
    CHECK(esc_stick_panel_page() == NULL);
    open_maker("Hobbywing");
    open_model("hobbywing-flyfun-8item", NULL);
    tap(80, STEP_CY(0));
    CHECK_EQ(esc_stick_panel_item_at(0) >= 0, true);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/*
 * The hold completes in the frame's tick, after the frame handed its
 * commands over, so its ARM waits a frame in the screen.  A stop or a
 * touch loss in between must take it away: the next frame would stamp it
 * with a fresh stop count and touch number, and it would clear the stop.
 * Both orderings: the interruption after the hold completed, and during
 * it.
 */
TEST_CASE(a_stop_or_a_lost_touch_takes_a_queued_arm_away)
{
    for (int k = 0; k < 2; ++k) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);                     /* ARM queued, not taken */
        CHECK_EQ(esc_stick_panel_runs(), 1u);
        if (k == 0) {
            esc_stick_panel_bench(10u, false, 1u, 1u, false);   /* STOP */
        } else {
            scr->cancel();                   /* touch events lost */
        }
        motor_cmd_t mc;
        int arms = 0;
        while (esc_stick_panel_poll_cmd(&mc)) {
            arms += (mc.kind == MOTOR_CMD_ARM) ? 1 : 0;
        }
        CHECK_EQ(arms, 0);
        const esc_stick_t *run = esc_stick_panel_stick();
        CHECK(!esc_stick_running(run));
        CHECK_EQ(run->reason, (k == 0) ? ESC_STICK_R_STOP : ESC_STICK_R_TOUCH);
        CHECK(!supply_screen_output_live());
    }

    /* The ARM already taken, the bench not yet armed: a lost touch still
     * ends the run, with a DISARM. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(drain_cmds(MOTOR_CMD_ARM), 1);
    scr->cancel();
    CHECK_EQ(esc_stick_panel_stick()->reason, ESC_STICK_R_TOUCH);
    CHECK_EQ(drain_cmds(MOTOR_CMD_DISARM), 1);

    /* Armed and running, a lost touch is the arm watch's to judge. */
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    for (int i = 0; i < 3000; ++i) {
        rig_step(&r);
    }
    scr->cancel();
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    scr->leave();
}

/* The reviewers' proof through the screen: the SUPPLY flag goes off with
 * the request, the module 300 ms later.  The run waits for the module. */
TEST_CASE(the_screen_passes_the_supplys_own_state)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    r.off_lag = 300u;
    rig_run(&r, 300000u);
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);
    CHECK_EQ(r.moved_while_on, 0u);
    CHECK(!r.module_on);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
}

int main(void)
{
    RUN(timing_steps_the_settings_and_closing_asks_for_a_save);
    RUN(an_item_steps_from_keep_and_stops_at_its_ends);
    RUN(every_phase_of_a_run_draws);
    RUN(every_end_draws_its_reason);
    RUN(a_run_that_cannot_start_says_why);
    RUN(pages_step_back_and_leaving_closes_timing);
    RUN(a_stop_or_a_lost_touch_takes_a_queued_arm_away);
    RUN(the_screen_passes_the_supplys_own_state);
    return test_summary("esc_stick_panel_phases");
}
