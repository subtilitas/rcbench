/*
 * The remote servo node's chip check (shared/outputs/chip_gate.c) and the
 * writes a chip before the RP2350 A4 stepping refuses
 * (shared/link/link_gate.c, link_dev_refuse_drive()).
 *
 * Under test: revision 2 with ROM 2 (A2), revision 3 with ROM 3 (A3),
 * revision 2 with ROM 4 and revision 3 with ROM 3 refused with the fault
 * bit set; revision 3 with ROM 4 (A4) and later taken with no fault bit;
 * every write that could drive a pin refused with NOT_ARMED -- ARM, SUPPLY
 * ON, RAIL 1, a PORT_WANT bit, every KST, HITEC and ESC_WIRE write, a PROG
 * FUNC other than NONE, a slot with a driver on OUTPUTS, OUTPUTS_HI,
 * BIND_OUT and BIND_OUT_HI -- and every OFF and every other write passed
 * on; the link refusing such a write before its page sees it, and passing
 * it once the refusal is lifted.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "chip_gate.h"
#include "link_dev.h"
#include "link_gate.h"
#include "link_msg.h"
#include "link_pages.h"

#define NA ((uint8_t)LINK_NACK_NOT_ARMED)

/* ------------------------------------------------------------ the check */

TEST_CASE(only_an_a4_chip_is_taken)
{
    /* A2: revision 2, ROM 2. */
    CHECK(!chip_gate_ok(2u, 2u));
    /* A3: revision 3, ROM 3; only the ROM byte tells it from A4. */
    CHECK(!chip_gate_ok(3u, 3u));
    /* Revision 2 with an A4 ROM byte, and revision 3 with ROM 3. */
    CHECK(!chip_gate_ok(2u, 4u));
    CHECK(!chip_gate_ok(2u, 3u));
    CHECK(!chip_gate_ok(0u, 0u));
    /* A4, and a later stepping. */
    CHECK(chip_gate_ok(3u, 4u));
    CHECK(chip_gate_ok(3u, 5u));
    CHECK(chip_gate_ok(4u, 4u));
    CHECK_EQ(CHIP_GATE_REV_MIN, 3u);
    CHECK_EQ(CHIP_GATE_ROM_MIN, 4u);
}

TEST_CASE(a_refused_chip_sets_the_fault_bit)
{
    CHECK_EQ(chip_gate_fault(2u, 2u), (uint16_t)LINK_FAULT_CHIP_STEPPING);
    CHECK_EQ(chip_gate_fault(3u, 3u), (uint16_t)LINK_FAULT_CHIP_STEPPING);
    CHECK_EQ(chip_gate_fault(2u, 4u), (uint16_t)LINK_FAULT_CHIP_STEPPING);
    CHECK_EQ(chip_gate_fault(3u, 4u), 0u);
    /* Bit 8, beside the bits a 4.40 IOmcu sets. */
    CHECK_EQ((unsigned)LINK_FAULT_CHIP_STEPPING, 0x0100u);
    CHECK_EQ(LINK_ID_CHIP_REV, 7);
    CHECK_EQ(LINK_ID_ROM_VER, 8);
    CHECK_EQ(LINK_ID_ALL, 9);
    CHECK_EQ(LINK_ID_COUNT, 7);
}

/* ------------------------------------------------------------- the gate */

static uint8_t one(uint8_t page, unsigned reg, uint16_t v)
{
    return link_gate_drive(page, (uint8_t)reg, 1u, &v);
}

TEST_CASE(a_write_that_could_drive_a_pin_is_refused)
{
    CHECK_EQ(one(LINK_PAGE_CONTROL, LINK_CT_ARM, LINK_ARM_ALL), NA);
    CHECK_EQ(one(LINK_PAGE_CONTROL, LINK_CT_ARM, LINK_ARM_SURFACE), NA);
    CHECK_EQ(one(LINK_PAGE_SUPPLY, LINK_SP_OUTPUT, 1u), NA);
    CHECK_EQ(one(LINK_PAGE_POWER, LINK_PW_RAIL, 1u), NA);
    CHECK_EQ(one(LINK_PAGE_POWER, LINK_PW_PORT_WANT, 0x0100u), NA);
    CHECK_EQ(one(LINK_PAGE_KST, 0u, 0u), NA);
    CHECK_EQ(one(LINK_PAGE_HITEC, 0u, 0u), NA);
    CHECK_EQ(one(LINK_PAGE_ESC_WIRE, 0u, 0u), NA);
    CHECK_EQ(one(LINK_PAGE_PROG, LINK_PG_FUNC(0u), LINK_PG_FN_SERVO_PWM), NA);
    CHECK_EQ(one(LINK_PAGE_PROG, LINK_PG_FUNC(3u), LINK_PG_FN_DSHOT600), NA);
    /* A slot with a driver, on each page that binds. */
    const uint8_t binding[4] = { LINK_PAGE_OUTPUTS, LINK_PAGE_OUTPUTS_HI,
                                 LINK_PAGE_BIND_OUT, LINK_PAGE_BIND_OUT_HI };
    for (unsigned i = 0; i < 4u; ++i) {
        uint16_t slot[LINK_OS_STRIDE * 2u] = { 0 };
        slot[LINK_OS_STRIDE + LINK_OS_DRIVER] = (uint16_t)LINK_DRIVER_PWM;
        slot[LINK_OS_STRIDE + LINK_OS_PIN]    = 18u;
        CHECK_EQ(link_gate_drive(binding[i], 0u, (uint8_t)(LINK_OS_STRIDE * 2u),
                                 slot), NA);
        /* The same slot written from its DRIVER register on. */
        CHECK_EQ(link_gate_drive(binding[i], (uint8_t)LINK_OS_STRIDE,
                                 (uint8_t)LINK_OS_STRIDE,
                                 &slot[LINK_OS_STRIDE]), NA);
    }
    /* A frame of several registers with the ARM among them. */
    const uint16_t ct[LINK_CT_COUNT] = { LINK_ARM_ALL, 0u, 0u, 0u };
    CHECK_EQ(link_gate_drive(LINK_PAGE_CONTROL, 0u, LINK_CT_COUNT, ct), NA);
    /* NULL values with registers to write. */
    CHECK_EQ(link_gate_drive(LINK_PAGE_CONTROL, 0u, 1u, NULL), NA);
}

TEST_CASE(every_off_and_every_other_write_is_passed_on)
{
    CHECK_EQ(one(LINK_PAGE_CONTROL, LINK_CT_ARM, 0u), 0u);
    CHECK_EQ(one(LINK_PAGE_CONTROL, LINK_CT_CLEAR, LINK_CLEAR_MAGIC), 0u);
    CHECK_EQ(one(LINK_PAGE_CONTROL, LINK_CT_THROTTLE, 5000u), 0u);
    CHECK_EQ(one(LINK_PAGE_SUPPLY, LINK_SP_OUTPUT, 0u), 0u);
    CHECK_EQ(one(LINK_PAGE_SUPPLY, LINK_SP_SET_MV, 6000u), 0u);
    CHECK_EQ(one(LINK_PAGE_POWER, LINK_PW_RAIL, 0u), 0u);
    CHECK_EQ(one(LINK_PAGE_POWER, LINK_PW_PORT_WANT, 0u), 0u);
    CHECK_EQ(one(LINK_PAGE_PROG, LINK_PG_FUNC(0u), LINK_PG_FN_NONE), 0u);
    CHECK_EQ(one(LINK_PAGE_PROG, LINK_PG_REG(0u, LINK_PG_PIN), 16u), 0u);
    CHECK_EQ(one(LINK_PAGE_SENSE_MAP, 0u, 0x0007u), 0u);
    CHECK_EQ(one(LINK_PAGE_SENSE_LIMIT, 0u, 3000u), 0u);
    /* A slot cleared, and a slot's pin or rate alone. */
    const uint16_t none[LINK_OS_STRIDE] = { (uint16_t)LINK_DRIVER_NONE, 16u,
                                            0u, 0u };
    CHECK_EQ(link_gate_drive(LINK_PAGE_OUTPUTS, 0u, LINK_OS_STRIDE, none), 0u);
    CHECK_EQ(one(LINK_PAGE_OUTPUTS, LINK_OS_PIN, 16u), 0u);
    CHECK_EQ(one(LINK_PAGE_OUTPUTS_HI, LINK_OS_RATE_HZ, 50u), 0u);
    /* No register: nothing to judge. */
    CHECK_EQ(link_gate_drive(LINK_PAGE_CONTROL, 0u, 0u, NULL), 0u);
}

/* --------------------------------------------------------- on the link */

static unsigned g_writes;
static uint16_t g_control[LINK_CT_COUNT];

static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, &g_control[off], (size_t)n * sizeof(uint16_t));
}

static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    (void)ctx;
    ++g_writes;
    memcpy(&g_control[off], in, (size_t)n * sizeof(uint16_t));
    return 0u;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_CONTROL, LINK_CT_COUNT, control_read, control_write },
};

static link_msg_t arm_req(uint16_t v)
{
    link_msg_t req;
    memset(&req, 0, sizeof(req));
    req.op      = LINK_OP_WRITE;
    req.page    = LINK_PAGE_CONTROL;
    req.offset  = LINK_CT_ARM;
    req.count   = 1u;
    req.regs[0] = v;
    return req;
}

TEST_CASE(the_link_refuses_before_the_page_sees_the_write)
{
    link_dev_t d;
    link_dev_init(&d, k_pages, 1u, NULL, 0u);
    CHECK(!d.refuse_drive);
    link_dev_refuse_drive(&d, true);
    g_writes = 0u;
    memset(g_control, 0, sizeof(g_control));

    link_msg_t req = arm_req(LINK_ARM_ALL), got;
    CHECK(link_dev_dispatch(&d, &req, &got, 10u));
    CHECK_EQ(got.op, LINK_OP_NACK);
    CHECK_EQ(got.regs[0], (uint16_t)LINK_NACK_NOT_ARMED);
    CHECK_EQ(g_writes, 0u);
    CHECK_EQ(g_control[LINK_CT_ARM], 0u);
    /* The disarm reaches the page. */
    req = arm_req(0u);
    CHECK(link_dev_dispatch(&d, &req, &got, 20u));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK_EQ(g_writes, 1u);
    /* Lifted: the ARM reaches the page and the page decides. */
    link_dev_refuse_drive(&d, false);
    req = arm_req(LINK_ARM_ALL);
    CHECK(link_dev_dispatch(&d, &req, &got, 30u));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK_EQ(g_writes, 2u);
    CHECK_EQ(g_control[LINK_CT_ARM], (uint16_t)LINK_ARM_ALL);
    link_dev_refuse_drive(NULL, true);           /* nothing for NULL */
}

int main(void)
{
    RUN(only_an_a4_chip_is_taken);
    RUN(a_refused_chip_sets_the_fault_bit);
    RUN(a_write_that_could_drive_a_pin_is_refused);
    RUN(every_off_and_every_other_write_is_passed_on);
    RUN(the_link_refuses_before_the_page_sees_the_write);
    return test_summary("chip_gate");
}
