/*
 * The page dispatcher: what the coprocessor answers, and what it refuses.
 *
 * The refusals are the interesting half.  Every one of them has to produce a
 * frame, because silence on this link already means "the coprocessor is not
 * there" and must never also mean "I heard you and declined".
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include <stdio.h>

#include "greatest.h"

#include "link_dev.h"
#include "link_pages.h"
#include "outputs.h"
#include "outputs_pages.h"
#include "rcbench_version.h"

/* A stand-in for the coprocessor's own state. */
typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
    int      clears;
} fake_dev_t;

static fake_dev_t g;

static void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const fake_dev_t *f = (const fake_dev_t *)ctx;
    memcpy(out, f->identity + off, (size_t)n * sizeof(uint16_t));
}

static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const fake_dev_t *f = (const fake_dev_t *)ctx;
    memcpy(out, f->control + off, (size_t)n * sizeof(uint16_t));
}

static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    fake_dev_t *f = (fake_dev_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if (reg == LINK_CT_THROTTLE && in[i] > LINK_THROTTLE_MAX) {
            return LINK_NACK_BAD_VALUE;
        }
        if (reg == LINK_CT_CLEAR) {
            if (in[i] != LINK_CLEAR_MAGIC) {
                return LINK_NACK_BAD_VALUE;
            }
            ++f->clears;
            f->control[reg] = 0;   /* the magic is an action, not a setting */
            continue;
        }
        f->control[reg] = in[i];
    }
    return 0;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
};

static link_dev_t dev;

static void fresh(void)
{
    memset(&g, 0, sizeof(g));
    g.identity[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
    g.identity[LINK_ID_PROTOCOL_MINOR] = LINK_PROTOCOL_MINOR;
    g.identity[LINK_ID_HARDWARE]       = 3;
    link_dev_init(&dev, k_pages, 2, &g, 0);
}

/* Ask the dispatcher directly.  What a transport does with the answer is
 * tested where the transport is; this file is about what the answer *is*. */
static bool ask(const link_msg_t *req, link_msg_t *reply)
{
    return link_dev_dispatch(&dev, req, reply, 0);
}

static link_msg_t read_req(uint8_t page, uint8_t off, uint8_t count)
{
    link_msg_t m = { 0 };
    m.op = LINK_OP_READ; m.page = page; m.offset = off; m.count = count;
    return m;
}

static bool ask_read(uint8_t page, uint8_t off, uint8_t count,
                     link_msg_t *reply)
{
    const link_msg_t req = read_req(page, off, count);
    return ask(&req, reply);
}

TEST_CASE(a_read_returns_the_registers)
{
    fresh();
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_IDENTITY, 0, LINK_ID_COUNT, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    CHECK_EQ(r.count, LINK_ID_COUNT);
    CHECK_EQ(r.regs[LINK_ID_PROTOCOL_MAJOR], LINK_PROTOCOL_MAJOR);
    CHECK_EQ(r.regs[LINK_ID_HARDWARE], 3);
}

/* Every offset and every width inside a page, because an off-by-one in the
 * window arithmetic is a wrong register rather than a crash. */
TEST_CASE(every_window_of_every_page_round_trips)
{
    fresh();
    for (uint8_t i = 0; i < LINK_ID_COUNT; ++i) {
        g.identity[i] = (uint16_t)(0xC000u + i);
    }
    for (uint8_t off = 0; off < LINK_ID_COUNT; ++off) {
        for (uint8_t n = 1; n <= LINK_ID_COUNT - off; ++n) {
            link_msg_t r;
            CHECK(ask_read(LINK_PAGE_IDENTITY, off, n, &r));
            CHECK_EQ(r.op, LINK_OP_DATA);
            CHECK_EQ(r.offset, off);
            CHECK_EQ(r.count, n);
            for (uint8_t k = 0; k < n; ++k) {
                CHECK_EQ(r.regs[k], (uint16_t)(0xC000u + off + k));
            }
        }
    }
}

TEST_CASE(a_write_is_acknowledged_and_takes_effect)
{
    fresh();
    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = LINK_PAGE_CONTROL;
    w.offset = LINK_CT_THROTTLE; w.count = 1; w.regs[0] = 2500;

    link_msg_t r;
    CHECK(ask(&w, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(r.regs[0], 2500);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 2500);
}

TEST_CASE(an_unknown_page_is_refused_not_ignored)
{
    fresh();
    link_msg_t r;
    CHECK(ask_read(0x7F, 0, 1, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
}

/* The frame layer refuses anything wider than a page; this is the narrower
 * question of whether it fits *this* page, which is smaller. */
TEST_CASE(a_window_past_the_end_of_a_page_is_refused)
{
    fresh();
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_IDENTITY, LINK_ID_COUNT - 1, 2, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_RANGE);

    CHECK(ask_read(LINK_PAGE_IDENTITY, 0, 0, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_RANGE);
}

TEST_CASE(writing_a_read_only_page_is_refused)
{
    fresh();
    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = LINK_PAGE_IDENTITY;
    w.offset = 0; w.count = 1; w.regs[0] = 99;

    link_msg_t r;
    CHECK(ask(&w, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_READ_ONLY);
    CHECK_EQ(g.identity[0], LINK_PROTOCOL_MAJOR);
}

TEST_CASE(a_value_the_page_rejects_is_refused_with_a_reason)
{
    fresh();
    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = LINK_PAGE_CONTROL;
    w.offset = LINK_CT_THROTTLE; w.count = 1;
    w.regs[0] = (uint16_t)(LINK_THROTTLE_MAX + 1);

    link_msg_t r;
    CHECK(ask(&w, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0);
}

/*
 * A write is all or nothing: a frame whose later register is refused stores
 * none of its earlier ones.
 *
 * A frame carries up to four registers (LINK_CAN_REGS_PER_FRAME), so a
 * handler that validated and stored in one pass would answer NACK with the
 * registers ahead of the refusal already committed, and a read-back would
 * show a page nobody accepted.  The case above sets count = 1, the one width
 * where a single-pass handler cannot half-apply.
 *
 * These pin the rule on the two page handlers that live in shared/.  The
 * CONTROL page's handler is in firmware/iomcu/src/main.c, which this suite
 * does not compile, so no host test reaches it.
 */
TEST_CASE(a_refused_chan_cfg_write_stores_none_of_its_registers)
{
    uint16_t regs[LINK_CC_COUNT];
    outputs_chan_cfg_defaults(regs);
    /*
     * One channel, four registers.  The role and the slew differ from the
     * defaults so a half-applied write shows, and the minimum is 100 us,
     * below the LINK_CC_FLOOR_US of 500 us.
     */
    const uint16_t in[LINK_CC_STRIDE] = {
        [LINK_CC_ROLE]   = LINK_CC_ROLE_THROTTLE,
        [LINK_CC_SLEW]   = 250u,
        [LINK_CC_MIN_US] = 100u,
        [LINK_CC_MAX_US] = 2000u,
    };
    CHECK_EQ(outputs_chan_cfg_write(regs, 0, LINK_CC_STRIDE, in),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(regs[LINK_CC_ROLE], LINK_CC_ROLE_SURFACE);
    CHECK_EQ(regs[LINK_CC_SLEW], 0u);
    CHECK_EQ(regs[LINK_CC_MIN_US], LINK_CC_DEFAULT_MIN);
    CHECK_EQ(regs[LINK_CC_MAX_US], LINK_CC_DEFAULT_MAX);
}

TEST_CASE(a_refused_slots_write_stores_none_of_its_registers)
{
    uint16_t regs[LINK_OS_COUNT];
    outputs_slots_defaults(regs);
    /*
     * Four registers across a slot boundary: slot 0's rate, then slot 1's
     * driver, pin and range.  The pin is 64, one above OUT_MAX_PIN, so the
     * refusal falls on the third register of the four and two accepted ones
     * precede it.
     */
    const uint16_t in[4] = {
        50u,                            /* slot 0, LINK_OS_RATE_HZ */
        (uint16_t)LINK_DRIVER_PWM,      /* slot 1, LINK_OS_DRIVER  */
        (uint16_t)(OUT_MAX_PIN + 1u),   /* slot 1, LINK_OS_PIN     */
        LINK_OS_RANGE_OF(1, 1),         /* slot 1, LINK_OS_RANGE   */
    };
    CHECK_EQ(outputs_slots_write(regs, LINK_OS_RATE_HZ, 4, in),
             LINK_NACK_BAD_VALUE);
    for (unsigned i = 0; i < LINK_OS_COUNT; ++i) {
        CHECK_EQ(regs[i], 0u);
    }
}

/* DATA, ACK (acknowledge) and NACK (negative acknowledge) are the
 * coprocessor's own vocabulary.  Receiving one means something is talking
 * that should be listening. */
TEST_CASE(the_coprocessor_refuses_to_be_spoken_to_in_its_own_voice)
{
    fresh();
    const uint8_t ops[] = { LINK_OP_DATA, LINK_OP_ACK, LINK_OP_NACK };
    for (size_t i = 0; i < sizeof(ops); ++i) {
        link_msg_t m = { 0 };
        m.op = ops[i]; m.page = LINK_PAGE_CONTROL; m.offset = 0; m.count = 1;
        link_msg_t r;
        CHECK(ask(&m, &r));
        CHECK_EQ(r.op, LINK_OP_NACK);
    }
}

/* Every request produces a reply, refusals included: silence on this link
 * already means the coprocessor is not there. */
TEST_CASE(every_request_is_answered)
{
    fresh();
    const link_msg_t reqs[] = {
        read_req(LINK_PAGE_IDENTITY, 0, 1),
        read_req(0x7F, 0, 1),
        read_req(LINK_PAGE_IDENTITY, 0, 0),
        read_req(LINK_PAGE_CONTROL, LINK_CT_COUNT, 1),
    };
    for (size_t i = 0; i < sizeof(reqs) / sizeof(reqs[0]); ++i) {
        link_msg_t ignored;
        CHECK(link_dev_dispatch(&dev, &reqs[i], &ignored, 0));
    }
}

/*
 * The version the coprocessor publishes on its identity page.  check_docs.py
 * holds the numbers to the changelog; this holds the string to the numbers,
 * because they are two spellings of one fact and the string is the one a
 * person reads off a splash screen.
 */
TEST_CASE(the_version_string_says_what_the_numbers_say)
{
    char want[32];
    snprintf(want, sizeof(want), "%d.%d.%d", RCBENCH_VERSION_MAJOR,
             RCBENCH_VERSION_MINOR, RCBENCH_VERSION_PATCH);
    CHECK_STR_EQ(RCBENCH_VERSION_STRING, want);

    /* And it fits the registers it travels in. */
    CHECK(RCBENCH_VERSION_MAJOR >= 0 && RCBENCH_VERSION_MAJOR <= 0xFFFF);
    CHECK(RCBENCH_VERSION_MINOR >= 0 && RCBENCH_VERSION_MINOR <= 0xFFFF);
    CHECK(RCBENCH_VERSION_PATCH >= 0 && RCBENCH_VERSION_PATCH <= 0xFFFF);
}

int main(void)
{
    RUN(a_read_returns_the_registers);
    RUN(every_window_of_every_page_round_trips);
    RUN(a_write_is_acknowledged_and_takes_effect);
    RUN(an_unknown_page_is_refused_not_ignored);
    RUN(a_window_past_the_end_of_a_page_is_refused);
    RUN(writing_a_read_only_page_is_refused);
    RUN(a_value_the_page_rejects_is_refused_with_a_reason);
    RUN(a_refused_chan_cfg_write_stores_none_of_its_registers);
    RUN(a_refused_slots_write_stores_none_of_its_registers);
    RUN(the_coprocessor_refuses_to_be_spoken_to_in_its_own_voice);
    RUN(every_request_is_answered);
    RUN(the_version_string_says_what_the_numbers_say);
    return test_summary("link_pages");
}
