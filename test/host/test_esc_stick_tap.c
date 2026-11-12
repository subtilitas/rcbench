/*
 * Stick programming: where the beeps are counted from.  A run counting
 * from the phase tap, at the lengths the tap measured on a Flycolor
 * FlyDragon Lite 40A at 7.6V (short beeps 127.9ms, long beeps 383.7ms),
 * a run counting from an INA3221 channel's 50ms windows at the same
 * lengths, and the ends a source gives: a beep lost, a capture that
 * stops, a meter that stops.  Beeps handed to the run one by one: one that
 * alone shows a group's end, one of a group already ended, one before the
 * power-up.
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

/* The profile of the family the tap heard. */
#define FLYDRAGON "flycolor-flydragon-lite-plus"

/*
 * The FlyDragon's menu at the tap's lengths: 128ms short and 384ms long
 * beeps (the simulation's whole ms for 127.9ms and 383.7ms), 128ms between
 * the beeps of a group and 1500ms between groups -- the gaps are not
 * measured.  The profile states the two lengths, and the run takes BEEP
 * MIN 64ms, LONG 256ms and LONG MAX 768ms from them, and GAP MIN as the
 * lower of the one set here and half the short beep, 64ms; GROUP GAP is
 * 700ms.
 */
static void flydragon(esc_stick_src_t src, uint32_t gap_min_ms)
{
    rig(FLYDRAGON);
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.beep_ms = 128u;
    c.long_ms = 384u;
    c.gap_ms = 128u;
    c.pause_ms = 1500u;
    esc_sim_init(&r.sim, r.p, &c);
    r.src = src;
    r.t.gap_min_ms = gap_min_ms;
    r.t.group_gap_ms = 700u;
    r.tap_lag = 60u;            /* the panel has a beep 60ms after its end */
    r.tap_skew = 123456u;       /* the tap's clock is its own */
}

/* Timing 7 (1 long and 2 short beeps) and PWM frequency 2. */
static size_t two_changes(esc_stick_change_t *c)
{
    c[0] = change(4, 7);
    c[1] = change(6, 2);
    return 2u;
}

TEST_CASE(the_tap_counts_the_groups_and_the_value_is_stored)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    CHECK(r.p != NULL);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    CHECK_EQ(r.e.src, ESC_STICK_SRC_TAP);
    run_for(600000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.reason, ESC_STICK_R_NONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 4), 7);
    CHECK_EQ(esc_sim_stored(&r.sim, 6), 2);
    CHECK(r.e.groups > 6u);
    ended_safe();
}

/* The supply as the tester's bench read it, READINGS EVERY 152ms: too
 * slow for the beeps as a source, and no matter to a run counting from the
 * tap, whose supply readings judge the power alone. */
TEST_CASE(a_supply_read_every_152_ms_does_not_refuse_a_tap_run)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    r.read_iv = 152u;
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_for(600000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 4), 7);
    CHECK_EQ(esc_sim_stored(&r.sim, 6), 2);

    /* The same readings as the meter's windows: late for the 64ms BEEP MIN
     * the profile's lengths give. */
    flydragon(ESC_STICK_SRC_METER, 100u);
    r.read_iv = 152u;
    CHECK(start(c, n));
    run_for(600000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_RATE);
    CHECK_EQ(esc_sim_stored(&r.sim, 4), 0);
    ended_safe();
}

/* An INA3221 channel in the ESC's supply line, read through its 50ms
 * windows, the highest current of each: the same beeps counted from the
 * current.  GAP MIN at 50ms, as a 128ms gap can hold a single whole window. */
TEST_CASE(the_ina3221_windows_count_the_same_groups)
{
    flydragon(ESC_STICK_SRC_METER, 50u);
    r.win_ms = 50u;
    r.read_iv = 152u;           /* the supply's own readings, power only */
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    CHECK_EQ(r.e.src, ESC_STICK_SRC_METER);
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    run_for(500u);
    CHECK_EQ(r.e.iv_ms, 50u);   /* the windows' interval, not the supply's */
    run_for(600000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 4), 7);
    CHECK_EQ(esc_sim_stored(&r.sim, 6), 2);
}

TEST_CASE(a_run_without_a_beep_source_is_refused)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    const esc_stick_bench_t b = bench();
    const char *why = NULL;
    CHECK(!esc_stick_start(&r.e, r.p, c, n, &r.t, 7600u, 1000u,
                           ESC_STICK_SRC_NONE, &b, &why));
    CHECK_STR_EQ(why, "no beep source");
    CHECK(!esc_stick_running(&r.e));

    /* No timing at all: refused by the rules, with their reason. */
    why = NULL;
    CHECK(!esc_stick_start(&r.e, r.p, c, n, NULL, 7600u, 1000u,
                           ESC_STICK_SRC_TAP, &b, &why));
    CHECK(why != NULL);
    CHECK(!esc_stick_running(&r.e));
}

/* A beep number that never arrives: the count would be one short. */
TEST_CASE(a_beep_lost_mid_run_ends_the_run)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ITEMS);
    r.tap_drop = (uint16_t)(r.tap_made + 3u);
    run_for(60000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_TAP_LOST);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    CHECK_STR_EQ(esc_stick_reason_text(r.e.reason), "BEEP LOST");
    ended_safe();

    /* The tap's own lost count moving says the same. */
    flydragon(ESC_STICK_SRC_TAP, 100u);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    run_for(3000u);
    CHECK(esc_stick_running(&r.e));
    r.tap_lost = 1u;
    tick();
    CHECK_EQ(r.e.reason, ESC_STICK_R_TAP_LOST);
    ended_safe();
}

TEST_CASE(a_tap_that_stops_mid_run_ends_the_run)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    run_for(2000u);
    CHECK(esc_stick_running(&r.e));
    r.tap_stop = true;
    tick();
    CHECK_EQ(r.e.reason, ESC_STICK_R_TAP_STOP);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    CHECK_STR_EQ(esc_stick_reason_text(r.e.reason), "TAP STOPPED");
    ended_safe();
}

/* No tap state at all for ESC_STICK_STALE_MS is a tap that is not seen
 * running: the run counts nothing it cannot hear. */
TEST_CASE(a_tap_state_that_stops_coming_ends_the_run)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    /* Stepped without the rig's tick: no tap state reaches the run. */
    for (uint32_t i = 0; i < 1100u && esc_stick_running(&r.e); ++i) {
        const esc_stick_bench_t b = bench();
        esc_stick_step(&r.e, &b);
        r.now++;
    }
    CHECK_EQ(r.e.reason, ESC_STICK_R_TAP_STOP);
}

/* The INA3221 stops sending windows while the supply reads on. */
TEST_CASE(a_meter_that_goes_quiet_mid_run_ends_the_run)
{
    flydragon(ESC_STICK_SRC_METER, 50u);
    r.win_ms = 50u;
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ITEMS);
    r.meter_stop = true;
    const uint32_t from = r.now;
    run_for(5000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_METER);
    CHECK(r.now - from <= ESC_STICK_STALE_MS + 60u);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    CHECK_STR_EQ(esc_stick_reason_text(r.e.reason), "INA3221 LOST");
    ended_safe();

    /* And before a power-up nothing is powered that the run cannot hear. */
    flydragon(ESC_STICK_SRC_METER, 50u);
    r.win_ms = 50u;
    r.meter_stop = true;
    CHECK(start(c, n));
    run_for(20000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_METER);
    CHECK_EQ(r.on_n, 0u);
    ended_safe();
}

/*
 * Beeps held back on the link for 900ms, the tap's status read 40ms old:
 * a group ends only once every beep the status holds has come in, one
 * whose end only the next beep shows is judged unclear and not acted on,
 * and the run stores what was asked all the same.
 */
TEST_CASE(beeps_held_back_past_a_groups_end_store_nothing_wrong)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    unsigned unclear = 0u;
    uint32_t seen = 0u;
    for (int k = 0; k < 40 && esc_stick_running(&r.e); ++k) {
        r.tap_lag = (k % 3 == 0) ? 900u : 60u;
        for (int i = 0; i < 2000 && esc_stick_running(&r.e); ++i) {
            tick();
            if (r.e.groups != seen) {
                seen = r.e.groups;
                unclear += (!r.e.last_valid || !r.e.last_in_order) ? 1u : 0u;
            }
        }
    }
    CHECK(unclear > 0u);        /* the held-back beeps were noticed */
    r.tap_lag = 60u;
    run_for(600000u);
    if (r.e.phase != ESC_STICK_DONE) {
        T_FAIL("ended %s", esc_stick_reason_text(r.e.reason));
    }
    CHECK_EQ(esc_sim_stored(&r.sim, 4), 7);
    CHECK_EQ(esc_sim_stored(&r.sim, 6), 2);
}

/*
 * Codex's case on #800: default timing, 250ms short beeps with 500ms
 * between the beeps of a group, the tap's beeps handed over 60ms after
 * they end.  The gap plus the next beep, 750ms, is longer than GROUP GAP
 * (700ms): a group may end only on the capture's word that nothing sounds,
 * never on the time since the last beep came in, or it ends under the next
 * beep.  The tap run stores item 3's value as the meter run does.  The
 * FlyDragon's menu under a copy of its profile without the beep lengths it
 * states, so the run takes the default timing and not the profile's.
 */
static esc_profile_t g_unmeasured;

static void codex_case(esc_stick_src_t src)
{
    rig(FLYDRAGON);
    g_unmeasured = *r.p;
    g_unmeasured.beep_ms = 0u;
    g_unmeasured.long_ms = 0u;
    r.p = &g_unmeasured;
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.beep_ms = 250u;
    c.gap_ms = 500u;
    esc_sim_init(&r.sim, r.p, &c);
    r.src = src;
    r.tap_lag = 60u;
}

TEST_CASE(a_beep_sounding_at_the_group_gap_keeps_the_group_open)
{
    esc_stick_change_t ch[1];
    codex_case(ESC_STICK_SRC_METER);
    ch[0] = change(3, 1);
    CHECK(start(ch, 1));
    run_for(600000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 1);

    codex_case(ESC_STICK_SRC_TAP);
    ch[0] = change(3, 1);
    CHECK(start(ch, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    /* Each group the ESC sounded is heard whole: items 2 and 3 and the
     * value groups follow their predecessor. */
    uint32_t seen = r.e.groups;
    unsigned whole = 0u;
    for (int i = 0; i < 600000 && esc_stick_running(&r.e); ++i) {
        tick();
        if (r.e.groups != seen) {
            seen = r.e.groups;
            whole += (r.e.last_valid && r.e.last_in_order) ? 1u : 0u;
        }
    }
    if (r.e.phase != ESC_STICK_DONE) {
        T_FAIL("ended %s", esc_stick_reason_text(r.e.reason));
    }
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 1);
    CHECK(whole >= 2u);
    ended_safe();
}

/*
 * The deadline passes while a beep sounds: a status past the last beep's
 * end plus GROUP GAP keeps the group open while it says a beep is under
 * way or its window held a tone, and while it holds a beep the run has not
 * taken; the beep, when it comes, joins the group, and only a quiet status
 * with every beep taken ends it.
 */
TEST_CASE(a_status_past_the_deadline_with_a_beep_under_way_ends_nothing)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ITEMS);
    /* From here the run hears only what the test hands it, on a tap
     * clock well after anything the simulation sounded. */
    const uint32_t t0 = r.e.tap_end + 5000u;
    uint16_t seq = r.e.tap_seq;
    const uint32_t gg = r.t.group_gap_ms;
    esc_stick_tap_t st = { .running = true, .lost = r.tap_lost,
                           .newest = seq, .quiet = true, .head = seq };
    /* A short beep, 128ms: a group opens. */
    seq = (uint16_t)(seq + 1u);
    const esc_stick_beep_t a = { seq, t0, 128u };
    esc_stick_tap_beep(&r.e, &a);
    CHECK(r.e.det.group_open);
    CHECK_EQ(esc_stick_beeps(&r.e), 1u);
    const uint32_t groups = r.e.groups;
    /* Past its end plus GROUP GAP, a beep under way: open. */
    st.window = (uint16_t)((t0 + 128u + gg + 40u) / 8u);
    st.head = seq;
    st.quiet = false;
    esc_stick_tap(&r.e, &st);
    CHECK(r.e.det.group_open);
    CHECK_EQ(r.e.groups, groups);
    /* Quiet, but the IOmcu holds a beep the run has not taken: open. */
    st.quiet = true;
    st.head = (uint16_t)(seq + 1u);
    esc_stick_tap(&r.e, &st);
    CHECK(r.e.det.group_open);
    CHECK_EQ(r.e.groups, groups);
    /* That beep: it began 500ms after the first ended, so it is the
     * group's second. */
    seq = (uint16_t)(seq + 1u);
    const esc_stick_beep_t b = { seq, t0 + 128u + 500u, 128u };
    esc_stick_tap_beep(&r.e, &b);
    CHECK(r.e.det.group_open);
    CHECK_EQ(esc_stick_beeps(&r.e), 2u);
    CHECK_EQ(r.e.groups, groups);
    /* Quiet, every beep taken, but the window ends before the second
     * beep's end plus GROUP GAP: open. */
    st.head = seq;
    st.window = (uint16_t)((t0 + 128u + 500u + 128u + gg - 16u) / 8u);
    esc_stick_tap(&r.e, &st);
    CHECK(r.e.det.group_open);
    /* Past it: the group of 2 ends. */
    st.window = (uint16_t)((t0 + 128u + 500u + 128u + gg + 8u) / 8u);
    esc_stick_tap(&r.e, &st);
    CHECK(!r.e.det.group_open);
    CHECK_EQ(r.e.groups, groups + 1u);
    CHECK_EQ(r.e.last_count, 2u);
    CHECK(r.e.last_valid);
}

/* The FlyDragon's run in ITEMS, from where on it hears only what the case
 * hands it, on a tap clock 5000ms after anything the simulation sounded.
 * @return The tap's clock to start from. */
static uint32_t hand_fed_items(void)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ITEMS);
    return r.e.tap_end + 5000u;
}

/* A beep that begins GROUP GAP after the one before with no quiet state
 * between them: only this beep shows the group's end, so that group is
 * judged unclear and not acted on, and the beep opens the next group. */
TEST_CASE(a_beep_that_alone_shows_a_groups_end_judges_it_unclear)
{
    const uint32_t t0 = hand_fed_items();
    const uint32_t gg = r.t.group_gap_ms;
    uint16_t seq = (uint16_t)(r.e.tap_seq + 1u);
    const esc_stick_beep_t a = { seq, t0, 128u };
    esc_stick_tap_beep(&r.e, &a);
    CHECK(r.e.det.group_open);
    const uint32_t groups = r.e.groups;
    seq = (uint16_t)(seq + 1u);
    const esc_stick_beep_t b = { seq, t0 + 128u + gg + 100u, 128u };
    esc_stick_tap_beep(&r.e, &b);
    CHECK_EQ(r.e.groups, groups + 1u);
    CHECK_EQ(r.e.last_count, 1u);
    CHECK(!r.e.last_valid);
    CHECK(r.e.det.group_open);              /* b's own group */
    CHECK(esc_stick_running(&r.e));
}

/* A beep that began less than GROUP GAP after a group the quiet state
 * ended belonged to that group: the run drops the order across it and
 * waits for a whole quiet before the next group. */
TEST_CASE(a_beep_of_a_group_already_ended_drops_the_order)
{
    const uint32_t t0 = hand_fed_items();
    const uint32_t gg = r.t.group_gap_ms;
    uint16_t seq = r.e.tap_seq;
    esc_stick_tap_t st = { .running = true, .lost = r.tap_lost,
                           .newest = seq, .quiet = true, .head = seq };
    /* Two short beeps 500ms apart, and the quiet past GROUP GAP: a
     * group of 2. */
    seq = (uint16_t)(seq + 1u);
    const esc_stick_beep_t a = { seq, t0, 128u };
    esc_stick_tap_beep(&r.e, &a);
    seq = (uint16_t)(seq + 1u);
    const esc_stick_beep_t b = { seq, t0 + 628u, 128u };
    esc_stick_tap_beep(&r.e, &b);
    const uint32_t b_end = t0 + 628u + 128u;
    st.head = seq;
    st.window = (uint16_t)((b_end + gg + 8u) / 8u);
    esc_stick_tap(&r.e, &st);
    CHECK(!r.e.det.group_open);
    CHECK_EQ(r.e.last_count, 2u);
    CHECK(r.e.last_valid);
    CHECK_EQ(r.e.prev, 2u);
    /* A beep handed over late, begun 600ms after b ended. */
    seq = (uint16_t)(seq + 1u);
    const esc_stick_beep_t late = { seq, b_end + gg - 100u, 128u };
    const uint32_t groups = r.e.groups;
    esc_stick_tap_beep(&r.e, &late);
    CHECK_EQ(r.e.prev, 0u);
    CHECK(r.e.det.wait_quiet);
    CHECK(!r.e.det.group_open);
    CHECK_EQ(r.e.groups, groups);
    CHECK(esc_stick_running(&r.e));
}

/* A beep before the first tap state, which names the beep numbers already
 * heard, is ignored; one before the supply is on is taken in order and
 * counts nowhere. */
TEST_CASE(a_beep_before_the_power_up_is_taken_and_not_counted)
{
    flydragon(ESC_STICK_SRC_TAP, 100u);
    esc_stick_change_t c[2];
    const size_t n = two_changes(c);
    CHECK(start(c, n));
    const esc_stick_beep_t early = { 1u, 500u, 128u };
    esc_stick_tap_beep(&r.e, &early);
    esc_stick_tap_beep(&r.e, NULL);
    CHECK(!r.e.tap_rx);
    CHECK(esc_stick_running(&r.e));
    const esc_stick_tap_t st = { .running = true, .lost = r.tap_lost,
                                 .newest = 7u, .quiet = true, .head = 7u };
    esc_stick_tap(&r.e, &st);
    CHECK(r.e.tap_base);
    CHECK(r.e.phase < ESC_STICK_ENTRY);
    const esc_stick_beep_t b = { 8u, 1000u, 128u };
    esc_stick_tap_beep(&r.e, &b);
    CHECK_EQ(r.e.tap_seq, 8u);
    CHECK(r.e.tap_rx);
    CHECK_EQ(r.e.pulses, 0u);
    CHECK(!r.e.det.group_open);
    CHECK(esc_stick_running(&r.e));
}

int main(void)
{
    RUN(the_tap_counts_the_groups_and_the_value_is_stored);
    RUN(a_supply_read_every_152_ms_does_not_refuse_a_tap_run);
    RUN(the_ina3221_windows_count_the_same_groups);
    RUN(a_run_without_a_beep_source_is_refused);
    RUN(a_beep_lost_mid_run_ends_the_run);
    RUN(a_tap_that_stops_mid_run_ends_the_run);
    RUN(a_tap_state_that_stops_coming_ends_the_run);
    RUN(a_meter_that_goes_quiet_mid_run_ends_the_run);
    RUN(beeps_held_back_past_a_groups_end_store_nothing_wrong);
    RUN(a_beep_sounding_at_the_group_gap_keeps_the_group_open);
    RUN(a_status_past_the_deadline_with_a_beep_under_way_ends_nothing);
    RUN(a_beep_that_alone_shows_a_groups_end_judges_it_unclear);
    RUN(a_beep_of_a_group_already_ended_drops_the_order);
    RUN(a_beep_before_the_power_up_is_taken_and_not_counted);
    return test_summary("esc_stick_tap");
}
