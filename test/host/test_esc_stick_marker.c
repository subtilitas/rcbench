/*
 * Stick programming: the item loop's marker (scheme.announce.loop_marker).
 * A Flycolor FlyDragon Lite 40A sounds its 12321 melody, one 808ms tone
 * to the tap, before every pass of its item loop; the value loops have
 * none.  The simulation sounds it as the tap heard it on 2026-11-11: 128ms
 * short and 384ms long beeps, 148ms between the beeps of a group, 3230ms
 * between groups and the 808ms marker.
 *
 * Held here: a run whose rule does not know the marker never reaches
 * item 1; with it, items 1 and 2 are selected on the second pass, from the
 * tap and from INA3221 windows; exactly one foreign group passes after the
 * highest item, and a second breaks the order; a profile without the field
 * breaks the order at the first; the value loops let none pass.  The
 * FlyDragon Lite's profile runs, with item 8 cut to 4 values on the 40A.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"
#include "esc_sim.h"
#include "esc_stick.h"

#include "esc_stick_rig.h"

#define FLYDRAGON "flycolor-flydragon-lite-plus"
#define LITE      "flycolor-flydragon-lite"

/* The profile the engine reads and the one the simulated ESC follows,
 * each a copy of the profile of record that a case may change. */
static esc_profile_t g_run, g_esc;
static esc_item_t    g_run_items[16];

/*
 * The FlyDragon as the tap heard it, counted from @p src.  The run reads
 * g_run, the simulated ESC follows g_esc; both start as the profile of
 * record.  From INA3221 windows GAP MIN is 50ms, as a 148ms gap can hold
 * a single whole 50ms window; from the tap it is the profile's 64ms.
 */
static void flydragon(esc_stick_src_t src)
{
    rig(FLYDRAGON);
    g_run = *r.p;
    g_esc = *r.p;
    memcpy(g_run_items, r.p->items, r.p->item_count * sizeof(esc_item_t));
    g_run.items = g_run_items;
    r.p = &g_run;
    r.src = src;
    r.t.group_gap_ms = 700u;
    if (src == ESC_STICK_SRC_METER) {
        r.t.gap_min_ms = 50u;
        r.win_ms = 50u;
        r.read_iv = 152u;       /* the supply's own readings, power only */
    }
    r.tap_lag = 60u;            /* the panel has a beep 60ms after its end */
    r.tap_skew = 123456u;       /* the tap's clock is its own */
}

/* The simulated ESC on g_esc as it stands now. */
static void esc_on(void)
{
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.beep_ms = 128u;
    c.long_ms = 384u;
    c.gap_ms = 148u;
    c.pause_ms = 3230u;
    CHECK_EQ(c.marker_ms, 808u);    /* the default: the tap's 808ms */
    esc_sim_init(&r.sim, &g_esc, &c);
}

/* Run until the run ends, at most 400s. */
static void run_out(void)
{
    run_for(400000u);
}

TEST_CASE(the_flydragon_profiles_set_the_loop_marker)
{
    const esc_profile_t *p = esc_profiles_find(FLYDRAGON);
    CHECK(p != NULL);
    CHECK(p->loop_marker);
    const esc_profile_t *lite = esc_profiles_find(LITE);
    CHECK(lite != NULL);
    CHECK(lite->loop_marker);
    /* No other profile of record sets it. */
    unsigned set = 0u;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        set += esc_profiles_at(i)->loop_marker ? 1u : 0u;
    }
    CHECK_EQ(set, 2u);
}

/* The index of model @p name in @p p, or -1. */
static int model_of(const esc_profile_t *p, const char *name)
{
    for (unsigned i = 0; i < p->model_count; ++i) {
        if (strcmp(p->models[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* The values item 8 (Battery cells) has on @p model: 4 on the 40A 2-4S, as
 * its value loop sounds them, 6 on the 60A to 120A 3-6S, as the manual
 * lists them; 0 when no item 8 is on the model. */
static unsigned cells_values(const esc_profile_t *p, int model)
{
    unsigned n = 0u;
    for (unsigned i = 0; i < p->item_count; ++i) {
        if (p->items[i].number == 8u && esc_item_applies(p, i, model)) {
            n += p->items[i].value_count;
        }
    }
    return n;
}

/* The FlyDragon Lite: a two-stage menu the engine runs, its measured
 * lengths, and item 8 by model. */
TEST_CASE(the_flydragon_lite_runs_with_item_8_by_model)
{
    const esc_profile_t *p = esc_profiles_find(LITE);
    CHECK(p != NULL);
    CHECK_EQ(esc_stick_kind(p, NULL), ESC_STICK_KIND_TWO_STAGE);
    CHECK_EQ(p->item_count, 11u);
    CHECK_EQ(p->model_count, 5u);
    CHECK_EQ(cells_values(p, model_of(p, "FlyDragon Lite 40A")), 4u);
    CHECK_EQ(cells_values(p, model_of(p, "FlyDragon Lite 60A")), 6u);
    CHECK_EQ(cells_values(p, model_of(p, "FlyDragon Lite 120A")), 6u);
    CHECK_EQ(cells_values(p, -1), 0u);  /* not on every model */
    /* 2S at 3.8V a cell: the family opens at the PD mini's 20V. */
    CHECK(esc_stick_profile_mv(p) <= 20000u);
}

/* The Lite 40A: item 8 value 4 (4S) and item 1 value 2 from one power-up,
 * over the marker, from the tap. */
TEST_CASE(the_flydragon_lite_40a_stores_4s_and_a_brake)
{
    flydragon(ESC_STICK_SRC_TAP);
    const esc_profile_t *lite = esc_profiles_find(LITE);
    g_run = *lite;
    g_esc = *lite;
    memcpy(g_run_items, lite->items, lite->item_count * sizeof(esc_item_t));
    g_run.items = g_run_items;
    esc_on();
    esc_stick_change_t c[2];
    c[0] = change(1, 2);
    c[1] = change(8, 4);
    CHECK_EQ(g_run.items[c[1].item].value_count, 4u);  /* the 40A's */
    CHECK(start(c, 2u));
    run_out();
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
    CHECK_EQ(esc_sim_stored(&r.sim, 8), 4);
    ended_safe();
}

/* The simulated ESC sounds the marker before each pass of the item loop
 * and never in a value loop: the tap's beeps of 808ms, each alone, with a
 * group's pause after it, and no move answers it. */
TEST_CASE(the_simulation_sounds_the_marker_before_each_item_pass)
{
    flydragon(ESC_STICK_SRC_TAP);
    esc_on();
    unsigned markers = 0u;
    bool high = false;
    uint32_t rise = 0u, marker_fall = 0u;
    uint32_t marker_groups[3] = { 0 };
    const uint32_t on = 1000u;
    float pct = 100.0f;
    for (uint32_t t = on; t < on + 130000u; ++t) {
        const int32_t ma = esc_sim_step(&r.sim, t, true, pct);
        const bool sounding = ma > (int32_t)(r.sim.c.idle_ma
                                             + r.sim.c.beep_ma / 2u);
        if (sounding && !high) {
            rise = t;
            if (marker_fall != 0u) {
                /* Item 1 follows the marker after the pause, alone. */
                CHECK(t - marker_fall >= r.sim.c.pause_ms);
                marker_fall = 0u;
            }
        } else if (!sounding && high && t - rise == 808u) {
            if (markers < 3u) {
                marker_groups[markers] = r.sim.menu_groups;
            }
            markers++;
            marker_fall = t;
        }
        high = sounding;
        if (markers == 2u && marker_fall != 0u && t - marker_fall == 200u) {
            pct = 0.0f;             /* the select move, after the marker */
        } else if (pct == 0.0f && t - marker_fall == 400u) {
            pct = 100.0f;
        }
    }
    CHECK_EQ(markers, 3u);
    /* Before item 1 of each pass: after 0, 10 and 20 menu groups. */
    CHECK_EQ(marker_groups[0], 0u);
    CHECK_EQ(marker_groups[1], 10u);
    CHECK_EQ(marker_groups[2], 20u);
    CHECK_EQ(r.sim.mode, ESC_SIM_ITEMS);   /* the move selected nothing */
}

/* The bug the field answers: the ESC sounds the marker, the run's rule
 * does not know it.  The marker comes between items 10 and 1, so item 1
 * never follows the highest item in order, and the run ends with TIMEOUT
 * after 180s. */
TEST_CASE(a_run_that_does_not_know_the_marker_never_reaches_item_1)
{
    flydragon(ESC_STICK_SRC_TAP);
    g_run.loop_marker = false;
    esc_on();
    const esc_stick_change_t c = change(1, 1);
    CHECK(start(&c, 1u));
    run_out();
    CHECK_EQ(r.e.reason, ESC_STICK_R_TIMEOUT);
    CHECK_EQ(r.e.phase, ESC_STICK_ABORTED);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 0);
    ended_safe();
}

/* With the marker known, item 1 is taken on the second pass (10, the
 * marker, 1) and item 2 after it, from one power-up. */
static void items_1_and_2(esc_stick_src_t src)
{
    flydragon(src);
    esc_on();
    esc_stick_change_t c[2];
    c[0] = change(1, 2);
    c[1] = change(2, 2);
    CHECK(start(c, 2u));
    run_out();
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.reason, ESC_STICK_R_NONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 2);
    CHECK_EQ(r.e.entries, 1u);
    ended_safe();
}

TEST_CASE(items_1_and_2_are_selected_from_the_tap)
{
    items_1_and_2(ESC_STICK_SRC_TAP);
}

TEST_CASE(items_1_and_2_are_selected_from_ina3221_windows)
{
    items_1_and_2(ESC_STICK_SRC_METER);
}

/* Item 1 alone, value 1: the case the tester's run ended with TIMEOUT on. */
TEST_CASE(item_1_value_1_is_stored_from_the_tap_and_from_the_windows)
{
    const esc_stick_src_t src[2] = { ESC_STICK_SRC_TAP,
                                     ESC_STICK_SRC_METER };
    for (unsigned i = 0; i < 2u; ++i) {
        flydragon(src[i]);
        esc_on();
        const esc_stick_change_t c = change(1, 1);
        CHECK(start(&c, 1u));
        run_out();
        CHECK_EQ(r.e.phase, ESC_STICK_DONE);
        CHECK_EQ(esc_sim_stored(&r.sim, 1), 1);
    }
}

/* The run reads the menu as items 2 to 10, so the ESC's item 1 is foreign
 * after the run's highest, 10: neither the loop's lowest, 2, nor one more
 * than 10.  @p marker: whether the run's profile sets the field. */
static void without_item_1(esc_stick_src_t src, bool marker)
{
    flydragon(src);
    g_run.items = g_run_items + 1;
    g_run.item_count = 9u;
    g_run.loop_marker = marker;
}

/*
 * Exactly one foreign group passes.  Without the ESC's marker the one
 * foreign group after 10 is its item 1, and item 2 is taken after it;
 * with the marker too, two foreign groups follow 10, the order breaks and
 * the run ends with TIMEOUT.
 */
TEST_CASE(one_foreign_group_passes_after_the_highest_item_and_a_second_breaks_the_order)
{
    const esc_stick_src_t src[2] = { ESC_STICK_SRC_TAP,
                                     ESC_STICK_SRC_METER };
    for (unsigned i = 0; i < 2u; ++i) {
        without_item_1(src[i], true);
        g_esc.loop_marker = false;  /* 10, 1, 2 */
        esc_on();
        const esc_stick_change_t c = change(2, 2);
        CHECK(start(&c, 1u));
        run_out();
        CHECK_EQ(r.e.phase, ESC_STICK_DONE);
        CHECK_EQ(esc_sim_stored(&r.sim, 2), 2);

        without_item_1(src[i], true);   /* 10, the marker, 1, 2 */
        esc_on();
        CHECK(start(&c, 1u));
        run_out();
        CHECK_EQ(r.e.reason, ESC_STICK_R_TIMEOUT);
        CHECK_EQ(esc_sim_stored(&r.sim, 2), 0);
        ended_safe();
    }
}

/* The marker passes on every pass, not once a run.  Item 1 of the second
 * pass sounds no beep, so item 2 follows the marker and breaks the order;
 * the third pass's marker passes again, and item 1 is taken there. */
TEST_CASE(the_marker_passes_again_on_the_next_pass)
{
    flydragon(ESC_STICK_SRC_TAP);
    esc_on();
    r.sim.c.drop_group = 10;        /* item 1 of the second pass */
    const esc_stick_change_t c = change(1, 1);
    CHECK(start(&c, 1u));
    run_out();
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 1);
    CHECK(r.sim.menu_groups > 20u); /* on the third pass */
}

/* A profile without the field: the one foreign group after the highest
 * item breaks the order, as in every profile before the field. */
TEST_CASE(a_profile_without_the_marker_lets_no_foreign_group_pass)
{
    without_item_1(ESC_STICK_SRC_TAP, false);
    g_esc.loop_marker = false;      /* 10, 1, 2 */
    esc_on();
    const esc_stick_change_t c = change(2, 2);
    CHECK(start(&c, 1u));
    run_out();
    CHECK_EQ(r.e.reason, ESC_STICK_R_TIMEOUT);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 0);

    /* And with the ESC's menu as the profile's, the wrap from 10 to 1
     * takes item 1 as it does with the field set. */
    flydragon(ESC_STICK_SRC_TAP);
    g_run.loop_marker = false;
    g_esc.loop_marker = false;
    esc_on();
    const esc_stick_change_t c1 = change(1, 1);
    CHECK(start(&c1, 1u));
    run_out();
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 1);
}

/* The value loops let no foreign group pass.  The run reads Brake's values
 * as 2 to 4, the ESC sounds 1 to 4: its value 1 after the run's highest,
 * 4, breaks the order, so value 2 is never taken. */
TEST_CASE(a_value_loop_lets_no_foreign_group_pass)
{
    flydragon(ESC_STICK_SRC_TAP);
    g_run_items[0].values = r.sim.p->items[0].values + 1;
    g_run_items[0].value_count = 3u;
    esc_on();
    const esc_stick_change_t c = change(1, 2);
    CHECK(start(&c, 1u));
    run_until_phase(ESC_STICK_VALUES, 200000u);
    CHECK_EQ(r.e.phase, ESC_STICK_VALUES);
    run_out();
    CHECK_EQ(r.e.reason, ESC_STICK_R_TIMEOUT);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 0);
}

int main(void)
{
    RUN(the_flydragon_profiles_set_the_loop_marker);
    RUN(the_flydragon_lite_runs_with_item_8_by_model);
    RUN(the_flydragon_lite_40a_stores_4s_and_a_brake);
    RUN(the_simulation_sounds_the_marker_before_each_item_pass);
    RUN(a_run_that_does_not_know_the_marker_never_reaches_item_1);
    RUN(items_1_and_2_are_selected_from_the_tap);
    RUN(items_1_and_2_are_selected_from_ina3221_windows);
    RUN(item_1_value_1_is_stored_from_the_tap_and_from_the_windows);
    RUN(one_foreign_group_passes_after_the_highest_item_and_a_second_breaks_the_order);
    RUN(the_marker_passes_again_on_the_next_pass);
    RUN(a_profile_without_the_marker_lets_no_foreign_group_pass);
    RUN(a_value_loop_lets_no_foreign_group_pass);
    return test_summary("esc_stick_marker");
}
