/**
 * @file
 * @brief The repeat of a request whose answer is lost: the panel's
 *        link_host_retry() against the IOmcu's link_dev_dispatch(), over a
 *        modelled CAN (Controller Area Network) bus with a millisecond clock.
 *
 * The bus loses or delays the frames of one transmission at a time: the
 * request as first sent is transmission 0, its repeat transmission 1.  The
 * wait is the panel's exchange() loop: receive, repeat when due, tick, until
 * the request is answered or abandoned.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"

/* ------------------------------------------------------------ the device */

/** @brief The far end's pages and what was done to them. */
typedef struct {
    uint16_t control[LINK_CT_COUNT]; /**< CONTROL: a write is an action  */
    uint32_t control_writes;         /**< CONTROL write calls            */
    uint32_t bench_reads;            /**< BENCH read calls: a drained queue */
    uint16_t wide[LINK_MAX_REGS];    /**< OUTPUTS, 32 registers          */
    uint32_t wide_writes;            /**< OUTPUTS write calls            */
    uint32_t wide_reads;             /**< OUTPUTS read calls             */
    uint32_t upd_writes;             /**< UPDATE_CTL write calls         */
} far_t;

static far_t      g;
static link_dev_t dev;
static link_host_t host;

/**
 * @brief CONTROL read.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param out receives them
 */
static void ct_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, g.control + off, (size_t)n * 2u);
}

/**
 * @brief CONTROL write: every call is an action, counted.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param in the values
 * @return 0, taken
 */
static uint8_t ct_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    memcpy(g.control + off, in, (size_t)n * 2u);
    ++g.control_writes;
    return 0;
}

/**
 * @brief BENCH read: each call takes the next values, as a queue drains.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param out receives them
 */
static void bn_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    ++g.bench_reads;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = (uint16_t)(g.bench_reads * 100u + off + i);
    }
}

/**
 * @brief OUTPUTS read, counted.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param out receives them
 */
static void wide_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    ++g.wide_reads;
    memcpy(out, g.wide + off, (size_t)n * 2u);
}

/**
 * @brief OUTPUTS write, counted.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param in the values
 * @return 0, taken
 */
static uint8_t wide_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    (void)ctx;
    ++g.wide_writes;
    memcpy(g.wide + off, in, (size_t)n * 2u);
    return 0;
}

/**
 * @brief UPDATE_CTL read: zeros.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param out receives them
 */
static void upd_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    (void)off;
    memset(out, 0, (size_t)n * 2u);
}

/**
 * @brief UPDATE_CTL write, counted.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param in the values
 * @return 0, taken
 */
static uint8_t upd_write(void *ctx, uint8_t off, uint8_t n,
                         const uint16_t *in)
{
    (void)ctx;
    (void)off;
    (void)n;
    (void)in;
    ++g.upd_writes;
    return 0;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_CONTROL,    LINK_CT_COUNT, ct_read,   ct_write },
    { LINK_PAGE_BENCH,      LINK_BN_COUNT, bn_read,   NULL },
    { LINK_PAGE_OUTPUTS,    LINK_MAX_REGS, wide_read, wide_write },
    { LINK_PAGE_UPDATE_CTL, LINK_MAX_REGS, upd_read,  upd_write },
};

/* --------------------------------------------------------------- the bus */

/** @brief Frames on their way, either direction. */
#define QUEUE 64

/** @brief One frame on its way. */
typedef struct {
    link_can_frame_t f;    /**< the frame                       */
    uint32_t         at;   /**< when it arrives, ms             */
    bool             up;   /**< to the IOmcu; else to the panel */
    unsigned         tx;   /**< the transmission it belongs to  */
} flight_t;

/** @brief What the bus does to each transmission's frames. */
typedef struct {
    bool     lose_request[2]; /**< the request frames do not arrive   */
    /** @brief The answer frames are lost, after answer_pass of them. */
    bool     lose_answer[2];
    unsigned answer_pass[2];  /**< answer frames that arrive first    */
    uint32_t answer_ms[2];    /**< how long the answer takes          */
} bus_rules_t;

static flight_t    s_q[QUEUE];
static unsigned    s_qn;
static bus_rules_t s_rules;
static unsigned    s_passed[2];   /* answer frames put, per transmission */
static uint32_t    s_now;

/**
 * @brief Put the frames of @p m on the bus, the ones the rules lose left out.
 * @param m the message
 * @param up towards the IOmcu
 * @param tx its transmission
 * @param at when its frames arrive
 */
static void put(const link_msg_t *m, bool up, unsigned tx, uint32_t at)
{
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(m, f, LINK_CAN_MAX_FRAMES);
    CHECK(n > 0u);
    for (size_t i = 0; i < n && s_qn < QUEUE; ++i) {
        const bool lost = up ? s_rules.lose_request[tx]
                             : (s_rules.lose_answer[tx]
                                && s_passed[tx]++ >= s_rules.answer_pass[tx]);
        if (lost) {
            continue;
        }
        s_q[s_qn].f  = f[i];
        s_q[s_qn].at = at;
        s_q[s_qn].up = up;
        s_q[s_qn].tx = tx;
        ++s_qn;
    }
}

/**
 * @brief The panel sends transmission @p tx of @p req.
 * @param req the request
 * @param tx 0 the request, 1 its repeat
 */
static void host_send(const link_msg_t *req, unsigned tx)
{
    put(req, true, tx, s_now);
}

/**
 * @brief Deliver what is due: a frame for the IOmcu is answered at once and
 *        its answer goes back after the transmission's delay; a frame for
 *        the panel is offered to the host.
 * @param whole receives the answer when the request is answered
 * @return true when the host took an answer
 */
static bool deliver(link_msg_t *whole)
{
    bool done = false;
    unsigned i = 0;
    while (i < s_qn) {
        if ((int32_t)(s_now - s_q[i].at) < 0) {
            ++i;
            continue;
        }
        const flight_t fl = s_q[i];
        s_q[i] = s_q[--s_qn];
        link_msg_t m;
        if (!link_can_decode(&fl.f, &m)) {
            continue;
        }
        if (fl.up) {
            link_msg_t reply;
            CHECK(link_dev_dispatch(&dev, &m, &reply, s_now));
            put(&reply, false, fl.tx, s_now + s_rules.answer_ms[fl.tx]);
        } else if (link_host_accept(&host, &m, s_now, whole)) {
            done = true;
        }
    }
    return done;
}

/**
 * @brief The panel's wait, a millisecond a step: receive, repeat when due,
 *        tick.
 * @param req the request, already built
 * @param whole receives the answer
 * @param ended receives when the wait ended
 * @return true when answered, false when abandoned
 */
static bool wait_for(const link_msg_t *req, link_msg_t *whole,
                     uint32_t *ended)
{
    host_send(req, 0u);
    for (unsigned step = 0; step < 3000u; ++step) {
        if (deliver(whole)) {
            *ended = s_now;
            return true;
        }
        link_msg_t again;
        if (link_host_retry(&host, s_now, &again)) {
            host_send(&again, 1u);
            if (deliver(whole)) {
                *ended = s_now;
                return true;
            }
        }
        (void)link_host_tick(&host, s_now);
        if (!link_host_pending(&host)) {
            *ended = s_now;
            return false;
        }
        (void)link_dev_tick(&dev, s_now);
        ++s_now;
    }
    CHECK(false);
    return false;
}

/**
 * @brief Both ends fresh at @p t, the host numbering when @p numbered, and
 *        the IOmcu having heard one request so its watchdog runs.
 * @param t the clock
 * @param numbered the far end reports 4.37 or later
 */
static void fresh(uint32_t t, bool numbered)
{
    memset(&g, 0, sizeof(g));
    memset(&s_rules, 0, sizeof(s_rules));
    s_rules.answer_ms[0] = 1u;
    s_rules.answer_ms[1] = 1u;
    s_qn  = 0u;
    s_passed[0] = 0u;
    s_passed[1] = 0u;
    s_now = t;
    link_dev_init(&dev, k_pages, (uint8_t)(sizeof(k_pages)
                                           / sizeof(k_pages[0])),
                  NULL, t);
    link_host_init(&host, t);
    host.numbered = numbered;
    link_msg_t req;
    link_msg_t reply;
    memset(&req, 0, sizeof(req));
    req.op = LINK_OP_READ;
    req.page = LINK_PAGE_CONTROL;
    req.count = 1u;
    CHECK(link_dev_dispatch(&dev, &req, &reply, t));
}

/**
 * @brief Write @p v to CONTROL register 1 over the modelled bus.
 * @param v the value
 * @param whole receives the answer
 * @param ended receives when the wait ended
 * @return true when answered
 */
static bool write_control(uint16_t v, link_msg_t *whole, uint32_t *ended)
{
    link_msg_t req;
    CHECK(link_host_write(&host, LINK_PAGE_CONTROL, 1u, 1u, &v, s_now, &req));
    return wait_for(&req, whole, ended);
}

/* ----------------------------------------------------------------- cases */

/* The answer lost once: the repeat at 50ms is answered from the copy, the
 * write acted once, nothing timed out and the IOmcu never went quiet. */
TEST_CASE(a_lost_answer_is_recovered_by_one_repeat_with_no_error)
{
    fresh(1000u, true);
    s_rules.lose_answer[0] = true;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(write_control(77u, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK_EQ(got.regs[0], 77u);
    CHECK_EQ(ended, 1000u + LINK_HOST_RETRY_MS + 1u);
    CHECK_EQ(host.retried, 1u);
    CHECK_EQ(host.timeouts, 0u);
    CHECK_EQ(host.mismatches, 0u);
    CHECK_EQ(g.control_writes, 1u);
    CHECK_EQ(dev.repeats, 1u);
    /* No FAULT 01 at the far end and no link loss at this end. */
    CHECK(!dev.failsafe);
    CHECK(!link_host_tick(&host, ended));
    CHECK(!host.escalated);
}

/* An IOmcu older than 4.37: unnumbered, no repeat, the 1000ms wait and the
 * far end's 200ms failsafe as before. */
TEST_CASE(an_unnumbered_request_is_never_repeated)
{
    fresh(1000u, false);
    s_rules.lose_answer[0] = true;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(!write_control(77u, &got, &ended));
    CHECK_EQ(ended, 1000u + LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(host.retried, 0u);
    CHECK_EQ(host.timeouts, 1u);
    CHECK_EQ(host.req.seq, 0u);
    CHECK(dev.failsafe);
}

/* The repeat lost as well: the existing path at its existing time, the
 * write acted once, the far end quiet 200ms after the repeat. */
TEST_CASE(a_lost_repeat_ends_at_the_existing_timeout)
{
    fresh(1000u, true);
    s_rules.lose_answer[0] = true;
    s_rules.lose_answer[1] = true;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(!write_control(5u, &got, &ended));
    CHECK_EQ(ended, 1000u + LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(host.timeouts, 1u);
    CHECK_EQ(host.retried, 0u);
    CHECK_EQ(g.control_writes, 1u);
    CHECK(dev.failsafe);
    CHECK_EQ(dev.last_request_ms, 1000u + LINK_HOST_RETRY_MS);
}

/* The request itself lost: the repeat is the first the IOmcu hears, and it
 * acts on it, once. */
TEST_CASE(a_repeat_of_a_request_that_never_arrived_acts_once)
{
    fresh(1000u, true);
    s_rules.lose_request[0] = true;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(write_control(9u, &got, &ended));
    CHECK_EQ(got.regs[0], 9u);
    CHECK_EQ(g.control_writes, 1u);
    CHECK_EQ(dev.repeats, 0u);
    CHECK_EQ(host.retried, 1u);
}

/*
 * The first answer only late, arriving after the repeat went out: taken
 * once.  The repeat's answer that follows is refused, and so is it when a
 * new request for the same window is already waiting: its number is
 * another.
 */
TEST_CASE(a_late_first_answer_after_the_repeat_is_taken_once)
{
    fresh(1000u, true);
    s_rules.answer_ms[0] = LINK_HOST_RETRY_MS + 20u;   /* 70ms */
    s_rules.answer_ms[1] = 30u;                         /* at 80ms */
    link_msg_t req;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 4u, s_now, &req));
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(wait_for(&req, &got, &ended));
    CHECK_EQ(ended, 1000u + 70u);
    CHECK_EQ(got.regs[0], 100u);   /* the first read */
    CHECK_EQ(g.bench_reads, 1u);   /* the repeat read nothing */
    CHECK_EQ(host.retried, 1u);

    /* The same window again while the repeat's answer is on its way. */
    s_rules.answer_ms[0] = 20u;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 4u, s_now, &req));
    const uint32_t refused_before = host.mismatches;
    CHECK(wait_for(&req, &got, &ended));
    CHECK_EQ(host.mismatches, refused_before + 1u);
    CHECK_EQ(got.regs[0], 200u);   /* its own read, not the late copy */
    CHECK_EQ(g.bench_reads, 2u);
    CHECK_EQ(host.retried, 1u);
}

/*
 * A write of a whole page is 8 frames, and the last two acknowledgements
 * are lost.  The repeat sends all 8; each is answered from the copy and the
 * page is written 8 times, not 16.
 */
TEST_CASE(a_wide_write_repeated_is_not_written_again)
{
    fresh(1000u, true);
    s_rules.lose_answer[0] = true;
    s_rules.answer_pass[0] = 6u;
    uint16_t v[LINK_MAX_REGS];
    for (unsigned i = 0; i < LINK_MAX_REGS; ++i) {
        v[i] = (uint16_t)(1000u + i);
    }
    link_msg_t req;
    CHECK(link_host_write(&host, LINK_PAGE_OUTPUTS, 0u, LINK_MAX_REGS, v,
                          s_now, &req));
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(wait_for(&req, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK_EQ(got.count, LINK_MAX_REGS);
    CHECK_EQ(got.regs[31], 1031u);
    CHECK_EQ(g.wide_writes, 8u);
    CHECK_EQ(dev.repeats, 8u);
    CHECK_EQ(host.retried, 1u);
}

/*
 * A read of 16 that keeps the rest, then the READ_HELD whose answer is
 * lost: the repeat gets the kept data from the copy, not BUSY for a copy
 * already spent, and the page was read once.
 */
TEST_CASE(a_repeated_read_held_gets_the_data_it_was_answered_with)
{
    fresh(1000u, true);
    g.wide[20] = 4242u;
    link_msg_t req;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(link_host_read(&host, LINK_PAGE_OUTPUTS, 0u, LINK_READ_REGS_MAX,
                         s_now, &req));
    CHECK(wait_for(&req, &got, &ended));
    s_rules.lose_answer[0] = true;
    CHECK(link_host_read_held(&host, LINK_PAGE_OUTPUTS, LINK_READ_REGS_MAX,
                              LINK_READ_REGS_MAX, s_now, &req));
    CHECK(wait_for(&req, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_DATA);
    CHECK_EQ(got.regs[4], 4242u);
    CHECK_EQ(g.wide_reads, 1u);
    CHECK_EQ(host.retried, 1u);
}

/* ACTIVATE restarts the IOmcu 20ms after its answer: never repeated. */
TEST_CASE(an_activate_is_never_repeated)
{
    fresh(1000u, true);
    s_rules.lose_answer[0] = true;
    const uint16_t cmd = LINK_UPD_CMD_ACTIVATE;
    link_msg_t req;
    CHECK(link_host_write(&host, LINK_PAGE_UPDATE_CTL, LINK_UPD_CMD, 1u, &cmd,
                          s_now, &req));
    CHECK(req.seq != 0u);
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(!wait_for(&req, &got, &ended));
    CHECK_EQ(ended, 1000u + LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(g.upd_writes, 1u);

    /* Another command on the page is repeated. */
    fresh(1000u, true);
    s_rules.lose_answer[0] = true;
    const uint16_t abort_cmd = LINK_UPD_CMD_ABORT;
    CHECK(link_host_write(&host, LINK_PAGE_UPDATE_CTL, LINK_UPD_CMD, 1u,
                          &abort_cmd, s_now, &req));
    CHECK(wait_for(&req, &got, &ended));
    CHECK_EQ(g.upd_writes, 1u);
    CHECK_EQ(host.retried, 1u);
}

/* A stop during the wait takes the repeat back: the request waits out its
 * timeout. */
TEST_CASE(a_cancelled_repeat_is_not_sent)
{
    fresh(1000u, true);
    s_rules.lose_answer[0] = true;
    link_msg_t req;
    const uint16_t arm = 1u;
    CHECK(link_host_write(&host, LINK_PAGE_CONTROL, LINK_CT_ARM, 1u, &arm,
                          s_now, &req));
    link_host_retry_cancel(&host);
    link_host_retry_cancel(NULL);
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(!wait_for(&req, &got, &ended));
    CHECK_EQ(ended, 1000u + LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(dev.repeats, 0u);
    CHECK_EQ(g.control_writes, 1u);
}

/* The window: not at 49ms, once at 50ms, and from 200ms never. */
TEST_CASE(the_repeat_is_due_at_its_window_and_not_past_its_last)
{
    link_host_init(&host, 0u);
    host.numbered = true;
    link_msg_t req;
    link_msg_t again;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 4u, 500u, &req));
    CHECK(!link_host_retry(&host, 500u + LINK_HOST_RETRY_MS - 1u, &again));
    CHECK(link_host_retry(&host, 500u + LINK_HOST_RETRY_MS, &again));
    CHECK(memcmp(&again, &req, sizeof(req)) == 0);
    CHECK(!link_host_retry(&host, 500u + LINK_HOST_RETRY_MS + 1u, &again));

    /* A caller back first at 199ms still sends it; at 200ms it does not,
     * and not later either. */
    link_host_abandon(&host);
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 4u, 500u, &req));
    CHECK(link_host_retry(&host, 500u + LINK_HOST_RETRY_LAST_MS - 1u,
                          &again));
    link_host_abandon(&host);
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 4u, 500u, &req));
    CHECK(!link_host_retry(&host, 500u + LINK_HOST_RETRY_LAST_MS, &again));
    CHECK(!link_host_retry(&host, 500u + LINK_HOST_RETRY_MS, &again));

    /* Nothing outstanding, or no state: nothing. */
    link_host_abandon(&host);
    CHECK(!link_host_retry(&host, 500u + LINK_HOST_RETRY_MS, &again));
    CHECK(!link_host_retry(NULL, 0u, &again));
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 4u, 500u, &req));
    CHECK(!link_host_retry(&host, 500u + LINK_HOST_RETRY_MS, NULL));
}

/* Across the 32-bit millisecond wrap: the repeat at 50ms and the timeout
 * at 1000ms, both counted from a send 20ms before the wrap. */
TEST_CASE(the_repeat_and_the_timeout_hold_across_the_wrap)
{
    const uint32_t t0 = UINT32_MAX - 19u;
    fresh(t0, true);
    s_rules.lose_answer[0] = true;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(write_control(3u, &got, &ended));
    CHECK_EQ(ended, (uint32_t)(t0 + LINK_HOST_RETRY_MS + 1u));
    CHECK_EQ(host.retried, 1u);

    fresh(t0, true);
    s_rules.lose_answer[0] = true;
    s_rules.lose_answer[1] = true;
    CHECK(!write_control(3u, &got, &ended));
    CHECK_EQ(ended, (uint32_t)(t0 + LINK_HOST_TIMEOUT_MS));
    CHECK_EQ(g.control_writes, 1u);
}

/* Numbers run 1 to 7 and round again, never 0 and never twice in a row. */
TEST_CASE(exchange_numbers_run_one_to_seven)
{
    CHECK_EQ(LINK_MINOR_REPEAT, 37u);
    CHECK_EQ(LINK_SEQ_MAX, 7u);
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_REPEAT);
    link_host_init(&host, 0u);
    host.numbered = true;
    uint8_t last = 0u;
    for (unsigned i = 0; i < 20u; ++i) {
        link_msg_t req;
        CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 1u, i, &req));
        CHECK(req.seq >= 1u && req.seq <= LINK_SEQ_MAX);
        CHECK(req.seq != last);
        CHECK_EQ(req.seq, (uint8_t)(i % LINK_SEQ_MAX + 1u));
        last = req.seq;
        link_host_abandon(&host);
    }
    /* A number out of range is put back to 1. */
    host.next_seq = 0u;
    link_msg_t req;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0u, 1u, 0u, &req));
    CHECK_EQ(req.seq, 1u);
}

/* -------------------------------------------------------- the IOmcu's copy */

/**
 * @brief A numbered CONTROL write of @p v to register 1.
 * @param seq its number
 * @param v the value
 * @return the message
 */
static link_msg_t numbered_write(uint8_t seq, uint16_t v)
{
    link_msg_t m;
    memset(&m, 0, sizeof(m));
    m.op = LINK_OP_WRITE;
    m.page = LINK_PAGE_CONTROL;
    m.offset = 1u;
    m.count = 1u;
    m.regs[0] = v;
    m.seq = seq;
    return m;
}

/*
 * The copy lasts LINK_DEV_REPEAT_MS: a repeat at 999ms is answered from it,
 * one at 1000ms acts.  A new number, and an unnumbered request, drop it.
 * A repeat restarts the silence watchdog.
 */
TEST_CASE(the_iomcu_keeps_an_exchange_for_its_repeat_only)
{
    fresh(0u, true);
    link_msg_t req = numbered_write(3u, 10u);
    link_msg_t reply;
    CHECK(link_dev_dispatch(&dev, &req, &reply, 100u));
    CHECK_EQ(reply.seq, 3u);
    CHECK_EQ(g.control_writes, 1u);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 100u + 150u));
    CHECK_EQ(g.control_writes, 1u);
    CHECK_EQ(dev.repeats, 1u);
    CHECK_EQ(reply.op, LINK_OP_ACK);
    CHECK_EQ(reply.seq, 3u);
    /* The repeat was a request: no silence at 200ms after the first. */
    CHECK(!link_dev_tick(&dev, 100u + 200u));
    CHECK(link_dev_dispatch(&dev, &req, &reply,
                            100u + LINK_DEV_REPEAT_MS - 1u));
    CHECK_EQ(g.control_writes, 1u);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 100u + LINK_DEV_REPEAT_MS));
    CHECK_EQ(g.control_writes, 2u);

    /* Another number is another exchange, even with the same content. */
    req = numbered_write(4u, 10u);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 2000u));
    CHECK_EQ(g.control_writes, 3u);
    req = numbered_write(3u, 10u);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 2001u));
    CHECK_EQ(g.control_writes, 4u);

    /* Same number, other values: a message of the same exchange. */
    req = numbered_write(3u, 11u);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 2002u));
    CHECK_EQ(g.control_writes, 5u);
    CHECK_EQ(dev.kept_count, 2u);

    /* Unnumbered drops what was kept; its reply is unnumbered. */
    req = numbered_write(0u, 11u);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 2003u));
    CHECK_EQ(reply.seq, 0u);
    CHECK_EQ(dev.kept_count, 0u);
    req = numbered_write(3u, 11u);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 2004u));
    CHECK_EQ(g.control_writes, 7u);
}

/* A refusal is kept and repeated as it was: the page is not asked again. */
TEST_CASE(a_refused_request_is_repeated_as_its_refusal)
{
    fresh(0u, true);
    link_msg_t req;
    memset(&req, 0, sizeof(req));
    req.op = LINK_OP_WRITE;
    req.page = LINK_PAGE_BENCH;   /* read-only */
    req.count = 1u;
    req.seq = 5u;
    link_msg_t reply;
    CHECK(link_dev_dispatch(&dev, &req, &reply, 10u));
    CHECK_EQ(reply.op, LINK_OP_NACK);
    CHECK_EQ(reply.regs[0], LINK_NACK_READ_ONLY);
    CHECK(link_dev_dispatch(&dev, &req, &reply, 60u));
    CHECK_EQ(reply.op, LINK_OP_NACK);
    CHECK_EQ(reply.seq, 5u);
    CHECK_EQ(dev.repeats, 1u);
}

/* More messages in one exchange than the copy holds: the ones past it are
 * acted on and not kept. */
TEST_CASE(messages_past_the_copy_are_acted_on_and_not_kept)
{
    fresh(0u, true);
    link_msg_t reply;
    for (unsigned i = 0; i < LINK_DEV_REPEAT_KEEP + 2u; ++i) {
        link_msg_t req = numbered_write(2u, (uint16_t)i);
        CHECK(link_dev_dispatch(&dev, &req, &reply, 10u));
    }
    CHECK_EQ(dev.kept_count, LINK_DEV_REPEAT_KEEP);
    link_msg_t last = numbered_write(2u, (uint16_t)(LINK_DEV_REPEAT_KEEP + 1u));
    CHECK(link_dev_dispatch(&dev, &last, &reply, 11u));
    CHECK_EQ(g.control_writes, LINK_DEV_REPEAT_KEEP + 3u);
    link_msg_t first = numbered_write(2u, 0u);
    CHECK(link_dev_dispatch(&dev, &first, &reply, 12u));
    CHECK_EQ(g.control_writes, LINK_DEV_REPEAT_KEEP + 3u);
}

int main(void)
{
    RUN(a_lost_answer_is_recovered_by_one_repeat_with_no_error);
    RUN(an_unnumbered_request_is_never_repeated);
    RUN(a_lost_repeat_ends_at_the_existing_timeout);
    RUN(a_repeat_of_a_request_that_never_arrived_acts_once);
    RUN(a_late_first_answer_after_the_repeat_is_taken_once);
    RUN(a_wide_write_repeated_is_not_written_again);
    RUN(a_repeated_read_held_gets_the_data_it_was_answered_with);
    RUN(an_activate_is_never_repeated);
    RUN(a_cancelled_repeat_is_not_sent);
    RUN(the_repeat_is_due_at_its_window_and_not_past_its_last);
    RUN(the_repeat_and_the_timeout_hold_across_the_wrap);
    RUN(exchange_numbers_run_one_to_seven);
    RUN(the_iomcu_keeps_an_exchange_for_its_repeat_only);
    RUN(a_refused_request_is_repeated_as_its_refusal);
    RUN(messages_past_the_copy_are_acted_on_and_not_kept);
    return test_summary("link_retry");
}
