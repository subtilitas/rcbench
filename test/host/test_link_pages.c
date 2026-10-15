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

#include "link_bringup.h"
#include "link_control.h"
#include "link_dev.h"
#include "link_pages.h"
#include "outputs.h"
#include "outputs_pages.h"
#include "rcbench_version.h"
#include "sense_page.h"
#include "tone_page.h"

/* A stand-in for the coprocessor's own state. */
typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
    int      clears;
    bool     may_arm;   /* out of failsafe and the heartbeat trusted */
} fake_dev_t;

static fake_dev_t g;

/* The member of link_page_t is a non-const void *ctx; this page only reads
 * through it, and a const parameter would need a cast at the table entry. */
// cppcheck-suppress constParameterCallback
static void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const fake_dev_t *f = (const fake_dev_t *)ctx;
    memcpy(out, f->identity + off, (size_t)n * sizeof(uint16_t));
}

/* The member of link_page_t is a non-const void *ctx; this page only reads
 * through it, and a const parameter would need a cast at the table entry. */
// cppcheck-suppress constParameterCallback
static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const fake_dev_t *f = (const fake_dev_t *)ctx;
    memcpy(out, f->control + off, (size_t)n * sizeof(uint16_t));
}

/* The coprocessor's handler is this: the page's rules from shared/, and a
 * clear acted on when the rules say the frame carried one. */
static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    fake_dev_t *f = (fake_dev_t *)ctx;
    bool cleared = false;
    const uint8_t nack = link_control_write(f->control, off, n, in,
                                            f->may_arm, &cleared);
    if (nack == 0u && cleared) {
        ++f->clears;
    }
    return nack;
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
    g.may_arm = true;
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
    for (unsigned i = 0; i < LINK_ID_COUNT; ++i) {
        g.identity[i] = (uint16_t)(0xC000u + i);
    }
    for (unsigned off = 0; off < LINK_ID_COUNT; ++off) {
        for (unsigned n = 1; n <= LINK_ID_COUNT - off; ++n) {
            link_msg_t r;
            CHECK(ask_read(LINK_PAGE_IDENTITY, (uint8_t)off, (uint8_t)n, &r));
            CHECK_EQ(r.op, LINK_OP_DATA);
            CHECK_EQ(r.offset, off);
            CHECK_EQ(r.count, n);
            for (unsigned k = 0; k < n; ++k) {
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
 * These pin the rule on the two output page handlers; the CONTROL page's
 * handler is link_control_write(), which the arming-frame cases below hold
 * to it.
 */
TEST_CASE(a_refused_chan_cfg_write_stores_none_of_its_registers)
{
    uint16_t regs[LINK_CC_COUNT];
    outputs_chan_cfg_defaults(regs);
    /*
     * One channel, four registers.  The role and the slew differ from the
     * defaults so a half-applied write shows, and the minimum is 100 us,
     * below the LINK_CC_FLOOR_US of 400 us.
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

/* The slew has an upper bound of LINK_CC_SLEW_MAX; both edges and a value on
 * each side of the upper one. */
TEST_CASE(a_slew_above_the_maximum_is_refused_and_stores_nothing)
{
    const uint16_t ok[] = { 0u, 1u, LINK_CC_SLEW_MAX - 1u, LINK_CC_SLEW_MAX };
    const uint16_t bad[] = { LINK_CC_SLEW_MAX + 1u, 10000u, 65535u };
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); ++i) {
        uint16_t regs[LINK_CC_COUNT];
        outputs_chan_cfg_defaults(regs);
        const uint16_t v = ok[i];
        CHECK_EQ(outputs_chan_cfg_write(regs, LINK_CC_SLEW, 1, &v), 0u);
        CHECK_EQ(regs[LINK_CC_SLEW], v);
    }
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        uint16_t regs[LINK_CC_COUNT];
        outputs_chan_cfg_defaults(regs);
        const uint16_t v = bad[i];
        CHECK_EQ(outputs_chan_cfg_write(regs, LINK_CC_SLEW, 1, &v),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(regs[LINK_CC_SLEW], 0u);
    }
    /* The last channel's slew is judged as well. */
    uint16_t regs[LINK_CC_COUNT];
    outputs_chan_cfg_defaults(regs);
    const uint16_t v = LINK_CC_SLEW_MAX + 1u;
    CHECK_EQ(outputs_chan_cfg_write(regs, LINK_CC_COUNT - LINK_CC_STRIDE
                                    + LINK_CC_SLEW, 1, &v),
             LINK_NACK_BAD_VALUE);
}

/* A CHAN_CFG kept by a build before the slew limit: a slew above it starts
 * as the maximum, everything else as kept. */
TEST_CASE(a_kept_slew_above_the_maximum_starts_as_the_maximum)
{
    const uint16_t old[] = { LINK_CC_SLEW_MAX + 1u, 65535u };
    for (unsigned i = 0; i < sizeof(old) / sizeof(old[0]); ++i) {
        uint16_t saved[LINK_CC_COUNT];
        uint16_t regs[LINK_CC_COUNT];
        outputs_chan_cfg_defaults(saved);
        for (unsigned c = 0; c < LINK_OUT_CHANNELS; ++c) {
            uint16_t *r = &saved[c * LINK_CC_STRIDE];
            r[LINK_CC_ROLE]   = (uint16_t)((c == 0u) ? LINK_CC_ROLE_THROTTLE
                                                     : LINK_CC_ROLE_SURFACE);
            r[LINK_CC_SLEW]   = (c % 2u == 0u) ? old[i]
                                               : (uint16_t)(100u * c);
            r[LINK_CC_MIN_US] = (uint16_t)(900u + c);
            r[LINK_CC_MAX_US] = (uint16_t)(2100u - c);
        }
        saved[LINK_CC_COUNT - LINK_CC_STRIDE + LINK_CC_SLEW] = old[i];
        memset(regs, 0xA5, sizeof(regs));
        outputs_chan_cfg_restore(regs, saved);
        for (unsigned c = 0; c < LINK_OUT_CHANNELS; ++c) {
            const uint16_t *r = &regs[c * LINK_CC_STRIDE];
            const uint16_t *k = &saved[c * LINK_CC_STRIDE];
            CHECK_EQ(r[LINK_CC_ROLE], k[LINK_CC_ROLE]);
            CHECK_EQ(r[LINK_CC_MIN_US], k[LINK_CC_MIN_US]);
            CHECK_EQ(r[LINK_CC_MAX_US], k[LINK_CC_MAX_US]);
            if (k[LINK_CC_SLEW] > LINK_CC_SLEW_MAX) {
                CHECK_EQ(r[LINK_CC_SLEW], LINK_CC_SLEW_MAX);
            } else {
                CHECK_EQ(r[LINK_CC_SLEW], k[LINK_CC_SLEW]);
            }
        }
        CHECK_EQ(regs[LINK_CC_COUNT - LINK_CC_STRIDE + LINK_CC_SLEW],
                 LINK_CC_SLEW_MAX);
        /* What starts is a page a write takes whole. */
        uint16_t page[LINK_CC_COUNT];
        outputs_chan_cfg_defaults(page);
        CHECK_EQ(outputs_chan_cfg_write(page, 0u, LINK_CC_COUNT, regs), 0u);
    }
    /* The maximum itself is kept as it is. */
    uint16_t saved[LINK_CC_COUNT];
    uint16_t regs[LINK_CC_COUNT];
    outputs_chan_cfg_defaults(saved);
    saved[LINK_CC_SLEW] = LINK_CC_SLEW_MAX;
    outputs_chan_cfg_restore(regs, saved);
    CHECK_EQ(memcmp(regs, saved, sizeof(regs)), 0);
    /* Nothing without both. */
    const uint16_t before = regs[LINK_CC_SLEW];
    outputs_chan_cfg_restore(regs, NULL);
    outputs_chan_cfg_restore(NULL, saved);
    CHECK_EQ(regs[LINK_CC_SLEW], before);
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

/*
 * The frame that arms: ARM, THROTTLE and MOTOR_POLES from offset 0, three
 * registers in one CAN frame.  The count is the divisor the far end starts
 * sampling with, and a run begun on the wrong one puts a wrong speed into
 * its sticky rpm_max, so it travels in the same frame as the arm and lands
 * with it or not at all.
 */
TEST_CASE(the_arming_frame_applies_arm_throttle_and_poles_together)
{
    fresh();
    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = LINK_PAGE_CONTROL;
    w.offset = LINK_CT_ARM; w.count = 3;
    w.regs[0] = 1; w.regs[1] = 2500; w.regs[2] = 14;

    link_msg_t r;
    CHECK(ask(&w, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], 1);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 2500);
    CHECK_EQ(g.control[LINK_CT_MOTOR_POLES], 14);
    CHECK_EQ(g.clears, 0);
}

/* An odd count is a typo, and a frame carrying one arms nothing: ARM is
 * checked after the count in register order, and a single pass that stored
 * as it went would leave the bench armed on a refused write. */
TEST_CASE(an_arming_frame_with_a_bad_pole_count_arms_nothing)
{
    fresh();
    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = LINK_PAGE_CONTROL;
    w.offset = LINK_CT_ARM; w.count = 3;
    w.regs[0] = 1; w.regs[1] = 2500; w.regs[2] = 7;

    link_msg_t r;
    CHECK(ask(&w, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(g.control[LINK_CT_ARM], 0);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0);
    CHECK_EQ(g.control[LINK_CT_MOTOR_POLES], 0);
    CHECK_EQ(g.clears, 0);
}

static bool write_control(uint8_t off, uint8_t count, const uint16_t *regs,
                          link_msg_t *reply)
{
    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = LINK_PAGE_CONTROL;
    w.offset = off; w.count = count;
    memcpy(w.regs, regs, (size_t)count * sizeof(uint16_t));
    return ask(&w, reply);
}

/* Whether the bench may arm is the coprocessor's answer, not the panel's:
 * while the link is in failsafe or the heartbeat is not trusted, ARM is
 * refused with its own reason, and nothing in the frame beside it lands. */
TEST_CASE(arm_is_refused_with_not_armed_while_the_bench_may_not_arm)
{
    fresh();
    g.may_arm = false;
    const uint16_t frame[LINK_CT_ARM_FRAME] = { 1, 2500, 14 };
    link_msg_t r;
    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, frame, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_NOT_ARMED);
    CHECK_EQ(g.control[LINK_CT_ARM], 0);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0);
    CHECK_EQ(g.control[LINK_CT_MOTOR_POLES], 0);

    /* ARM = 0 is a disarm and is never refused. */
    const uint16_t disarm[2] = { 0, 0 };
    CHECK(write_control(LINK_CT_ARM, 2, disarm, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
}

/* CLEAR is an action: the magic runs it and is not stored, anything else is
 * refused, and a refused clear clears nothing. */
TEST_CASE(clear_takes_the_magic_and_nothing_else)
{
    fresh();
    link_msg_t r;
    const uint16_t wrong = (uint16_t)(LINK_CLEAR_MAGIC + 1u);
    CHECK(write_control(LINK_CT_CLEAR, 1, &wrong, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(g.clears, 0);

    const uint16_t magic = LINK_CLEAR_MAGIC;
    CHECK(write_control(LINK_CT_CLEAR, 1, &magic, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.clears, 1);
    CHECK_EQ(g.control[LINK_CT_CLEAR], 0);
}

/* Zero, and every even count from LINK_POLES_MIN to LINK_POLES_MAX; the
 * odd ones and the ones past either end are refused. */
TEST_CASE(a_pole_count_is_zero_or_even_and_within_range)
{
    fresh();
    link_msg_t r;
    const uint16_t ok[] = { 0, LINK_POLES_MIN, 14, LINK_POLES_MAX };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); ++i) {
        CHECK(write_control(LINK_CT_MOTOR_POLES, 1, &ok[i], &r));
        CHECK_EQ(r.op, LINK_OP_ACK);
        CHECK_EQ(g.control[LINK_CT_MOTOR_POLES], ok[i]);
    }
    const uint16_t bad[] = { 1, 15, (uint16_t)(LINK_POLES_MAX + 2u) };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(write_control(LINK_CT_MOTOR_POLES, 1, &bad[i], &r));
        CHECK_EQ(r.op, LINK_OP_NACK);
        CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
        CHECK_EQ(g.control[LINK_CT_MOTOR_POLES], LINK_POLES_MAX);
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

/*
 * Protocol 4.7 adds two pages and two BENCH flags and moves nothing: every
 * page number an earlier minor had is where it was, and the new ones fit a
 * page.  A renumbering here would be a major.
 */
TEST_CASE(the_sense_pages_extend_the_map_without_moving_it)
{
    CHECK_EQ(LINK_PROTOCOL_MAJOR, 4u);
    CHECK(LINK_PROTOCOL_MINOR >= 7u);
    CHECK_EQ(LINK_PAGE_BENCH, 0x20);
    CHECK_EQ(LINK_PAGE_SERVO, 0x29);
    CHECK_EQ(LINK_PAGE_SUPPLY, 0x2A);
    CHECK_EQ(LINK_PAGE_SENSE, 0x2B);
    CHECK_EQ(LINK_PAGE_SERVO_SENSE, 0x2C);
    CHECK(LINK_SN_COUNT <= LINK_MAX_REGS);
    CHECK(LINK_SS_COUNT <= LINK_MAX_REGS);
    CHECK_EQ(LINK_BN_COUNT, 13);

    /* The set-up is three frames of four, each a whole part, so a frame
     * that lands lands whole (docs/Link.md, Frames). */
    CHECK_EQ(LINK_SN_ENABLE, 0);
    CHECK_EQ(LINK_SN_I228_ADDR, 4);
    CHECK_EQ(LINK_SN_I3221_ADDR, 8);
    CHECK_EQ(LINK_SN_CONFIG_COUNT, 12u);
    CHECK_EQ(LINK_SN_FLAGS, (int)LINK_SN_CONFIG_COUNT);
    CHECK_EQ(LINK_SS_CAP_STATE - LINK_SS_CAP_ARM, (int)LINK_SS_CAP_FRAME);
    CHECK_EQ(LINK_SS_CAP_FRAME, 4u);
}

/*
 * Protocol 4.8 adds the TONE page and moves nothing.  The set-up is two
 * frames of four, the read-only block starts on a frame, and the page fits.
 */
TEST_CASE(the_tone_page_extends_the_map_without_moving_it)
{
    CHECK_EQ(LINK_PROTOCOL_MAJOR, 4u);
    CHECK(LINK_PROTOCOL_MINOR >= 8u);
    CHECK_EQ(LINK_PAGE_SERVO_SENSE, 0x2C);
    CHECK_EQ(LINK_PAGE_TONE, 0x2D);
    CHECK(LINK_TN_COUNT <= LINK_MAX_REGS);
    CHECK_EQ(LINK_TN_COUNT, 24);
    CHECK_EQ(LINK_TN_ENABLE, 0);
    CHECK_EQ(LINK_TN_SPLIT_PCT, 4);
    CHECK_EQ(LINK_TN_RESERVED_7, 7);
    CHECK_EQ(LINK_TN_CONFIG_COUNT, 7u);
    CHECK_EQ(LINK_TN_FLAGS, 8);
    CHECK_EQ(LINK_TN_EVT_SEL, 13);
    CHECK_EQ(LINK_TN_EVT_START_HI - LINK_TN_EVT_START_LO, 1);
    /* The 20 Hz read is registers 8 to 23: four frames of four. */
    CHECK_EQ(LINK_TN_COUNT - LINK_TN_FLAGS, 16);
    CHECK_EQ(LINK_TN_RING, 64u);
}

/*
 * Protocol 4.9 adds the output encoder to SENSE and moves nothing: the
 * set-up frames are where they were, ENABLE gains bit 2, and the registers
 * after ESC_FLAGS are new.  The 4.8 page's 26 registers read the same.
 */
TEST_CASE(the_encoder_extends_the_sense_page_without_moving_it)
{
    CHECK_EQ(LINK_PROTOCOL_MAJOR, 4u);
    CHECK(LINK_PROTOCOL_MINOR >= 9u);
    CHECK_EQ(LINK_SN_ENABLE, 0);
    CHECK_EQ(LINK_SN_I228_ADDR, 4);
    CHECK_EQ(LINK_SN_I3221_ADDR, 8);
    CHECK_EQ(LINK_SN_CONFIG_COUNT, 12u);
    CHECK_EQ(LINK_SN_FLAGS, 12);
    CHECK_EQ(LINK_SN_ESC_VOLTAGE_CV, 23);
    CHECK_EQ(LINK_SN_ESC_FLAGS, 25);
    CHECK_EQ(LINK_SN_COUNT_V48, (unsigned)LINK_SN_ESC_FLAGS + 1u);
    CHECK_EQ(LINK_SN_AS5600_FLAGS, (int)LINK_SN_COUNT_V48);
    CHECK_EQ(LINK_SN_COUNT, LINK_MAX_REGS);
    CHECK_EQ(LINK_SN_EN_I228, 1u);
    CHECK_EQ(LINK_SN_EN_I3221, 2u);
    CHECK_EQ(LINK_SN_EN_AS5600, 4u);
    CHECK_EQ(LINK_SN_EN_ALL, 7u);
}

/*
 * Protocol 4.11 adds the SERVO_WIN page after the BIND pages, gives SENSE's
 * reserved register 31 a meaning and makes CAP_HOLD_MA signed.  Nothing
 * moves: every page number an earlier minor had is where it was, and SENSE
 * and SERVO_SENSE keep their sizes.
 */
TEST_CASE(servo_win_extends_the_map_without_moving_it)
{
    CHECK_EQ(LINK_PROTOCOL_MAJOR, 4u);
    CHECK(LINK_PROTOCOL_MINOR >= 11u);
    CHECK_EQ(LINK_MINOR_SERVO_WIN, 11u);
    CHECK_EQ(LINK_MINOR_BIND, 10u);
    CHECK_EQ(LINK_PAGE_SENSE, 0x2B);
    CHECK_EQ(LINK_PAGE_SERVO_SENSE, 0x2C);
    CHECK_EQ(LINK_PAGE_TONE, 0x2D);
    CHECK_EQ(LINK_PAGE_BIND_CFG, 0x2E);
    CHECK_EQ(LINK_PAGE_BIND_OUT, 0x2F);
    CHECK_EQ(LINK_PAGE_BIND, 0x30);
    CHECK_EQ(LINK_PAGE_SERVO_WIN, 0x31);
    CHECK_EQ(LINK_SN_COUNT, 32);
    CHECK_EQ(LINK_SN_RESETS, 31);
    CHECK_EQ(LINK_SS_COUNT, 25);
    CHECK_EQ(LINK_SS_CAP_HOLD_MA, 15);
    CHECK_EQ(LINK_SW_COUNT, 28);
    CHECK(LINK_SW_COUNT <= LINK_MAX_REGS);
    /* The header and the two newest windows are 16 registers: the read a
     * poll makes, four data frames as the BENCH read. */
    CHECK_EQ(LINK_SW_ENTRIES + 2 * LINK_SW_E_STRIDE, 16);
}

/* Two flags join BENCH at the bits that were free, and none of the old
 * ones changes: a 4.6 panel reads the same voltage, current and validity
 * from a 4.7 coprocessor and ignores what it does not know. */
TEST_CASE(the_bench_flags_add_bits_5_and_6_and_move_none)
{
    CHECK_EQ(LINK_BN_VOLTAGE_OK, 0x01u);
    CHECK_EQ(LINK_BN_CURRENT_OK, 0x02u);
    CHECK_EQ(LINK_BN_RPM_OK, 0x04u);
    CHECK_EQ(LINK_BN_TEMP_OK, 0x08u);
    CHECK_EQ(LINK_BN_TEMP_MOT_OK, 0x10u);
    CHECK_EQ(LINK_BN_SENSED, 0x20u);
    CHECK_EQ(LINK_BN_TOTALS_OK, 0x40u);
    CHECK_EQ(LINK_BN_SIMULATED, 0x80u);
    CHECK_EQ(LINK_BN_CHARGE_MAH, 6);
    CHECK_EQ(LINK_BN_ENERGY_DWH, 7);
}

/* The coprocessor's SENSE handler is the page's rules and nothing more. */
static sense_page_t s_sense;
static outputs_t    s_out;

static void sense_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_page_read(&s_sense, off, n, out);
}

static uint8_t sense_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    (void)ctx;
    return sense_page_write(&s_sense, off, n, in, &s_out, 0u);
}

static void servo_sense_read(void *ctx, uint8_t off, uint8_t n,
                             uint16_t *out)
{
    (void)ctx;
    sense_servo_read(&s_sense, off, n, out);
}

static uint8_t servo_sense_write(void *ctx, uint8_t off, uint8_t n,
                                 const uint16_t *in)
{
    (void)ctx;
    return sense_servo_write(&s_sense, off, n, in, &s_out);
}

static const link_page_t k_pages_47[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
    { LINK_PAGE_SENSE,    LINK_SN_COUNT, sense_read,    sense_write },
    { LINK_PAGE_SERVO_SENSE, LINK_SS_COUNT, servo_sense_read,
      servo_sense_write },
};

static void fresh_47(void)
{
    fresh();
    g.identity[LINK_ID_PROTOCOL_MINOR] = 7u;   /* the pages below are 4.7's */
    outputs_init(&s_out, 0u);
    sense_page_init(&s_sense);
    link_dev_init(&dev, k_pages_47, 4, &g, 0);
}

static bool write_page(uint8_t page, uint8_t off, uint8_t count,
                       const uint16_t *regs, link_msg_t *reply)
{
    link_msg_t w = { 0 };
    w.op = LINK_OP_WRITE; w.page = page;
    w.offset = off; w.count = count;
    memcpy(w.regs, regs, (size_t)count * sizeof(uint16_t));
    return ask(&w, reply);
}

TEST_CASE(the_sense_pages_are_served_and_refuse_whole)
{
    fresh_47();
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_SENSE, 0, LINK_SN_COUNT, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    CHECK_EQ(r.count, LINK_SN_COUNT);
    CHECK_EQ(r.regs[LINK_SN_SDA_PIN], 16u);
    CHECK_EQ(r.regs[LINK_SN_FLAGS], 0u);
    CHECK(ask_read(LINK_PAGE_SERVO_SENSE, 0, LINK_SS_COUNT, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);

    /* The bus enabled on GP16/GP17 in one frame. */
    const uint16_t on[4] = { LINK_SN_EN_I228, 16u, 17u, 400u };
    CHECK(write_page(LINK_PAGE_SENSE, LINK_SN_ENABLE, 4, on, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(s_sense.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);

    /* A frame refused in its last register keeps none of it. */
    const uint16_t bad[4] = { 0x44u, 300u, 1000u, 1u };
    CHECK(write_page(LINK_PAGE_SENSE, LINK_SN_I228_ADDR, 4, bad, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(s_sense.sense[LINK_SN_I228_ADDR], 0x45u);
    CHECK_EQ(s_sense.sense[LINK_SN_I228_SHUNT_UOHM], 200u);

    /* Read-only registers answer READ_ONLY through the dispatcher too. */
    const uint16_t one = 1u;
    CHECK(write_page(LINK_PAGE_SENSE, LINK_SN_FLAGS, 1, &one, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_READ_ONLY);

    /* And a capture on a bank that is not armed, NOT_ARMED -- after the
     * INA3221 is enabled and a surface is on a PWM slot. */
    const uint16_t both[4] = { LINK_SN_EN_I228 | LINK_SN_EN_I3221, 16u, 17u,
                               400u };
    CHECK(write_page(LINK_PAGE_SENSE, LINK_SN_ENABLE, 4, both, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&s_out, 0, &pwm));
    CHECK(outputs_set_role(&s_out, 0, OUT_ROLE_SURFACE));
    sense_page_bound(&s_sense, 0x01u);       /* the silicon bound it */
    const uint16_t cap[4] = { LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u };
    CHECK(write_page(LINK_PAGE_SERVO_SENSE, LINK_SS_CAP_ARM, 4, cap, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_NOT_ARMED);
    outputs_arm(&s_out, true, 0u);
    CHECK(write_page(LINK_PAGE_SERVO_SENSE, LINK_SS_CAP_ARM, 4, cap, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(s_sense.servo[LINK_SS_CAP_STATE], (uint16_t)LINK_CAP_ARMED);
}

/*
 * A 4.7 panel and a 4.6 coprocessor.  The link comes up and the bench arms
 * on the major alone; the 4.6 coprocessor has no SENSE page and says so
 * with BAD_PAGE, which is why the panel sends nothing there below minor 7.
 */
TEST_CASE(a_4_6_coprocessor_links_and_arms_without_the_sense_pages)
{
    fresh();
    g.identity[LINK_ID_PROTOCOL_MINOR] = 6u;
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_IDENTITY, 0, LINK_ID_COUNT, &r));
    link_bringup_t b;
    memset(&b, 0, sizeof(b));
    b.polls = 100u;
    b.replies = 100u;
    b.have_identity = true;
    b.proto_major = r.regs[LINK_ID_PROTOCOL_MAJOR];
    b.proto_minor = r.regs[LINK_ID_PROTOCOL_MINOR];
    CHECK_EQ(link_bringup_diagnose(&b), LINK_DIAG_OK);
    CHECK(b.proto_minor < 7u);

    const uint16_t frame[LINK_CT_ARM_FRAME] = { 1, 0, 14 };
    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, frame, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], 1u);

    CHECK(ask_read(LINK_PAGE_SENSE, 0, 1, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
    CHECK(ask_read(LINK_PAGE_SERVO_SENSE, 0, 1, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
}

/*
 * A 4.6 panel and a 4.7 coprocessor.  The panel never writes SENSE; the
 * coprocessor's identity and CONTROL answer as before, and the bench arms.
 */
TEST_CASE(a_4_6_panel_links_and_arms_on_a_4_7_coprocessor)
{
    fresh_47();
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_IDENTITY, 0, LINK_ID_COUNT, &r));
    CHECK_EQ(r.regs[LINK_ID_PROTOCOL_MAJOR], 4u);
    CHECK_EQ(r.regs[LINK_ID_PROTOCOL_MINOR], 7u);
    const uint16_t frame[LINK_CT_ARM_FRAME] = { 1, 2500, 14 };
    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, frame, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], 1u);
}

/* The coprocessor's TONE handler is the page's rules and nothing more. */
static tone_page_t s_tone;

static void servo_win_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_win_read(&s_sense, off, n, out);
}

/* A 4.11 coprocessor: the 4.7 pages and SERVO_WIN, read only. */
static const link_page_t k_pages_411[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
    { LINK_PAGE_SENSE,    LINK_SN_COUNT, sense_read,    sense_write },
    { LINK_PAGE_SERVO_SENSE, LINK_SS_COUNT, servo_sense_read,
      servo_sense_write },
    { LINK_PAGE_SERVO_WIN, LINK_SW_COUNT, servo_win_read, NULL },
};

TEST_CASE(servo_win_is_served_read_only)
{
    fresh();
    outputs_init(&s_out, 0u);
    sense_page_init(&s_sense);
    link_dev_init(&dev, k_pages_411, 5, &g, 0);
    /* A window published, as core 1's snapshot does. */
    sense_snap_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.have_win = true;
    snap.win[SENSE_SRC_CH1].number = 41u;
    snap.ring[0] = (sense_ring_win_t){ .closed = true, .n_i = 50u,
        .n_clip = 2u, .clip_hi = true, .i_mean_ua = 345000,
        .i_min_ua = -2000, .i_max_ua = 1638000 };
    sense_page_publish(&s_sense, &snap, 0u);

    link_msg_t r;
    /* The poll's read, 16 registers, and the whole page. */
    CHECK(ask_read(LINK_PAGE_SERVO_WIN, 0, 16, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    CHECK_EQ(r.count, 16);
    CHECK_EQ(r.regs[LINK_SW_WINDOW], 41u);
    CHECK_EQ(r.regs[LINK_SW_FLAGS], LINK_SW_HAVE);
    CHECK_EQ(r.regs[LINK_SW_CAP_STATE], (uint16_t)LINK_CAP_IDLE);
    CHECK_EQ(r.regs[LINK_SW_ENTRY(0u, LINK_SW_E_MEAN_MA)], 345u);
    CHECK_EQ(r.regs[LINK_SW_ENTRY(0u, LINK_SW_E_MAX_MA)], 1638u);
    CHECK_EQ((int16_t)r.regs[LINK_SW_ENTRY(0u, LINK_SW_E_MIN_MA)], -2);
    CHECK_EQ(r.regs[LINK_SW_ENTRY(0u, LINK_SW_E_FLAGS)],
             (uint16_t)(LINK_SW_E_CLOSED | LINK_SW_E_CURRENT
                        | LINK_SW_E_CLIP_HI | 2u));
    CHECK_EQ(r.regs[LINK_SW_ENTRY(1u, LINK_SW_E_FLAGS)], 0u);
    CHECK(ask_read(LINK_PAGE_SERVO_WIN, 0, LINK_SW_COUNT, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    CHECK_EQ(r.count, LINK_SW_COUNT);
    /* Past the page's end. */
    CHECK(ask_read(LINK_PAGE_SERVO_WIN, 0, LINK_SW_COUNT + 1, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_RANGE);

    /* Every write is refused, header or entry, and changes nothing. */
    const uint16_t v[4] = { 1u, 2u, 3u, 4u };
    CHECK(write_page(LINK_PAGE_SERVO_WIN, LINK_SW_WINDOW, 4, v, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_READ_ONLY);
    CHECK(write_page(LINK_PAGE_SERVO_WIN, LINK_SW_ENTRIES, 1, v, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_READ_ONLY);
    CHECK(ask_read(LINK_PAGE_SERVO_WIN, 0, 4, &r));
    CHECK_EQ(r.regs[LINK_SW_WINDOW], 41u);
}

/*
 * A 4.11 panel and a 4.10 or older coprocessor.  The coprocessor has no
 * SERVO_WIN page and answers BAD_PAGE, which is why a panel sends nothing
 * there below minor 11; SENSE's register 31 reads 0, and SERVO_SENSE is
 * where it was.
 */
TEST_CASE(an_older_coprocessor_has_no_servo_win_page_and_says_so)
{
    fresh_47();
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_SERVO_WIN, 0, 16, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
    CHECK(ask_read(LINK_PAGE_SERVO_SENSE, 0, LINK_SS_COUNT, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    CHECK(ask_read(LINK_PAGE_SENSE, LINK_SN_RESETS, 1, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    CHECK_EQ(r.regs[0], 0u);
}

/*
 * Protocol 4.12 adds the KST page after SERVO_WIN and moves nothing.  The
 * page is 32 registers to read and 28 to write, both inside one message,
 * and its write map is windows of whole frames: the command, the image,
 * the unlock set, and the frame that ends with the supply of an entry,
 * which 4.13 adds.
 */
TEST_CASE(kst_extends_the_map_without_moving_it)
{
    CHECK_EQ(LINK_PROTOCOL_MAJOR, 4u);
    CHECK(LINK_PROTOCOL_MINOR >= 13u);
    CHECK_EQ(LINK_MINOR_KST, 12u);
    CHECK_EQ(LINK_MINOR_SERVO_WIN, 11u);
    CHECK_EQ(LINK_PAGE_SERVO_WIN, 0x31);
    CHECK_EQ(LINK_PAGE_KST, 0x32);
    CHECK_EQ(LINK_SW_COUNT, 28);
    CHECK_EQ(LINK_KS_COUNT, 32);
    CHECK(LINK_KS_COUNT <= LINK_MAX_REGS);
    CHECK((int)LINK_KS_W_COUNT <= (int)LINK_KS_COUNT);
    /* The status a poll reads is one frame, the results three, the image
     * four: 16 registers hold the 32 servo registers. */
    CHECK_EQ(LINK_KS_FAIL, 4);
    CHECK_EQ(LINK_KS_IMAGE, 16);
    CHECK_EQ(LINK_KS_IMAGE + LINK_KS_IMAGE_REGS, LINK_KS_COUNT);
    CHECK_EQ(LINK_KS_W_CMD_FRAME, 4u);
    CHECK_EQ(LINK_KS_W_IMAGE, 4);
    CHECK_EQ(LINK_KS_W_UNLOCK, 20);
    CHECK_EQ(LINK_KS_W_START_CRC, 24);
    CHECK_EQ(LINK_KS_W_VIEW, 25);
    CHECK_EQ(LINK_KS_W_SUPPLY_MV, 26);
    CHECK_EQ(LINK_KS_W_SUPPLY_MA, 27);
    CHECK_EQ(LINK_KS_W_LEAVE_MS, 28);
    CHECK_EQ(LINK_KS_W_COUNT, 29);
    /* The state, its flags and the port share register 0 and do not
     * overlap. */
    const uint16_t flags = LINK_KS_F_BUSY | LINK_KS_F_IN_MODE
                           | LINK_KS_F_MUST_READ | LINK_KS_F_IMAGE
                           | LINK_KS_F_BACKUP | LINK_KS_F_FP_OK
                           | LINK_KS_F_LOCKED | LINK_KS_F_RAIL
                           | LINK_KS_F_VIEW_BACKUP;
    CHECK_EQ(flags, 0x0FF8u);
    CHECK_EQ(LINK_KS_STATE_OF(0xA7FDu), 5u);
    CHECK_EQ(LINK_KS_PORT_OF(0xA7FDu), 10u);
    CHECK(LINK_KST_RAIL_WAIT < 8);
    CHECK_EQ(LINK_KST_LEAVING, 6);
    CHECK_EQ(LINK_KS_STATE_OF(0xA7FEu), (unsigned)LINK_KST_LEAVING);
    CHECK_EQ(LINK_KS_OP_OF(0xA000u), (unsigned)LINK_KST_OP_LEAVE);
    CHECK_EQ(LINK_KS_W_CMD_OF(LINK_KST_OP_LEAVE, 2u), 0x024Au);
    CHECK_EQ(LINK_KS_OP_OF(0x8FFFu), (unsigned)LINK_KST_OP_POWER_CYCLED);
    CHECK_EQ(LINK_KS_OP_OF(0x9000u), (unsigned)LINK_KST_OP_REBOOT);
    CHECK_EQ(LINK_KS_FRAMES_OF(0x8FFFu), 4095u);
    CHECK(LINK_KST_OP_COUNT <= 16);
    /* A command names its port and says that it does: the word a 4.12
     * panel sends, an operation and an output channel, never carries the
     * marker. */
    CHECK_EQ(LINK_KS_W_CMD_PORT, 0x0040u);
    CHECK_EQ(LINK_KS_W_CMD_OF(LINK_KST_OP_ENTER, 0u), 0x0041u);
    CHECK_EQ(LINK_KS_W_CMD_OF(LINK_KST_OP_REBOOT, 3u), 0x0349u);
    CHECK_EQ(LINK_KS_W_CMD_PORT & LINK_KS_W_CMD_CONFIRM, 0u);
    for (unsigned ch = 0; ch < 16u; ch++) {
        CHECK_EQ((LINK_KST_OP_ENTER | (ch << 8)) & LINK_KS_W_CMD_PORT, 0u);
    }
    /* The refusals and the result 4.13 adds keep the earlier values. */
    CHECK_EQ(LINK_KST_REF_PORT, 2);
    CHECK_EQ(LINK_KST_REF_NO_PIN, 16);
    CHECK_EQ(LINK_KST_REF_SUPPLY_RANGE, 17);
    CHECK_EQ(LINK_KST_REF_NO_SUPPLY, 18);
    CHECK_EQ(LINK_KST_RESULT_SUPPLY, 32);
    /* 4.14 adds results and moves no register: a write's planning runs
     * after the command's frame.  The refusal 4.13 gave keeps its number. */
    CHECK(LINK_PROTOCOL_MINOR >= 14u);
    CHECK_EQ(LINK_MINOR_KST_PLAN, 14u);
    CHECK_EQ(LINK_KST_RESULT_NO_PATH, 33);
    CHECK_EQ(LINK_KST_RESULT_TOO_COMPLEX, 34);
    CHECK_EQ(LINK_KST_RESULT_PLANNING, 35);
    CHECK_EQ(LINK_KST_REF_PLAN_RULES, 12);
    CHECK_EQ(LINK_KST_REF_PLAN_R00, 13);
    CHECK_EQ(LINK_KST_REF_PLAN_PAIRING, 14);
    CHECK_EQ(LINK_KST_REF_PLAN_NO_PATH, 15);
    /* 4.15 adds an operation, a state, a write register and two refusals
     * after the ones there were: the way from PROGRAMMING to PWM without a
     * supply cycle. */
    CHECK(LINK_PROTOCOL_MINOR >= 15u);
    CHECK_EQ(LINK_MINOR_KST_LEAVE, 15u);
    CHECK_EQ(LINK_KST_OP_LEAVE, 10);
    CHECK_EQ(LINK_KST_OP_COUNT, 11);
    CHECK_EQ(LINK_KST_REF_LEAVE_MS, 19);
    CHECK_EQ(LINK_KST_REF_LOCKED, 20);
    CHECK_EQ(LINK_KST_REF_SUPPLY_LOW, 21);
    CHECK_EQ(LINK_KST_LEAVE_MS_MAX, 10000u);
    CHECK_EQ(LINK_KST_LEAVE_MS_DEFAULT, 100u);
    CHECK(LINK_KST_LEAVE_MS_DEFAULT <= LINK_KST_LEAVE_MS_MAX);
    /* The key a write carries is neither of the values a cleared or a
     * stuck register reads. */
    CHECK(LINK_KST_WRITE_KEY != 0u);
    CHECK(LINK_KST_WRITE_KEY != 0xFFFFu);
    CHECK(LINK_KST_WRITE_KEY != 1u);
}

/*
 * Protocol 4.13 adds the PROG page after KST and moves nothing: three
 * registers for the bank of ports and five for each of four ports, the
 * model's maximum whatever the build has.  SUPPLY and SERVO_SENSE each
 * take one bit that was reserved.
 */
TEST_CASE(prog_extends_the_map_without_moving_it)
{
    CHECK(LINK_PROTOCOL_MINOR >= 13u);
    CHECK_EQ(LINK_MINOR_PROG, 13u);
    CHECK_EQ(LINK_PAGE_KST, 0x32);
    CHECK_EQ(LINK_PAGE_PROG, 0x33);
    CHECK_EQ(LINK_PG_COUNT, 0);
    CHECK_EQ(LINK_PG_FLAGS, 1);
    CHECK_EQ(LINK_PG_PWM, 2);
    CHECK_EQ(LINK_PG_TARGET, 3);
    CHECK_EQ(LINK_PG_PORT0, 4);
    CHECK_EQ(LINK_PG_STRIDE, 5);
    CHECK_EQ(LINK_PG_PORTS_MAX, 4u);
    CHECK_EQ(LINK_PG_REGS, LINK_PG_PORT0 + LINK_PG_PORTS_MAX * LINK_PG_STRIDE);
    CHECK_EQ(LINK_PG_REGS, 24);
    CHECK(LINK_PG_REGS <= LINK_MAX_REGS);
    CHECK_EQ(LINK_PG_REG(0u, LINK_PG_PIN), 4u);
    CHECK_EQ(LINK_PG_REG(3u, LINK_PG_MAX_US), 23u);
    CHECK_EQ(LINK_PG_PIN_NONE, 0xFFFFu);
    /* A bound bit and a refused bit for each port, none shared. */
    uint16_t seen = 0;
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; port++) {
        CHECK_EQ(seen & LINK_PG_F_BOUND(port), 0u);
        seen |= LINK_PG_F_BOUND(port);
        CHECK_EQ(seen & LINK_PG_F_REFUSED(port), 0u);
        seen |= LINK_PG_F_REFUSED(port);
    }
    CHECK_EQ(seen, 0x0F0Fu);
    /* The supply's hold and the capture's port bit. */
    CHECK_EQ(LINK_SP_PORT, 0x800u);
    CHECK_EQ(LINK_SS_ARM_PORT, 0x0800u);
    CHECK_EQ(LINK_SS_ARM_BITS, 0x0F83u);
    CHECK_EQ(LINK_SS_ARM_OUT(LINK_SS_ARM_PORT | LINK_SS_ARM_OF(1u, 3u)), 3u);
}

/* The check over the staged registers: a changed register, a swapped pair
 * and a shorter run each give another value. */
TEST_CASE(the_kst_staged_check_tells_a_change_a_swap_and_a_length)
{
    uint16_t v[LINK_KS_W_STAGED];
    for (unsigned i = 0; i < LINK_KS_W_STAGED; i++) {
        v[i] = (uint16_t)(0x1100u + i);
    }
    const uint16_t whole = link_kst_crc(v, LINK_KS_W_STAGED);
    CHECK_EQ(link_kst_crc(v, LINK_KS_W_STAGED), whole);
    CHECK(link_kst_crc(v, LINK_KS_W_STAGED - 1u) != whole);
    for (unsigned i = 0; i < LINK_KS_W_STAGED; i++) {
        for (unsigned bit = 0; bit < 16u; bit++) {
            v[i] ^= (uint16_t)(1u << bit);
            CHECK(link_kst_crc(v, LINK_KS_W_STAGED) != whole);
            v[i] ^= (uint16_t)(1u << bit);
        }
    }
    const uint16_t t = v[3];
    v[3] = v[4];
    v[4] = t;
    CHECK(link_kst_crc(v, LINK_KS_W_STAGED) != whole);
}

/*
 * A panel of 4.12 or later and a 4.11 coprocessor.  The coprocessor has no KST page and
 * answers BAD_PAGE to a read and to a command, which is why a panel sends
 * nothing there below minor 12; SERVO_WIN is where it was.
 */
TEST_CASE(a_4_11_coprocessor_has_no_kst_page_and_says_so)
{
    fresh();
    outputs_init(&s_out, 0u);
    sense_page_init(&s_sense);
    link_dev_init(&dev, k_pages_411, 5, &g, 0);
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_KST, 0, LINK_KS_W_CMD_FRAME, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
    const uint16_t cmd[LINK_KS_W_CMD_FRAME] = {
        LINK_KS_W_CMD_OF(LINK_KST_OP_ENTER, 0u), 1u, 0u, 0u };
    CHECK(write_page(LINK_PAGE_KST, LINK_KS_W_CMD, LINK_KS_W_CMD_FRAME, cmd,
                     &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
    CHECK(ask_read(LINK_PAGE_SERVO_WIN, 0, LINK_SW_COUNT, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    /* Nor a PROG page, as every coprocessor before 4.13. */
    CHECK(ask_read(LINK_PAGE_PROG, 0, LINK_PG_PORT0, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
    const uint16_t pwm = 1u;
    CHECK(write_page(LINK_PAGE_PROG, LINK_PG_PWM, 1, &pwm, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
}

static void tone_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    tone_page_read(&s_tone, off, n, out);
}

static uint8_t tone_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    (void)ctx;
    return tone_page_write(&s_tone, off, n, in, &s_out, 0u);
}

static const link_page_t k_pages_48[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
    { LINK_PAGE_SENSE,    LINK_SN_COUNT, sense_read,    sense_write },
    { LINK_PAGE_TONE,     LINK_TN_COUNT, tone_read,     tone_write },
};

static void fresh_48(void)
{
    fresh();
    outputs_init(&s_out, 0u);
    sense_page_init(&s_sense);
    tone_page_init(&s_tone);
    link_dev_init(&dev, k_pages_48, 4, &g, 0);
}

TEST_CASE(the_tone_page_is_served_in_frames_and_refuses_whole)
{
    fresh_48();
    link_msg_t r;
    /* The 20 Hz read: registers 8 to 23, 16 of them, four frames. */
    CHECK(ask_read(LINK_PAGE_TONE, LINK_TN_FLAGS,
                   LINK_TN_COUNT - LINK_TN_FLAGS, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
    CHECK_EQ(r.count, LINK_TN_COUNT - LINK_TN_FLAGS);
    CHECK(ask_read(LINK_PAGE_TONE, 0, LINK_TN_COUNT, &r));
    CHECK_EQ(r.regs[LINK_TN_PIN], 22u);
    CHECK_EQ(r.regs[LINK_TN_F_MAX_HZ], 6500u);

    /* The set-up in its two frames. */
    const uint16_t a[4] = { LINK_TN_EN_TAP, 22u, 500u, 5000u };
    const uint16_t b[4] = { 10u, 5u, 4u, 0u };
    CHECK(write_page(LINK_PAGE_TONE, LINK_TN_ENABLE, 4, a, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK(write_page(LINK_PAGE_TONE, LINK_TN_SPLIT_PCT, 4, b, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(s_tone.cfg[LINK_TN_F_MIN_HZ], 500u);
    CHECK_EQ(s_tone.cfg[LINK_TN_MIN_PERIODS], 4u);

    /* A value out of range is refused with its reason and stores nothing. */
    const uint16_t bad[4] = { LINK_TN_EN_TAP, 22u, 500u, 7000u };
    CHECK(write_page(LINK_PAGE_TONE, LINK_TN_ENABLE, 4, bad, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(s_tone.cfg[LINK_TN_F_MAX_HZ], 5000u);

    /* Read only registers; EVT_SEL alone is writable. */
    const uint16_t one = 1u;
    CHECK(write_page(LINK_PAGE_TONE, LINK_TN_FLAGS, 1, &one, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_READ_ONLY);
    CHECK(write_page(LINK_PAGE_TONE, LINK_TN_EVT_SEL, 1, &one, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(s_tone.evt_sel, 1u);

    /* The SENSE page's pins are not the tap's. */
    const uint16_t sn[4] = { LINK_SN_EN_I228, 16u, 17u, 400u };
    CHECK(write_page(LINK_PAGE_SENSE, LINK_SN_ENABLE, 4, sn, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
}

/*
 * A 4.8 panel and a 4.7 coprocessor.  The coprocessor has no TONE page and
 * answers BAD_PAGE, which is why the panel sends nothing there below
 * minor 8.
 */
TEST_CASE(a_4_7_coprocessor_has_no_tone_page_and_says_so)
{
    fresh_47();
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_TONE, 0, 1, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
    const uint16_t a[4] = { 1u, 22u, 400u, 6500u };
    CHECK(write_page(LINK_PAGE_TONE, 0, 4, a, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_PAGE);
    /* The 4.7 pages are where they were. */
    CHECK(ask_read(LINK_PAGE_SENSE, 0, 1, &r));
    CHECK_EQ(r.op, LINK_OP_DATA);
}

/*
 * A 4.7 panel and a 4.8 coprocessor.  The panel never reads or writes
 * TONE; the identity and CONTROL answer as before, the bench arms, and a
 * tap kept in the coprocessor's flash is still there for a 4.8 panel.
 */
TEST_CASE(a_4_7_panel_links_and_arms_on_a_4_8_coprocessor)
{
    fresh_48();
    s_tone.cfg[LINK_TN_ENABLE] = LINK_TN_EN_TAP;
    link_msg_t r;
    CHECK(ask_read(LINK_PAGE_IDENTITY, 0, LINK_ID_COUNT, &r));
    CHECK_EQ(r.regs[LINK_ID_PROTOCOL_MAJOR], 4u);
    CHECK(r.regs[LINK_ID_PROTOCOL_MINOR] >= 8u);
    const uint16_t frame[LINK_CT_ARM_FRAME] = { 1, 2500, 14 };
    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, frame, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], 1u);
    CHECK(tone_page_enabled(&s_tone));
}

/* The fault bits are one bit each; 4.7's store-off bit is bit 6 and moves
 * none of the others. */
TEST_CASE(the_fault_bits_are_one_bit_each)
{
    static const uint16_t bits[] = {
        LINK_FAULT_LINK_SILENT, LINK_FAULT_OVERCURRENT, LINK_FAULT_OVERTEMP,
        LINK_FAULT_STALL, LINK_FAULT_HEARTBEAT, LINK_FAULT_VERSION,
        LINK_FAULT_STORE_OFF,
    };
    for (unsigned i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) {
        CHECK_EQ(bits[i], (uint16_t)(1u << i));
    }
}

/*
 * CHAN_CFG and OUTPUTS under an armed bank.  Disarmed, both take anything
 * the page's own rules take.  Armed, a slot does not change, a role does
 * not change and a throttle channel's slew and endpoints do not change; a
 * surface's slew and endpoints do, because the SERVO screen states them
 * with every position.  The reason is the SENSE page's for the same rule.
 */
TEST_CASE(chan_cfg_under_an_armed_bank_takes_a_surfaces_range_and_no_more)
{
    uint16_t regs[LINK_CC_COUNT];
    uint16_t next[LINK_CC_COUNT];
    outputs_chan_cfg_defaults(regs);
    regs[LINK_CC_ROLE] = LINK_CC_ROLE_THROTTLE;      /* channel 0: a motor */
    regs[LINK_CC_MIN_US] = 1000u;
    regs[LINK_CC_MAX_US] = 2000u;

    /* The page in force, written again. */
    memcpy(next, regs, sizeof(next));
    CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true), 0u);

    /* The throttle's endpoints, one microsecond either way. */
    for (int d = -1; d <= 1; d += 2) {
        memcpy(next, regs, sizeof(next));
        next[LINK_CC_MIN_US] = (uint16_t)(1000 + d);
        CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, false), 0u);
        memcpy(next, regs, sizeof(next));
        next[LINK_CC_MAX_US] = (uint16_t)(2000 + d);
        CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, false), 0u);
    }
    /* Its slew. */
    memcpy(next, regs, sizeof(next));
    next[LINK_CC_SLEW] = 1u;
    CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true),
             LINK_NACK_BAD_VALUE);

    /* A role, either way, on any channel. */
    memcpy(next, regs, sizeof(next));
    next[LINK_CC_ROLE] = LINK_CC_ROLE_SURFACE;
    CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true),
             LINK_NACK_BAD_VALUE);
    for (unsigned c = 1; c < LINK_OUT_CHANNELS; ++c) {
        memcpy(next, regs, sizeof(next));
        next[c * LINK_CC_STRIDE + LINK_CC_ROLE] = LINK_CC_ROLE_THROTTLE;
        CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, false), 0u);
    }

    /* A surface's slew and endpoints: taken armed, on every surface. */
    for (unsigned c = 1; c < LINK_OUT_CHANNELS; ++c) {
        memcpy(next, regs, sizeof(next));
        next[c * LINK_CC_STRIDE + LINK_CC_SLEW]   = 600u;
        next[c * LINK_CC_STRIDE + LINK_CC_MIN_US] = 660u;
        next[c * LINK_CC_STRIDE + LINK_CC_MAX_US] = 860u;
        CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true), 0u);
    }
    /* But not in one write with a throttle's. */
    next[LINK_CC_MAX_US] = 1900u;
    CHECK_EQ(outputs_chan_cfg_armed_check(regs, next, true),
             LINK_NACK_BAD_VALUE);

    CHECK_EQ(outputs_chan_cfg_armed_check(NULL, next, true), 0u);
    CHECK_EQ(outputs_chan_cfg_armed_check(regs, NULL, true), 0u);
}

TEST_CASE(outputs_under_an_armed_bank_takes_no_change)
{
    uint16_t regs[LINK_OS_COUNT];
    uint16_t next[LINK_OS_COUNT];
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = (uint16_t)LINK_DRIVER_PWM;
    regs[LINK_OS_PIN]     = 0u;
    regs[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1);
    regs[LINK_OS_RATE_HZ] = 50u;

    memcpy(next, regs, sizeof(next));
    CHECK_EQ(outputs_slots_armed_check(regs, next, true), 0u);

    /* Every register of every slot, changed alone. */
    for (unsigned i = 0; i < LINK_OS_COUNT; ++i) {
        memcpy(next, regs, sizeof(next));
        next[i] = (uint16_t)(next[i] + 1u);
        CHECK_EQ(outputs_slots_armed_check(regs, next, true),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(outputs_slots_armed_check(regs, next, false), 0u);
    }
    CHECK_EQ(outputs_slots_armed_check(NULL, next, true), 0u);
    CHECK_EQ(outputs_slots_armed_check(regs, NULL, true), 0u);
}

/*
 * 4.16 moves no register and adds no refusal code: a PWM endpoint longer
 * than its frame is refused with the BAD_VALUE every limit uses.
 */
TEST_CASE(a_pulse_longer_than_its_frame_needs_no_new_register)
{
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_PULSE_FRAME);
    CHECK_EQ(LINK_MINOR_PULSE_FRAME, 16u);
    CHECK(LINK_MINOR_PULSE_FRAME > LINK_MINOR_KST_LEAVE);
    CHECK_EQ(LINK_CC_CEILING_US, 2500u);
}

/*
 * 4.17 adds one refusal code after the ones there were: BUSY, which the TONE
 * page says for a pin something else holds.  No register moves.
 */
TEST_CASE(a_busy_pin_is_a_refusal_code_of_its_own)
{
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_TONE_BUSY);
    CHECK_EQ(LINK_MINOR_TONE_BUSY, 17u);
    CHECK(LINK_MINOR_TONE_BUSY > LINK_MINOR_PULSE_FRAME);
    CHECK_EQ(LINK_NACK_NOT_ARMED, 5);
    CHECK_EQ(LINK_NACK_BUSY, 6);
}

/*
 * 4.18 gives ARM's non-zero values a meaning and moves no register: 1 is
 * every role, which is what an older panel writes, and the roles are bits
 * above it.
 */
TEST_CASE(an_arm_names_its_roles_from_4_18)
{
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_ARM_ROLE);
    CHECK_EQ(LINK_MINOR_ARM_ROLE, 18u);
    CHECK(LINK_MINOR_ARM_ROLE > LINK_MINOR_TONE_BUSY);
    CHECK_EQ(LINK_CT_ARM, 0u);
    CHECK_EQ(LINK_ARM_ALL, 1u);
    CHECK_EQ(LINK_ARM_ROLES & LINK_ARM_ALL, 0u);
    CHECK_EQ(LINK_ARM_THROTTLE & LINK_ARM_SURFACE, 0u);
}

/* ARM is 0, LINK_ARM_ALL, or a set of role bits; any other value names no
 * role and is not an arm. */
TEST_CASE(arm_takes_zero_one_or_a_set_of_roles)
{
    static const uint16_t ok[] = {
        0u, LINK_ARM_ALL, LINK_ARM_THROTTLE, LINK_ARM_SURFACE, LINK_ARM_ROLES,
    };
    static const uint16_t bad[] = {
        LINK_ARM_ALL | LINK_ARM_THROTTLE, LINK_ARM_ALL | LINK_ARM_SURFACE,
        LINK_ARM_ALL | LINK_ARM_ROLES, 0x0008u, 0x8000u, 0xFFFFu,
    };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); ++i) {
        CHECK(link_arm_value_ok(ok[i]));
    }
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(!link_arm_value_ok(bad[i]));
        CHECK_EQ(link_arm_roles(bad[i]), 0u);
    }
    CHECK_EQ(link_arm_roles(0u), 0u);
    CHECK_EQ(link_arm_roles(LINK_ARM_ALL), LINK_ARM_ROLES);
    CHECK_EQ(link_arm_roles(LINK_ARM_THROTTLE), LINK_ARM_THROTTLE);
    CHECK_EQ(link_arm_roles(LINK_ARM_SURFACE), LINK_ARM_SURFACE);
    CHECK_EQ(link_arm_roles(LINK_ARM_ROLES), LINK_ARM_ROLES);
}

/*
 * The roles of an arm change only through a disarm.  A different set while
 * armed is refused with BAD_VALUE and changes nothing; the same set again is
 * a re-assert; 0 and then the new set is accepted.
 */
TEST_CASE(an_armed_bench_changes_its_roles_only_through_a_disarm)
{
    fresh();
    link_msg_t r;
    const uint16_t throttle[LINK_CT_ARM_FRAME] = { LINK_ARM_THROTTLE, 2500, 14 };
    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, throttle, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], LINK_ARM_THROTTLE);

    const uint16_t surface[LINK_CT_ARM_FRAME] = { LINK_ARM_SURFACE, 1000, 12 };
    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, surface, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(g.control[LINK_CT_ARM], LINK_ARM_THROTTLE);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 2500);
    CHECK_EQ(g.control[LINK_CT_MOTOR_POLES], 14);

    /* Every role is a different set too, and so is 1. */
    const uint16_t all[1] = { LINK_ARM_ALL };
    CHECK(write_control(LINK_CT_ARM, 1, all, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);

    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, throttle, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], LINK_ARM_THROTTLE);

    const uint16_t disarm[1] = { 0 };
    CHECK(write_control(LINK_CT_ARM, 1, disarm, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK(write_control(LINK_CT_ARM, LINK_CT_ARM_FRAME, surface, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], LINK_ARM_SURFACE);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 1000);
}

/*
 * A value that names no role is BAD_VALUE even while the bench may not arm:
 * the panel's mistake is reported as its own, ahead of NOT_ARMED.  A valid
 * role arm in that state is NOT_ARMED as an old-style 1 is.
 */
TEST_CASE(a_bad_arm_value_is_refused_ahead_of_not_armed)
{
    fresh();
    g.may_arm = false;
    link_msg_t r;
    const uint16_t bad[1] = { LINK_ARM_ALL | LINK_ARM_THROTTLE };
    CHECK(write_control(LINK_CT_ARM, 1, bad, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(g.control[LINK_CT_ARM], 0);

    const uint16_t role[1] = { LINK_ARM_SURFACE };
    CHECK(write_control(LINK_CT_ARM, 1, role, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_NOT_ARMED);
    CHECK_EQ(g.control[LINK_CT_ARM], 0);

    g.may_arm = true;
    const uint16_t past[1] = { 0x0008u };
    CHECK(write_control(LINK_CT_ARM, 1, past, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(g.control[LINK_CT_ARM], 0);
}

/* A panel before 4.18 writes 1; that is still an arm, of every role. */
TEST_CASE(an_old_style_arm_of_one_arms_every_role)
{
    fresh();
    link_msg_t r;
    const uint16_t one[1] = { LINK_ARM_ALL };
    CHECK(write_control(LINK_CT_ARM, 1, one, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], LINK_ARM_ALL);
    CHECK_EQ(link_arm_roles(g.control[LINK_CT_ARM]), LINK_ARM_ROLES);
}

/* The wire values are the ones the docs and an older or newer panel state. */
_Static_assert(LINK_ARM_THROTTLE == 0x0002u && LINK_ARM_SURFACE == 0x0004u,
               "the role bits are wire values");

TEST_CASE(the_arm_values_are_the_documented_literals)
{
    CHECK(link_arm_value_ok(0x0002u));
    CHECK(link_arm_value_ok(0x0004u));
    CHECK(link_arm_value_ok(0x0006u));
    CHECK(!link_arm_value_ok(0x0003u));
    CHECK(!link_arm_value_ok(0x0010u));
    CHECK(!link_arm_value_ok(0x0020u));
    CHECK_EQ(link_arm_roles(0x0002u), 0x0002u);
    CHECK_EQ(link_arm_roles(0x0004u), 0x0004u);
    CHECK_EQ(link_arm_roles(1u), 0x0006u);
}

/*
 * Every non-zero to different non-zero write is refused, whether the sets
 * overlap or not, and leaves ARM as it was.
 */
TEST_CASE(a_change_between_overlapping_sets_is_refused)
{
    static const uint16_t pairs[][2] = {
        { 0x0002u, 0x0006u }, { 0x0006u, 0x0002u }, { 0x0004u, 0x0006u },
        { 0x0006u, 0x0004u }, { 1u, 0x0006u }, { 0x0006u, 1u },
        { 1u, 0x0002u }, { 0x0002u, 1u },
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        fresh();
        link_msg_t r;
        const uint16_t first[1] = { pairs[i][0] };
        const uint16_t next[1]  = { pairs[i][1] };
        CHECK(write_control(LINK_CT_ARM, 1, first, &r));
        CHECK_EQ(r.op, LINK_OP_ACK);
        CHECK(write_control(LINK_CT_ARM, 1, next, &r));
        CHECK_EQ(r.op, LINK_OP_NACK);
        CHECK_EQ(r.regs[0], LINK_NACK_BAD_VALUE);
        CHECK_EQ(g.control[LINK_CT_ARM], pairs[i][0]);
    }
}

/*
 * 4.19 moves no register: the SERVO rate follows PROG's TARGET, and
 * the registers that carry both are where 4.16 had them.
 */
TEST_CASE(the_servo_rate_follows_the_target_without_a_new_register)
{
    CHECK(LINK_PROTOCOL_MINOR >= 19u);
    CHECK_EQ(LINK_MINOR_SERVO_TARGET_RATE, 19u);
    CHECK(LINK_MINOR_SERVO_TARGET_RATE > LINK_MINOR_ARM_ROLE);
    CHECK_EQ(LINK_SV_FRAME_HZ, 0);
    CHECK_EQ(LINK_PG_TARGET, 3);
    CHECK_EQ(LINK_PAGE_SERVO, 0x29);
}

/*
 * 4.20 keeps ARTWORK and ART_DATA where they were and grows ARTWORK by the
 * two hash registers after the CRC; the payload is a drawing, and the
 * largest one still fits the 32-bit length the page carries.
 */
TEST_CASE(the_artwork_page_serves_a_drawing_with_its_hash)
{
    CHECK_EQ(LINK_MINOR_DRAWING, 20u);
    CHECK(LINK_MINOR_DRAWING > LINK_MINOR_SERVO_TARGET_RATE);
    CHECK_EQ(LINK_PAGE_ARTWORK, 0x26);
    CHECK_EQ(LINK_PAGE_ART_DATA, 0x27);
    CHECK_EQ(LINK_AW_CRC, 6);
    CHECK_EQ(LINK_AW_HASH_LO, 7);
    CHECK_EQ(LINK_AW_HASH_HI, 8);
    CHECK_EQ(LINK_AW_COUNT, 9);
    CHECK(LINK_AW_COUNT <= LINK_MAX_REGS);
    CHECK_EQ(LINK_ART_DRAWING, 2);
    CHECK_EQ(LINK_DRAW_MAX_BYTES, 16u + 64u * 8u + 400u * 12u);
    CHECK(LINK_DRAW_MAX_BYTES <= 0xFFFFu);
    /* The fourth pad kind does not collide with the PADS page's three. */
    CHECK(LINK_DRAW_PAD_GPIO != LINK_PAD_GROUND);
    CHECK(LINK_DRAW_PAD_GPIO != LINK_PAD_POWER);
    CHECK(LINK_DRAW_PAD_GPIO != LINK_PAD_OTHER);
}

/*
 * 4.21 grows STATUS by the two halves of the refusal count, after the eight
 * registers a 4.20 page has, and moves none of them.
 */
TEST_CASE(the_status_page_counts_refused_frames_after_its_old_registers)
{
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_REFUSED);
    CHECK_EQ(LINK_MINOR_REFUSED, 21u);
    CHECK(LINK_MINOR_REFUSED > LINK_MINOR_DRAWING);
    CHECK_EQ(LINK_PAGE_STATUS, 0x01);
    CHECK_EQ(LINK_ST_FRAMES_LO, 4);
    CHECK_EQ(LINK_ST_RESYNCS, 7);
    CHECK_EQ(LINK_ST_COUNT_V420, 8u);
    CHECK_EQ(LINK_ST_REFUSED_LO, 8);
    CHECK_EQ(LINK_ST_REFUSED_HI, 9);
    CHECK_EQ(LINK_ST_COUNT, 10);
    CHECK(LINK_ST_COUNT <= LINK_MAX_REGS);
}

/*
 * Frozen across majors (link_pages.h): the identity registers a panel reads
 * before it compares majors, and the two update pages, keep their numbers
 * and their layout.  A change here is a coprocessor no later panel can move
 * to its own image over the link.
 */
TEST_CASE(the_frozen_registers_keep_their_numbers)
{
    CHECK_EQ(LINK_PROTOCOL_MINOR, 22u);
    CHECK_EQ(LINK_MINOR_UPDATE, 22u);
    CHECK(LINK_MINOR_UPDATE > LINK_MINOR_REFUSED);

    CHECK_EQ(LINK_PAGE_IDENTITY, 0x00);
    CHECK_EQ(LINK_ID_PROTOCOL_MAJOR, 0);
    CHECK_EQ(LINK_ID_PROTOCOL_MINOR, 1);
    CHECK_EQ(LINK_ID_FIRMWARE_MAJOR, 2);
    CHECK_EQ(LINK_ID_FIRMWARE_MINOR, 3);
    CHECK_EQ(LINK_ID_FIRMWARE_PATCH, 4);
    CHECK_EQ(LINK_ID_HARDWARE, 5);
    CHECK_EQ(LINK_ID_CAPABILITIES, 6);
    CHECK_EQ(LINK_ID_COUNT, 7);

    CHECK_EQ(LINK_PAGE_UPDATE_CTL, 0xF0);
    CHECK_EQ(LINK_PAGE_UPDATE_DATA, 0xF1);
    CHECK_EQ(LINK_UPD_VERSION, 0);
    CHECK_EQ(LINK_UPD_STATE, 1);
    CHECK_EQ(LINK_UPD_ERROR, 2);
    CHECK_EQ(LINK_UPD_SLOT, 3);
    CHECK_EQ(LINK_UPD_SLOT_SECTORS, 4);
    CHECK_EQ(LINK_UPD_NEXT_BLOCK, 5);
    CHECK_EQ(LINK_UPD_SIZE_LO, 6);
    CHECK_EQ(LINK_UPD_SIZE_HI, 7);
    CHECK_EQ(LINK_UPD_CMD, 8);
    CHECK_EQ(LINK_UPD_ARG0, 9);
    CHECK_EQ(LINK_UPD_ARG1, 10);
    CHECK_EQ(LINK_UPD_ARG2, 11);
    CHECK_EQ(LINK_UPD_DIGEST_OF, 12);
    CHECK_EQ(LINK_UPD_DIGEST, 16);
    CHECK_EQ(LINK_UPD_COUNT, 32);
    CHECK_EQ(LINK_UPD_BLOCK_BYTES, 64u);
    CHECK_EQ(LINK_UPD_DATA_COUNT, 32u);
    CHECK_EQ(LINK_UPD_SECTOR_BYTES, 4096u);
    CHECK_EQ(LINK_UPD_CMD_HASH_RUNNING, 1);
    CHECK_EQ(LINK_UPD_CMD_BEGIN, 2);
    CHECK_EQ(LINK_UPD_CMD_COMMIT, 3);
    CHECK_EQ(LINK_UPD_CMD_FINISH, 4);
    CHECK_EQ(LINK_UPD_CMD_ACTIVATE, 5);
    CHECK_EQ(LINK_UPD_CMD_ABORT, 6);
    CHECK_EQ(LINK_UPD_CMD_BUY, 7);
    CHECK_EQ(LINK_UPD_SLOT_TRIAL, 0x0100u);
    CHECK_EQ(LINK_UPD_SLOT_NONE, 0x000Fu);
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
    RUN(a_slew_above_the_maximum_is_refused_and_stores_nothing);
    RUN(a_kept_slew_above_the_maximum_starts_as_the_maximum);
    RUN(a_refused_slots_write_stores_none_of_its_registers);
    RUN(the_arming_frame_applies_arm_throttle_and_poles_together);
    RUN(an_arming_frame_with_a_bad_pole_count_arms_nothing);
    RUN(arm_is_refused_with_not_armed_while_the_bench_may_not_arm);
    RUN(clear_takes_the_magic_and_nothing_else);
    RUN(a_pole_count_is_zero_or_even_and_within_range);
    RUN(the_coprocessor_refuses_to_be_spoken_to_in_its_own_voice);
    RUN(every_request_is_answered);
    RUN(the_version_string_says_what_the_numbers_say);
    RUN(the_sense_pages_extend_the_map_without_moving_it);
    RUN(the_tone_page_extends_the_map_without_moving_it);
    RUN(the_encoder_extends_the_sense_page_without_moving_it);
    RUN(the_bench_flags_add_bits_5_and_6_and_move_none);
    RUN(servo_win_extends_the_map_without_moving_it);
    RUN(servo_win_is_served_read_only);
    RUN(an_older_coprocessor_has_no_servo_win_page_and_says_so);
    RUN(kst_extends_the_map_without_moving_it);
    RUN(prog_extends_the_map_without_moving_it);
    RUN(the_kst_staged_check_tells_a_change_a_swap_and_a_length);
    RUN(a_4_11_coprocessor_has_no_kst_page_and_says_so);
    RUN(the_sense_pages_are_served_and_refuse_whole);
    RUN(a_4_6_coprocessor_links_and_arms_without_the_sense_pages);
    RUN(a_4_6_panel_links_and_arms_on_a_4_7_coprocessor);
    RUN(the_tone_page_is_served_in_frames_and_refuses_whole);
    RUN(a_4_7_coprocessor_has_no_tone_page_and_says_so);
    RUN(a_4_7_panel_links_and_arms_on_a_4_8_coprocessor);
    RUN(the_fault_bits_are_one_bit_each);
    RUN(chan_cfg_under_an_armed_bank_takes_a_surfaces_range_and_no_more);
    RUN(outputs_under_an_armed_bank_takes_no_change);
    RUN(a_pulse_longer_than_its_frame_needs_no_new_register);
    RUN(a_busy_pin_is_a_refusal_code_of_its_own);
    RUN(an_arm_names_its_roles_from_4_18);
    RUN(arm_takes_zero_one_or_a_set_of_roles);
    RUN(an_armed_bench_changes_its_roles_only_through_a_disarm);
    RUN(a_bad_arm_value_is_refused_ahead_of_not_armed);
    RUN(an_old_style_arm_of_one_arms_every_role);
    RUN(the_arm_values_are_the_documented_literals);
    RUN(a_change_between_overlapping_sets_is_refused);
    RUN(the_servo_rate_follows_the_target_without_a_new_register);
    RUN(the_artwork_page_serves_a_drawing_with_its_hash);
    RUN(the_status_page_counts_refused_frames_after_its_old_registers);
    RUN(the_frozen_registers_keep_their_numbers);
    return test_summary("link_pages");
}
