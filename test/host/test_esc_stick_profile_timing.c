/*
 * Stick programming: the timing a profile's stated lengths give a run
 * (esc_stick_profile_timing()).  The values derived from the Flycolor
 * FlyDragon's measured beeps, 127.9ms short and 383.7ms long; a tap run on
 * them with the default settings; a profile that states nothing, which
 * keeps the settings; and a stated timing the run's rules refuse.
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

#define FLYDRAGON "flycolor-flydragon-lite-plus"

/* A copy of a profile of record, its lengths changed by the test. */
static esc_profile_t g_copy;

static const esc_profile_t *copy_of(const char *id)
{
    const esc_profile_t *p = esc_profiles_find(id);
    if (p == NULL) {
        return NULL;
    }
    g_copy = *p;
    return &g_copy;
}

/* Timing 7 (1 long and 2 short beeps) and PWM frequency 2. */
static size_t two_changes(esc_stick_change_t *c)
{
    c[0] = change(4, 7);
    c[1] = change(6, 2);
    return 2u;
}

/*
 * The FlyDragon's menu at the tap's lengths (128ms and 384ms in the
 * simulation, 127.9ms and 383.7ms as the tap hands them over), counted
 * from the tap, every setting at its default.  The gaps are not measured:
 * 128ms between the beeps of a group, as the tap tests of the FlyDragon
 * take them, under the default GAP MIN of 200ms and over the 64ms the
 * profile's short beep gives, and 1500ms between groups.
 */
static void flydragon_defaults(const esc_profile_t *p)
{
    rig(FLYDRAGON);
    if (p != NULL) {
        r.p = p;
    }
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.beep_ms = 128u;
    c.long_ms = 384u;
    c.gap_ms = 128u;
    c.pause_ms = 1500u;
    esc_sim_init(&r.sim, r.p, &c);
    r.src = ESC_STICK_SRC_TAP;
    r.tap_lag = 60u;
    r.tap_skew = 123456u;
}

TEST_CASE(the_flydragon_profile_states_its_measured_beeps)
{
    const esc_profile_t *p = esc_profiles_find(FLYDRAGON);
    CHECK(p != NULL);
    CHECK_EQ(p->beep_ms, 128u);
    CHECK_EQ(p->long_ms, 384u);
    CHECK_EQ(p->gap_ms, 0u);            /* not measured */
    CHECK_EQ(p->group_gap_ms, 0u);
    CHECK(esc_stick_profile_beeps(p));
    /* The FlyDragon Lite's profile states the same lengths, from the same
     * ESC; no other profile of record states a beep length. */
    const esc_profile_t *lite = esc_profiles_find("flycolor-flydragon-lite");
    CHECK(lite != NULL);
    CHECK_EQ(lite->beep_ms, 128u);
    CHECK_EQ(lite->long_ms, 384u);
    unsigned stated = 0u;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *q = esc_profiles_at(i);
        stated += (q->beep_ms != 0u || q->long_ms != 0u || q->gap_ms != 0u
                   || q->group_gap_ms != 0u) ? 1u : 0u;
    }
    CHECK_EQ(stated, 2u);
}

/*
 * BEEP MIN 64ms, LONG 256ms, LONG MAX 768ms: the 127.9ms short is 63.9ms
 * over BEEP MIN and 128.1ms under LONG, the 383.7ms long 127.7ms over LONG
 * and 384.3ms under LONG MAX.  No gap is stated: GAP MIN is the setting
 * or half the short beep, the lower, and GROUP GAP the setting.
 */
TEST_CASE(the_measured_lengths_give_beep_min_long_and_long_max)
{
    esc_stick_timing_t t, d;
    esc_stick_timing_defaults(&t);
    d = t;
    CHECK(esc_stick_profile_timing(esc_profiles_find(FLYDRAGON), &t));
    CHECK_EQ(t.beep_min_ms, 64u);
    CHECK_EQ(t.long_ms, 256u);
    CHECK_EQ(t.long_max_ms, 768u);
    CHECK_EQ(t.gap_min_ms, 64u);        /* under the default 200ms */
    CHECK_EQ(t.group_gap_ms, d.group_gap_ms);
    CHECK_EQ(t.entry_ms, d.entry_ms);
    CHECK_EQ(t.threshold_ma, d.threshold_ma);
    /* In tenths of a ms: each length on its side of each bound. */
    CHECK(1279u >= 10u * t.beep_min_ms && 1279u < 10u * t.long_ms);
    CHECK(3837u >= 10u * t.long_ms && 3837u <= 10u * t.long_max_ms);
    /* Applied twice, the same. */
    esc_stick_timing_t again = t;
    CHECK(esc_stick_profile_timing(esc_profiles_find(FLYDRAGON), &again));
    CHECK(memcmp(&again, &t, sizeof(t)) == 0);
    /* A GAP MIN set lower than half the short beep stays. */
    esc_stick_timing_defaults(&t);
    t.gap_min_ms = 50u;
    CHECK(esc_stick_profile_timing(esc_profiles_find(FLYDRAGON), &t));
    CHECK_EQ(t.gap_min_ms, 50u);
}

TEST_CASE(a_profile_that_states_nothing_keeps_the_settings)
{
    esc_stick_timing_t t, d;
    esc_stick_timing_defaults(&t);
    t.beep_min_ms = 100u;       /* a setting changed by hand */
    t.long_ms = 250u;
    d = t;
    const esc_profile_t *p = esc_profiles_find("hobbywing-flyfun-8item");
    CHECK(!esc_stick_profile_beeps(p));
    CHECK(!esc_stick_profile_timing(p, &t));
    CHECK(memcmp(&t, &d, sizeof(t)) == 0);
    CHECK_EQ(t.gap_min_ms, 200u);       /* no beep_ms: the setting */
    CHECK(!esc_stick_profile_timing(NULL, &t));
    CHECK(memcmp(&t, &d, sizeof(t)) == 0);

    /* A short_long profile with a short beep and no long one: none of the
     * three is fixed. */
    esc_profile_t *q = (esc_profile_t *)copy_of(FLYDRAGON);
    q->long_ms = 0u;
    CHECK(!esc_stick_profile_beeps(q));
    CHECK(!esc_stick_profile_timing(q, &t));
    CHECK(memcmp(&t, &d, sizeof(t)) == 0);
}

TEST_CASE(counted_beeps_and_stated_gaps_fix_their_own_values)
{
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    esc_profile_t *q = (esc_profile_t *)copy_of(FLYDRAGON);
    q->encoding = ESC_ENC_COUNT;
    q->beep_ms = 200u;
    q->long_ms = 0u;
    CHECK(esc_stick_profile_timing(q, &t));
    CHECK_EQ(t.beep_min_ms, 100u);
    CHECK_EQ(t.long_ms, 400u);          /* twice the short is no short */
    CHECK_EQ(t.long_max_ms, 800u);
    CHECK_EQ(t.gap_min_ms, 100u);       /* half the short, no gap stated */

    esc_stick_timing_defaults(&t);
    q = (esc_profile_t *)copy_of(FLYDRAGON);
    q->gap_ms = 100u;
    q->group_gap_ms = 1500u;
    CHECK(esc_stick_profile_timing(q, &t));
    CHECK_EQ(t.gap_min_ms, 50u);
    CHECK_EQ(t.group_gap_ms, 800u);     /* midway, 100 to 1500 */

    /* A group gap alone: half of it. */
    esc_stick_timing_defaults(&t);
    q = (esc_profile_t *)copy_of("hobbywing-flyfun-8item");
    q->group_gap_ms = 1600u;
    CHECK(esc_stick_profile_timing(q, &t));
    CHECK_EQ(t.group_gap_ms, 800u);
    CHECK_EQ(t.beep_min_ms, 200u);
    CHECK_EQ(t.gap_min_ms, 200u);
}

/* With every setting at its default, the run takes the profile's lengths
 * and stores both values. */
TEST_CASE(a_tap_run_at_the_default_settings_stores_the_values)
{
    flydragon_defaults(NULL);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    CHECK_EQ(r.e.t.beep_min_ms, 64u);
    CHECK_EQ(r.e.t.long_ms, 256u);
    CHECK_EQ(r.e.t.long_max_ms, 768u);
    CHECK_EQ(r.e.t.gap_min_ms, 64u);    /* half the short beep */
    CHECK_EQ(r.e.t.group_gap_ms, 700u); /* the setting */
    CHECK_EQ(r.e.det.t.long_ms, 256u);  /* the detector's own copy */
    run_for(600000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.reason, ESC_STICK_R_NONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 4), 7);
    CHECK_EQ(esc_sim_stored(&r.sim, 6), 2);
    ended_safe();
}

/* The same ESC under a profile that states no lengths: the settings
 * apply, the 128ms short and the 128ms gap spoil their group under BEEP
 * MIN and GAP MIN 200ms, the 384ms long reads as short under LONG 500ms,
 * and nothing is stored. */
TEST_CASE(the_same_run_without_stated_lengths_uses_the_settings)
{
    esc_profile_t *q = (esc_profile_t *)copy_of(FLYDRAGON);
    q->beep_ms = 0u;
    q->long_ms = 0u;
    flydragon_defaults(q);
    esc_stick_timing_t d;
    esc_stick_timing_defaults(&d);
    d.entry_ms = q->entry_hold_ms;
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    CHECK(memcmp(&r.e.t, &d, sizeof(d)) == 0);
    CHECK_EQ(r.e.t.gap_min_ms, 200u);
    run_for(400000u);
    CHECK(r.e.phase != ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 4), 0);
    CHECK_EQ(esc_sim_stored(&r.sim, 6), 0);
}

/* A stated timing is held to the settings' rules, and refused, not
 * adjusted, where it breaks one. */
TEST_CASE(a_stated_timing_that_breaks_the_rules_is_refused)
{
    esc_stick_change_t c[2];
    const char *why = NULL;

    /* A 1ms beep: BEEP MIN 0. */
    esc_profile_t *q = (esc_profile_t *)copy_of(FLYDRAGON);
    q->beep_ms = 1u;
    q->long_ms = 3u;
    flydragon_defaults(q);
    size_t n = two_changes(c);
    esc_stick_bench_t b = bench();
    CHECK(!esc_stick_start(&r.e, r.p, c, n, &r.t, 7600u, 1000u,
                           ESC_STICK_SRC_TAP, &b, &why));
    CHECK_STR_EQ(why, "BEEP MIN and GAP MIN above 0");
    CHECK(!esc_stick_running(&r.e));

    /* A group gap of 100ms alone: GROUP GAP 50ms, under the 64ms GAP MIN
     * the short beep gives. */
    q = (esc_profile_t *)copy_of(FLYDRAGON);
    q->group_gap_ms = 100u;
    flydragon_defaults(q);
    b = bench();
    CHECK(!esc_stick_start(&r.e, r.p, c, n, &r.t, 7600u, 1000u,
                           ESC_STICK_SRC_TAP, &b, &why));
    CHECK_STR_EQ(why, "GROUP GAP above GAP MIN");

    /* Gaps of 200ms and 6000ms: GROUP GAP 3100ms misses the profile's
     * 3000ms select window. */
    q = (esc_profile_t *)copy_of(FLYDRAGON);
    q->gap_ms = 200u;
    q->group_gap_ms = 6000u;
    flydragon_defaults(q);
    b = bench();
    CHECK(!esc_stick_start(&r.e, r.p, c, n, &r.t, 7600u, 1000u,
                           ESC_STICK_SRC_TAP, &b, &why));
    CHECK_STR_EQ(why, "GROUP GAP misses the select window");

    /* esc_stick_check() on the derived timing says the same. */
    esc_stick_timing_t t = r.t;
    CHECK(esc_stick_check(r.p, c, n, &t, NULL));
    (void)esc_stick_profile_timing(r.p, &t);
    CHECK(!esc_stick_check(r.p, c, n, &t, &why));
    CHECK_STR_EQ(why, "GROUP GAP misses the select window");
}

int main(void)
{
    RUN(the_flydragon_profile_states_its_measured_beeps);
    RUN(the_measured_lengths_give_beep_min_long_and_long_max);
    RUN(a_profile_that_states_nothing_keeps_the_settings);
    RUN(counted_beeps_and_stated_gaps_fix_their_own_values);
    RUN(a_tap_run_at_the_default_settings_stores_the_values);
    RUN(the_same_run_without_stated_lengths_uses_the_settings);
    RUN(a_stated_timing_that_breaks_the_rules_is_refused);
    return test_summary("esc_stick_profile_timing");
}
