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
    RUN(the_sense_pages_are_served_and_refuse_whole);
    RUN(a_4_6_coprocessor_links_and_arms_without_the_sense_pages);
    RUN(a_4_6_panel_links_and_arms_on_a_4_7_coprocessor);
    RUN(the_tone_page_is_served_in_frames_and_refuses_whole);
    RUN(a_4_7_coprocessor_has_no_tone_page_and_says_so);
    RUN(a_4_7_panel_links_and_arms_on_a_4_8_coprocessor);
    RUN(the_fault_bits_are_one_bit_each);
    RUN(chan_cfg_under_an_armed_bank_takes_a_surfaces_range_and_no_more);
    RUN(outputs_under_an_armed_bank_takes_no_change);
    return test_summary("link_pages");
}
