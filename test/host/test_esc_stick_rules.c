/*
 * Stick programming: what can run (esc_stick_rules.c).  Which profiles the
 * engine runs, the set points a model is given, and the changes, timings,
 * manual steps and power-up positions a run refuses before it starts.
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

TEST_CASE(the_profiles_the_engine_runs_are_the_counted_menus)
{
    unsigned two = 0, one = 0;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        const char *why = NULL;
        switch (esc_stick_kind(p, &why)) {
        case ESC_STICK_KIND_TWO_STAGE: ++two; break;
        case ESC_STICK_KIND_ONE_STAGE: ++one; break;
        default:
            if (why == NULL || why[0] == '\0') {
                T_FAIL("%s refused without a reason", p->id);
            }
            break;
        }
    }
    /* docs/StickProgramming.md gives these counts. */
    CHECK_EQ(two, 13);
    CHECK_EQ(one, 11);

    const char *why = NULL;
    CHECK_EQ(esc_stick_kind(esc_profiles_find("castle-phoenix-edge"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "yes/no menu");
    CHECK_EQ(esc_stick_kind(esc_profiles_find("graupner-brushless-control-t"),
                            &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "needs a person at the ESC");
    /* A jumper pulled during the menu has no moment the run can know. */
    CHECK_EQ(esc_stick_kind(esc_profiles_find("kontronik-mini20"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    /* A switch thrown at power-up with the stick at MAX: no hand at a
     * powered ESC whose stick is not at MIN. */
    CHECK_EQ(esc_stick_kind(esc_profiles_find("turnigy-aquastar"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    CHECK_EQ(esc_stick_kind(esc_profiles_find("kontronik-jazz"), &why),
             ESC_STICK_KIND_ONE_STAGE);
    CHECK_EQ(esc_stick_kind(esc_profiles_find("robbe-roxxy-bl-smart-control"),
                            &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "item and value, one move");
    CHECK_EQ(esc_stick_kind(NULL, &why), ESC_STICK_KIND_NONE);
    /* The YGE menus rest at minimum after a maximum entry whose length no
     * manual gives: the move is not guessed. */
    CHECK_EQ(esc_stick_kind(esc_profiles_find("yge-mode-setup-5"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "rest move, no entry time");
    CHECK_EQ(esc_stick_listen(esc_profiles_find("yge-mode-setup-5")),
             ESC_THR_MIN);
    /* The Silver Series toggles its brake; it sounds no menu of values. */
    CHECK_EQ(esc_stick_kind(
                 esc_profiles_find("greatplanes-electrifly-silver-series"),
                 &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "menu of its own kind");
    CHECK_EQ(esc_stick_listen(esc_profiles_find("hobbywing-flyfun-8item")),
             ESC_THR_MAX);
}

TEST_CASE(a_run_that_cannot_work_is_refused_before_it_starts)
{
    rig("hobbywing-flyfun-8item");
    const char *why = NULL;
    esc_stick_change_t c[2] = { change(1, 2), change(1, 1) };
    CHECK(!esc_stick_check(r.p, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "one change per item");
    CHECK(!esc_stick_check(r.p, c, 0, &r.t, &why));
    CHECK(!esc_stick_check(r.p, NULL, 1, &r.t, &why));
    CHECK(!esc_stick_check(r.p, c, 1, NULL, &why));
    esc_stick_change_t bad = { 200u, 0u };
    CHECK(!esc_stick_check(r.p, &bad, 1, &r.t, &why));
    CHECK_STR_EQ(why, "no such item");
    bad.item = 0u;
    bad.value = 200u;
    CHECK(!esc_stick_check(r.p, &bad, 1, &r.t, &why));
    CHECK_STR_EQ(why, "no such value");
    esc_stick_change_t many[ESC_STICK_MAX_CHANGES + 1u];
    memset(many, 0, sizeof(many));
    CHECK(!esc_stick_check(r.p, many, ESC_STICK_MAX_CHANGES + 1u, &r.t,
                           &why));

    /* Every timing rule, one at a time. */
    static const struct { size_t off; uint32_t v; const char *why; } k[] = {
        { offsetof(esc_stick_timing_t, beep_min_ms), 0u,
          "BEEP MIN and GAP MIN above 0" },
        { offsetof(esc_stick_timing_t, long_ms), 200u, "LONG above BEEP MIN" },
        { offsetof(esc_stick_timing_t, long_max_ms), 500u,
          "LONG MAX above LONG" },
        { offsetof(esc_stick_timing_t, group_gap_ms), 200u,
          "GROUP GAP above GAP MIN" },
        { offsetof(esc_stick_timing_t, hysteresis_ma), 100u,
          "THRESHOLD above HYSTERESIS" },
        { offsetof(esc_stick_timing_t, entry_ms), 500u,
          "ENTRY above 500 ms" },
        /* The item has to be moved on within 3000ms of its tone. */
        { offsetof(esc_stick_timing_t, group_gap_ms), 2800u,
          "GROUP GAP misses the select window" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        esc_stick_timing_t t = r.t;
        memcpy((char *)&t + k[i].off, &k[i].v, sizeof(uint32_t));
        if (esc_stick_check(r.p, c, 1, &t, &why)) {
            T_FAIL("timing case %u accepted", (unsigned)i);
        } else {
            CHECK_STR_EQ(why, k[i].why);
        }
    }
    CHECK(esc_stick_check(r.p, c, 1, &r.t, &why));

    /* A value numbered 0 is never sounded. */
    rig("eflite-pro-sbec-7menu");
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_NONE);

    /* And a refused start leaves the run idle. */
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t dup[2] = { change(1, 2), change(1, 1) };
    CHECK(!start(dup, 2));
    CHECK(!esc_stick_running(&r.e));
}

TEST_CASE(the_voltage_is_the_lowest_cell_count_the_family_states)
{
    CHECK_EQ(esc_stick_profile_mv(esc_profiles_find("hobbywing-flyfun-8item")),
             7600u);
    CHECK_EQ(esc_stick_profile_mv(NULL), 0u);
    esc_model_t m[2] = { { "a", 0, 0, false, 0, 0, 0 }, { "b", 6, 6, true, 0, 0, 0 } };
    esc_profile_t p;
    memset(&p, 0, sizeof(p));
    p.models = m;
    p.model_count = 1u;
    CHECK_EQ(esc_stick_profile_mv(&p), 0u);
    p.model_count = 2u;
    CHECK_EQ(esc_stick_profile_mv(&p), 7200u);   /* six NiMH cells */
}

/* Reset and exit are actions: the ESC acts on the select move and sounds
 * no values, so they are not offered as changes. */
TEST_CASE(reset_and_exit_are_not_changes)
{
    rig("hobbywing-flyfun-8item");
    const char *why = NULL;
    esc_stick_change_t c[2] = { change(3, 2), change(7, 1) };
    CHECK(esc_stick_is_action(&r.p->items[6]));
    CHECK(esc_stick_is_action(&r.p->items[7]));
    CHECK(!esc_stick_is_action(&r.p->items[0]));
    CHECK(!esc_stick_is_action(NULL));
    CHECK(!esc_stick_check(r.p, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "reset and exit are actions, not settings");
    c[1] = change(8, 1);
    CHECK(!esc_stick_check(r.p, c, 2, &r.t, &why));
}

/* An item with one value -- a rule written as a value, "N beeps = N
 * cells" -- is nothing to choose, and is not offered. */
TEST_CASE(an_item_with_one_value_is_not_offered)
{
    rig("hobbywing-flyfun-hv-9item");
    const int i = item_index(r.p, 7);
    CHECK(i >= 0);
    CHECK_STR_EQ(esc_stick_not_offered(&r.p->items[i]),
                 "one value: nothing to choose");
    CHECK_STR_EQ(esc_stick_not_offered(&r.p->items[i + 1]),
                 "reset and exit are actions, not settings");
    CHECK(esc_stick_not_offered(&r.p->items[0]) == NULL);
    CHECK_STR_EQ(esc_stick_not_offered(NULL), "no such item");
    esc_stick_change_t c[1] = { change(7, 1) };
    const char *why = NULL;
    CHECK(!esc_stick_check(r.p, c, 1, &r.t, &why));
    CHECK_STR_EQ(why, "one value: nothing to choose");
}

/*
 * A profile with no entry time of its own but one on every value: the
 * profile-wide rest rule leaves it to the per-change one, which sees each
 * value's time.  YGE mode setup 6 rests away from its entry and states
 * no hold; with a hold on every value it runs, and a value without one is
 * refused by itself.
 */
TEST_CASE(a_hold_on_every_value_times_the_rest_move)
{
    const char *why = NULL;
    const esc_profile_t *y = esc_profiles_find("yge-mode-setup-6");
    CHECK(y != NULL);
    if (y == NULL) {
        return;
    }
    CHECK_EQ(esc_stick_kind(y, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "rest move, no entry time");
    static esc_profile_t q;
    static esc_item_t items[8];
    static esc_value_t values[8][16];
    q = *y;
    CHECK(y->item_count <= 8u);
    for (unsigned i = 0; i < y->item_count && i < 8u; ++i) {
        items[i] = y->items[i];
        CHECK(items[i].value_count <= 16u);
        for (unsigned v = 0; v < items[i].value_count && v < 16u; ++v) {
            values[i][v] = y->items[i].values[v];
            values[i][v].entry_hold_ms = 3000u;
        }
        items[i].values = values[i];
    }
    q.items = items;
    CHECK(esc_stick_kind(&q, &why) != ESC_STICK_KIND_NONE);
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    esc_stick_change_t c[1] = { { 0u, 0u } };
    CHECK(esc_stick_check(&q, c, 1, &t, &why));
    CHECK_EQ(esc_stick_change_entry_ms(&q, &c[0], &t), 3000u);
    values[0][0].entry_hold_ms = 0u;            /* this one states none */
    CHECK(esc_stick_kind(&q, &why) != ESC_STICK_KIND_NONE);
    CHECK(!esc_stick_check(&q, c, 1, &t, &why));
    CHECK_STR_EQ(why, "rest move, no entry time");
    if (items[0].value_count > 1u) {
        c[0].value = 1u;
        CHECK(esc_stick_check(&q, c, 1, &t, &why));
    }
}

/* What the run cannot wait for is refused before it starts. */
TEST_CASE(a_step_the_run_cannot_wait_for_is_refused)
{
    const char *why = NULL;
    static const esc_manual_t during[] = {
        { ESC_MANUAL_DURING_MENU, "Pull the jumper.", 0u, NULL, false, false },
    };
    rig_hand("kontronik-jazz", during, 1);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    /* A hand at a powered ESC with the stick at MAX: sunrise-pro enters
     * at full throttle. */
    static const esc_manual_t menu[] = {
        { ESC_MANUAL_BEFORE_MENU, "Press the button.", 0u, NULL, false, false },
    };
    rig_hand("sunrise-pro", menu, 1);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    static const esc_manual_t at_power[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 0u, NULL, false, false },
    };
    rig_hand("sunrise-pro", at_power, 1);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    /* Before the power and after the run nobody reaches a powered ESC. */
    static const esc_manual_t outside[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u, NULL, false, false },
        { ESC_MANUAL_AFTER_PROGRAMMING, "Pull the jumper.", 0u, NULL, false, false },
    };
    rig_hand("sunrise-pro", outside, 2);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_ONE_STAGE);
    /* Assisted with no steps: a person at the ESC the run cannot ask. */
    rig_hand("kontronik-jazz", outside, 0);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "needs a person at the ESC");
}

/* What one power-up cannot take is refused, not reordered or guessed. */
TEST_CASE(a_power_up_position_that_cannot_work_is_refused)
{
    const char *why = NULL;
    /* Two stages: the ESC picks the order, so the changes share one. */
    rig("hobbywing-flyfun-8item");
    static esc_profile_t two;
    static esc_item_t two_items[2];
    static esc_value_t two_values[2];
    two = *r.p;
    memcpy(two_items, r.p->items, sizeof(two_items));
    memcpy(two_values, r.p->items[1].values, sizeof(two_values));
    two_values[1].entry_throttle = ESC_THR_MID;
    two_items[1].values = two_values;
    two.items = two_items;
    two.item_count = 2;
    esc_stick_change_t c[2] = { { 0u, 1u }, { 1u, 1u } };
    CHECK(!esc_stick_check(&two, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "changes need different power-up positions");
    c[0] = (esc_stick_change_t){ 1u, 1u };
    CHECK(esc_stick_check(&two, c, 1, &r.t, &why));
    /* The rest there is the select move: hobbywing selects at MIN. */
    two_values[1].entry_throttle = ESC_THR_MIN;
    CHECK(!esc_stick_check(&two, c, 1, &r.t, &why));
    CHECK_STR_EQ(why, "select move is the rest");

    /* A hand at a powered ESC with the stick at MAX. */
    rig("kontronik-jazz");
    static esc_profile_t jazz;
    static esc_item_t jazz_item;
    static esc_value_t jazz_values[9];
    jazz = *r.p;
    jazz_item = r.p->items[0];
    memcpy(jazz_values, r.p->items[0].values,
           r.p->items[0].value_count * sizeof(jazz_values[0]));
    jazz_values[2].entry_throttle = ESC_THR_MAX;
    jazz_item.values = jazz_values;
    jazz.items = &jazz_item;
    esc_stick_change_t m[1] = { { 0u, 2u } };
    CHECK(!esc_stick_check(&jazz, m, 1, &r.t, &why));
    CHECK_STR_EQ(why, "select move is the rest");
    jazz.select_throttle = ESC_THR_MID;
    CHECK(!esc_stick_check(&jazz, m, 1, &r.t, &why));
    CHECK_STR_EQ(why, "manual step");
    /* A named rest away from a value's position needs the entry time. */
    jazz.listen_throttle = ESC_THR_MIN;
    jazz.entry_hold_ms = 0u;
    jazz_values[2].entry_throttle = ESC_THR_MID;
    jazz.select_throttle = ESC_THR_MAX;
    CHECK(!esc_stick_check(&jazz, m, 1, &r.t, &why));
    CHECK_STR_EQ(why, "rest move, no entry time");
    /* An at_power_up step's hold is a time the profile states: the run
     * waits it (esc_stick_change_entry_ms()), so the move to the rest is
     * timed and the change runs.  Entry MIN, the value at MID, the rest at
     * MIN, the hold the only time given. */
    static const esc_manual_t k_held[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 3000u, NULL, false, false },
    };
    jazz.manual = k_held;
    jazz.manual_count = 1;
    CHECK_EQ(jazz.entry_throttle, ESC_THR_MIN);
    CHECK_EQ(esc_stick_change_entry(&jazz, &m[0]), ESC_THR_MID);
    CHECK_EQ(esc_stick_change_entry_ms(&jazz, &m[0], &r.t), 3000u);
    CHECK(esc_stick_check(&jazz, m, 1, &r.t, &why));
    /* The same hold, the value at the entry's MIN and the rest at MID:
     * the profile-wide rule takes the hold too. */
    jazz.listen_throttle = ESC_THR_MID;
    jazz_values[2].entry_throttle = ESC_THR_NONE;
    CHECK(esc_stick_kind(&jazz, &why) != ESC_STICK_KIND_NONE);
    jazz.manual = NULL;
    jazz.manual_count = 0;
    jazz.automatable = ESC_AUTO_FULL;
    CHECK_EQ(esc_stick_kind(&jazz, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "rest move, no entry time");
}

/*
 * Items that apply to different models may share their number and a
 * value number, each value with its own moves after the selection.  The
 * simulated ESC takes the first match's moves only, and never writes more
 * than the space it is given: the store move and 4 moves each would be 9
 * into the sim's 5 (an overflow ASan reports without the cap).
 */
TEST_CASE(shared_numbers_do_not_overflow_the_store_moves)
{
    static esc_value_t v1[1], v2[1];
    static esc_item_t it[2];
    static esc_profile_t p;
    p = *esc_profiles_find("kontronik-pix");
    v1[0] = (esc_value_t){ "a", 2u, false, ESC_THR_NONE, 0u, 4u,
                           { ESC_THR_MIN, ESC_THR_MAX, ESC_THR_MIN,
                             ESC_THR_MAX } };
    v2[0] = (esc_value_t){ "b", 2u, false, ESC_THR_NONE, 0u, 4u,
                           { ESC_THR_MID, ESC_THR_MIN, ESC_THR_MID,
                             ESC_THR_MIN } };
    it[0] = p.items[0];
    it[0].number = 1u;
    it[0].values = v1;
    it[0].value_count = 1u;
    it[1] = it[0];
    it[1].values = v2;
    p.items = it;
    p.item_count = 2u;
    p.store_throttle = ESC_THR_MID;
    esc_throttle_t out[ESC_AFTER_MAX + 1u];
    CHECK_EQ(esc_sim_store_moves(&p, 1u, 2u, out,
                                 sizeof(out) / sizeof(out[0])), 5u);
    CHECK_EQ(out[0], ESC_THR_MID);              /* the store move */
    CHECK_EQ(out[1], ESC_THR_MIN);              /* the first match's */
    CHECK_EQ(out[4], ESC_THR_MAX);
    esc_throttle_t two[2];
    CHECK_EQ(esc_sim_store_moves(&p, 1u, 2u, two, 2u), 2u);
    CHECK_EQ(esc_sim_store_moves(&p, 1u, 9u, out, 5u), 1u);   /* no value */
    CHECK_EQ(esc_sim_store_moves(NULL, 1u, 2u, out, 5u), 0u);
    CHECK_EQ(esc_sim_store_moves(&p, 1u, 2u, NULL, 5u), 0u);
}

/*
 * The ESC's ratings hold the supply's set points: a voltage over the
 * model's v_max_mv is refused, set by hand or from the cell count; where
 * the rating is not stated only the model's own cell count is taken; a
 * current over the rated one is refused.
 */
TEST_CASE(the_set_points_stay_within_the_escs_ratings)
{
    const esc_profile_t *p = esc_profiles_find("hobbywing-flyfun-8item");
    CHECK(p != NULL);
    if (p == NULL) {
        return;
    }
    int six = -1;
    for (int m = 0; m < (int)p->model_count; ++m) {
        six = (strcmp(p->models[m].name, "FlyFun-6A") == 0) ? m : six;
    }
    CHECK(six >= 0);
    if (six < 0) {
        return;
    }
    CHECK_EQ(esc_stick_model_v_max(p, six), 8400u);
    CHECK_EQ(esc_stick_rating(p, six, 20000u, 1000u, true),
             ESC_STICK_RATING_V_OVER);
    CHECK_EQ(esc_stick_rating(p, six, 8500u, 1000u, true),
             ESC_STICK_RATING_V_OVER);
    CHECK_EQ(esc_stick_rating(p, six, 8400u, 1000u, true),
             ESC_STICK_RATING_OK);
    CHECK_EQ(esc_stick_rating(p, six, 7600u, 1000u, true),
             ESC_STICK_RATING_OK);
    CHECK_EQ(esc_stick_rating(p, six, esc_stick_model_mv(p, six), 1000u,
                              false), ESC_STICK_RATING_OK);
    /* The current: FlyFun-6A is rated 6A. */
    CHECK_EQ(esc_stick_model_ma_max(p, six), 6000u);
    CHECK_EQ(esc_stick_rating(p, six, 7600u, 6100u, true),
             ESC_STICK_RATING_I_OVER);
    CHECK_EQ(esc_stick_rating(p, six, 7600u, 6000u, true),
             ESC_STICK_RATING_OK);
    /* The family: the lowest rating of its models. */
    CHECK_EQ(esc_stick_model_v_max(p, -1), 8400u);
    CHECK_EQ(esc_stick_rating(p, -1, 12000u, 1000u, true),
             ESC_STICK_RATING_V_OVER);

    /* No rating stated: nothing is refused for it.  VOLTAGE by hand, and
     * another model's cell count standing in, are said to be unvouched;
     * the model's own cell count is not. */
    static esc_model_t k_m[2];
    static esc_profile_t q;
    q = *p;
    k_m[0] = p->models[six];
    k_m[0].v_max_mv = 0u;                  /* cells stated, no rating */
    k_m[1] = p->models[six];
    k_m[1].v_max_mv = 0u;
    k_m[1].cells_min = 0u;                 /* neither */
    k_m[1].cells_max = 0u;
    q.models = k_m;
    q.model_count = 2;
    CHECK_EQ(esc_stick_model_v_max(&q, 0), 0u);
    CHECK_EQ(esc_stick_rating(&q, 0, 20000u, 1000u, true),
             ESC_STICK_RATING_OK);
    CHECK(esc_stick_rating_unknown(&q, 0, 7600u, true));
    CHECK(!esc_stick_rating_unknown(&q, 0, esc_stick_model_mv(&q, 0), false));
    CHECK_EQ(esc_stick_rating(&q, 1, esc_stick_model_mv(&q, 1), 1000u, false),
             ESC_STICK_RATING_OK);
    CHECK(esc_stick_rating_unknown(&q, 1, esc_stick_model_mv(&q, 1), false));
    CHECK(esc_stick_rating_unknown(&q, 1, 0u, false));
    CHECK(!esc_stick_rating_unknown(&q, -1, 7600u, false));
    CHECK(esc_stick_rating_unknown(&q, -1, 7600u, true));
    CHECK(!esc_stick_rating_unknown(p, six, 20000u, true));   /* stated */
    /* A stated current or lowest input still refuses with no voltage
     * rating. */
    k_m[0].current_a = 1u;
    CHECK_EQ(esc_stick_rating(&q, 0, 7600u, 2000u, true),
             ESC_STICK_RATING_I_OVER);
    k_m[0].current_a = 6u;
    k_m[0].v_min_mv = 8000u;
    CHECK_EQ(esc_stick_rating(&q, 0, 7600u, 1000u, true),
             ESC_STICK_RATING_V_UNDER);
    k_m[0].v_min_mv = 0u;
    CHECK_EQ(esc_stick_model_v_max(&q, -1), 0u);
    CHECK_EQ(esc_stick_model_v_max(NULL, -1), 0u);
    CHECK_EQ(esc_stick_model_ma_max(NULL, 0), 0u);
    CHECK_EQ(esc_stick_model_v_max(p, (int)p->model_count), 0u);
    CHECK_EQ(esc_stick_model_ma_max(p, (int)p->model_count), 0u);

    /* The lowest input: Hacker Master Basic 90 Opto takes 12V and up. */
    const esc_profile_t *hb = esc_profiles_find("hacker-master-basic");
    CHECK(hb != NULL);
    for (int m = 0; hb != NULL && m < (int)hb->model_count; ++m) {
        if (strcmp(hb->models[m].name, "Master Basic 90 Opto") != 0) {
            continue;
        }
        CHECK_EQ(esc_stick_model_v_min(hb, m), 12000u);
        CHECK_EQ(esc_stick_rating(hb, m, 11900u, 1000u, true),
                 ESC_STICK_RATING_V_UNDER);
        CHECK_EQ(esc_stick_rating(hb, m, 12000u, 1000u, true),
                 ESC_STICK_RATING_OK);
    }
    CHECK_EQ(esc_stick_model_v_min(hb, -1), 12000u);
    CHECK_EQ(esc_stick_model_v_min(NULL, 0), 0u);
    CHECK_EQ(esc_stick_model_v_min(p, (int)p->model_count), 0u);

    /* The automatic voltage of every model of record stays within its
     * rating, at or over its lowest input, and the CURRENT LIMIT's most
     * (3A) under every stated current. */
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *x = esc_profiles_at(i);
        for (int m = 0; m < (int)x->model_count; ++m) {
            const uint32_t mv = esc_stick_model_mv(x, m);
            const uint32_t vm = esc_stick_model_v_max(x, m);
            if (vm != 0u && mv > vm) {
                T_FAIL("%s %s: %u mV over %u mV", x->id, x->models[m].name,
                       (unsigned)mv, (unsigned)vm);
            }
            const uint32_t vl = esc_stick_model_v_min(x, m);
            if (x->models[m].cells_min != 0u && mv < vl) {
                T_FAIL("%s %s: %u mV under %u mV", x->id, x->models[m].name,
                       (unsigned)mv, (unsigned)vl);
            }
            const uint32_t am = esc_stick_model_ma_max(x, m);
            if (am != 0u && am < 3000u) {
                T_FAIL("%s %s: rated %u mA", x->id, x->models[m].name,
                       (unsigned)am);
            }
        }
    }
}

/*
 * What the PD mini's 20V leaves out, as the docs count it: of the profiles
 * the engine runs, one family whose lowest cell count needs more
 * (hobbywing-skywalker-v2-hv-opto, 22.8V), and six model rows, each at
 * 22.8V: its two models and four of families that open.
 */
TEST_CASE(twenty_volts_leave_out_one_family_and_six_models)
{
    static const char *const k_rows[] = {
        "FLYFUN 130A HV OPTO V5", "FLYFUN 160A HV OPTO V5",
        "Skywalker 130A HV OPTO V2", "Skywalker 160A HV OPTO V2",
        "Gecko 120A OPTO HV", "Gecko 150A OPTO HV",
    };
    unsigned families = 0u, rows = 0u, runs = 0u;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        if (esc_stick_kind(p, NULL) == ESC_STICK_KIND_NONE) {
            continue;
        }
        ++runs;
        if (esc_stick_model_mv(p, -1) > 20000u) {
            ++families;
            CHECK_STR_EQ(p->id, "hobbywing-skywalker-v2-hv-opto");
            CHECK_EQ(esc_stick_model_mv(p, -1), 22800u);
        }
        for (int m = 0; m < (int)p->model_count; ++m) {
            if (esc_stick_model_mv(p, m) <= 20000u) {
                continue;
            }
            ++rows;
            CHECK_EQ(esc_stick_model_mv(p, m), 22800u);
            bool known = false;
            for (size_t k = 0; k < sizeof(k_rows) / sizeof(k_rows[0]); ++k) {
                known = known || strcmp(p->models[m].name, k_rows[k]) == 0;
            }
            if (!known) {
                T_FAIL("%s: %s needs %u mV", p->id, p->models[m].name,
                       (unsigned)esc_stick_model_mv(p, m));
            }
        }
    }
    CHECK_EQ(runs, 24u);
    CHECK_EQ(families, 1u);
    CHECK_EQ(rows, 6u);
}

/* A two-stage power-up takes one entry time for every change; a value
 * whose time is under the settle time cannot have a floor. */
TEST_CASE(an_entry_time_that_cannot_work_is_refused)
{
    const char *why = NULL;
    rig("hobbywing-flyfun-8item");
    static esc_profile_t two;
    static esc_item_t two_items[2];
    static esc_value_t two_values[2];
    two = *r.p;
    memcpy(two_items, r.p->items, sizeof(two_items));
    memcpy(two_values, r.p->items[1].values, sizeof(two_values));
    two_values[1].entry_hold_ms = 9000u;
    two_items[1].values = two_values;
    two.items = two_items;
    two.item_count = 2;
    esc_stick_change_t c[2] = { { 0u, 1u }, { 1u, 1u } };
    CHECK(!esc_stick_check(&two, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "changes need different entry times");
    CHECK(esc_stick_check(&two, &c[1], 1, &r.t, &why));
    two_values[1].entry_hold_ms = 400u;
    CHECK(!esc_stick_check(&two, &c[1], 1, &r.t, &why));
    CHECK_STR_EQ(why, "ENTRY above 500 ms");
}

/* Values asking 2s and 3s, with a button held 5s at power-up: the run
 * waits 5s for either, so they share a power-up.  The check compares what
 * the run waits, not what the values ask. */
TEST_CASE(shared_entry_times_compare_what_the_run_waits)
{
    const char *why = NULL;
    rig("hobbywing-flyfun-8item");
    static esc_profile_t two;
    static esc_item_t two_items[2];
    static esc_value_t v0[2], v1[2];
    static const esc_manual_t hold[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold SET.", 5000u, NULL, false, false },
    };
    two = *r.p;
    memcpy(two_items, r.p->items, sizeof(two_items));
    memcpy(v0, r.p->items[0].values, sizeof(v0));
    memcpy(v1, r.p->items[1].values, sizeof(v1));
    v0[1].entry_hold_ms = 2000u;
    v1[1].entry_hold_ms = 3000u;
    two_items[0].values = v0;
    two_items[1].values = v1;
    two.items = two_items;
    two.item_count = 2;
    /* A hand at a powered ESC is asked for at MIN. */
    two.entry_throttle = ESC_THR_MIN;
    two.select_throttle = ESC_THR_MAX;
    two.value_select_throttle = ESC_THR_MID;
    two.listen_throttle = ESC_THR_NONE;
    two.automatable = ESC_AUTO_ASSISTED;
    two.manual = hold;
    two.manual_count = 1;
    CHECK_EQ(esc_stick_kind(&two, NULL), ESC_STICK_KIND_TWO_STAGE);
    esc_stick_change_t c[2] = { { 0u, 1u }, { 1u, 1u } };
    CHECK_EQ(esc_stick_change_entry_ms(&two, &c[0], &r.t), 5000u);
    CHECK_EQ(esc_stick_change_entry_ms(&two, &c[1], &r.t), 5000u);
    CHECK(esc_stick_check(&two, c, 2, &r.t, &why));
    /* A hold shorter than one of them: 3s against 2.5s, refused. */
    static const esc_manual_t short_hold[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold SET.", 2500u, NULL, false, false },
    };
    two.manual = short_hold;
    CHECK(!esc_stick_check(&two, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "changes need different entry times");
}

int main(void)
{
    RUN(the_profiles_the_engine_runs_are_the_counted_menus);
    RUN(a_run_that_cannot_work_is_refused_before_it_starts);
    RUN(the_voltage_is_the_lowest_cell_count_the_family_states);
    RUN(reset_and_exit_are_not_changes);
    RUN(an_item_with_one_value_is_not_offered);
    RUN(a_hold_on_every_value_times_the_rest_move);
    RUN(a_step_the_run_cannot_wait_for_is_refused);
    RUN(a_power_up_position_that_cannot_work_is_refused);
    RUN(shared_numbers_do_not_overflow_the_store_moves);
    RUN(the_set_points_stay_within_the_escs_ratings);
    RUN(twenty_volts_leave_out_one_family_and_six_models);
    RUN(an_entry_time_that_cannot_work_is_refused);
    RUN(shared_entry_times_compare_what_the_run_waits);
    return test_summary("esc_stick_rules");
}
