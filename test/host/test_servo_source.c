/*
 * Which meter measures the servo rail (servo_source.h): the conditions on
 * their own, against facts written by hand, and through the whole chain of
 * sense_chain.h -- the modelled INA3221, the coprocessor's schedule and
 * pages, and the panel's sense_link.
 *
 * Under test: the INA3221 the meter only while every condition holds and
 * has held for 1000 ms, at 999, 1000 and 1001 ms; each of the conditions 1
 * to 6 failing alone, the PD mini the meter in that step, and the 1000 ms
 * counted again from the step they all hold; a SENSE read and a window
 * number fresh at 199 ms and not at 200 and 201 ms; the reset count moving
 * between two reads with the part online in both, and across 255 to 0; a
 * set-up taken starting the count again without a reset said; the model
 * while no coprocessor answers; a coprocessor older than 4.11 never the
 * INA3221's, said once per link and once per switching on, and not said
 * for one without SENSE; the events one at a time, 5000 ms apart, put back
 * when the band never showed them; every timer across the 2^32 ms tick
 * wrap; a clipped window and a negative current changing nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "sense_chain.h"
#include "servo_source.h"

static servo_source_t src;

/* Facts under which the INA3221 is the meter, read at @p now. */
static sense_link_meter_t good(uint32_t now)
{
    const sense_link_meter_t m = {
        .page = true, .wanted = true, .held = true, .setups = 3u,
        .status = true, .status_ms = now,
        .flags = (uint16_t)(LINK_SN_I3221_ONLINE | LINK_SN_I3221_ID_OK
                            | LINK_SN_BUS_OPEN),
        .resets_read = true, .resets = 0u,
        .win = true, .win_valid = true, .win_ms = now,
    };
    return m;
}

/* A poll every 50 ms for @p ms with the facts good and fresh; returns the
 * tick after the last. */
static uint32_t hold(uint32_t now, uint32_t ms)
{
    for (uint32_t t = 0u; t < ms; t += 50u) {
        const sense_link_meter_t m = good(now + t);
        (void)servo_source_step(&src, now + t, true, 11u, &m);
    }
    return now + ms;
}

/* The INA3221 as the meter at @p now: good facts for 1000 ms before. */
static void settled(uint32_t now)
{
    servo_source_init(&src);
    (void)hold(now - 1000u, 1050u);
}

/* ------------------------------------------------------- condition 7 */

TEST_CASE(the_meter_is_the_model_until_a_coprocessor_answers)
{
    servo_source_init(&src);
    CHECK_EQ(servo_source_id(&src), SERVO_SOURCE_MODEL);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NO_LINK);
    sense_link_meter_t m = good(100u);
    CHECK_EQ(servo_source_step(&src, 100u, false, 11u, &m),
             SERVO_SOURCE_MODEL);
    CHECK_EQ(servo_source_step(&src, 150u, true, 11u, NULL),
             SERVO_SOURCE_MODEL);
    CHECK_EQ(servo_source_step(NULL, 150u, true, 11u, &m),
             SERVO_SOURCE_MODEL);
    CHECK_EQ(servo_source_id(NULL), SERVO_SOURCE_MODEL);
    CHECK_EQ(servo_source_why(NULL), SERVO_SOURCE_WHY_NO_LINK);
    CHECK_EQ(servo_source_event(NULL, 0u), 0u);
    servo_source_event_back(NULL, SERVO_SOURCE_EV_OLD);
    servo_source_init(NULL);
    CHECK_EQ(SERVO_SOURCE_FRESH_MS, 200u);
    CHECK_EQ(SERVO_SOURCE_SETTLE_MS, 1000u);
    CHECK_EQ(SERVO_SOURCE_EVENT_GAP_MS, 5000u);
}

/* The INA3221 is the meter 1000 ms after every condition holds: not at
 * 999 ms, at 1000 and at 1001 ms.  At tick 0 and across the wrap. */
TEST_CASE(the_ina3221_is_the_meter_after_1000_ms_of_good_readings)
{
    static const uint32_t k_start[] = { 0u, 5000u, 0xFFFFFC00u,
                                        0xFFFFFFFFu, 0xFFFFFF38u };
    static const uint32_t k_after[] = { 999u, 1000u, 1001u };
    for (size_t i = 0u; i < sizeof(k_start) / sizeof(k_start[0]); ++i) {
        for (size_t k = 0u; k < 3u; ++k) {
            const uint32_t t0 = k_start[i];
            servo_source_init(&src);
            sense_link_meter_t m = good(t0);
            CHECK_EQ(servo_source_step(&src, t0, true, 11u, &m),
                     SERVO_SOURCE_PDMINI);
            CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SETTLING);
            (void)hold(t0 + 50u, 900u);
            CHECK_EQ(servo_source_id(&src), SERVO_SOURCE_PDMINI);
            const uint32_t t = t0 + k_after[k];
            m = good(t);
            const servo_source_id_t id =
                servo_source_step(&src, t, true, 11u, &m);
            CHECK_EQ(id, (k_after[k] >= 1000u) ? SERVO_SOURCE_INA3221
                                               : SERVO_SOURCE_PDMINI);
            CHECK_EQ(servo_source_why(&src),
                     (k_after[k] >= 1000u) ? SERVO_SOURCE_WHY_NONE
                                           : SERVO_SOURCE_WHY_SETTLING);
        }
    }
}

/* A meter that stays is not asked for its clock again: 2^32 ms on, with
 * the facts good at every poll, it is still the INA3221. */
TEST_CASE(a_meter_that_stays_outlasts_the_tick_wrap)
{
    settled(0xFFFFF000u);
    CHECK_EQ(servo_source_id(&src), SERVO_SOURCE_INA3221);
    uint32_t t = 0xFFFFF000u + 50u;
    for (unsigned i = 0u; i < 400u; ++i) {     /* 20 s, across the wrap */
        const sense_link_meter_t m = good(t);
        CHECK_EQ(servo_source_step(&src, t, true, 11u, &m),
                 SERVO_SOURCE_INA3221);
        t += 50u;
    }
    CHECK(t < 0x00010000u);
    /* And with good_ms exactly 2^32 ms back. */
    src.good_ms = t;
    const sense_link_meter_t m = good(t);
    CHECK_EQ(servo_source_step(&src, t, true, 11u, &m),
             SERVO_SOURCE_INA3221);
}

/* ------------------------------------------------- conditions 1 to 6 */

typedef void (*spoil_t)(sense_link_meter_t *m, uint16_t *minor);

static void spoil_off(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->wanted = false;
    m->held   = false;
}

static void spoil_old(sense_link_meter_t *m, uint16_t *minor)
{
    (void)m;
    *minor = 10u;
}

static void spoil_unheld(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->held = false;
}

static void spoil_offline(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->flags &= (uint16_t)~LINK_SN_I3221_ONLINE;
}

static void spoil_identity(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->flags &= (uint16_t)~LINK_SN_I3221_ID_OK;
}

static void spoil_stuck(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->flags |= LINK_SN_BUS_STUCK;
}

static void spoil_no_status(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->status = false;
}

static void spoil_no_resets(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->resets_read = false;
}

static void spoil_no_window(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->win = false;
}

static void spoil_empty_window(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->win_valid = false;
}

static void spoil_reset(sense_link_meter_t *m, uint16_t *minor)
{
    (void)minor;
    m->resets = 1u;
}

/* Each condition failing alone, for one poll: the PD mini in that step,
 * the reason named, and the INA3221 back 1000 ms after the first step
 * they all hold again, not 999 ms. */
TEST_CASE(each_condition_failing_alone_drops_to_the_pd_mini)
{
    static const struct { spoil_t spoil; servo_source_why_t why; } k_c[] = {
        { spoil_off,          SERVO_SOURCE_WHY_OFF },
        { spoil_old,          SERVO_SOURCE_WHY_OLD },
        { spoil_unheld,       SERVO_SOURCE_WHY_NOT_HELD },
        { spoil_offline,      SERVO_SOURCE_WHY_SILENT },
        { spoil_identity,     SERVO_SOURCE_WHY_SILENT },
        { spoil_stuck,        SERVO_SOURCE_WHY_SILENT },
        { spoil_no_status,    SERVO_SOURCE_WHY_SILENT },
        { spoil_no_resets,    SERVO_SOURCE_WHY_SILENT },
        { spoil_no_window,    SERVO_SOURCE_WHY_NO_WINDOW },
        { spoil_empty_window, SERVO_SOURCE_WHY_NO_WINDOW },
        { spoil_reset,        SERVO_SOURCE_WHY_RESET },
    };
    static const uint32_t k_at[] = { 20000u, 0xFFFFFFF0u, 0xFFFFFD00u };
    for (size_t a = 0u; a < sizeof(k_at) / sizeof(k_at[0]); ++a) {
        for (size_t i = 0u; i < sizeof(k_c) / sizeof(k_c[0]); ++i) {
            const uint32_t t = k_at[a];
            settled(t);
            CHECK_EQ(servo_source_id(&src), SERVO_SOURCE_INA3221);
            sense_link_meter_t m = good(t + 100u);
            uint16_t minor = 11u;
            k_c[i].spoil(&m, &minor);
            CHECK_EQ(servo_source_step(&src, t + 100u, true, minor, &m),
                     SERVO_SOURCE_PDMINI);
            CHECK_EQ(servo_source_why(&src), k_c[i].why);
            /* Good again from t + 150: the count starts there.  A count
             * that moved stays where it moved to. */
            const uint8_t resets = m.resets;
            for (uint32_t d = 150u; d <= 1100u; d += 50u) {
                m = good(t + d);
                m.resets = resets;
                (void)servo_source_step(&src, t + d, true, 11u, &m);
                CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SETTLING);
            }
            m = good(t + 1149u);
            m.resets = resets;
            CHECK_EQ(servo_source_step(&src, t + 1149u, true, 11u, &m),
                     SERVO_SOURCE_PDMINI);
            m = good(t + 1150u);
            m.resets = resets;
            CHECK_EQ(servo_source_step(&src, t + 1150u, true, 11u, &m),
                     SERVO_SOURCE_INA3221);
        }
    }
}

/* A loose wire: a condition that fails every 900 ms never lets the 1000 ms
 * pass, and the meter does not change back and forth. */
TEST_CASE(a_part_that_drops_out_every_900_ms_is_never_the_meter)
{
    servo_source_init(&src);
    unsigned was_ina = 0u;
    for (uint32_t t = 0u; t < 20000u; t += 50u) {
        sense_link_meter_t m = good(t);
        if (t % 900u == 0u) {
            m.flags &= (uint16_t)~LINK_SN_I3221_ONLINE;
        }
        if (servo_source_step(&src, t, true, 11u, &m)
            == SERVO_SOURCE_INA3221) {
            ++was_ina;
        }
    }
    CHECK_EQ(was_ina, 0u);
}

/* A SENSE read and the newest window's number count for less than 200 ms:
 * fresh at 199, not at 200 and 201 ms.  At every phase of the tick wrap. */
TEST_CASE(a_read_is_fresh_for_199_ms_and_not_for_200)
{
    static const uint32_t k_at[] = { 20000u, 0xFFFFFFFFu, 0xFFFFFF9Cu,
                                     0xFFFFFF38u };
    static const uint32_t k_age[] = { 199u, 200u, 201u };
    for (size_t a = 0u; a < sizeof(k_at) / sizeof(k_at[0]); ++a) {
        for (size_t k = 0u; k < 3u; ++k) {
            const uint32_t t = k_at[a];
            /* The SENSE read ages; the window is this poll's. */
            settled(t);
            sense_link_meter_t m = good(t + k_age[k]);
            m.status_ms = t;
            (void)servo_source_step(&src, t + k_age[k], true, 11u, &m);
            CHECK_EQ(servo_source_id(&src),
                     (k_age[k] < 200u) ? SERVO_SOURCE_INA3221
                                       : SERVO_SOURCE_PDMINI);
            CHECK_EQ(servo_source_why(&src),
                     (k_age[k] < 200u) ? SERVO_SOURCE_WHY_NONE
                                       : SERVO_SOURCE_WHY_SILENT);
            /* The window's number stands; the SENSE read is this poll's. */
            settled(t);
            m = good(t + k_age[k]);
            m.win_ms = t;
            (void)servo_source_step(&src, t + k_age[k], true, 11u, &m);
            CHECK_EQ(servo_source_id(&src),
                     (k_age[k] < 200u) ? SERVO_SOURCE_INA3221
                                       : SERVO_SOURCE_PDMINI);
            CHECK_EQ(servo_source_why(&src),
                     (k_age[k] < 200u) ? SERVO_SOURCE_WHY_NONE
                                       : SERVO_SOURCE_WHY_NO_WINDOW);
        }
    }
}

/* ------------------------------------------------------ the reset count */

/* The count moves once: one step on the PD mini, one event.  It moves
 * again, across 255 to 0: again.  A set-up taken starts the count at 0 at
 * the coprocessor, which is no reset. */
TEST_CASE(a_reset_count_that_moves_is_said_once_each_time)
{
    settled(20000u);
    CHECK_EQ(servo_source_event(&src, 20000u), 0u);
    sense_link_meter_t m = good(20100u);
    m.resets = 255u;
    (void)servo_source_step(&src, 20100u, true, 11u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_RESET);
    CHECK_EQ(servo_source_event(&src, 20100u), SERVO_SOURCE_EV_RESET);
    CHECK_EQ(servo_source_event(&src, 20100u), 0u);
    m = good(20150u);
    m.resets = 255u;
    (void)servo_source_step(&src, 20150u, true, 11u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SETTLING);
    CHECK_EQ(servo_source_event(&src, 30000u), 0u);
    m = good(20200u);
    m.resets = 0u;                              /* 255 to 0 */
    (void)servo_source_step(&src, 20200u, true, 11u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_RESET);
    CHECK_EQ(servo_source_event(&src, 30000u), SERVO_SOURCE_EV_RESET);

    /* The part found reset and still offline: the first failing condition
     * is named, and the reset is said all the same. */
    settled(40000u);
    m = good(40100u);
    m.resets = 1u;
    m.flags &= (uint16_t)~LINK_SN_I3221_ONLINE;
    (void)servo_source_step(&src, 40100u, true, 11u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SILENT);
    CHECK_EQ(servo_source_event(&src, 40100u), SERVO_SOURCE_EV_RESET);

    /* A set-up taken: the reads start again and so does the count. */
    settled(60000u);
    m = good(60100u);
    m.resets = 7u;
    (void)servo_source_step(&src, 60100u, true, 11u, &m);
    CHECK_EQ(servo_source_event(&src, 60100u), SERVO_SOURCE_EV_RESET);
    m = good(60150u);
    m.resets = 0u;
    m.setups = 4u;
    (void)servo_source_step(&src, 60150u, true, 11u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SETTLING);
    CHECK_EQ(servo_source_event(&src, 70000u), 0u);
    /* And a read without the count is no count of 0. */
    m = good(60200u);
    m.setups = 4u;
    m.resets_read = false;
    (void)servo_source_step(&src, 60200u, true, 11u, &m);
    m = good(60250u);
    m.setups = 4u;
    m.resets = 9u;
    (void)servo_source_step(&src, 60250u, true, 11u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SETTLING);
    CHECK_EQ(servo_source_event(&src, 80000u), 0u);
}

/* ------------------------------------------------ the older coprocessor */

TEST_CASE(an_older_coprocessor_is_said_once_per_link_and_per_switching_on)
{
    servo_source_init(&src);
    sense_link_meter_t m = good(1000u);
    m.resets_read = false;
    m.win = false;
    for (uint32_t t = 1000u; t < 9000u; t += 50u) {
        m.status_ms = t;
        CHECK_EQ(servo_source_step(&src, t, true, 10u, &m),
                 SERVO_SOURCE_PDMINI);
        CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_OLD);
    }
    CHECK_EQ(servo_source_event(&src, 9000u), SERVO_SOURCE_EV_OLD);
    CHECK_EQ(servo_source_event(&src, 20000u), 0u);     /* once */
    /* Switched off and on again while it answers: said again. */
    m.wanted = false;
    (void)servo_source_step(&src, 20000u, true, 10u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_OFF);
    m.wanted = true;
    (void)servo_source_step(&src, 20050u, true, 10u, &m);
    CHECK_EQ(servo_source_event(&src, 20050u), SERVO_SOURCE_EV_OLD);
    CHECK_EQ(servo_source_event(&src, 30000u), 0u);
    /* The link gone and back: said again. */
    (void)servo_source_step(&src, 30000u, false, 0u, NULL);
    CHECK_EQ(servo_source_id(&src), SERVO_SOURCE_MODEL);
    (void)servo_source_step(&src, 31000u, true, 10u, &m);
    CHECK_EQ(servo_source_event(&src, 40000u), SERVO_SOURCE_EV_OLD);
    /* Waiting when the part is switched off: no longer said. */
    (void)servo_source_step(&src, 41000u, false, 0u, NULL);
    (void)servo_source_step(&src, 42000u, true, 10u, &m);
    m.wanted = false;
    (void)servo_source_step(&src, 42050u, true, 10u, &m);
    CHECK_EQ(servo_source_event(&src, 50000u), 0u);
    /* Waiting when the link goes: gone with it. */
    m.wanted = true;
    (void)servo_source_step(&src, 50000u, true, 10u, &m);
    (void)servo_source_step(&src, 50050u, false, 0u, NULL);
    CHECK_EQ(servo_source_event(&src, 60000u), 0u);

    /* Every minor from the first with SENSE to 4.10; 4.11 says nothing. */
    for (uint16_t minor = 7u; minor <= 11u; ++minor) {
        servo_source_init(&src);
        m = good(1000u);
        (void)servo_source_step(&src, 1000u, true, minor, &m);
        CHECK_EQ(servo_source_event(&src, 1000u),
                 (minor < 11u) ? SERVO_SOURCE_EV_OLD : 0u);
    }
    /* A coprocessor without SENSE: sense_link says that one. */
    servo_source_init(&src);
    m = good(1000u);
    m.page = false;
    (void)servo_source_step(&src, 1000u, true, 6u, &m);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_OLD);
    CHECK_EQ(servo_source_event(&src, 1000u), 0u);
    /* And nothing to say with the part off. */
    servo_source_init(&src);
    m = good(1000u);
    m.wanted = false;
    (void)servo_source_step(&src, 1000u, true, 10u, &m);
    CHECK_EQ(servo_source_event(&src, 1000u), 0u);
}

/* One event a turn, the older coprocessor first, the next 5000 ms later
 * and not 4999 ms; across the tick wrap.  One the band never showed goes
 * back, and not once the link has gone. */
TEST_CASE(events_are_handed_out_one_at_a_time_5000_ms_apart)
{
    static const uint32_t k_at[] = { 1000u, 0xFFFFF000u, 0xFFFFFFFFu };
    for (size_t a = 0u; a < sizeof(k_at) / sizeof(k_at[0]); ++a) {
        const uint32_t t = k_at[a];
        servo_source_init(&src);
        sense_link_meter_t m = good(t);
        (void)servo_source_step(&src, t, true, 10u, &m);
        m.resets = 1u;
        (void)servo_source_step(&src, t + 50u, true, 10u, &m);
        CHECK_EQ(servo_source_event(&src, t + 50u), SERVO_SOURCE_EV_OLD);
        CHECK_EQ(servo_source_event(&src, t + 50u + 4999u), 0u);
        CHECK_EQ(servo_source_event(&src, t + 50u + 5000u),
                 SERVO_SOURCE_EV_RESET);
        CHECK_EQ(servo_source_event(&src, t + 50u + 10001u), 0u);
        /* Replaced before a frame took it: back, and out after the gap. */
        servo_source_event_back(&src, SERVO_SOURCE_EV_RESET);
        CHECK_EQ(servo_source_event(&src, t + 50u + 9999u), 0u);
        CHECK_EQ(servo_source_event(&src, t + 50u + 10000u),
                 SERVO_SOURCE_EV_RESET);
        /* A bit that is no event is not kept. */
        servo_source_event_back(&src, 0x80u);
        CHECK_EQ(servo_source_event(&src, t + 50u + 20000u), 0u);
        /* The link gone: nothing goes back. */
        (void)servo_source_step(&src, t + 100u, false, 0u, NULL);
        servo_source_event_back(&src, SERVO_SOURCE_EV_RESET);
        CHECK_EQ(servo_source_event(&src, t + 50u + 30000u), 0u);
    }
    /* A bit this build does not know is dropped, not handed out. */
    servo_source_init(&src);
    sense_link_meter_t m = good(1000u);
    (void)servo_source_step(&src, 1000u, true, 11u, &m);
    src.events = 0x100u;
    CHECK_EQ(servo_source_event(&src, 1000u), 0u);
    CHECK_EQ(src.events, 0u);
}

/* ---------------------------------------------------- through the chain */

static uint32_t said;        /* events handed out, by bit, counted below */
static unsigned said_old, said_reset;

/* One poll of the chain and the meter's step straight after it, as the
 * control task makes them, with the windows taken as the log does; then
 * the rest of @p period_ms. */
static servo_source_id_t chain_step(unsigned period_ms, bool link_up)
{
    const unsigned spent = link_up ? chain_poll() : 0u;
    sense_link_win_t w;
    while (sense_link_take_win(&ch.sl, &w)) {
    }
    sense_link_meter_t m;
    sense_link_meter(&ch.sl, &m);
    const servo_source_id_t id =
        servo_source_step(&src, chain_now(), link_up, ch.minor,
                          link_up ? &m : NULL);
    const uint32_t ev = servo_source_event(&src, chain_now());
    said |= ev;
    if ((ev & SERVO_SOURCE_EV_OLD) != 0u) {
        ++said_old;
    }
    if ((ev & SERVO_SOURCE_EV_RESET) != 0u) {
        ++said_reset;
    }
    if (spent < period_ms) {
        chain_far(period_ms - spent);
    }
    return id;
}

static void chain_fresh(uint16_t minor, uint32_t tick0)
{
    chain_start(minor, tick0, 0x01u);
    chain_far(400u);
    chain_link_up();
    servo_source_init(&src);
    said = 0u;
    said_old = said_reset = 0u;
}

/* Polls until the meter is @p want, at most @p most; how many it took. */
static unsigned until(servo_source_id_t want, unsigned most)
{
    unsigned n = 0u;
    while (n < most && chain_step(53u, true) != want) {
        ++n;
    }
    return n;
}

/* A 4.11 bench with the part answering: the PD mini for the first
 * 1000 ms of good readings, then the INA3221, and it stays -- at 53 ms
 * polls, with a reply lost now and then, and across the tick wrap. */
TEST_CASE(a_working_ina3221_becomes_the_meter_and_stays)
{
    static const uint32_t k_tick0[] = { 1000u, 0xFFFFF800u, 0xFFFFFE00u };
    for (size_t a = 0u; a < sizeof(k_tick0) / sizeof(k_tick0[0]); ++a) {
        chain_fresh(LINK_PROTOCOL_MINOR, k_tick0[a]);
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
        const unsigned n = until(SERVO_SOURCE_INA3221, 60u);
        /* 1000 ms from the first poll with a window: 19 to 21 polls. */
        CHECK(n >= 18u && n <= 22u);
        for (unsigned i = 0u; i < 400u; ++i) {
            if (i % 30u == 5u) {
                ch.lose_win = 1u;
            }
            if (i % 30u == 20u) {
                ch.lose_status = 1u;
            }
            CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_INA3221);
        }
        CHECK_EQ(said, 0u);
        CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NONE);
    }
}

/* Clipped windows and a current below zero are readings: the meter stays
 * the INA3221 through both. */
TEST_CASE(clipped_and_negative_readings_leave_the_meter_alone)
{
    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    ch.flat = true;
    static const double k_amps[] = { 2.0, -0.05, -2.0, 0.0 };
    for (size_t i = 0u; i < sizeof(k_amps) / sizeof(k_amps[0]); ++i) {
        ch.i3221->amps[0] = k_amps[i];
        for (unsigned k = 0u; k < 60u; ++k) {
            CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_INA3221);
        }
    }
    CHECK_EQ(said, 0u);
}

/* The part gone from the bus: the PD mini from the poll that shows it,
 * and the INA3221 again 1000 ms after it is back and read. */
TEST_CASE(a_part_that_stops_answering_drops_to_the_pd_mini)
{
    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    ch.i3221->present = false;
    CHECK(until(SERVO_SOURCE_PDMINI, 10u) <= 2u);
    for (unsigned i = 0u; i < 40u; ++i) {
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
    }
    CHECK(servo_source_why(&src) == SERVO_SOURCE_WHY_SILENT);
    ch.i3221->present = true;
    /* Found by the next scan, up to 1000 ms; then 1000 ms of readings. */
    const unsigned n = until(SERVO_SOURCE_INA3221, 80u);
    CHECK(n >= 19u && n <= 42u);
    CHECK_EQ(said, 0u);
}

/* SDA held low: the bus reads stuck, and the meter is the PD mini. */
TEST_CASE(a_stuck_bus_drops_to_the_pd_mini)
{
    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    ch.fb.low = true;
    CHECK(until(SERVO_SOURCE_PDMINI, 20u) < 20u);
    for (unsigned i = 0u; i < 20u; ++i) {
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
    }
    ch.fb.low = false;
    CHECK(until(SERVO_SOURCE_INA3221, 80u) < 80u);
}

/* The part resets itself.  Polled throughout, the panel sees it offline
 * for 1000 ms.  With the panel's loop held for 1300 ms the coprocessor
 * finds the reset, repairs it and has the part online again before the
 * next read: only the count shows it.  Either way the PD mini, the reset
 * said once, and the INA3221 again after 1000 ms of good readings. */
TEST_CASE(a_reset_repaired_between_two_reads_shows_in_the_count)
{
    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    fake_reset3221(ch.i3221);
    CHECK(until(SERVO_SOURCE_PDMINI, 5u) <= 1u);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SILENT);
    const unsigned back = until(SERVO_SOURCE_INA3221, 80u);
    CHECK(back >= 36u && back <= 42u);          /* 1000 ms and 1000 ms */
    CHECK_EQ(said_reset, 1u);
    CHECK_EQ(said_old, 0u);

    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    fake_reset3221(ch.i3221);
    chain_far(1300u);                           /* no poll meanwhile */
    CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
    sense_link_meter_t m;
    sense_link_meter(&ch.sl, &m);
    CHECK((m.flags & LINK_SN_I3221_ONLINE) != 0u);
    CHECK((m.flags & LINK_SN_I3221_ID_OK) != 0u);
    CHECK(m.win && m.win_valid);
    CHECK_EQ(m.resets, 1u);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_RESET);
    CHECK_EQ(said_reset, 1u);
    const unsigned n = until(SERVO_SOURCE_INA3221, 60u);
    CHECK(n >= 17u && n <= 20u);                /* 1000 ms at 53 ms */
    CHECK_EQ(said_reset, 1u);
}

/* An edit on SETUP: not held from the edit until the page has taken it,
 * and the INA3221 again 1000 ms after the first good poll under it.  The
 * count that starts again with the set-up is no reset. */
TEST_CASE(a_setup_the_page_does_not_hold_drops_to_the_pd_mini)
{
    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    fake_reset3221(ch.i3221);                   /* the count reads 1 */
    CHECK(until(SERVO_SOURCE_PDMINI, 5u) < 5u);
    CHECK(until(SERVO_SOURCE_INA3221, 80u) < 80u);
    CHECK_EQ(said_reset, 1u);

    sense_setup_t w = chain_setup(0x03u);
    sense_link_want(&ch.sl, &w, chain_now());
    CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NOT_HELD);
    const unsigned n = until(SERVO_SOURCE_INA3221, 80u);
    CHECK(n >= 28u && n <= 34u);                /* 500 ms rest, 1000 ms */
    CHECK_EQ(said_reset, 1u);                   /* 1 to 0 is no reset */

    /* A bank that drives: the edit waits, and so does the meter. */
    w = chain_setup(0x01u);
    sense_link_want(&ch.sl, &w, chain_now());
    ch.idle = false;
    for (unsigned i = 0u; i < 60u; ++i) {
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
    }
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NOT_HELD);
    ch.idle = true;
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);

    /* Switched off on SETUP: condition 1. */
    w = chain_setup(0x01u);
    w.i3221 = false;
    sense_link_want(&ch.sl, &w, chain_now());
    CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_OFF);
    /* CH1 not among its channels: the same. */
    w = chain_setup(0x06u);
    sense_link_want(&ch.sl, &w, chain_now());
    for (unsigned i = 0u; i < 60u; ++i) {
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
        CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_OFF);
    }
    CHECK_EQ(said, SERVO_SOURCE_EV_RESET);
}

/* The panel's loop held: the last reads age.  Polls 150 ms apart keep the
 * meter; a hold of 250 ms drops it. */
TEST_CASE(reads_that_age_past_200_ms_drop_to_the_pd_mini)
{
    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    for (unsigned i = 0u; i < 40u; ++i) {
        CHECK_EQ(chain_step(150u, true), SERVO_SOURCE_INA3221);
    }
    for (unsigned i = 0u; i < 5u; ++i) {
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_INA3221);
    }
    /* SERVO_WIN's replies lost for 5 polls, 265 ms: the number stands. */
    ch.lose_win = 5u;
    unsigned dropped = 0u;
    for (unsigned i = 0u; i < 5u; ++i) {
        if (chain_step(53u, true) == SERVO_SOURCE_PDMINI) {
            ++dropped;
            CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NO_WINDOW);
        }
    }
    CHECK_EQ(dropped, 2u);              /* at 212 and 265 ms, not at 159 */
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    /* SENSE's replies lost: the read stands. */
    ch.lose_status = 5u;
    dropped = 0u;
    for (unsigned i = 0u; i < 5u; ++i) {
        if (chain_step(53u, true) == SERVO_SOURCE_PDMINI) {
            ++dropped;
            CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SILENT);
        }
    }
    CHECK_EQ(dropped, 2u);
}

/* The coprocessor's schedule stops: the window number stands while every
 * read is answered.  The PD mini from 200 ms on, and still 2^32 ms later,
 * when the tick has come round to within 200 ms of the last move. */
TEST_CASE(a_window_number_that_stands_stays_stale_across_the_tick_wrap)
{
    chain_fresh(LINK_PROTOCOL_MINOR, 1000u);
    CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
    ch.exch_ms = 0u;                    /* core 1 no longer ticks */
    unsigned held_for = 0u;
    while (held_for < 40u) {
        chain_far_late(50u);
        if (chain_step(0u, true) != SERVO_SOURCE_INA3221) {
            break;
        }
        ++held_for;
    }
    CHECK(held_for >= 3u && held_for <= 4u);        /* under 200 ms */
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NO_WINDOW);
    for (unsigned i = 0u; i < 20u; ++i) {
        chain_far_late(50u);
        CHECK_EQ(chain_step(0u, true), SERVO_SOURCE_PDMINI);
    }
    sense_link_meter_t m;
    sense_link_meter(&ch.sl, &m);
    CHECK(!m.win);
    /* The tick 2^32 ms on, less what brings the last move to 100 ms ago. */
    ch.tick0 += (uint32_t)(0u - (chain_now() - m.win_ms)) + 100u;
    sense_link_meter(&ch.sl, &m);
    CHECK_EQ((uint32_t)(chain_now() - m.win_ms), 100u);
    for (unsigned i = 0u; i < 40u; ++i) {
        chain_far_late(50u);
        CHECK_EQ(chain_step(0u, true), SERVO_SOURCE_PDMINI);
        CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NO_WINDOW);
    }
    /* The schedule runs again: the number moves, and 1000 ms later the
     * INA3221 is the meter. */
    ch.exch_ms = 1u;
    const unsigned n = until(SERVO_SOURCE_INA3221, 60u);
    CHECK(n >= 18u && n <= 22u);
}

/* A 4.10 coprocessor: the PD mini is read, nothing is sent to SERVO_WIN,
 * and the operator is told once.  Again after a link lost and back. */
TEST_CASE(a_4_10_coprocessor_is_never_the_ina3221s_and_is_said_once)
{
    chain_fresh(10u, 1000u);
    for (unsigned i = 0u; i < 400u; ++i) {
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
        CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_OLD);
    }
    CHECK_EQ(ch.to_win, 0u);
    CHECK_EQ(said, SERVO_SOURCE_EV_OLD);
    CHECK_EQ(said_old, 1u);
    CHECK_EQ(sense_link_events(&ch.sl), 0u);    /* and nothing from SENSE */

    sense_link_lost(&ch.sl);
    CHECK_EQ(chain_step(1000u, false), SERVO_SOURCE_MODEL);
    chain_link_up();
    for (unsigned i = 0u; i < 200u; ++i) {
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
    }
    CHECK_EQ(said_old, 2u);
    CHECK_EQ(ch.to_win, 0u);
}

/* The link lost: the model.  Back: the PD mini, and the INA3221 after
 * 1000 ms of good readings counted from the link-up, across the wrap. */
TEST_CASE(a_link_lost_is_the_model_and_its_return_settles_again)
{
    static const uint32_t k_tick0[] = { 1000u, 0xFFFFF000u };
    for (size_t a = 0u; a < sizeof(k_tick0) / sizeof(k_tick0[0]); ++a) {
        chain_fresh(LINK_PROTOCOL_MINOR, k_tick0[a]);
        CHECK(until(SERVO_SOURCE_INA3221, 60u) < 60u);
        sense_link_lost(&ch.sl);
        for (unsigned i = 0u; i < 3u; ++i) {
            CHECK_EQ(chain_step(1000u, false), SERVO_SOURCE_MODEL);
            CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_NO_LINK);
        }
        chain_link_up();
        CHECK_EQ(chain_step(53u, true), SERVO_SOURCE_PDMINI);
        const unsigned n = until(SERVO_SOURCE_INA3221, 60u);
        CHECK(n >= 18u && n <= 22u);
        CHECK_EQ(said, 0u);
    }
}

int main(void)
{
    RUN(the_meter_is_the_model_until_a_coprocessor_answers);
    RUN(the_ina3221_is_the_meter_after_1000_ms_of_good_readings);
    RUN(a_meter_that_stays_outlasts_the_tick_wrap);
    RUN(each_condition_failing_alone_drops_to_the_pd_mini);
    RUN(a_part_that_drops_out_every_900_ms_is_never_the_meter);
    RUN(a_read_is_fresh_for_199_ms_and_not_for_200);
    RUN(a_reset_count_that_moves_is_said_once_each_time);
    RUN(an_older_coprocessor_is_said_once_per_link_and_per_switching_on);
    RUN(events_are_handed_out_one_at_a_time_5000_ms_apart);
    RUN(a_working_ina3221_becomes_the_meter_and_stays);
    RUN(clipped_and_negative_readings_leave_the_meter_alone);
    RUN(a_part_that_stops_answering_drops_to_the_pd_mini);
    RUN(a_stuck_bus_drops_to_the_pd_mini);
    RUN(a_reset_repaired_between_two_reads_shows_in_the_count);
    RUN(a_setup_the_page_does_not_hold_drops_to_the_pd_mini);
    RUN(reads_that_age_past_200_ms_drop_to_the_pd_mini);
    RUN(a_window_number_that_stands_stays_stale_across_the_tick_wrap);
    RUN(a_4_10_coprocessor_is_never_the_ina3221s_and_is_said_once);
    RUN(a_link_lost_is_the_model_and_its_return_settles_again);
    return test_summary("servo_source");
}
