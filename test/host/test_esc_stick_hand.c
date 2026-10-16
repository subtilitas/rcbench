/*
 * Stick programming: the steps a person does at the ESC (electronic speed
 * controller) -- a jumper, a button -- asked for with a prompt and DONE,
 * and the supply held or kept off around them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"
#include "esc_sim.h"
#include "esc_stick.h"

#include "esc_stick_rig.h"

/* Kontronik JAZZ: the jumper is on before the warning is held, the run
 * powers up and waits the 2 s entry, then asks for the pull and listens
 * from that moment, powered and at MIN.  The ESC is silent until the pull;
 * SILENCE does not end the run meanwhile.  No DONE: the menu heard in
 * order takes the step as done, and the mode is stored. */
TEST_CASE(a_jumper_pulled_after_the_entry_is_waited_for)
{
    rig("kontronik-jazz");
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_ONE_STAGE);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    CHECK(esc_stick_hand(&r.e) == NULL);
    run_until_asked(60000u);
    const esc_manual_t *m = esc_stick_hand(&r.e);
    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_EQ(m->when, ESC_MANUAL_BEFORE_MENU);
    }
    CHECK_EQ(r.e.phase, ESC_STICK_VALUES);        /* listening already */
    CHECK(r.e.hand_menu);
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_out(&r.e)->arm);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    CHECK_EQ(r.e.entries, 1);
    /* A tap within the first second is not the operator's answer. */
    CHECK(!esc_stick_hand_ready(&r.e));
    CHECK(!esc_stick_confirm(&r.e));
    CHECK_EQ(esc_stick_hand_left_ms(&r.e), ESC_STICK_HAND_WAIT_MS);
    run_for(r.t.silence_ms + 1000u);
    CHECK(esc_stick_running(&r.e));
    CHECK(r.e.hand_menu);
    CHECK_EQ(r.e.groups, 0u);
    act(false);
    for (uint32_t i = 0; i < 20000u && r.e.hand_menu; ++i) {
        tick();
    }
    CHECK(!r.e.hand_menu);
    CHECK(r.e.hand_done);
    CHECK(esc_stick_hand(&r.e) == NULL);
    CHECK(!esc_stick_confirm(&r.e));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    ended_safe();
}

/* The pull starts the series at once, and DONE comes when the operator is
 * back at the screen, here 4 s later.  Counting from the pull, mode 3 is
 * acted on in the first loop -- groups 1, 2, 3 in order -- not a whole
 * loop later, as it would be counting from DONE.  A partial first group is
 * never acted on: the quiet-first and order rules hold from the prompt. */
TEST_CASE(a_pull_starts_the_menu_and_no_group_is_lost)
{
    rig("kontronik-jazz");
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    run_for(300u);
    act(false);
    const uint32_t pulled = r.now;
    run_for(4000u);
    (void)esc_stick_confirm(&r.e);            /* back at the screen */
    run_until_phase(ESC_STICK_STORE, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_STORE);
    /* The answer (1.25 s), the pause, then 1, 2 and 3 at about 1.75 s
     * each: under 9 s.  One loop of nine modes more is over 15 s. */
    if (r.now - pulled > 9000u) {
        T_FAIL("mode 3 acted on %u ms after the pull",
               (unsigned)(r.now - pulled));
    }
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);

    /* The ESC already sounding when the run asks: no group counted until
     * GROUP GAP of quiet, and the order rule needs three in a row. */
    rig("kontronik-jazz");
    c[0] = change(1, 2);
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ENTRY, 60000u);
    act(false);                               /* pulled early */
    run_until_asked(60000u);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
}

/* Two before_menu steps: the first waits in HAND_ON for DONE, the second,
 * marked starts_menu, is listened through. */
TEST_CASE(an_earlier_step_still_waits_for_done)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_MENU, "Check the LED.", 0u, NULL, false, false },
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true, false },
    };
    rig_hand("kontronik-jazz", k, 2);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_ON, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);
    CHECK(!r.e.hand_menu);
    CHECK_EQ(r.e.hand, 0u);
    run_for(5000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);      /* waits for DONE */
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK(r.e.hand_menu);
    CHECK_EQ(r.e.hand, 1u);
    CHECK_EQ(r.e.phase, ESC_STICK_VALUES);
    act(false);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);

    /* A menu that rests elsewhere: listening would need a move under the
     * operator's hand at a powered ESC, and waiting for DONE would drop
     * the groups the pull starts.  Not run: the profile's listen move away
     * from the entry, or a value powered up away from the listen move. */
    const char *why = NULL;
    rig_hand("kontronik-jazz", &k[1], 1);
    g_hand.listen_throttle = ESC_THR_MID;
    CHECK_EQ(esc_stick_kind(&g_hand, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "menu start, rest elsewhere");
    CHECK(!start(c, 1));
    g_hand.listen_throttle = ESC_THR_MIN;     /* the entry's */
    CHECK(esc_stick_kind(&g_hand, &why) != ESC_STICK_KIND_NONE);
    CHECK(start(c, 1));
    esc_stick_abort(&r.e, ESC_STICK_R_USER);
    esc_stick_change_t car[1] = { change(1, 6) };   /* powered at MID */
    CHECK(!esc_stick_check(&g_hand, car, 1, &r.t, &why));
    CHECK_STR_EQ(why, "menu start, rest elsewhere");

    /* A last step the profile does not mark as starting the menu waits for
     * DONE as any other: a card profile's own step, not the series. */
    rig_hand("kontronik-jazz", &k[0], 1);
    CHECK(!g_hand.manual[0].starts_menu);
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_ON, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);
    CHECK(!r.e.hand_menu);
    act(false);
    run_for(5000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);      /* still waits */
    CHECK_EQ(r.e.groups, 0u);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_VALUES);
    CHECK(!r.e.hand_menu);
    CHECK(esc_stick_hand(&r.e) == NULL);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
}

/* No DONE: the run ends after ESC_STICK_HAND_WAIT_MS, everything off, with
 * the red light lit. */
TEST_CASE(a_step_never_confirmed_ends_the_run)
{
    rig("kontronik-jazz");
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    run_for(ESC_STICK_HAND_WAIT_MS - 2u);
    CHECK(r.e.hand_menu);                   /* the ESC waits, silent */
    CHECK(esc_stick_hand_left_ms(&r.e) <= 2u);
    run_for(10u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_HAND);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    CHECK_STR_EQ(esc_stick_reason_text(r.e.reason), "NOT CONFIRMED");
    ended_safe();
    CHECK(esc_stick_hand(&r.e) == NULL);
    CHECK_EQ(esc_stick_hand_left_ms(&r.e), 0u);
    CHECK(!esc_stick_confirm(&r.e));
}

/*
 * Kontronik KONTROL-X: the supply off before the ESC has confirmed its
 * mode locks the ESC (Kontronik_Kontrol-X_Kolibri-X.pdf p.4, 8 flashes).
 * After the store the run holds the ESC powered, the stick where the store
 * left it, until DONE; then the supply goes off and the run ends as any
 * other.  No DONE in ESC_STICK_HAND_WAIT_MS: the run ends safe and says
 * the ESC may be locked; so does any other end while the step is asked.
 */
TEST_CASE(the_supply_stays_on_until_the_esc_has_confirmed)
{
    rig("kontronik-kontrol-x");
    CHECK_EQ(esc_profile_manual_count(r.p, ESC_MANUAL_BEFORE_POWER_OFF), 1u);
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(esc_stick_done_count(&r.e), 1u);
    const esc_manual_t *m = esc_stick_hand(&r.e);
    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_EQ(m->when, ESC_MANUAL_BEFORE_POWER_OFF);
    }
    CHECK(!r.e.hand_menu);
    CHECK(r.e.end_open);
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_out(&r.e)->arm);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MAX);
    CHECK_STR_EQ(esc_stick_phase_text(r.e.phase), "WAITING FOR THE ESC");
    run_for(30000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);     /* powered, waiting */
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MAX);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_OFF);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    CHECK(!r.e.end_open);
    run_for(20000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(!esc_stick_lock_risk(&r.e));
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);

    /* Mode 3 goes to full reverse after full forward: the step is asked
     * with the stick where the last move left it. */
    rig("kontronik-kontrol-x");
    c[0] = change(1, 3);
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_for(20000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);

    /* No DONE: NOT CONFIRMED, everything off, and the ESC may be locked. */
    rig("kontronik-kontrol-x");
    c[0] = change(1, 2);
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK(!esc_stick_lock_risk(&r.e));           /* not while it runs */
    run_for(ESC_STICK_HAND_WAIT_MS - 2u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    run_for(10u);
    CHECK_EQ(r.e.phase, ESC_STICK_ABORTED);
    CHECK_EQ(r.e.reason, ESC_STICK_R_HAND);
    ended_safe();
    CHECK(esc_stick_lock_risk(&r.e));

    /* STOP while the step is asked: the same. */
    rig("kontronik-kontrol-x");
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    r.stops++;
    r.pressed++;
    tick();
    CHECK_EQ(r.e.reason, ESC_STICK_R_STOP);
    ended_safe();
    CHECK(esc_stick_lock_risk(&r.e));

    /* An end before the step, or after its DONE, is no such end. */
    rig("kontronik-kontrol-x");
    CHECK(start(c, 1));
    run_until_asked(60000u);
    esc_stick_abort(&r.e, ESC_STICK_R_USER);
    CHECK(!esc_stick_lock_risk(&r.e));

    /* A profile with two such steps asks for both, in order. */
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true, false },
        { ESC_MANUAL_BEFORE_POWER_OFF, "Watch the LED.", 0u, NULL, false, false },
        { ESC_MANUAL_BEFORE_POWER_OFF, "Listen for the tones.", 0u, NULL,
          false, false },
    };
    rig_hand("kontronik-jazz", k, 3);
    esc_stick_change_t j[1] = { change(1, 3) };
    CHECK(start(j, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(r.e.hand, 1u);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(r.e.hand, 2u);
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(!esc_stick_confirm(&r.e));            /* too soon after the last */
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_for(20000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
}

/*
 * Kontronik 3SL repeats the stored mode as tones before the manual
 * disconnects it (Kontrollausgabe, Kontronik_3SL.pdf p.5-17): mode 7 as
 * seven, longer than STORE.  The run holds the ESC powered after the store
 * until DONE, whatever the mode; an end before DONE says the mode may not
 * be stored, and not that the ESC locks, which the 3SL manual does not
 * say.  Every runnable Kontronik profile carries such a step, and only
 * the five whose manuals name the lock mark it.
 */
TEST_CASE(the_supply_stays_on_while_the_esc_repeats_the_mode)
{
    rig("kontronik-3sl");
    esc_stick_change_t c[1] = { change(1, 7) };
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(esc_stick_done_count(&r.e), 1u);
    const esc_manual_t *m = esc_stick_hand(&r.e);
    CHECK(m != NULL && !m->locks);
    CHECK(esc_stick_out(&r.e)->supply_on);
    run_for(r.t.store_ms * 4u);                /* well past STORE */
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_confirm(&r.e));
    run_for(20000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(!esc_stick_cut_short(&r.e));
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 7);

    /* No DONE: cut short, not locked. */
    rig("kontronik-3sl");
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    run_for(ESC_STICK_HAND_WAIT_MS + 10u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_HAND);
    ended_safe();
    CHECK(esc_stick_cut_short(&r.e));
    CHECK(!esc_stick_lock_risk(&r.e));

    /* KONTROL-X's step marks the lock: both. */
    rig("kontronik-kontrol-x");
    c[0] = change(1, 2);
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    esc_stick_abort(&r.e, ESC_STICK_R_USER);
    CHECK(esc_stick_cut_short(&r.e));
    CHECK(esc_stick_lock_risk(&r.e));
    CHECK(!esc_stick_cut_short(NULL));
    CHECK(!esc_stick_lock_risk(NULL));

    /* Every Kontronik profile that runs repeats its mode before the
     * power-off; the lock is the five's. */
    static const char *const k_locks[] = {
        "kontronik-koby", "kontronik-kontrol-x", "kontronik-jive-pro",
        "kontronik-kolibri", "kontronik-kosmik",
    };
    unsigned runs = 0u;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        if (strncmp(p->id, "kontronik-", 10) != 0) {
            continue;
        }
        bool want_lock = false;
        for (size_t k = 0; k < sizeof(k_locks) / sizeof(k_locks[0]); ++k) {
            want_lock = want_lock || strcmp(p->id, k_locks[k]) == 0;
        }
        const bool runnable = esc_stick_kind(p, NULL) != ESC_STICK_KIND_NONE;
        runs += runnable ? 1u : 0u;
        unsigned offs = 0u, locks = 0u;
        for (unsigned k = 0; k < p->manual_count; ++k) {
            offs += (p->manual[k].when == ESC_MANUAL_BEFORE_POWER_OFF);
            locks += p->manual[k].locks ? 1u : 0u;
        }
        if ((runnable || want_lock) && offs != 1u) {
            T_FAIL("%s: %u steps before the power-off", p->id, offs);
        }
        if (locks != (want_lock ? 1u : 0u)) {
            T_FAIL("%s: %u steps mark the lock", p->id, locks);
        }
    }
    CHECK_EQ(runs, 10u);
}

/*
 * The confirmation is pending from the selection on, not only once the
 * step before the power-off is asked: an end while the ESC stores
 * (ESC_STICK_STORE) switches the supply off under it as well.  The result
 * says so, and that a KONTROL-X may be locked; a profile without such a
 * step says neither.
 */
TEST_CASE(an_end_while_the_esc_stores_says_it_was_cut_short)
{
    static const char *const k_ids[] = {
        "kontronik-kontrol-x", "kontronik-3sl", "hobbywing-flyfun-8item",
    };
    for (size_t k = 0; k < 3; ++k) {
        rig(k_ids[k]);
        esc_stick_change_t c[1] = {
            change(1, 2)
        };
        CHECK(start(c, 1));
        for (uint32_t i = 0; i < 240000u && esc_stick_running(&r.e)
                             && r.e.phase != ESC_STICK_STORE; ++i) {
            if (r.e.hand_menu) {
                act(false);
            }
            tick();
        }
        CHECK_EQ(r.e.phase, ESC_STICK_STORE);
        CHECK(!esc_stick_cut_short(&r.e));      /* not while it runs */
        r.stops++;
        r.pressed++;
        tick();
        CHECK_EQ(r.e.reason, ESC_STICK_R_STOP);
        ended_safe();
        CHECK_EQ(esc_stick_cut_short(&r.e), k < 2);
        CHECK_EQ(esc_stick_lock_risk(&r.e), k == 0);
    }
    /* Before the selection: nothing is stored yet. */
    rig("kontronik-kontrol-x");
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    esc_stick_abort(&r.e, ESC_STICK_R_USER);
    CHECK(!esc_stick_cut_short(&r.e));
}

/*
 * A value stored by moves after its selection is not stored until they
 * are made, with or without a step before the power-off: PIX mode 2 goes
 * to the brake after full throttle.  A STOP before that move says the
 * mode may not be stored; mode 3, stored by its selection, says nothing.
 */
TEST_CASE(a_store_cut_before_its_moves_says_so)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_MENU, "Press the button.", 0u, NULL, true,
          false },
    };
    static const uint8_t k_mode[] = { 2u, 3u };
    for (size_t i = 0; i < 2; ++i) {
        rig_hand("kontronik-pix", k, 1);      /* no before_power_off */
        CHECK_EQ(esc_profile_manual_count(r.p, ESC_MANUAL_BEFORE_POWER_OFF),
                 0u);
        esc_stick_change_t c[1] = { change(1, k_mode[i]) };
        CHECK(start(c, 1));
        for (uint32_t t = 0; t < 240000u && esc_stick_running(&r.e)
                             && r.e.phase != ESC_STICK_STORE; ++t) {
            if (r.e.hand_menu) {
                act(false);
            }
            tick();
        }
        if (r.e.phase != ESC_STICK_STORE) {
            T_FAIL("mode %u ended %s", k_mode[i],
                   esc_stick_reason_text(r.e.reason));
        }
        CHECK_EQ(esc_stick_done_count(&r.e), 1u);
        r.stops++;
        r.pressed++;
        tick();
        ended_safe();
        CHECK_EQ(esc_stick_cut_short(&r.e), i == 0);
        CHECK(!esc_stick_lock_risk(&r.e));
        /* Selected, not stored for certain: not counted as made. */
        CHECK_EQ(esc_stick_unsure(&r.e, 0u), i == 0);
        CHECK_EQ(esc_stick_made_count(&r.e), (i == 0) ? 0u : 1u);
        CHECK_EQ(esc_stick_done_count(&r.e), 1u);
        CHECK(!esc_stick_unsure(&r.e, 1u));
        CHECK(!esc_stick_unsure(NULL, 0u));
        CHECK_EQ(esc_stick_made_count(NULL), 0u);
    }
    /* Run through, the moves made: not cut short. */
    rig_hand("kontronik-pix", k, 1);
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    act(false);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(!esc_stick_cut_short(&r.e));
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
}

/*
 * Two steps before the power-off, only the second marking the lock: the
 * lock is the profile's until both are confirmed, so the first step's
 * prompt and an end during it say the ESC may be locked.
 */
TEST_CASE(a_later_locking_step_counts_from_the_first)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true, false },
        { ESC_MANUAL_BEFORE_POWER_OFF, "Listen for the tones.", 0u, NULL,
          false, false },
        { ESC_MANUAL_BEFORE_POWER_OFF, "Watch the LED flash.", 0u, NULL,
          false, true },
    };
    rig_hand("kontronik-jazz", k, 3);
    CHECK(esc_stick_end_locks(&r.e) == false);   /* not started */
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(r.e.hand, 1u);                      /* the one without locks */
    CHECK(esc_stick_end_locks(&r.e));
    esc_stick_abort(&r.e, ESC_STICK_R_USER);
    CHECK(esc_stick_cut_short(&r.e));
    CHECK(esc_stick_lock_risk(&r.e));
    CHECK(!esc_stick_end_locks(NULL));
}

/* While a step is waited for, every end a run has still ends it: ABORT,
 * STOP, the bench's own stop, a disarm, the supply and the link. */
TEST_CASE(stop_abort_and_the_supply_end_a_run_waiting_for_a_step)
{
    static const esc_stick_reason_t want[] = {
        ESC_STICK_R_USER, ESC_STICK_R_STOP, ESC_STICK_R_BENCH_STOP,
        ESC_STICK_R_DISARMED, ESC_STICK_R_LINK, ESC_STICK_R_SUPPLY_OFF,
        ESC_STICK_R_SUPPLY_LOST, ESC_STICK_R_STALE,
    };
    for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); ++k) {
        rig("kontronik-jazz");
        esc_stick_change_t c[1] = { change(1, 3) };
        CHECK(start(c, 1));
        run_until_asked(60000u);
        run_for(ESC_STICK_HAND_MIN_MS);
        CHECK(r.e.hand_menu);
        /* A DONE taken in the same frame as the end: the end wins, and
         * the menu never starts. */
        CHECK(esc_stick_confirm(&r.e));
        switch (want[k]) {
        case ESC_STICK_R_USER:  esc_stick_abort(&r.e, ESC_STICK_R_USER); break;
        case ESC_STICK_R_STOP:  r.stops++; r.pressed++;                  break;
        case ESC_STICK_R_BENCH_STOP: r.stops++;                          break;
        case ESC_STICK_R_DISARMED: r.arm_refused = true; r.armed = false; break;
        case ESC_STICK_R_LINK:  r.link = false;                          break;
        case ESC_STICK_R_SUPPLY_OFF: r.supply_dead = true;               break;
        case ESC_STICK_R_SUPPLY_LOST: r.online = false;                  break;
        case ESC_STICK_R_STALE: r.readings_stop = true;                  break;
        default: break;
        }
        run_for(5000u);
        if (r.e.reason != want[k]) {
            T_FAIL("case %u ended %s", (unsigned)k,
                   esc_stick_reason_text(r.e.reason));
        }
        CHECK_EQ(r.e.groups, 0u);
        ended_safe();
    }
}

/* A button held while the supply comes on: the run stops before the power
 * with the supply off and the stick at the entry, and DONE switches it on.
 * A stop before the DONE is acted on leaves the supply off. */
TEST_CASE(a_step_at_power_up_is_asked_with_the_supply_off)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 3000u, NULL, false, false },
    };
    rig_hand("kontronik-jazz", k, 1);
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_ONE_STAGE);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    CHECK(!r.supply_on);
    CHECK(esc_stick_out(&r.e)->arm);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    CHECK_EQ(r.e.entries, 0);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_POWER);
    CHECK(esc_stick_out(&r.e)->supply_on);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);

    /* DONE, and a STOP before the next step: the supply never comes on. */
    rig_hand("kontronik-jazz", k, 1);
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    r.stops++;
    r.pressed++;
    tick();
    CHECK_EQ(r.e.reason, ESC_STICK_R_STOP);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    ended_safe();
}

/* A button held 3 s while the supply comes on, and an entry of 2 s: the
 * pull asked after the entry waits for the hold to end, not the entry. */
TEST_CASE(the_entry_lasts_at_least_the_hold_at_power_up)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 3000u, NULL, false, false },
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true, false },
    };
    rig_hand("kontronik-jazz", k, 2);
    CHECK_EQ(r.t.entry_ms, 2000u);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    CHECK_EQ(esc_stick_entry_ms(&r.e), 3000u);
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_until_phase(ESC_STICK_ENTRY, 10000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ENTRY);
    const uint32_t on = r.e.on_ms;
    run_until_asked(10000u);
    CHECK(r.e.hand_menu);
    CHECK(r.e.hand_ms - on >= 3000u);
    CHECK(r.e.hand_ms - on < 3010u);
    /* Without the hold, the entry's own 2 s. */
    static const esc_manual_t pull[] = {
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true, false },
    };
    rig_hand("kontronik-jazz", pull, 1);
    CHECK(start(c, 1));
    CHECK_EQ(esc_stick_entry_ms(&r.e), 2000u);
    CHECK_EQ(esc_stick_entry_ms(NULL), 0u);
}

/* One change per power-up and a jumper fitted before each: the warning
 * covers the first power-up, and the run asks before every later one, with
 * the supply off and seen off. */
TEST_CASE(a_step_before_power_is_asked_before_every_later_power_up)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u, NULL, false, false },
        { ESC_MANUAL_AFTER_PROGRAMMING, "Pull the jumper.", 0u, NULL, false, false },
    };
    rig_hand("sunrise-pro", k, 2);
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_ONE_STAGE);
    esc_stick_change_t c[2] = { change(1, 3), change(2, 4) };
    CHECK(start(c, 2));
    run_until_phase(ESC_STICK_ENTRY, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ENTRY);         /* first: not asked */
    run_until_phase(ESC_STICK_HAND_OFF, 240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
    CHECK_EQ(r.e.entries, 1);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    CHECK(!r.supply_on);
    const esc_manual_t *m = esc_stick_hand(&r.e);
    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_STR_EQ(m->action, "Fit the jumper.");
    }
    /* The supply has been off at least OFF TIME by now. */
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.entries, 2);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 4);
}

/* A button to hold while the supply comes on: the step at an unpowered
 * ESC. */
static const esc_manual_t k_hold_button[] = {
    { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 0u, NULL, false, false },
};

/* A run started with the module's output still on: nothing is powered or
 * asked of a person until the supply reads off; a module that stays on
 * ends the run with SUPPLY STAYS ON, the step never shown. */
TEST_CASE(a_run_started_with_the_output_on_asks_nothing)
{
    rig_hand("kontronik-jazz", k_hold_button, 1);
    r.supply_on = true;                 /* live before the run */
    r.supply_stuck = true;
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    bool asked = false, powered = false;
    for (int i = 0; i < 10000 && esc_stick_running(&r.e); ++i) {
        tick();
        asked = asked || esc_stick_hand(&r.e) != NULL;
        powered = powered || esc_stick_out(&r.e)->supply_on;
    }
    CHECK(!asked);
    CHECK(!powered);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    ended_safe();

    /* The same module going off 600 ms into the run: the step is asked
     * only once it has read off for ESC_STICK_OFF_SETTLE_MS. */
    rig_hand("kontronik-jazz", k_hold_button, 1);
    r.supply_on = true;
    r.off_at = r.now;
    r.off_lag_ms = 600u;
    CHECK(start(c, 1));
    uint32_t off_at = 0u;
    for (int i = 0; i < 10000 && esc_stick_running(&r.e)
                    && esc_stick_hand(&r.e) == NULL; ++i) {
        tick();
        if (!r.supply_on && off_at == 0u) {
            off_at = r.now;
        }
    }
    CHECK(esc_stick_hand(&r.e) != NULL);
    CHECK(off_at != 0u);
    CHECK(r.now - off_at >= ESC_STICK_OFF_SETTLE_MS);
    CHECK(!r.supply_on);
}

/* While a step at an unpowered ESC is asked, a reading that shows the
 * output on, or the current up, ends the run at once; readings that stop
 * end it too. */
TEST_CASE(a_supply_that_comes_on_during_the_step_ends_the_run)
{
    esc_stick_change_t c[1] = { change(1, 3) };
    for (int k = 0; k < 3; ++k) {
        rig_hand("kontronik-jazz", k_hold_button, 1);
        CHECK(start(c, 1));
        run_until_phase(ESC_STICK_HAND_OFF, 60000u);
        CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
        CHECK(esc_stick_hand(&r.e) != NULL);
        if (k == 0) {
            r.supply_on = true;         /* the module, by itself */
            r.supply_stuck = true;
        } else if (k == 1) {
            r.extra_ma = 200;           /* current through the ESC */
        } else {
            r.readings_stop = true;
        }
        const uint32_t at = r.now;
        run_for(5000u);
        if (k < 2) {
            CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
            CHECK(r.e.phase_ms - at <= r.read_iv + 2u);   /* the next one */
        } else {
            CHECK_EQ(r.e.reason, ESC_STICK_R_STALE);
        }
        CHECK(esc_stick_hand(&r.e) == NULL);
        ended_safe();
    }

    /* Before a later power-up: a module on again after the run saw it off
     * ends the run before the jumper is asked for. */
    static const esc_manual_t fit[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u, NULL, false, false },
    };
    rig_hand("sunrise-pro", fit, 1);
    esc_stick_change_t two[2] = { change(1, 3), change(2, 4) };
    CHECK(start(two, 2));
    run_until_phase(ESC_STICK_CYCLE, 240000u);
    for (int i = 0; i < 20000 && esc_stick_running(&r.e)
                    && !r.e.cycle_moved; ++i) {
        tick();
    }
    CHECK(r.e.cycle_moved);
    r.supply_on = true;
    r.supply_stuck = true;
    bool asked = false;
    for (int i = 0; i < 20000 && esc_stick_running(&r.e); ++i) {
        tick();
        asked = asked || esc_stick_hand(&r.e) != NULL;
    }
    CHECK(!asked);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
}

/* Readings that stop after the supply read off: no step is asked, and
 * nothing powered, on a reading older than ESC_STICK_STALE_MS. */
TEST_CASE(no_step_is_asked_on_an_old_reading)
{
    static const esc_manual_t fit[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u, NULL, false, false },
    };
    rig_hand("sunrise-pro", fit, 1);
    esc_stick_change_t two[2] = { change(1, 3), change(2, 4) };
    CHECK(start(two, 2));
    run_until_phase(ESC_STICK_CYCLE, 240000u);
    for (int i = 0; i < 20000 && esc_stick_running(&r.e) && !r.e.off_seen;
         ++i) {
        tick();
    }
    CHECK(r.e.off_seen);
    r.readings_stop = true;
    bool asked = false, powered = false;
    for (int i = 0; i < 20000 && esc_stick_running(&r.e); ++i) {
        tick();
        asked = asked || esc_stick_hand(&r.e) != NULL;
        powered = powered || esc_stick_out(&r.e)->supply_on;
    }
    CHECK(!asked);
    CHECK(!powered);
    CHECK_EQ(r.e.reason, ESC_STICK_R_STALE);
    ended_safe();
}

int main(void)
{
    RUN(a_jumper_pulled_after_the_entry_is_waited_for);
    RUN(a_pull_starts_the_menu_and_no_group_is_lost);
    RUN(an_earlier_step_still_waits_for_done);
    RUN(a_step_never_confirmed_ends_the_run);
    RUN(the_supply_stays_on_until_the_esc_has_confirmed);
    RUN(the_supply_stays_on_while_the_esc_repeats_the_mode);
    RUN(an_end_while_the_esc_stores_says_it_was_cut_short);
    RUN(a_store_cut_before_its_moves_says_so);
    RUN(a_later_locking_step_counts_from_the_first);
    RUN(stop_abort_and_the_supply_end_a_run_waiting_for_a_step);
    RUN(a_step_at_power_up_is_asked_with_the_supply_off);
    RUN(the_entry_lasts_at_least_the_hold_at_power_up);
    RUN(a_step_before_power_is_asked_before_every_later_power_up);
    RUN(a_run_started_with_the_output_on_asks_nothing);
    RUN(a_supply_that_comes_on_during_the_step_ends_the_run);
    RUN(no_step_is_asked_on_an_old_reading);
    return test_summary("esc_stick_hand");
}
