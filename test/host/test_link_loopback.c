/*
 * The two ends talking to each other.
 *
 * Everything below drives the real host state machine against the real device
 * dispatcher through a wire that can lose, corrupt and delay -- no mock of
 * either end, because a mock agrees with whatever the test expects.  This is
 * the closest thing to the bench that exists without hardware, and it is what
 * the two UART transports have to reproduce: move bytes, honour the
 * turnaround, and change nothing else.
 */
#include <string.h>

#include "greatest.h"

#include "fake_wire.h"
#include "link_dev.h"
#include "link_frame.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_wire.h"

/* ------------------------------------------------------------ the far end */

typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
} dev_state_t;

static dev_state_t   g_state;
static link_dev_t    g_dev;
static link_decoder_t g_dev_rx;

static void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    memcpy(out, ((dev_state_t *)ctx)->identity + off, (size_t)n * 2u);
}
static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    memcpy(out, ((dev_state_t *)ctx)->control + off, (size_t)n * 2u);
}
static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    dev_state_t *s = (dev_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if (reg == LINK_CT_THROTTLE && in[i] > LINK_THROTTLE_MAX) {
            return LINK_NACK_BAD_VALUE;
        }
        if (reg == LINK_CT_CLEAR) {
            if (in[i] != LINK_CLEAR_MAGIC) {
                return LINK_NACK_BAD_VALUE;
            }
            link_dev_clear_failsafe(&g_dev, 0);
            continue;
        }
        s->control[reg] = in[i];
    }
    return 0;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
};

/* --------------------------------------------------------------- the wire */

static fake_wire_t to_dev, to_host;
static link_host_t host;
static link_decoder_t host_rx;
static uint32_t now_ms;

static void fresh(void)
{
    memset(&g_state, 0, sizeof(g_state));
    g_state.identity[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
    g_state.identity[LINK_ID_HARDWARE]       = 7;
    now_ms = 0;
    link_dev_init(&g_dev, k_pages, 2, &g_state, now_ms);
    link_decoder_reset(&g_dev_rx);
    link_decoder_reset(&host_rx);
    link_host_init(&host, now_ms);
    fake_wire_reset(&to_dev);
    fake_wire_reset(&to_host);
}

/*
 * One turn of the coprocessor's loop: drain the wire, and when a whole frame
 * verifies, answer it.
 *
 * The turnaround is why this is not simply "reply".  The panel's own direction
 * circuit holds the bus for up to 179 us after its last falling edge, so the
 * far end waits before it answers -- and on this wire that wait is a number
 * rather than a delay, which is the only reason it can be asserted at all.
 */
static uint32_t g_dev_answered_us;
static uint32_t g_dev_frame_us;

static void dev_pump(uint32_t at_us)
{
    uint8_t byte;
    link_msg_t req;
    while (fake_wire_read(&to_dev, &byte, 1) == 1) {
        if (link_decode_byte(&g_dev_rx, byte, &req)) {
            g_dev_frame_us = at_us;
            uint8_t reply[LINK_MAX_FRAME];
            const size_t n = link_dev_handle(&g_dev, &req, reply,
                                             sizeof(reply), now_ms);
            g_dev_answered_us = at_us + LINK_TURNAROUND_US;
            fake_wire_write(&to_host, reply, n);
        }
    }
}

/** One turn of the panel's loop: drain the wire into the host. */
static bool host_pump(void)
{
    uint8_t byte;
    link_msg_t reply;
    bool answered = false;
    while (fake_wire_read(&to_host, &byte, 1) == 1) {
        if (link_decode_byte(&host_rx, byte, &reply)) {
            if (link_host_reply(&host, &reply, now_ms)) {
                answered = true;
            }
        }
    }
    return answered;
}

/** Ask, let the far end answer, and collect it. */
static bool transact_read(uint8_t page, uint8_t off, uint8_t count)
{
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_host_read(&host, page, off, count, wire,
                                    sizeof(wire));
    if (n == 0) {
        return false;
    }
    fake_wire_write(&to_dev, wire, n);
    dev_pump(0);
    return host_pump();
}

/* --------------------------------------------------------------- the cases */

TEST_CASE(a_poll_gets_its_answer)
{
    fresh();
    CHECK(transact_read(LINK_PAGE_IDENTITY, 0, LINK_ID_COUNT));
    CHECK_EQ(host.replies, 1u);
    CHECK_EQ(host.mismatches, 0u);
    CHECK(!host.pending);
}

TEST_CASE(a_write_round_trips_through_the_wire)
{
    fresh();
    const uint16_t v = 4200;
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_host_write(&host, LINK_PAGE_CONTROL,
                                     LINK_CT_THROTTLE, 1, &v, wire,
                                     sizeof(wire));
    fake_wire_write(&to_dev, wire, n);
    dev_pump(0);
    CHECK(host_pump());
    CHECK_EQ(g_state.control[LINK_CT_THROTTLE], 4200);
}

/* The whole conversation, a thousand times, with the counters agreeing at the
 * end.  A leak in the decoder or a frame the far end fails to consume shows up
 * here and nowhere else. */
TEST_CASE(a_thousand_polls_leave_nothing_behind)
{
    fresh();
    for (int i = 0; i < 1000; ++i) {
        CHECK(transact_read(LINK_PAGE_IDENTITY, 0, LINK_ID_COUNT));
    }
    CHECK_EQ(host.polls, 1000u);
    CHECK_EQ(host.replies, 1000u);
    CHECK_EQ(host.mismatches, 0u);
    CHECK_EQ(g_dev.requests, 1000u);
    CHECK_EQ(fake_wire_pending(&to_dev), 0u);
    CHECK_EQ(fake_wire_pending(&to_host), 0u);
    CHECK_EQ(host_rx.len, 0);
    CHECK_EQ(g_dev_rx.len, 0);
}

/* A corrupted request must not be answered at all -- the far end never saw a
 * frame -- and the host must then time out rather than hang. */
TEST_CASE(a_corrupted_request_is_never_answered)
{
    fresh();
    to_dev.corrupt_byte = 3;

    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_host_read(&host, LINK_PAGE_IDENTITY, 0, 4, wire,
                                    sizeof(wire));
    fake_wire_write(&to_dev, wire, n);
    dev_pump(0);

    CHECK_EQ(g_dev.requests, 0u);
    CHECK_EQ(fake_wire_pending(&to_host), 0u);
    CHECK(!host_pump());

    now_ms = LINK_HOST_TIMEOUT_MS;
    CHECK(link_host_tick(&host, now_ms));
    CHECK(host.escalated);
}

/* A corrupted *reply* is the other half: the far end acted, the host never
 * learned it did.  The host must time out, and the next poll must work. */
TEST_CASE(a_corrupted_reply_times_out_and_the_next_poll_recovers)
{
    fresh();
    to_host.corrupt_byte = 2;

    uint8_t wire[LINK_MAX_FRAME];
    size_t n = link_host_read(&host, LINK_PAGE_IDENTITY, 0, 4, wire,
                              sizeof(wire));
    fake_wire_write(&to_dev, wire, n);
    dev_pump(0);
    CHECK_EQ(g_dev.requests, 1u);      /* it did act */
    CHECK(!host_pump());               /* and the host did not hear */

    now_ms = LINK_HOST_TIMEOUT_MS;
    CHECK(link_host_tick(&host, now_ms));

    to_host.corrupt_byte = -1;
    CHECK(transact_read(LINK_PAGE_IDENTITY, 0, 4));
    CHECK(!host.escalated);
}

/* A reply cut off mid-frame is what a mistimed turnaround looks like. */
TEST_CASE(a_truncated_reply_is_not_an_answer)
{
    fresh();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_host_read(&host, LINK_PAGE_IDENTITY, 0,
                                    LINK_ID_COUNT, wire, sizeof(wire));
    fake_wire_write(&to_dev, wire, n);
    to_host.truncate_after = 5;
    dev_pump(0);
    CHECK(!host_pump());
    CHECK_EQ(host.replies, 0u);
}

/* The far end answers only after the turnaround, and the panel's own driver
 * needs every microsecond of it. */
TEST_CASE(the_far_end_waits_the_turnaround_before_answering)
{
    fresh();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_host_read(&host, LINK_PAGE_IDENTITY, 0, 2, wire,
                                    sizeof(wire));
    fake_wire_write(&to_dev, wire, n);
    dev_pump(1000);
    CHECK_EQ(g_dev_frame_us, 1000u);
    CHECK_EQ(g_dev_answered_us - g_dev_frame_us, LINK_TURNAROUND_US);
    /* And the wait covers the slowest release the circuit can produce. */
    CHECK(LINK_TURNAROUND_US >= 179u);
}

/* A refusal must travel the wire as an answer, or a read-only page would be
 * indistinguishable from a dead link. */
TEST_CASE(a_refusal_travels_as_an_answer)
{
    fresh();
    const uint16_t v = 1;
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_host_write(&host, LINK_PAGE_IDENTITY, 0, 1, &v,
                                     wire, sizeof(wire));
    fake_wire_write(&to_dev, wire, n);
    dev_pump(0);
    CHECK(host_pump());
    CHECK_EQ(host.nacks, 1u);
    CHECK(!host.pending);
}

/* The whole failure, end to end: the far end stops answering, both watchdogs
 * fire in the right order, and traffic returning does not re-arm anything. */
TEST_CASE(a_dead_far_end_trips_both_watchdogs_in_order)
{
    fresh();
    CHECK(transact_read(LINK_PAGE_IDENTITY, 0, 2));

    to_dev.deaf = true;
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_host_read(&host, LINK_PAGE_IDENTITY, 0, 2, wire,
                                    sizeof(wire));
    fake_wire_write(&to_dev, wire, n);

    /*
     * Both loops keep running throughout -- that is what makes the wire's
     * deafness the thing under test.  An earlier version ticked the clock
     * without pumping either end, so the coprocessor saw silence because
     * nobody delivered its bytes rather than because the wire had swallowed
     * them, and it passed just as happily with the fault switched off.
     */
    uint32_t dev_fired = 0, host_fired = 0;
    for (now_ms = 1; now_ms <= 2000; ++now_ms) {
        dev_pump(0);
        (void)host_pump();
        if (link_dev_tick(&g_dev, now_ms) && !dev_fired) {
            dev_fired = now_ms;
        }
        if (link_host_tick(&host, now_ms) && !host_fired) {
            host_fired = now_ms;
        }
    }
    CHECK_EQ(dev_fired, LINK_DEV_SILENCE_MS);
    CHECK_EQ(host_fired, LINK_HOST_TIMEOUT_MS);

    /* The wire comes back.  The bench does not. */
    to_dev.deaf = false;
    CHECK(transact_read(LINK_PAGE_IDENTITY, 0, 2));
    CHECK(!g_dev.silent);
    CHECK(g_dev.failsafe);

    /* Only the deliberate write clears it. */
    const uint16_t magic = LINK_CLEAR_MAGIC;
    const size_t m = link_host_write(&host, LINK_PAGE_CONTROL, LINK_CT_CLEAR,
                                     1, &magic, wire, sizeof(wire));
    fake_wire_write(&to_dev, wire, m);
    dev_pump(0);
    CHECK(host_pump());
    CHECK(!g_dev.failsafe);
}

int main(void)
{
    RUN(a_poll_gets_its_answer);
    RUN(a_write_round_trips_through_the_wire);
    RUN(a_thousand_polls_leave_nothing_behind);
    RUN(a_corrupted_request_is_never_answered);
    RUN(a_corrupted_reply_times_out_and_the_next_poll_recovers);
    RUN(a_truncated_reply_is_not_an_answer);
    RUN(the_far_end_waits_the_turnaround_before_answering);
    RUN(a_refusal_travels_as_an_answer);
    RUN(a_dead_far_end_trips_both_watchdogs_in_order);
    return test_summary("link_loopback");
}
