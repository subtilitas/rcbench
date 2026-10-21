/**
 * @file test_esc_stick_panel.c
 * @brief ESC STICK, the session MOTOR & ESC > PROGRAM opens: that a run starts
 * only from the warning held for its two seconds, that it is refused
 * rather than adjusted when the supply cannot give what it needs, and that
 * leaving, ABORT and STOP each end it with the bench let go.  The other
 * cases of the session are in test_esc_stick_panel_*.c.
 *
 * Cases:
 * - a_profile_the_engine_cannot_run_goes_no_further
 * - no_run_starts_while_the_servo_programmer_holds_a_port
 * - no_run_starts_without_a_throttle_pin_a_read_binding_or_a_supply
 * - a_bench_that_loses_its_throttle_closes_the_warning
 * - a_run_needs_a_change_and_the_warning_held_two_seconds
 * - a_hold_that_is_interrupted_starts_nothing
 * - a_whole_run_on_the_modelled_bench_stores_and_lets_go
 * - leaving_abort_and_stop_end_a_run_and_let_go
 * - leaving_switches_off_a_supply_the_screen_switched_on_after_its_run
 * - a_run_the_supply_cannot_give_is_refused
 * - a_voltage_equal_to_the_supplys_cap_or_minimum_runs
 * - a_profile_at_the_supplys_cap_opens_from_the_list
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* The list puts what can run first; a row that cannot run says why and goes
 * no further. */
TEST_CASE(a_profile_the_engine_cannot_run_goes_no_further)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Castle Creations");          /* a yes/no menu: cannot run */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(esc_stick_panel_page() == NULL);
    tap(WRITE_X, BTN_CY);                    /* where RUN would be */
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    CHECK_EQ(drain_cmds(MOTOR_CMD_ARM), 0);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/* The servo programmer's port as the IOmcu's page gives it. */
static void kst_port(link_kst_state_t st)
{
    kst_prog_feed_t f;

    memset(&f, 0, sizeof(f));
    f.rd.phase = KST_LINK_READY;
    f.rd.known = true;
    f.rd.port = st;
    kst_panel_feed(&f);
}

/* A run arms the bench: none starts over a port in programming. */
TEST_CASE(no_run_starts_while_the_servo_programmer_holds_a_port)
{
    const gfx_rect_t area = { 0, 0, W, H };
    motor_cmd_t mc;

    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        kst_panel_init(area);
        kst_port(LINK_KST_PROGRAMMING);
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        CHECK_EQ(esc_stick_panel_runs(), 0u);
        CHECK(!esc_stick_running(esc_stick_panel_stick()));
        CHECK(!esc_stick_panel_poll_cmd(&mc));
        CHECK_STR_EQ(esc_stick_panel_note(), TR(SP_KST_HELD));
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);
        CHECK(lit() > 20000);
        /* The port back in PWM: the same RUN starts. */
        kst_port(LINK_KST_PWM);
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        CHECK_EQ(esc_stick_panel_runs(), 1u);
        scr->leave();
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * RUN moves the throttle pins and powers the ESC from the supply: with the
 * supply the panel's model, no binding read, or no pin bound as a
 * throttle, none starts and the note names which.  Each reason put right,
 * the same RUN starts.
 */
TEST_CASE(no_run_starts_without_a_throttle_pin_a_read_binding_or_a_supply)
{
    enum { R_MODEL, R_NOT_READ, R_NO_THROTTLE, R_COUNT };
    /* Inside RUN, left of its label: the button's body. */
    const int run_px = (BTN_CY * W) + WRITE_X - 76;
    static gfx_color_t before[W * H];
    motor_cmd_t mc;

    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        for (int k = 0; k < R_COUNT; ++k) {
            fresh();
            ui_text_set_language((ui_lang_t)lang);
            const char *want = NULL;
            if (k == R_MODEL) {
                supply_screen_set_model(true);
                want = TR(SP_SUPPLY_MODEL);
            } else if (k == R_NOT_READ) {
                /* A mask with no read behind it is no throttle known. */
                esc_stick_panel_set_throttle(false, 0x01u);
                want = TR(SP_BIND_NOT_READ);
            } else {
                esc_stick_panel_set_throttle(true, 0x00u);
                want = TR(SP_NO_THROTTLE);
            }
            descend_to_hobbywing();
            pick_cutoff();
            /* RUN is drawn as taking no press, and a tap on it opens no
             * warning, before any tick could close one: the frame after
             * the tap is the frame before it, with the note set. */
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, 0);
            CHECK_EQ(fb[run_px], GFX_RGB(38, 46, 58));
            memcpy(before, fb, sizeof(before));
            tap(WRITE_X, BTN_CY);
            CHECK_STR_EQ(esc_stick_panel_note(), want);
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, 0);
            CHECK(memcmp(before, fb, sizeof(before)) == 0);
            hold_for(2.25f);
            CHECK_EQ(esc_stick_panel_runs(), 0u);
            CHECK(!esc_stick_running(esc_stick_panel_stick()));
            CHECK(!esc_stick_panel_poll_cmd(&mc));
            CHECK_STR_EQ(esc_stick_panel_note(), want);
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, 0);
            CHECK(lit() > 20000);
            /* Put right: the same RUN starts. */
            supply_screen_set_model(false);
            esc_stick_panel_set_throttle(true, 0x80u);   /* channel 7 */
            scr->tick(0.02f);
            CHECK_STR_EQ(esc_stick_panel_note(), "");
            /* RUN takes a press again, drawn in the colour of danger. */
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, 0);
            CHECK_EQ(fb[run_px], ui_theme_color(UI_C_DANGER));
            tap(WRITE_X, BTN_CY);
            hold_for(2.25f);
            CHECK_EQ(esc_stick_panel_runs(), 1u);
            scr->leave();
            supply_screen_set_model(false);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The warning open, and the bench stops being able to carry a run: the
 * warning closes and the note says why.  Put right again, the next hold
 * at HOLD TO RUN is no run: RUN has to be pressed anew.
 */
TEST_CASE(a_bench_that_loses_its_throttle_closes_the_warning)
{
    for (int k = 0; k < 3; ++k) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);                /* RUN: the warning */
        press(TOUCH_EVENT_DOWN, HOLD_X, HOLD_Y);
        scr->tick(0.25f);                    /* a hold under way */
        if (k == 0) {
            supply_screen_set_model(true);
        } else if (k == 1) {
            esc_stick_panel_set_throttle(false, 0x00u);
        } else {
            esc_stick_panel_set_throttle(true, 0x00u);
        }
        scr->tick(0.02f);
        press(TOUCH_EVENT_UP, HOLD_X, HOLD_Y);
        CHECK_STR_EQ(esc_stick_panel_note(),
                     k == 0 ? TR(SP_SUPPLY_MODEL)
                     : k == 1 ? TR(SP_BIND_NOT_READ) : TR(SP_NO_THROTTLE));
        supply_screen_set_model(false);
        esc_stick_panel_set_throttle(true, 0x01u);
        scr->tick(0.02f);
        hold_for(2.25f);
        if (esc_stick_panel_runs() != 0u) {
            T_FAIL("reason %d left the warning open", k);
        }
        /* RUN again opens it, and the hold starts the run. */
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        CHECK_EQ(esc_stick_panel_runs(), 1u);
        scr->leave();
    }
}

TEST_CASE(a_run_needs_a_change_and_the_warning_held_two_seconds)
{
    fresh();
    descend_to_hobbywing();
    tap(WRITE_X, BTN_CY);                    /* nothing picked: no RUN */
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);

    pick_cutoff();
    tap(WRITE_X, BTN_CY);                    /* RUN: the warning */
    hold_for(1.5f);                          /* let go early */
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    tap(CANCEL_X, HOLD_Y);                   /* and closed */
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);

    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    /* It begins by arming, with the throttle at rest and the supply off. */
    motor_cmd_t mc;
    CHECK(esc_stick_panel_poll_cmd(&mc));
    CHECK_EQ(mc.kind, MOTOR_CMD_ARM);
    CHECK(!esc_stick_panel_poll_cmd(&mc));
    CHECK(!supply_screen_output_live());
}

/* The hold is the finger on the button: off it, touch lost, or a STOP, and
 * the hold is gone; a new one is a new decision. */
TEST_CASE(a_hold_that_is_interrupted_starts_nothing)
{
    for (int k = 0; k < 3; ++k) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);
        press(TOUCH_EVENT_DOWN, HOLD_X, HOLD_Y);
        scr->tick(0.25f);
        scr->tick(0.25f);
        if (k == 0) {
            press(TOUCH_EVENT_MOVE, HOLD_X, 100);   /* off the button */
        } else if (k == 1) {
            scr->cancel();                          /* touch went missing */
        } else {
            esc_stick_panel_bench(0u, false, 1u, 1u, false);  /* STOP */
        }
        for (int i = 0; i < 12; ++i) {
            scr->tick(0.25f);
        }
        press(TOUCH_EVENT_UP, HOLD_X, HOLD_Y);
        if (esc_stick_panel_runs() != 0u) {
            T_FAIL("interruption %d still started a run", k);
        }
    }
}

TEST_CASE(a_whole_run_on_the_modelled_bench_stores_and_lets_go)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    rig_run(&r, 300000u);
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
    CHECK_EQ(r.sim.stores, 1u);
    CHECK_EQ(r.arms, 1);
    CHECK_EQ(r.disarms, 1);
    CHECK(!r.armed);
    CHECK(!r.on);
    CHECK(r.pct == 0.0f);
    /* The supply ran at the profile's two cells. */
    CHECK_NEAR(supply_screen_set_v(), 7.6f, 0.001f);
    CHECK(esc_stick_panel_profile() == NULL);

    /* The result stands until OK, and the menu comes back after it. */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int result = lit();
    tap(WRITE_X, BTN_CY);                    /* OK */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    if (lit() == result) {
        T_FAIL("OK left the result showing");
    }
}

/* Leaving the screen, ABORT and STOP each end a run with the bench let go:
 * a DISARM, and the supply's OFF. */
TEST_CASE(leaving_abort_and_stop_end_a_run_and_let_go)
{
    for (int k = 0; k < 4; ++k) {
        fresh();
        descend_to_hobbywing();
        pick_cutoff();
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        static rig_t r;
        rig_start(&r);
        for (int i = 0; i < 12000; ++i) {
            rig_step(&r);                    /* powered, in the menu */
        }
        CHECK(r.on && r.armed);
        /* The supply the run switched on is ESC STICK's. */
        CHECK_EQ(supply_screen_owner(), SUPPLY_BY_STICK);
        CHECK(esc_stick_panel_profile() != NULL);
        esc_stick_reason_t want = ESC_STICK_R_LEFT;
        if (k == 0) {
            scr->leave();
        } else if (k == 1) {
            tap(BACK_X, BACK_Y);             /* not while it runs */
            CHECK(esc_stick_running(esc_stick_panel_stick()));
            tap(WRITE_X, BTN_CY);            /* ABORT */
            want = ESC_STICK_R_USER;
        } else if (k == 2) {
            r.stops++;                       /* STOP pressed */
            r.pressed++;
            want = ESC_STICK_R_STOP;
        } else {
            r.stops++;                       /* the bench's own: touch */
            want = ESC_STICK_R_BENCH_STOP;
        }
        rig_run(&r, 1000u);
        const esc_stick_t *run = esc_stick_panel_stick();
        CHECK_EQ(run->phase, ESC_STICK_ABORTED);
        CHECK_EQ(run->reason, want);
        /* An end the operator chose lights no red; the bench's own does. */
        bool red = true, green = true;
        esc_stick_panel_lights(&red, &green);
        CHECK_EQ(red, want == ESC_STICK_R_BENCH_STOP);
        CHECK(!green);
        CHECK(!r.armed);
        CHECK(!r.on);
        CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
        CHECK_EQ(r.sim.stores, 0u);
        /* Leaving closes the session: PROGRAM's list shows.  Else the
         * result stays on the page. */
        CHECK_EQ(esc_stick_panel_stage() == 0, k == 0);
        if (k != 0) {
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, 0);
            CHECK(lit() > 20000);
        }
    }
}

/* The backstop in the leave: a supply ESC STICK switched on that no run
 * owns any more -- the run is over, the output is not -- goes off with
 * the screen. */
TEST_CASE(leaving_switches_off_a_supply_the_screen_switched_on_after_its_run)
{
    fresh();
    supply_screen_ask_on(SUPPLY_BY_STICK);
    supply_cmd_t sc;
    CHECK(supply_screen_poll_cmd(&sc) && sc.on);
    supply_screen_set_output(true);
    CHECK(!esc_stick_running(esc_stick_panel_stick()));
    scr->leave();
    CHECK(supply_screen_poll_cmd(&sc));
    CHECK(sc.off && !sc.on);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
}

/* Refused, not adjusted: a voltage over the SUPPLY cap, or a live output,
 * keeps RUN from opening the warning. */
TEST_CASE(a_run_the_supply_cannot_give_is_refused)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 5.0f);
    supply_screen_limits_changed();
    descend_to_hobbywing();
    pick_cutoff();
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);

    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    supply_screen_ask_on(SUPPLY_BY_SERVO);  /* the output on its way */
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);

    /* A current limit over the SUPPLY cap. */
    fresh();
    settings_set(SET_STICK_I, 3.0f);
    settings_set(SET_SUPPLY_I_MAX, 2.0f);
    supply_screen_limits_changed();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(3.0f);
    CHECK_EQ(esc_stick_panel_runs(), 0u);
}

/* The supply's own range, in place of the PPS default's 3.3 to 21 V. */
static void supply_gives(float v_min, float v_max)
{
    supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    caps.v_min = v_min;
    caps.v_max = v_max;
    supply_screen_set_caps(&caps);
}

/* RUN held on the profile page: whether a run started. */
static bool run_starts(void)
{
    const uint32_t before = esc_stick_panel_runs();
    scr->tick(0.02f);
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    return esc_stick_panel_runs() == before + 1u;
}

/*
 * A voltage the supply can give is not refused: the profile's 7.6 V runs on
 * a supply whose highest voltage is 7.6 V and on one whose lowest is 7.6 V.
 * 0.01 V the other side of either is refused, with the reason on the page.
 */
TEST_CASE(a_voltage_equal_to_the_supplys_cap_or_minimum_runs)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    supply_gives(3.3f, 7.6f);
    CHECK(run_starts());
    CHECK_EQ(esc_stick_panel_stick()->out.supply_mv, 7600u);

    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    supply_gives(3.3f, 7.59f);
    CHECK(!run_starts());
    CHECK_STR_EQ(esc_stick_panel_note(),
                 "7.60 V exceeds the SUPPLY limit of 7.59 V.");

    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    supply_gives(7.6f, 21.0f);
    CHECK(run_starts());

    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    supply_gives(7.61f, 21.0f);
    CHECK(!run_starts());
    CHECK_STR_EQ(esc_stick_panel_note(),
                 "VOLTAGE is below the supply minimum of 7.61 V.");
}

/* The list the same: a profile that needs 7.6 V opens on a supply whose
 * highest voltage is 7.6 V, and 0.01 V under it the row is a refused one
 * that says what it needs. */
TEST_CASE(a_profile_at_the_supplys_cap_opens_from_the_list)
{
    fresh();
    supply_gives(3.3f, 7.6f);
    descend_to_hobbywing();
    CHECK(esc_stick_panel_page() != NULL);

    fresh();
    supply_gives(3.3f, 7.59f);
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    open_maker("Hobbywing");
    bool found = false;
    for (int i = 0; i < esc_stick_panel_listed(NULL); ++i) {
        const esc_profile_t *p = esc_stick_panel_row(i, NULL);
        if (p != NULL && strcmp(p->id, "hobbywing-flyfun-8item") == 0) {
            found = true;
            const char *why = esc_stick_panel_row_why(i);
            CHECK(why != NULL);
            if (why != NULL) {
                CHECK_STR_EQ(why, "needs 7.6 V; limit 7.5 V");
            }
            break;
        }
    }
    CHECK(found);
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK(esc_stick_panel_page() == NULL);
}

int main(void)
{
    RUN(a_profile_the_engine_cannot_run_goes_no_further);
    RUN(no_run_starts_while_the_servo_programmer_holds_a_port);
    RUN(no_run_starts_without_a_throttle_pin_a_read_binding_or_a_supply);
    RUN(a_bench_that_loses_its_throttle_closes_the_warning);
    RUN(a_run_needs_a_change_and_the_warning_held_two_seconds);
    RUN(a_hold_that_is_interrupted_starts_nothing);
    RUN(a_whole_run_on_the_modelled_bench_stores_and_lets_go);
    RUN(leaving_abort_and_stop_end_a_run_and_let_go);
    RUN(leaving_switches_off_a_supply_the_screen_switched_on_after_its_run);
    RUN(a_run_the_supply_cannot_give_is_refused);
    RUN(a_voltage_equal_to_the_supplys_cap_or_minimum_runs);
    RUN(a_profile_at_the_supplys_cap_opens_from_the_list);
    return test_summary("esc_stick_panel");
}
