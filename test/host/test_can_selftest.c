/*
 * The echo test, run against a simulated bus that can lose, delay, corrupt
 * and reorder.
 *
 * Both ends are here, talking to each other through a bus this file controls,
 * so every fault a bring-up can meet is constructed rather than waited for.
 */
#include <string.h>

#include "greatest.h"

#include "can_selftest.h"
#include "link_pages.h"

/* A bus with faults in it.  Frames go in one end and come out the other,
 * or do not. */
typedef struct {
    bool     drop_next;
    bool     flip_a_bit;
    bool     deaf;          /* the far end is not listening at all */
    uint32_t delivered;
} bus_t;

/* Run one probe/echo exchange.  Returns the milliseconds it consumed. */
static uint32_t exchange(can_selftest_t *st, bus_t *bus, uint32_t *now_ms)
{
    link_can_frame_t probe;
    if (!can_selftest_probe(st, *now_ms, &probe)) {
        return 0;
    }

    link_can_frame_t echo;
    bool got = false;
    if (!bus->deaf && !bus->drop_next && can_selftest_echo(&probe, &echo)) {
        if (bus->flip_a_bit) {
            echo.data[4] ^= 0x08u;
        }
        got = true;
        ++bus->delivered;
    }
    bus->drop_next = false;
    bus->flip_a_bit = false;

    if (got) {
        *now_ms += 1;
        can_selftest_rx(st, &echo, 400u);
    } else {
        *now_ms += st->timeout_ms;
        can_selftest_tick(st, *now_ms);
    }
    return 1;
}

static can_selftest_verdict_t run(can_selftest_t *st, bus_t *bus, int n)
{
    uint32_t now = 1000;
    for (int i = 0; i < n; ++i) {
        exchange(st, bus, &now);
    }
    return can_selftest_verdict(st);
}

TEST_CASE(a_good_bus_echoes_everything_and_says_so)
{
    can_selftest_t st;
    bus_t bus;
    can_selftest_init(&st, 50);
    memset(&bus, 0, sizeof(bus));

    CHECK_EQ(run(&st, &bus, 20), CAN_SELFTEST_OK);
    CHECK_EQ(st.sent, 20);
    CHECK_EQ(st.echoed, 20);
    CHECK_EQ(st.corrupt, 0);
    CHECK_EQ(st.timed_out, 0);
    CHECK_STR_EQ(can_selftest_text(CAN_SELFTEST_OK),
                 "every probe came back intact");
}

/* No verdict before there is evidence for one: a bring-up report that cries
 * fault on its first frame is one nobody reads. */
TEST_CASE(no_verdict_before_enough_probes)
{
    can_selftest_t st;
    bus_t bus;
    can_selftest_init(&st, 50);
    memset(&bus, 0, sizeof(bus));
    bus.deaf = true;

    uint32_t now = 1000;
    for (unsigned i = 0; i < CAN_SELFTEST_MIN_PROBES - 1u; ++i) {
        exchange(&st, &bus, &now);
        if (can_selftest_verdict(&st) != CAN_SELFTEST_RUNNING) {
            T_FAIL("a verdict after %u probes", i + 1u);
        }
    }
    exchange(&st, &bus, &now);
    CHECK_EQ(can_selftest_verdict(&st), CAN_SELFTEST_SILENT);
}

/*
 * Silence, reported as silence.  A dead bus also times out on every probe, so
 * the timeouts are the loudest number and the least useful one -- the same
 * ordering trap the link's own diagnosis has.
 */
TEST_CASE(a_silent_bus_is_reported_as_silent_not_as_timeouts)
{
    can_selftest_t st;
    bus_t bus;
    can_selftest_init(&st, 50);
    memset(&bus, 0, sizeof(bus));
    bus.deaf = true;

    CHECK_EQ(run(&st, &bus, 12), CAN_SELFTEST_SILENT);
    CHECK_EQ(st.echoed, 0);
    CHECK_EQ(st.timed_out, 12);
    CHECK(can_selftest_hint(CAN_SELFTEST_SILENT)[0] != '\0');
}

/*
 * A single altered bit outranks everything else, however few.  Corruption on
 * CAN means the bit timing or the termination is wrong, and a bus that mostly
 * works is not a defence -- it will stop mostly working when it warms up.
 */
TEST_CASE(one_altered_bit_outranks_a_hundred_good_frames)
{
    can_selftest_t st;
    bus_t bus;
    can_selftest_init(&st, 50);
    memset(&bus, 0, sizeof(bus));

    uint32_t now = 1000;
    for (int i = 0; i < 40; ++i) {
        if (i == 17) {
            bus.flip_a_bit = true;
        }
        exchange(&st, &bus, &now);
    }
    CHECK_EQ(st.corrupt, 1);
    CHECK_EQ(st.echoed, 39);
    CHECK_EQ(can_selftest_verdict(&st), CAN_SELFTEST_CORRUPT);
}

/*
 * A marginal bus does both at once -- it drops some frames and alters others --
 * and that is the case where the ordering earns its keep. Loss is the louder
 * number and the less useful one: it points at cable length and terminators,
 * while the corruption says the bit timing is wrong. Report the loss and
 * somebody spends the afternoon at the wrong end of the problem.
 */
TEST_CASE(corruption_outranks_loss_when_a_bus_does_both)
{
    can_selftest_t st;
    bus_t bus;
    can_selftest_init(&st, 50);
    memset(&bus, 0, sizeof(bus));

    uint32_t now = 1000;
    for (int i = 0; i < 40; ++i) {
        if (i % 5 == 0) {
            bus.drop_next = true;      /* lots of loss ... */
        } else if (i == 23) {
            bus.flip_a_bit = true;     /* ... and one altered bit */
        }
        exchange(&st, &bus, &now);
    }
    CHECK(st.timed_out > 4);           /* loss is the louder number */
    CHECK_EQ(st.corrupt, 1);           /* corruption is the rarer one */
    CHECK_EQ(can_selftest_verdict(&st), CAN_SELFTEST_CORRUPT);
}

/*
 * Loss with bus errors and loss without them are different faults, and they
 * send you to opposite ends of the bench.
 *
 * A frame corrupted on the wire is counted by whichever controller saw it go
 * wrong.  A frame dropped because nobody read it in time arrives perfectly and
 * is counted by nothing at all.  The first is termination, timing or cable
 * length; the second is a receive buffer that overran while its owner was busy
 * elsewhere, and telling somebody to check terminators for it wastes an
 * afternoon on hardware that is working.
 *
 * The real bring-up produced exactly this: 2019 of 2024 echoed, and the
 * controller reporting tx_err 0, rx_err 0, bus_err 0 two lines under a hint
 * about terminators.
 */
TEST_CASE(loss_with_bus_errors_and_loss_without_are_different_faults)
{
    for (int with_errors = 0; with_errors <= 1; ++with_errors) {
        can_selftest_t st;
        bus_t bus;
        can_selftest_init(&st, 50);
        memset(&bus, 0, sizeof(bus));

        uint32_t now = 1000;
        for (int i = 0; i < 40; ++i) {
            if (i % 9 == 0) {
                bus.drop_next = true;
            }
            exchange(&st, &bus, &now);
        }
        /* The controller's own count, which only the transport can supply. */
        st.bus_errors = with_errors ? 17u : 0u;

        CHECK(st.timed_out > 0);
        CHECK(st.echoed > 0);
        CHECK_EQ(st.corrupt, 0);

        const can_selftest_verdict_t want = with_errors ? CAN_SELFTEST_LOSSY
                                                        : CAN_SELFTEST_DROPPED;
        if (can_selftest_verdict(&st) != want) {
            T_FAIL("%s bus errors: wrong verdict",
                   with_errors ? "with" : "without");
        }
    }

    /* And the hint for the no-error case must not mention the wire, because
     * the absence of bus errors is evidence the wire is fine. */
    const char *h = can_selftest_hint(CAN_SELFTEST_DROPPED);
    CHECK(h[0] != '\0');
    CHECK(strstr(h, "terminator") == NULL);
    CHECK(strstr(h, "not a wiring fault") != NULL);
}

/* Corruption still outranks both, because a frame that arrives altered is a
 * wire fault whatever the counters say about the ones that did not. */
TEST_CASE(corruption_outranks_a_clean_bus_losing_frames)
{
    can_selftest_t st;
    bus_t bus;
    can_selftest_init(&st, 50);
    memset(&bus, 0, sizeof(bus));

    uint32_t now = 1000;
    for (int i = 0; i < 40; ++i) {
        if (i % 9 == 0) { bus.drop_next = true; }
        else if (i == 23) { bus.flip_a_bit = true; }
        exchange(&st, &bus, &now);
    }
    st.bus_errors = 0;
    CHECK_EQ(can_selftest_verdict(&st), CAN_SELFTEST_CORRUPT);
}

/*
 * An echo of a probe already given up on.  A bus slower than the timeout
 * produces these, and they are not corruption -- calling them corruption would
 * send somebody to check terminators when the answer is a longer timeout.
 */
TEST_CASE(a_late_echo_is_stale_rather_than_corrupt)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);

    link_can_frame_t probe, echo;
    CHECK(can_selftest_probe(&st, 1000, &probe));
    CHECK(can_selftest_echo(&probe, &echo));

    /* It times out ... */
    CHECK(can_selftest_tick(&st, 1060));
    CHECK_EQ(st.timed_out, 1);
    /* ... and then the answer arrives. */
    CHECK(can_selftest_rx(&st, &echo, 60000));
    CHECK_EQ(st.stale, 1);
    CHECK_EQ(st.corrupt, 0);
    CHECK_EQ(st.echoed, 0);
}

/*
 * The payload has to be checked, not just the sequence number.  An echo
 * carrying the right sequence and the wrong body is exactly what a bus with a
 * marginal sample point produces, and a test that only compared sequence
 * numbers would pass on it.
 */
TEST_CASE(the_whole_payload_is_checked_and_not_just_the_sequence)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);

    link_can_frame_t probe, echo;
    CHECK(can_selftest_probe(&st, 1000, &probe));
    CHECK(can_selftest_echo(&probe, &echo));
    echo.data[7] ^= 0x01u;              /* sequence intact, body altered */
    CHECK(can_selftest_rx(&st, &echo, 400));
    CHECK_EQ(st.corrupt, 1);
    CHECK_EQ(st.echoed, 0);

    /* And a short frame is corruption too, not a different kind of event. */
    can_selftest_init(&st, 50);
    CHECK(can_selftest_probe(&st, 1000, &probe));
    CHECK(can_selftest_echo(&probe, &echo));
    echo.dlc = 4;
    CHECK(can_selftest_rx(&st, &echo, 400));
    CHECK_EQ(st.corrupt, 1);
}

/*
 * The patterns exist to stress bit stuffing, so the test must actually send
 * the ones that do.  A bus that fails only on long runs of one polarity would
 * pass a test that sent counting integers for ever.
 */
TEST_CASE(the_probes_cycle_through_the_patterns_that_stress_stuffing)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);
    bool seen_00 = false, seen_ff = false, seen_55 = false, seen_aa = false;

    uint32_t now = 1000;
    for (int i = 0; i < 8; ++i) {
        link_can_frame_t p;
        CHECK(can_selftest_probe(&st, now, &p));
        CHECK_EQ(p.dlc, 8);
        switch (p.data[2]) {
        case 0x00: seen_00 = true; break;
        case 0xFF: seen_ff = true; break;
        case 0x55: seen_55 = true; break;
        case 0xAA: seen_aa = true; break;
        default: T_FAIL("probe %d carried an unplanned pattern 0x%02X",
                        i, p.data[2]); break;
        }
        /* The body is one pattern throughout, which is what makes a long run
         * long. */
        for (int b = 3; b < 8; ++b) {
            CHECK_EQ(p.data[b], p.data[2]);
        }
        link_can_frame_t e;
        CHECK(can_selftest_echo(&p, &e));
        can_selftest_rx(&st, &e, 400);
        now += 1;
    }
    CHECK(seen_00 && seen_ff && seen_55 && seen_aa);
}

/* One probe outstanding at a time, so a loss is attributed to the probe that
 * was lost rather than to whichever reply arrives next. */
TEST_CASE(only_one_probe_is_in_flight_at_a_time)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);
    link_can_frame_t a, b;
    CHECK(can_selftest_probe(&st, 1000, &a));
    CHECK_EQ(can_selftest_probe(&st, 1000, &b), false);
    CHECK_EQ(can_selftest_probe(&st, 1040, &b), false);   /* still waiting */
    CHECK(can_selftest_tick(&st, 1050));
    CHECK(can_selftest_probe(&st, 1050, &b));             /* now it may */
    CHECK(b.data[0] != a.data[0] || b.data[1] != a.data[1]);
}

/*
 * It runs at the lowest priority on the bus and on a page the map does not
 * use, so it can be left running beside real traffic and can never delay a
 * control write.
 */
TEST_CASE(the_probes_never_outrank_real_traffic)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);
    link_can_frame_t p, e;
    CHECK(can_selftest_probe(&st, 1000, &p));
    CHECK(can_selftest_echo(&p, &e));

    const uint32_t control = link_can_id(LINK_CAN_PRIO_CONTROL, LINK_OP_WRITE,
                                         LINK_PAGE_CONTROL, 0, 1);
    const uint32_t telemetry = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_READ,
                                           LINK_PAGE_BENCH, 0, 13);
    CHECK(p.id > control);
    CHECK(p.id > telemetry);
    CHECK(e.id > control);
    CHECK(e.id > telemetry);

    /* And on a page nothing else uses, so no dispatcher can mistake it for
     * register traffic. */
    uint8_t page = 0;
    link_can_id_split(p.id, NULL, NULL, &page, NULL, NULL);
    CHECK_EQ(page, CAN_SELFTEST_PAGE);
    CHECK(page != LINK_PAGE_BENCH && page != LINK_PAGE_CONTROL
          && page != LINK_PAGE_STATUS && page != LINK_PAGE_IDENTITY
          && page != LINK_PAGE_LIMITS && page != LINK_PAGE_FAILSAFE);
}

/* Somebody else's traffic is not this test's business and must not be counted
 * as anything at all. */
TEST_CASE(unrelated_traffic_is_ignored_rather_than_miscounted)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);
    link_can_frame_t p;
    CHECK(can_selftest_probe(&st, 1000, &p));

    link_can_frame_t other;
    memset(&other, 0, sizeof(other));
    other.id = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_DATA,
                           LINK_PAGE_BENCH, 0, 4);
    other.dlc = 8;
    CHECK_EQ(can_selftest_rx(&st, &other, 400), false);
    CHECK_EQ(st.corrupt, 0);
    CHECK_EQ(st.stale, 0);
    CHECK_EQ(st.echoed, 0);
    CHECK(st.waiting);   /* still waiting for its own */

    /* And the responder does not echo it either. */
    link_can_frame_t e;
    CHECK_EQ(can_selftest_echo(&other, &e), false);
}

TEST_CASE(the_round_trip_keeps_its_extremes)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);
    const uint32_t us[] = { 900, 400, 1500, 600 };
    uint32_t now = 1000;
    for (size_t i = 0; i < sizeof(us) / sizeof(us[0]); ++i) {
        link_can_frame_t p, e;
        CHECK(can_selftest_probe(&st, now, &p));
        CHECK(can_selftest_echo(&p, &e));
        CHECK(can_selftest_rx(&st, &e, us[i]));
        now += 1;
    }
    CHECK_EQ(st.rtt_min_us, 400);
    CHECK_EQ(st.rtt_max_us, 1500);
}

/*
 * The far end's numbers, carried across the bus so that one console shows both
 * halves of a fault instead of a USB cable being swapped to see the other.
 */
TEST_CASE(the_far_ends_counters_survive_the_round_trip)
{
    const can_remote_status_t sent = {
        .up = true, .tx_errors = 128, .rx_errors = 3, .flags = 0xC0,
        .echoes = 51234, .overflows = 7,
    };
    link_can_frame_t req, rsp;
    CHECK(can_selftest_status_request(&req));
    CHECK_EQ(req.dlc, 0);            /* the whole question is its address */
    CHECK(can_selftest_status_reply(&req, &sent, &rsp));
    CHECK_EQ(rsp.dlc, 8);

    can_remote_status_t got;
    memset(&got, 0, sizeof(got));
    CHECK(can_selftest_status_parse(&rsp, &got));
    CHECK_EQ(got.up, true);
    CHECK_EQ(got.tx_errors, 128);
    CHECK_EQ(got.rx_errors, 3);
    CHECK_EQ(got.flags, 0xC0);
    CHECK_EQ(got.echoes, 51234);     /* past a byte, so the order matters */
    CHECK_EQ(got.overflows, 7);

    /* A far end that never came up says so, and says it distinctly from one
     * that came up with nothing to report. */
    const can_remote_status_t down = { .up = false };
    CHECK(can_selftest_status_reply(&req, &down, &rsp));
    CHECK(can_selftest_status_parse(&rsp, &got));
    CHECK_EQ(got.up, false);
}

/*
 * The status exchange shares a page with the echo test, so the two must not be
 * mistaken for each other in either direction -- an echo counted as a status
 * would be read as counters, and a status counted as an echo would be a lost
 * probe that never was.
 */
TEST_CASE(status_and_echo_traffic_do_not_answer_each_other)
{
    can_selftest_t st;
    can_selftest_init(&st, 50);

    link_can_frame_t probe, echo, sreq, srsp;
    const can_remote_status_t rs = { .up = true, .echoes = 9 };
    CHECK(can_selftest_probe(&st, 1000, &probe));
    CHECK(can_selftest_status_request(&sreq));
    CHECK(probe.id != sreq.id);

    /* The echo responder ignores a status request ... */
    CHECK_EQ(can_selftest_echo(&sreq, &echo), false);
    /* ... and the status responder ignores a probe. */
    CHECK_EQ(can_selftest_status_reply(&probe, &rs, &srsp), false);

    /* A status reply is not an echo, and does not disturb an outstanding
     * probe or get counted as anything. */
    CHECK(can_selftest_status_reply(&sreq, &rs, &srsp));
    CHECK_EQ(can_selftest_rx(&st, &srsp, 400), false);
    CHECK_EQ(st.echoed, 0);
    CHECK_EQ(st.corrupt, 0);
    CHECK_EQ(st.stale, 0);
    CHECK(st.waiting);

    /* And an echo is not a status. */
    can_remote_status_t junk;
    CHECK(can_selftest_echo(&probe, &echo));
    CHECK_EQ(can_selftest_status_parse(&echo, &junk), false);
}

/* Both stay at the lowest priority, so leaving the diagnostics running cannot
 * delay anything the bench actually does. */
TEST_CASE(the_status_exchange_never_outranks_real_traffic)
{
    link_can_frame_t sreq, srsp;
    const can_remote_status_t rs = { .up = true };
    CHECK(can_selftest_status_request(&sreq));
    CHECK(can_selftest_status_reply(&sreq, &rs, &srsp));
    const uint32_t control = link_can_id(LINK_CAN_PRIO_CONTROL, LINK_OP_WRITE,
                                         LINK_PAGE_CONTROL, 0, 1);
    const uint32_t telemetry = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_READ,
                                           LINK_PAGE_BENCH, 0, 13);
    CHECK(sreq.id > control);
    CHECK(sreq.id > telemetry);
    CHECK(srsp.id > control);
    CHECK(srsp.id > telemetry);
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    can_selftest_t st;
    link_can_frame_t f;
    memset(&f, 0, sizeof(f));
    can_selftest_init(NULL, 50);
    can_selftest_init(&st, 0);
    CHECK(st.timeout_ms > 0);   /* a zero timeout would never wait at all */
    CHECK_EQ(can_selftest_probe(NULL, 0, &f), false);
    CHECK_EQ(can_selftest_probe(&st, 0, NULL), false);
    CHECK_EQ(can_selftest_rx(NULL, &f, 0), false);
    CHECK_EQ(can_selftest_rx(&st, NULL, 0), false);
    CHECK_EQ(can_selftest_tick(NULL, 0), false);
    CHECK_EQ(can_selftest_verdict(NULL), CAN_SELFTEST_RUNNING);
    CHECK_EQ(can_selftest_echo(NULL, &f), false);
    CHECK_EQ(can_selftest_echo(&f, NULL), false);
    can_remote_status_t rs;
    CHECK_EQ(can_selftest_status_request(NULL), false);
    CHECK_EQ(can_selftest_status_reply(NULL, NULL, NULL), false);
    CHECK_EQ(can_selftest_status_parse(NULL, &rs), false);
    CHECK_EQ(can_selftest_status_parse(&f, NULL), false);
    CHECK(can_selftest_text(CAN_SELFTEST_OK)[0] != '\0');
    CHECK(can_selftest_hint((can_selftest_verdict_t)99)[0] == '\0');
}

int main(void)
{
    RUN(a_good_bus_echoes_everything_and_says_so);
    RUN(no_verdict_before_enough_probes);
    RUN(a_silent_bus_is_reported_as_silent_not_as_timeouts);
    RUN(one_altered_bit_outranks_a_hundred_good_frames);
    RUN(corruption_outranks_loss_when_a_bus_does_both);
    RUN(loss_with_bus_errors_and_loss_without_are_different_faults);
    RUN(corruption_outranks_a_clean_bus_losing_frames);
    RUN(a_late_echo_is_stale_rather_than_corrupt);
    RUN(the_whole_payload_is_checked_and_not_just_the_sequence);
    RUN(the_probes_cycle_through_the_patterns_that_stress_stuffing);
    RUN(only_one_probe_is_in_flight_at_a_time);
    RUN(the_probes_never_outrank_real_traffic);
    RUN(unrelated_traffic_is_ignored_rather_than_miscounted);
    RUN(the_round_trip_keeps_its_extremes);
    RUN(the_far_ends_counters_survive_the_round_trip);
    RUN(status_and_echo_traffic_do_not_answer_each_other);
    RUN(the_status_exchange_never_outranks_real_traffic);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("can_selftest");
}
