/*
 * ESC STICK's stack light and manual steps: the green light, the prompt
 * for a manual step, DONE, and what the supply does until it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* ------------------------------------------------------- the stack light */

/* Green lights with the detector's beeps during a run, both ways. */
TEST_CASE(the_green_light_follows_the_beeps)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    unsigned on = 0u, off = 0u, rises = 0u;
    bool was = false, red = true;
    for (int i = 0; i < 240000 && esc_stick_running(esc_stick_panel_stick());
         ++i) {
        rig_step(&r);
        bool g = false;
        esc_stick_panel_lights(&red, &g);
        CHECK(!red);
        if (esc_stick_panel_stick()->det.high) {
            CHECK(g);                        /* never dark in a beep */
        }
        on += g ? 1u : 0u;
        off += g ? 0u : 1u;
        rises += (g && !was) ? 1u : 0u;
        was = g;
        if ((i & 4095) == 0) {
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            scr->render(&cv, i & 1);
        }
    }
    rig_run(&r, 10u);
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);
    CHECK(on > 1000u && off > 1000u);
    CHECK(rises > 5u);
    bool g = true;
    esc_stick_panel_lights(&red, &g);
    CHECK(!red && !g);                       /* DONE: both dark */
}

/* Past five changes the result says how many more there were. */
TEST_CASE(a_result_of_many_changes_counts_the_rest)
{
    fresh();
    /* hobbywing-flyfun-v5: twelve items. */
    open_profile("hobbywing-flyfun-v5");
    for (int i = 0; i < 7; ++i) {
        tap(STEP_UP_X, STEP_CY(i));
    }
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    CHECK_EQ(esc_stick_panel_stick()->n, 7);
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK(lit() > 20000);
}

/* The steps show by themselves the first time the profile opens, under
 * its red button every time, and cover the page while they show. */
TEST_CASE(manual_steps_show_once_by_themselves_and_on_their_button)
{
    fresh();
    descend_to_jazz();
    const esc_profile_t *p = esc_stick_panel_page();
    CHECK(p != NULL);
    if (p == NULL) {
        return;
    }
    CHECK_STR_EQ(p->id, "kontronik-jazz");
    CHECK(esc_stick_panel_hand_shown());
    draws();
    tap(WRITE_X, BTN_CY);                    /* RUN's place: covered */
    tap(BACK_X, BACK_Y);
    CHECK(esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_page() == p);
    tap(CANCEL_X, HOLD_Y);                   /* OK */
    CHECK(!esc_stick_panel_hand_shown());
    draws();

    /* Opened again, it stays closed; the button opens it. */
    tap(BACK_X, BACK_Y);
    open_model("kontronik-jazz", NULL);
    CHECK(esc_stick_panel_page() == p);
    CHECK(!esc_stick_panel_hand_shown());
    tap(HAND_X, HAND_Y);
    CHECK(esc_stick_panel_hand_shown());
    tap(CANCEL_X, HOLD_Y);
    CHECK(!esc_stick_panel_hand_shown());

    /* A profile without steps has no button and no pop-up. */
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);                     /* the makers */
    open_maker("Hobbywing");
    open_model("hobbywing-flyfun-8item", NULL);
    CHECK_STR_EQ(esc_stick_panel_page()->id,
                 "hobbywing-flyfun-8item");
    CHECK(!esc_stick_panel_hand_shown());
    tap(HAND_X, HAND_Y);
    CHECK(!esc_stick_panel_hand_shown());

    /* Leaving the screen closes it. */
    tap(BACK_X, BACK_Y);
    tap(BACK_X, BACK_Y);
    open_maker("Kontronik");
    open_model("kontronik-jazz", NULL);
    tap(HAND_X, HAND_Y);
    CHECK(esc_stick_panel_hand_shown());
    scr->leave();
    CHECK(!esc_stick_panel_hand_shown());
}

/* A row the bench does not run opens nothing, unless it has manual steps:
 * then they show, over the list, with why it does not run. */
TEST_CASE(a_row_that_does_not_run_shows_its_manual_steps)
{
    fresh();
    esc_stick_panel_bench(0u, false, 0u, 0u, false);
    open_stick();
    tap(FIND_X, FIND_Y);
    find_type("KOSMIK\n");
    CHECK_EQ(listed(), 1);                   /* one maker */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK_EQ(listed(), 12);                  /* its KOSMIK models */
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(esc_stick_panel_page() == NULL);
    CHECK(esc_stick_panel_hand_shown());
    draws();
    tap(CANCEL_X, HOLD_Y);
    CHECK(!esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_page() == NULL);

    /* With the keyboard open, the tap closes it first. */
    tap(FIND_X, FIND_Y);
    CHECK(esc_stick_panel_typing());
    tap(100, SP_ROW_CY(0));
    CHECK(!esc_stick_panel_typing());
    CHECK(esc_stick_panel_hand_shown());
    tap(CANCEL_X, HOLD_Y);

    /* graupner-brushless-control-t: a person reads its LEDs; no steps. */
    tap(FIND_CLR_X, FIND_Y);
    tap(BACK_X, BACK_Y);                     /* the makers */
    open_maker("Graupner");
    tap(ROW_CX, SP_ROW_CY(0));
    CHECK(!esc_stick_panel_hand_shown());
    CHECK(esc_stick_panel_page() == NULL);
}

/* A whole JAZZ run: the warning holds the jumper step, the run stops after
 * the entry with a prompt, DONE counts only after a second, and the mode
 * is stored. */
TEST_CASE(a_run_asks_for_its_manual_step_and_goes_on_with_done)
{
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* KEEP, 1, 2, 3 */
    }
    supply_reads_off();                      /* the jumper on */
    tap(WRITE_X, BTN_CY);
    draws();                                 /* the warning, with its step */
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    static rig_t r;
    rig_start(&r);
    const esc_stick_t *run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);                   /* asked, and listening */
    CHECK(r.on);
    CHECK(r.armed);
    CHECK(r.pct == 0.0f);
    draws();                                 /* the prompt */
    tap(HOLD_X, HOLD_Y);                     /* DONE, too soon */
    for (int i = 0; i < 5; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);
    tap(WRITE_X, BTN_CY);                    /* ABORT's place: covered */
    CHECK(esc_stick_running(run));
    for (uint32_t i = 0; i < ESC_STICK_HAND_MIN_MS; ++i) {
        rig_step(&r);
    }
    draws();
    esc_sim_hand(&r.sim, r.now);             /* the jumper, pulled */
    tap(HOLD_X, HOLD_Y);                     /* DONE */
    rig_step(&r);
    CHECK(!run->hand_menu);
    CHECK_EQ(run->phase, ESC_STICK_VALUES);
    rig_run(&r, 300000u);
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK(!r.armed);
    CHECK(!r.on);
    draws();
}

/* A step shows in German where the profile gives it and German shows,
 * and in English otherwise; every step of record has its German. */
TEST_CASE(a_manual_step_shows_in_the_language_showing)
{
    const esc_profile_t *p = esc_profiles_find("kontronik-jazz");
    CHECK(p != NULL && p->manual_count == 3u);
    if (p == NULL || p->manual_count < 2u) {
        return;
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(esc_stick_panel_step_text(&p->manual[1]),
                 p->manual[1].action);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(esc_stick_panel_step_text(&p->manual[1]),
                 "Nach 2 s oder der Tonfolge: JAZZ: Jumper abziehen. "
                 "MINIJAZZ: Taster dr\xC3\xBC" "cken.");
    /* No German in the step: the English. */
    esc_manual_t m = p->manual[0];
    m.action_de = "";
    CHECK_STR_EQ(esc_stick_panel_step_text(&m), m.action);
    m.action_de = NULL;
    CHECK_STR_EQ(esc_stick_panel_step_text(&m), m.action);
    CHECK_STR_EQ(esc_stick_panel_step_text(NULL), "");
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *q = esc_profiles_at(i);
        for (unsigned k = 0; k < q->manual_count; ++k) {
            if (q->manual[k].action_de == NULL
                || q->manual[k].action_de[0] == '\0') {
                T_FAIL("%s step %u has no German", q->id, k + 1u);
            }
        }
    }
    /* Drawn in German: the pop-up of a profile opened for the first
     * time. */
    fresh();
    ui_text_set_language(UI_LANG_DE);
    descend_to_jazz();
    CHECK(esc_stick_panel_hand_shown());
    draws();
    ui_text_set_language(UI_LANG_EN);
}

/* KONTROL-X mode 2 to the step before the supply goes off: the button
 * pressed when asked, the ESC powered and the stick at MAX after the
 * store. */
static const esc_stick_t *run_to_end_step(rig_t *r)
{
    fresh();
    open_profile("kontronik-kontrol-x");
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    tap(STEP_UP_X, STEP_CY(0));              /* KEEP, 1 */
    tap(STEP_UP_X, STEP_CY(0));              /* 2 */
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    rig_start(r);
    r->no_watch = true;
    const esc_stick_t *run = esc_stick_panel_stick();
    bool pressed = false;
    for (uint32_t i = 0; i < 240000u && esc_stick_running(run)
                         && run->phase != ESC_STICK_HAND_END; ++i) {
        if (run->hand_menu && !pressed) {
            esc_sim_hand(&r->sim, r->now);   /* the button */
            pressed = true;
        }
        rig_step(r);
    }
    CHECK_EQ(run->phase, ESC_STICK_HAND_END);
    return run;
}

/* The run holds the ESC powered after the store until DONE, with the
 * prompt up; DONE switches the supply off.  No DONE: the run ends with
 * NOT CONFIRMED and says the ESC may be locked. */
TEST_CASE(the_supply_stays_on_until_done_after_the_store)
{
    static rig_t r;
    const esc_stick_t *run = run_to_end_step(&r);
    CHECK(r.on);
    CHECK(r.armed);
    CHECK(r.pct == ESC_STICK_PCT_MAX);
    CHECK(esc_stick_panel_hand_shown() == false);
    CHECK_STR_EQ(esc_stick_panel_left_text(),
                 "Without DONE within %u s, the run ends and power switches "
                 "off. The ESC may lock itself.");
    draws();                                 /* the prompt */
    for (uint32_t i = 0; i < ESC_STICK_HAND_MIN_MS; ++i) {
        rig_step(&r);
    }
    CHECK(r.on);
    tap(HOLD_X, HOLD_Y);                     /* DONE */
    rig_step(&r);
    CHECK_EQ(run->phase, ESC_STICK_OFF);
    rig_run(&r, 60000u);
    CHECK_EQ(run->phase, ESC_STICK_DONE);
    CHECK(!esc_stick_lock_risk(run));
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
    CHECK(!r.on);
    draws();

    run = run_to_end_step(&r);
    rig_run(&r, ESC_STICK_HAND_WAIT_MS + 1000u);
    CHECK_EQ(run->phase, ESC_STICK_ABORTED);
    CHECK_EQ(run->reason, ESC_STICK_R_HAND);
    CHECK(esc_stick_lock_risk(run));
    CHECK(!r.on);
    CHECK(!r.armed);
    draws();                                 /* the result, with the lock */
    ui_text_set_language(UI_LANG_DE);
    draws();
    ui_text_set_language(UI_LANG_EN);

    /* JAZZ repeats its mode with no lock named: the prompt and the result
     * say the mode may not be stored. */
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);                   /* the steps, read */
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));          /* KEEP, 1, 2, 3 */
    }
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), 1u);
    rig_start(&r);
    r.no_watch = true;
    run = esc_stick_panel_stick();
    bool pulled = false;
    for (uint32_t i = 0; i < 240000u && esc_stick_running(run)
                         && run->phase != ESC_STICK_HAND_END; ++i) {
        if (run->hand_menu && !pulled) {
            esc_sim_hand(&r.sim, r.now);     /* the jumper */
            pulled = true;
        }
        rig_step(&r);
    }
    CHECK_EQ(run->phase, ESC_STICK_HAND_END);
    CHECK(esc_stick_hand(run) != NULL && !esc_stick_hand(run)->locks);
    CHECK_STR_EQ(esc_stick_panel_left_text(),
                 "Without DONE within %u s, the run ends and power switches "
                 "off. The mode may not be saved.");
    draws();                                 /* the prompt */
    rig_run(&r, ESC_STICK_HAND_WAIT_MS + 1000u);
    CHECK_EQ(run->reason, ESC_STICK_R_HAND);
    CHECK(esc_stick_cut_short(run));
    CHECK(!esc_stick_lock_risk(run));
    draws();                                 /* the result */
    ui_text_set_language(UI_LANG_DE);
    draws();
    ui_text_set_language(UI_LANG_EN);
}

/*
 * A selection cut short while the ESC confirms it is not counted as made:
 * six changes on a two-stage card profile with a step before the
 * power-off, aborted while that step is asked: 5 of 6 made.  One being
 * confirmed past the four rows shown is named unsure on the summary line.
 * A 1-of-1 run cut the same way says 0 of 1.
 */
TEST_CASE(an_unsure_selection_is_not_counted_as_made)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_POWER_OFF, "Wait for the confirmation tones.",
          0u, NULL, false, false },
    };
    fresh();
    esc_profiles_clear_overrides();
    esc_profile_t card = *esc_profiles_find("hobbywing-flyfun-8item");
    card.id = "card-confirm";
    card.automatable = ESC_AUTO_ASSISTED;
    card.automatable_note = "Confirmation before power-off.";
    card.manual = k;
    card.manual_count = 1;
    CHECK(esc_profiles_override(&card, NULL));
    for (int pass = 0; pass < 2; ++pass) {
        fresh();
        open_profile("card-confirm");
        tap(CANCEL_X, HOLD_Y);               /* the first opening */
        const int picks = (pass == 0) ? 6 : 1;
        for (int i = 0; i < picks; ++i) {
            tap(STEP_UP_X, STEP_CY(i));
        }
        tap(WRITE_X, BTN_CY);
        hold_for(2.25f);
        static rig_t r;
        rig_start(&r);
        r.no_watch = true;
        const esc_stick_t *run = esc_stick_panel_stick();
        CHECK_EQ(run->n, (uint8_t)picks);
        for (uint32_t i = 0; i < 900000u && esc_stick_running(run)
                             && run->phase != ESC_STICK_HAND_END; ++i) {
            rig_step(&r);
        }
        CHECK_EQ(run->phase, ESC_STICK_HAND_END);
        CHECK_EQ(esc_stick_done_count(run), (unsigned)picks);
        tap(CANCEL_X, HOLD_Y);               /* ABORT on the prompt */
        rig_run(&r, 1000u);
        CHECK(esc_stick_cut_short(run));
        CHECK_EQ(esc_stick_made_count(run), (unsigned)picks - 1u);
        if (pass == 0) {
            CHECK_STR_EQ(esc_stick_panel_result_head(),
                         "5 of 6 selections completed.");
            /* The ESC's loop decides which selection comes last; here the
             * one being confirmed is shown on a row, and the line for the
             * two past the fourth counts both made. */
            CHECK(run->active < 4u && esc_stick_unsure(run, run->active));
            CHECK_STR_EQ(esc_stick_panel_result_more(),
                         "AND 2 MORE, 2 COMPLETED");
            /* The same end with the one being confirmed past the fourth
             * row: the line names it unsure (the run's state moved to
             * that change, as a loop that sounds it last leaves it). */
            /* NOLINTNEXTLINE(performance-no-int-to-ptr): drops const to move the state under test */
            esc_stick_t *moved = (esc_stick_t *)(uintptr_t)run;
            moved->active = 5u;
            CHECK(esc_stick_unsure(run, 5u));
            CHECK_STR_EQ(esc_stick_panel_result_head(),
                         "5 of 6 selections completed.");
            CHECK_STR_EQ(esc_stick_panel_result_more(),
                         "AND 2 MORE, 1 COMPLETED, 1 UNCERTAIN");
            ui_text_set_language(UI_LANG_DE);
            CHECK_STR_EQ(esc_stick_panel_result_more(),
                         "UND 2 WEITERE, 1 AUSGEFÜHRT, 1 UNKLAR");
            draws();
            ui_text_set_language(UI_LANG_EN);
        } else {
            CHECK_STR_EQ(esc_stick_panel_result_head(),
                         "0 of 1 selections completed.");
            CHECK_STR_EQ(esc_stick_panel_result_more(), "");
        }
        draws();
        scr->leave();
    }
    esc_profiles_clear_overrides();
}

/* ABORT on the prompt ends the run as ABORT does: disarmed, supply off. */
TEST_CASE(abort_on_the_prompt_ends_the_run)
{
    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));
    }
    supply_reads_off();                      /* the jumper on */
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    const esc_stick_t *run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);
    tap(CANCEL_X, HOLD_Y);                   /* ABORT */
    rig_run(&r, 1000u);
    CHECK_EQ(run->phase, ESC_STICK_ABORTED);
    CHECK_EQ(run->reason, ESC_STICK_R_USER);
    CHECK(!r.armed);
    CHECK(!r.on);
    draws();                                 /* the result */
}

int main(void)
{
    RUN(a_result_of_many_changes_counts_the_rest);
    RUN(the_green_light_follows_the_beeps);
    RUN(manual_steps_show_once_by_themselves_and_on_their_button);
    RUN(a_row_that_does_not_run_shows_its_manual_steps);
    RUN(a_run_asks_for_its_manual_step_and_goes_on_with_done);
    RUN(abort_on_the_prompt_ends_the_run);
    RUN(the_supply_stays_on_until_done_after_the_store);
    RUN(a_manual_step_shows_in_the_language_showing);
    RUN(an_unsure_selection_is_not_counted_as_made);
    return test_summary("esc_stick_panel_steps");
}
