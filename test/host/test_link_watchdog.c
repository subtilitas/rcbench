/*
 * Both watchdogs, driven through their transitions against a mock clock.
 *
 * The ratio is the design: the coprocessor gives up at 200 ms and the host at
 * a second, so the end holding the outputs is always the more suspicious of
 * the two.  These cases exist to make that a property rather than a comment.
 */
#include <string.h>

#include "greatest.h"

#include "link_dev.h"
#include "link_frame.h"
#include "link_host.h"
#include "link_pages.h"

/* ------------------------------------------------------- a minimal device */

static uint16_t g_control[LINK_CT_COUNT];
static link_dev_t dev;

static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, g_control + off, (size_t)n * sizeof(uint16_t));
}

static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    (void)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if (reg == LINK_CT_CLEAR) {
            if (in[i] != LINK_CLEAR_MAGIC) {
                return LINK_NACK_BAD_VALUE;
            }
            link_dev_clear_failsafe(&dev, 0);
            continue;
        }
        g_control[reg] = in[i];
    }
    return 0;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_CONTROL, LINK_CT_COUNT, control_read, control_write },
};

static void fresh_dev(uint32_t now)
{
    memset(g_control, 0, sizeof(g_control));
    link_dev_init(&dev, k_pages, 1, NULL, now);
}

static void poll_at(uint32_t now)
{
    link_msg_t req = { 0 };
    req.op = LINK_OP_READ; req.page = LINK_PAGE_CONTROL;
    req.offset = 0; req.count = 1;
    uint8_t wire[LINK_MAX_FRAME];
    link_dev_handle(&dev, &req, wire, sizeof(wire), now);
}

/* -------------------------------------------------- the coprocessor's 200 ms */

TEST_CASE(the_coprocessor_fails_safe_after_two_hundred_milliseconds)
{
    fresh_dev(1000);

    /* One millisecond short is still alive.  The boundary is the whole point
     * of the number, so it is tested at the boundary. */
    CHECK(!link_dev_tick(&dev, 1000 + LINK_DEV_SILENCE_MS - 1));
    CHECK(!dev.failsafe);

    CHECK(link_dev_tick(&dev, 1000 + LINK_DEV_SILENCE_MS));
    CHECK(dev.failsafe);
    CHECK(dev.silent);
}

/* The edge is reported once, so the caller drops the outputs once rather than
 * on every tick for as long as the link stays down. */
TEST_CASE(the_failsafe_edge_is_reported_exactly_once)
{
    fresh_dev(0);
    int edges = 0;
    for (uint32_t t = 0; t <= 5000; t += 10) {
        if (link_dev_tick(&dev, t)) {
            ++edges;
        }
    }
    CHECK_EQ(edges, 1);
}

TEST_CASE(polling_keeps_the_coprocessor_alive)
{
    fresh_dev(0);
    for (uint32_t t = 0; t <= 10000; t += 50) {
        poll_at(t);
        CHECK(!link_dev_tick(&dev, t));
    }
    CHECK(!dev.failsafe);
}

/*
 * The one that matters most.  A link that comes back is not consent to spin a
 * propeller: traffic clears the silence, and only a deliberate write clears
 * the latch.
 */
TEST_CASE(traffic_returning_does_not_lift_the_failsafe)
{
    fresh_dev(0);
    CHECK(link_dev_tick(&dev, LINK_DEV_SILENCE_MS));
    CHECK(dev.failsafe);

    for (uint32_t t = 300; t < 2000; t += 20) {
        poll_at(t);
        link_dev_tick(&dev, t);
    }
    CHECK(!dev.silent);      /* the host is plainly back      */
    CHECK(dev.failsafe);     /* and the outputs are still off */
}

TEST_CASE(only_the_magic_value_clears_the_failsafe)
{
    fresh_dev(0);
    link_dev_tick(&dev, LINK_DEV_SILENCE_MS);
    CHECK(dev.failsafe);

    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = LINK_PAGE_CONTROL;
    w.offset = LINK_CT_CLEAR; w.count = 1;
    uint8_t wire[LINK_MAX_FRAME];

    w.regs[0] = 1;                     /* any old truthy value */
    CHECK(link_dev_handle(&dev, &w, wire, sizeof(wire), 300) > 0);
    CHECK(dev.failsafe);

    w.regs[0] = LINK_CLEAR_MAGIC;
    CHECK(link_dev_handle(&dev, &w, wire, sizeof(wire), 300) > 0);
    CHECK(!dev.failsafe);
}

/*
 * A real millisecond counter wraps after 49 days.
 *
 * The failure mode is not the obvious one.  `now >= then + timeout` is
 * evaluated in uint32 arithmetic, so the deadline wraps along with the clock
 * and the two agree perfectly *after* the turnover.  They differ **before**
 * it: while `now` is still large and the deadline has already wrapped small,
 * the naive form says the timeout expired the moment the deadline wrapped --
 * firing early by up to a whole interval, on a bench, with the outputs live.
 *
 * So the sample that matters is taken between the start and the wrap, and it
 * asserts that nothing has fired.  Two earlier versions of this case did not:
 * the first started 255 ms short of the top, where adding 200 never
 * overflowed at all; the second crossed the wrap correctly but only sampled
 * on the far side, where a broken implementation looks identical to a working
 * one.  Both were found by making the comparison naive on purpose and
 * noticing that nothing went red.
 */
TEST_CASE(the_coprocessor_watchdog_survives_the_millisecond_wrap)
{
    const uint32_t near_wrap = (uint32_t)0u - (LINK_DEV_SILENCE_MS / 2u);
    fresh_dev(near_wrap);

    /* The deadline really is on the other side of the turnover. */
    CHECK(near_wrap + LINK_DEV_SILENCE_MS < near_wrap);

    /* Still before the wrap, and only a quarter of the interval has passed. */
    link_dev_tick(&dev, near_wrap + LINK_DEV_SILENCE_MS / 4u);
    CHECK(!dev.failsafe);

    /* Past the wrap, but still inside the interval. */
    link_dev_tick(&dev, near_wrap + LINK_DEV_SILENCE_MS - 1u);
    CHECK(!dev.failsafe);

    CHECK(link_dev_tick(&dev, near_wrap + LINK_DEV_SILENCE_MS));
    CHECK(dev.failsafe);
}

/* ----------------------------------------------------- the host's one second */

static link_host_t host;

TEST_CASE(the_host_escalates_after_one_second)
{
    link_host_init(&host, 0);
    CHECK(!link_host_tick(&host, LINK_HOST_TIMEOUT_MS - 1));
    CHECK(link_host_tick(&host, LINK_HOST_TIMEOUT_MS));
    CHECK(host.escalated);
}

TEST_CASE(the_host_is_the_more_patient_of_the_two)
{
    /* Not a restatement of the constants: the ordering is the design, and a
     * later edit that made the host stricter would invert the argument for
     * having two watchdogs at all. */
    CHECK(LINK_DEV_SILENCE_MS < LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(LINK_HOST_TIMEOUT_MS / LINK_DEV_SILENCE_MS, 5);
}

TEST_CASE(a_reply_resets_the_host_and_its_escalation)
{
    link_host_init(&host, 0);
    uint8_t wire[LINK_MAX_FRAME];
    CHECK(link_host_read(&host, LINK_PAGE_CONTROL, 0, 1, wire,
                         sizeof(wire)) > 0);
    CHECK(link_host_tick(&host, LINK_HOST_TIMEOUT_MS));
    CHECK(host.escalated);
    CHECK_EQ(host.timeouts, 1u);

    /* The abandoned request must not block the next one -- a host that stopped
     * asking would never notice the link recovering. */
    CHECK(link_host_read(&host, LINK_PAGE_CONTROL, 0, 1, wire,
                         sizeof(wire)) > 0);

    link_msg_t reply = { 0 };
    reply.op = LINK_OP_DATA; reply.page = LINK_PAGE_CONTROL;
    reply.offset = 0; reply.count = 1;
    CHECK(link_host_reply(&host, &reply, 1500));
    CHECK(!host.escalated);
}

TEST_CASE(only_one_request_is_outstanding_at_a_time)
{
    link_host_init(&host, 0);
    uint8_t wire[LINK_MAX_FRAME];
    CHECK(link_host_read(&host, LINK_PAGE_CONTROL, 0, 1, wire,
                         sizeof(wire)) > 0);
    CHECK_EQ(link_host_read(&host, LINK_PAGE_CONTROL, 0, 1, wire,
                            sizeof(wire)), 0u);
}

/*
 * A reply delayed past a timeout arrives after the host has moved on.
 * Accepting it would attribute one page's registers to another -- silently,
 * and with the CRC entirely happy.
 */
TEST_CASE(a_stale_reply_cannot_answer_the_current_question)
{
    link_host_init(&host, 0);
    uint8_t wire[LINK_MAX_FRAME];

    link_host_read(&host, LINK_PAGE_CONTROL, 0, 1, wire, sizeof(wire));
    link_host_tick(&host, LINK_HOST_TIMEOUT_MS);          /* gave up */
    link_host_read(&host, LINK_PAGE_BENCH, 4, 2, wire, sizeof(wire));

    /*
     * Each of these differs from the outstanding request in exactly one
     * field, so each can only be rejected by the check for that field.  An
     * earlier version sent a reply that differed in page *and* width, which
     * the width check caught -- leaving the page check untested and a
     * deliberate break of it invisible.
     */
    link_msg_t wrong_page = { 0 };
    wrong_page.op = LINK_OP_DATA; wrong_page.page = LINK_PAGE_CONTROL;
    wrong_page.offset = 4; wrong_page.count = 2;
    CHECK(!link_host_reply(&host, &wrong_page, 1100));

    link_msg_t wrong_offset = { 0 };
    wrong_offset.op = LINK_OP_DATA; wrong_offset.page = LINK_PAGE_BENCH;
    wrong_offset.offset = 0; wrong_offset.count = 2;
    CHECK(!link_host_reply(&host, &wrong_offset, 1100));

    link_msg_t wrong_width = { 0 };
    wrong_width.op = LINK_OP_DATA; wrong_width.page = LINK_PAGE_BENCH;
    wrong_width.offset = 4; wrong_width.count = 1;
    CHECK(!link_host_reply(&host, &wrong_width, 1100));

    CHECK_EQ(host.mismatches, 3u);

    link_msg_t good = { 0 };
    good.op = LINK_OP_DATA; good.page = LINK_PAGE_BENCH;
    good.offset = 4; good.count = 2;
    CHECK(link_host_reply(&host, &good, 1200));
}

TEST_CASE(a_reply_of_the_wrong_shape_is_not_an_answer)
{
    link_host_init(&host, 0);
    uint8_t wire[LINK_MAX_FRAME];

    /* An ACK does not answer a READ, and a DATA of the wrong width does not
     * answer the window that was asked for. */
    link_host_read(&host, LINK_PAGE_CONTROL, 0, 2, wire, sizeof(wire));
    link_msg_t ack = { 0 };
    ack.op = LINK_OP_ACK; ack.page = LINK_PAGE_CONTROL;
    ack.offset = 0; ack.count = 2;
    CHECK(!link_host_reply(&host, &ack, 10));

    link_msg_t narrow = { 0 };
    narrow.op = LINK_OP_DATA; narrow.page = LINK_PAGE_CONTROL;
    narrow.offset = 0; narrow.count = 1;
    CHECK(!link_host_reply(&host, &narrow, 10));

    link_msg_t right = { 0 };
    right.op = LINK_OP_DATA; right.page = LINK_PAGE_CONTROL;
    right.offset = 0; right.count = 2;
    CHECK(link_host_reply(&host, &right, 10));
}

/* A refusal is an answer.  The host must stop waiting on a NACK, or a page it
 * is not allowed to write would look identical to a dead link. */
TEST_CASE(a_nack_answers_the_request_it_refuses)
{
    link_host_init(&host, 0);
    uint8_t wire[LINK_MAX_FRAME];
    const uint16_t v = 1;
    link_host_write(&host, LINK_PAGE_IDENTITY, 0, 1, &v, wire, sizeof(wire));

    link_msg_t nack = { 0 };
    nack.op = LINK_OP_NACK; nack.page = LINK_PAGE_IDENTITY;
    nack.offset = 0; nack.count = 1; nack.regs[0] = LINK_NACK_READ_ONLY;
    CHECK(link_host_reply(&host, &nack, 5));
    CHECK_EQ(host.nacks, 1u);
    CHECK(!host.pending);
}

TEST_CASE(the_host_watchdog_survives_the_millisecond_wrap)
{
    const uint32_t near_wrap = (uint32_t)0u - (LINK_HOST_TIMEOUT_MS / 2u);
    link_host_init(&host, near_wrap);

    CHECK(near_wrap + LINK_HOST_TIMEOUT_MS < near_wrap);

    /* Before the turnover -- the sample a naive comparison fails. */
    link_host_tick(&host, near_wrap + LINK_HOST_TIMEOUT_MS / 4u);
    CHECK(!host.escalated);

    link_host_tick(&host, near_wrap + LINK_HOST_TIMEOUT_MS - 1u);
    CHECK(!host.escalated);

    CHECK(link_host_tick(&host, near_wrap + LINK_HOST_TIMEOUT_MS));
    CHECK(host.escalated);
}

/* ------------------------------------------------------------ both together */

/*
 * The whole point of the ratio, end to end: when the wire dies, the
 * coprocessor has already failed safe long before the panel has even noticed.
 */
TEST_CASE(the_coprocessor_gives_up_long_before_the_panel_does)
{
    fresh_dev(0);
    link_host_init(&host, 0);
    uint8_t wire[LINK_MAX_FRAME];
    link_host_read(&host, LINK_PAGE_CONTROL, 0, 1, wire, sizeof(wire));

    uint32_t dev_fired = 0, host_fired = 0;
    for (uint32_t t = 1; t <= 2000; ++t) {
        if (link_dev_tick(&dev, t) && dev_fired == 0) {
            dev_fired = t;
        }
        if (link_host_tick(&host, t) && host_fired == 0) {
            host_fired = t;
        }
    }
    CHECK_EQ(dev_fired, LINK_DEV_SILENCE_MS);
    CHECK_EQ(host_fired, LINK_HOST_TIMEOUT_MS);
    CHECK(dev_fired < host_fired);
}

int main(void)
{
    RUN(the_coprocessor_fails_safe_after_two_hundred_milliseconds);
    RUN(the_failsafe_edge_is_reported_exactly_once);
    RUN(polling_keeps_the_coprocessor_alive);
    RUN(traffic_returning_does_not_lift_the_failsafe);
    RUN(only_the_magic_value_clears_the_failsafe);
    RUN(the_coprocessor_watchdog_survives_the_millisecond_wrap);
    RUN(the_host_escalates_after_one_second);
    RUN(the_host_is_the_more_patient_of_the_two);
    RUN(a_reply_resets_the_host_and_its_escalation);
    RUN(only_one_request_is_outstanding_at_a_time);
    RUN(a_stale_reply_cannot_answer_the_current_question);
    RUN(a_reply_of_the_wrong_shape_is_not_an_answer);
    RUN(a_nack_answers_the_request_it_refuses);
    RUN(the_host_watchdog_survives_the_millisecond_wrap);
    RUN(the_coprocessor_gives_up_long_before_the_panel_does);
    return test_summary("link_watchdog");
}
