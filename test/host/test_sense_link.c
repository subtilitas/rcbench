/*
 * The panel's half of the SENSE and SERVO_SENSE pages, against the
 * coprocessor's half (shared/outputs/sense_page.c) and against a
 * coprocessor that speaks protocol 4.6.
 *
 * Under test: nothing sent to a coprocessor older than 4.7, and the
 * operator told once when a part is enabled for one; the page read before
 * anything is written and only what differs written; both parts off before
 * a part's frame changes while both are on, so two enabled parts never
 * meet on one address on the way; a refused frame not written again until the set-up changes, and a
 * refused part left off; a set-up with unset pins or one address for both
 * written with neither part enabled; writes only on an idle bank and once
 * the set-up has rested; the identity read again after a write is taken;
 * the reads' rates; and the events a read raises -- a part not answering,
 * the wrong identity, a stuck bus, a clipped reading, the store off --
 * each once.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "sense_link.h"
#include "sense_page.h"

static outputs_t    o;
static sense_page_t pg;      /* the coprocessor's end */
static sense_link_t sl;      /* the panel's end */
static uint32_t     now;
static uint16_t     minor;   /* the coprocessor's protocol minor */
static unsigned     writes, reads, to_sense, idents;
static bool         idle;

/* The setup SETUP starts with: both parts off, the page's own defaults. */
static sense_setup_t setup_default(void)
{
    const sense_setup_t w = {
        .i228 = false, .i3221 = false, .sda = 16, .scl = 17,
        .i228_addr = 0x45u, .i228_uohm = 200u, .i228_max_da = 2048u,
        .i3221_addr = 0x40u, .i3221_dmohm = 1000u, .i3221_ch = 0x01u,
    };
    return w;
}

static void fresh(uint16_t m)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, (uint64_t)1u << 3);
    sense_page_init(&pg);
    sense_link_init(&sl);
    now = 10000u;
    minor = m;
    writes = reads = to_sense = idents = 0u;
    idle = true;
    const sense_setup_t w = setup_default();
    sense_link_want(&sl, &w, now);
    sense_link_came_up(&sl, minor, now);
}

/*
 * One exchange as the coprocessor answers it.  A 4.6 coprocessor has no
 * SENSE or SERVO_SENSE page and refuses both with BAD_PAGE.
 */
static int far_exchange(const sense_link_op_t *op, uint16_t *regs)
{
    if (op->page == LINK_PAGE_SENSE || op->page == LINK_PAGE_SERVO_SENSE) {
        ++to_sense;
        if (minor < SENSE_LINK_MINOR) {
            return LINK_NACK_BAD_PAGE;
        }
    }
    if (op->write) {
        ++writes;
        const uint8_t nack = sense_page_write(&pg, op->off, op->n, op->regs,
                                              &o, 0u);
        return (nack == 0u) ? SENSE_LINK_ACK : (int)nack;
    }
    ++reads;
    memset(regs, 0, LINK_MAX_REGS * sizeof(uint16_t));
    if (op->page == LINK_PAGE_IDENTITY) {
        ++idents;
        regs[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
        regs[LINK_ID_PROTOCOL_MINOR] = minor;
        regs[LINK_ID_CAPABILITIES]   = sense_page_caps(&pg);
    } else if (op->page == LINK_PAGE_SENSE) {
        sense_page_read(&pg, op->off, op->n, regs);
    } else {
        sense_servo_read(&pg, op->off, op->n, regs);
    }
    return SENSE_LINK_ACK;
}

/* One poll of the control task: every exchange owed, then 50 ms on. */
static void poll_once(void)
{
    for (int k = 0; k < 8; ++k) {
        sense_link_op_t op;
        if (!sense_link_next(&sl, now, idle, &op)) {
            break;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int result = far_exchange(&op, regs);
        sense_link_done(&sl, result, op.write ? NULL : regs, now);
    }
    now += 50u;
}

static void polls(int n)
{
    for (int i = 0; i < n; ++i) {
        poll_once();
    }
}

static void want(const sense_setup_t *w)
{
    sense_link_want(&sl, w, now);
}

/* The page as core 1 would leave it: FLAGS and what goes with them. */
static void far_flags(uint16_t f)
{
    pg.sense[LINK_SN_FLAGS] = f;
}

/* ---------------------------------------------------------- the version */

TEST_CASE(nothing_is_sent_to_a_4_6_coprocessor)
{
    fresh(6u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    /* A part enabled with a coprocessor up that cannot read it is said at
     * once, */
    want(&w);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_NO_PAGE);
    polls(100);
    CHECK_EQ(to_sense, 0u);
    CHECK_EQ(writes + reads, 0u);
    /* not again for another edit that enables nothing new, */
    w.i228_uohm = 250u;
    want(&w);
    CHECK_EQ(sense_link_events(&sl), 0u);
    /* and at the next link-up that finds it. */
    sense_link_came_up(&sl, 6u, now);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_NO_PAGE);
    polls(40);
    CHECK_EQ(to_sense, 0u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK(sense_link_settled(&sl));
    CHECK_EQ(sense_link_flags(&sl), 0u);

    /* With nothing enabled there is nothing to say. */
    fresh(6u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    polls(10);
    CHECK_EQ(to_sense, 0u);

    /* And the same panel on a 4.7 coprocessor reads the page. */
    fresh(7u);
    polls(1);
    CHECK(to_sense > 0u);
}

TEST_CASE(a_coprocessor_that_refuses_the_page_is_left_alone)
{
    /* One that names 4.7 and answers BAD_PAGE: nothing more is sent. */
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    sense_link_op_t op;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_READ_SETUP);
    sense_link_done(&sl, LINK_NACK_BAD_PAGE, NULL, now);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_NO_PAGE);
    CHECK(!sense_link_next(&sl, now + 1000u, true, &op));
}

/* -------------------------------------------------------- the set-up */

TEST_CASE(the_page_is_read_first_and_only_what_differs_is_written)
{
    fresh(7u);
    sense_link_op_t op;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_READ_SETUP);
    CHECK(!op.write);
    CHECK_EQ(op.page, LINK_PAGE_SENSE);
    CHECK_EQ(op.off, 0u);
    CHECK_EQ(op.n, LINK_SN_CONFIG_COUNT);
    /* The defaults are the page's: nothing to write, nothing enabled to
     * read. */
    polls(20);
    CHECK_EQ(writes, 0u);
    CHECK_EQ(reads, 1u);
    CHECK(sense_link_settled(&sl));
}

TEST_CASE(enabling_a_part_writes_the_bus_frame_and_reads_identity)
{
    fresh(7u);
    polls(1);
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    /* Not before the set-up has rested. */
    polls(9);
    CHECK_EQ(writes, 0u);
    polls(2);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
    CHECK_EQ(idents, 1u);
    uint16_t caps = 0u;
    CHECK(sense_link_take_caps(&sl, &caps));
    CHECK_EQ(caps, (uint16_t)LINK_CAP_PACK_SENSE);
    CHECK(!sense_link_take_caps(&sl, &caps));
    CHECK(sense_link_settled(&sl));

    /* Then SENSE's read-only registers at 20 Hz and no SERVO_SENSE. */
    const unsigned before = reads;
    polls(20);
    CHECK_EQ(reads - before, 20u);

    /* The INA3221 adds SERVO_SENSE, one poll in four. */
    w.i3221 = true;
    want(&w);
    polls(12);
    const unsigned at = reads;
    polls(40);
    CHECK_EQ(reads - at, 40u + 10u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE],
             (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221));
    CHECK(sense_link_take_caps(&sl, &caps));
    CHECK_EQ(caps, (uint16_t)(LINK_CAP_PACK_SENSE | LINK_CAP_SERVO_SENSE));
}

TEST_CASE(a_part_frame_goes_with_both_parts_off)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    polls(12);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);

    /* One part on the page: its new shunt goes alone, the part left on. */
    w.i228_uohm = 300u;
    want(&w);
    now += SENSE_LINK_SETTLE_MS;
    sense_link_op_t op;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_I228);
    uint16_t regs[LINK_MAX_REGS];
    sense_link_done(&sl, far_exchange(&op, regs), NULL, now);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 300u);
    polls(2);
    CHECK(sense_link_settled(&sl));

    /* Both on the page: a new shunt is off, the INA228's frame, the bus
     * frame. */
    w.i3221 = true;
    want(&w);
    polls(12);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE],
             (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221));
    w.i228_uohm = 250u;
    want(&w);
    now += SENSE_LINK_SETTLE_MS;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_OFF);
    CHECK(op.write);
    CHECK_EQ(op.off, LINK_SN_ENABLE);
    CHECK_EQ(op.regs[0], 0u);
    CHECK_EQ(op.regs[1], 16u);
    CHECK_EQ(op.regs[2], 17u);
    CHECK_EQ(op.regs[3], LINK_SN_KHZ_BUS);
    sense_link_done(&sl, far_exchange(&op, regs), NULL, now);
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_I228);
    CHECK_EQ(op.off, LINK_SN_I228_ADDR);
    CHECK_EQ(op.n, 4u);
    CHECK_EQ(op.regs[1], 250u);
    sense_link_done(&sl, far_exchange(&op, regs), NULL, now);
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_BUS);
    CHECK_EQ(op.regs[0], (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221));
    sense_link_done(&sl, far_exchange(&op, regs), NULL, now);
    CHECK(sense_link_settled(&sl));
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 250u);
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(two_parts_swap_addresses_without_meeting)
{
    /* Both enabled, and the addresses exchanged: written one frame at a
     * time with both on, the first would put two parts on one address and
     * be refused. */
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    w.i228_addr = 0x40u;
    w.i3221_addr = 0x41u;
    want(&w);
    polls(14);
    CHECK_EQ(pg.sense[LINK_SN_I228_ADDR], 0x40u);
    CHECK_EQ(pg.sense[LINK_SN_I3221_ADDR], 0x41u);
    w.i228_addr = 0x41u;
    w.i3221_addr = 0x40u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_I228_ADDR], 0x41u);
    CHECK_EQ(pg.sense[LINK_SN_I3221_ADDR], 0x40u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE],
             (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221));
    CHECK(sense_link_settled(&sl));
}

TEST_CASE(a_refused_frame_is_not_written_again_and_its_part_stays_off)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    /* 20000 uOhm at 655.3 A is 13 V across the shunt: no range reads it. */
    w.i228_uohm = 20000u;
    w.i228_max_da = 6553u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I228_REFUSED);
    /* The INA3221 runs; the INA228 does not, on the shunt it had. */
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I3221);
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], SENSE_DEFAULT_I228_UOHM);
    CHECK(sense_link_settled(&sl));
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I3221_ONLINE);
    const unsigned before = writes;
    polls(60);
    CHECK_EQ(writes, before);
    CHECK_EQ(sense_link_events(&sl), 0u);

    /* An edit asks again: 8.0 A puts 160 mV across 20000 uOhm. */
    w.i228_max_da = 80u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE],
             (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221));
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 20000u);
}

TEST_CASE(pins_the_page_refuses_are_said_and_leave_the_parts_off)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.sda = 5;            /* 5 mod 4 is 1: an SCL */
    w.scl = 6;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_BUS_REFUSED);
    CHECK_EQ(sense_link_sda(&sl), 5);
    CHECK_EQ(sense_link_scl(&sl), 6);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 0u);
    const unsigned before = writes;
    polls(20);
    CHECK_EQ(writes, before);

    /* The heartbeat's pin, GP3, is reserved: refused the same way. */
    w.sda = 2;
    w.scl = 3;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_BUS_REFUSED);

    w.sda = 16;
    w.scl = 17;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
}

TEST_CASE(unset_pins_and_one_address_are_not_written_enabled)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i3221 = true;
    w.sda = -1;
    want(&w);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_PINS_UNSET);
    polls(14);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 0u);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 0u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK_EQ(sense_link_sda(&sl), -1);

    w.sda = 16;
    w.i228 = true;
    w.i228_addr = 0x40u;
    want(&w);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_SAME_ADDR);
    polls(14);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], 0u);
    /* Said once per edit, not at every poll. */
    CHECK_EQ(sense_link_events(&sl), 0u);
    want(&w);
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(writes_wait_for_an_idle_bank)
{
    fresh(7u);
    polls(1);
    idle = false;
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    polls(40);
    CHECK_EQ(writes, 0u);
    idle = true;
    polls(1);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
}

TEST_CASE(an_unanswered_write_is_written_again)
{
    fresh(7u);
    polls(1);
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    now += SENSE_LINK_SETTLE_MS;
    sense_link_op_t op;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_BUS);
    sense_link_done(&sl, SENSE_LINK_NO_ANSWER, NULL, now);
    CHECK(!sense_link_settled(&sl));
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_BUS);

    /* An identity read nobody answered is asked again too. */
    uint16_t regs[LINK_MAX_REGS];
    sense_link_done(&sl, far_exchange(&op, regs), NULL, now);
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_IDENTITY);
    sense_link_done(&sl, SENSE_LINK_NO_ANSWER, NULL, now);
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_IDENTITY);
}

TEST_CASE(a_lost_link_reads_the_page_again)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    polls(14);
    const unsigned before = writes;
    sense_link_lost(&sl);
    sense_link_op_t op;
    CHECK(!sense_link_next(&sl, now, true, &op));
    CHECK_EQ(sense_link_flags(&sl), 0u);
    /* A coprocessor that kept the set-up in flash is not written again. */
    sense_link_came_up(&sl, 7u, now);
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_READ_SETUP);
    polls(14);
    CHECK_EQ(writes, before);
    /* One that restarted with its defaults is. */
    sense_page_init(&pg);
    sense_link_came_up(&sl, 7u, now);
    polls(14);
    CHECK_EQ(writes, before + 1u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
}

/* ------------------------------------------------------------ the reads */

TEST_CASE(a_part_that_does_not_answer_is_said_once)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    /* Written at 10500 ms, so the wait ends at 13000 ms. */
    polls(12);
    far_flags(LINK_SN_BUS_OPEN);
    /* Not while the coprocessor may still be finding it. */
    polls(40);
    CHECK(now <= 10500u + SENSE_LINK_GRACE_MS);
    CHECK_EQ(sense_link_events(&sl), 0u);
    polls(10);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I228_SILENT);
    polls(40);
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK_EQ(sense_link_addr(&sl, SENSE_LINK_INA228), 0x45u);

    /* PRESENT names where something did answer: 0x44, a bridge set. */
    pg.sense[LINK_SN_PRESENT] = (uint16_t)(1u << 4);
    polls(1);
    CHECK_EQ(sense_link_found(&sl, SENSE_LINK_INA228), 0x44u);
    /* An INA3221 is not looked for above 0x43. */
    CHECK_EQ(sense_link_found(&sl, SENSE_LINK_INA3221), 0u);

    /* Online, then gone: said at once, without the wait. */
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I228_ONLINE | LINK_SN_I228_ID_OK);
    polls(2);
    CHECK_EQ(sense_link_events(&sl), 0u);
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I228_ID_OK);
    polls(1);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I228_SILENT);
    polls(10);
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(another_identity_is_said_once_with_what_it_read)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i3221 = true;
    want(&w);
    polls(12);
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I3221_ID_WRONG);
    pg.sense[LINK_SN_I3221_ID] = 0x1408u;
    polls(1);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I3221_WRONG);
    CHECK_EQ(sense_link_id(&sl, SENSE_LINK_INA3221), 0x1408u);
    CHECK_EQ(sense_link_addr(&sl, SENSE_LINK_INA3221), 0x40u);
    /* Never as "not answering" as well: it answers. */
    polls((int)(SENSE_LINK_GRACE_MS / 50u) + 10);
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(a_stuck_bus_and_clipped_readings_are_said_on_their_edges)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    w.i3221_ch = 0x07u;
    want(&w);
    polls(14);
    const uint16_t ok = (uint16_t)(LINK_SN_BUS_OPEN | LINK_SN_I228_ONLINE
                                   | LINK_SN_I3221_ONLINE);
    far_flags(ok);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), 0u);

    far_flags((uint16_t)(ok | LINK_SN_BUS_STUCK));
    polls(1);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_STUCK);
    polls(3);
    CHECK_EQ(sense_link_events(&sl), 0u);
    far_flags(ok);
    polls(1);
    far_flags((uint16_t)(ok | LINK_SN_BUS_STUCK));
    polls(1);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_STUCK);

    far_flags((uint16_t)(ok | LINK_SN_I228_CLIPPED));
    polls(1);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I228_CLIPPED);
    polls(5);
    CHECK_EQ(sense_link_events(&sl), 0u);

    /* CH2 at the top of its range, on SERVO_SENSE's own cadence. */
    pg.servo[LINK_SS_CH_FLAGS] = (uint16_t)(LINK_SS_CH_VALID(2)
                                            | LINK_SS_CH_CLIPPED(2));
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_clipped_channel(&sl), 2u);
    CHECK_EQ(sense_link_i3221_dmohm(&sl), 1000u);
    CHECK_EQ(sense_i3221_full_scale_ma(sense_link_i3221_dmohm(&sl)), 1638u);
    polls(8);
    CHECK_EQ(sense_link_events(&sl), 0u);
}

TEST_CASE(a_clip_on_a_channel_not_read_is_not_said)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i3221 = true;                 /* CH1 only */
    want(&w);
    polls(14);
    pg.servo[LINK_SS_CH_FLAGS] = LINK_SS_CH_CLIPPED(3);
    polls(8);
    CHECK_EQ(sense_link_events(&sl), 0u);
    pg.servo[LINK_SS_CH_FLAGS] = LINK_SS_CH_CLIPPED(1);
    polls(4);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_clipped_channel(&sl), 1u);
}

TEST_CASE(the_store_off_is_said_once_per_link)
{
    fresh(7u);
    sense_link_faults(&sl, 0u);
    CHECK_EQ(sense_link_events(&sl), 0u);
    sense_link_faults(&sl, LINK_FAULT_STORE_OFF);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_STORE_OFF);
    sense_link_faults(&sl, LINK_FAULT_STORE_OFF | LINK_FAULT_HEARTBEAT);
    CHECK_EQ(sense_link_events(&sl), 0u);
    sense_link_lost(&sl);
    sense_link_came_up(&sl, 7u, now);
    sense_link_faults(&sl, LINK_FAULT_STORE_OFF);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_STORE_OFF);
}

TEST_CASE(the_esc_figures_and_the_totals_come_from_the_last_read)
{
    fresh(7u);
    bool v_ok = true;
    bool i_ok = true;
    float v = 1.0f;
    float a = 1.0f;
    int32_t mah = 0;
    uint32_t wh = 0u;
    CHECK(!sense_link_esc(&sl, now, &v_ok, &v, &i_ok, &a));
    CHECK(!v_ok);
    CHECK(!i_ok);
    CHECK(!sense_link_totals(&sl, now, &mah, &wh));

    sense_setup_t w = setup_default();
    w.i228 = true;
    want(&w);
    polls(14);
    sense_page_esc(&pg, true, 24.37f, true, 31.5f);
    pg.sense[LINK_SN_I228_CHARGE_LO] = 0x2345u;   /* -0x10000 + ... */
    pg.sense[LINK_SN_I228_CHARGE_HI] = 0xFFFFu;
    pg.sense[LINK_SN_I228_ENERGY_LO] = 0x0002u;
    pg.sense[LINK_SN_I228_ENERGY_HI] = 0x0001u;
    const uint32_t reads_before = sense_link_reads(&sl);
    polls(1);
    CHECK_EQ(sense_link_reads(&sl), reads_before + 1u);
    CHECK(sense_link_esc(&sl, now, &v_ok, &v, &i_ok, &a));
    CHECK(v_ok);
    CHECK(i_ok);
    CHECK_NEAR(v, 24.37f, 0.006f);
    CHECK_NEAR(a, 31.5f, 0.006f);
    CHECK(sense_link_totals(&sl, now, &mah, &wh));
    /* Two polls with no read: BENCH's totals are used instead. */
    CHECK(!sense_link_totals(&sl, now + SENSE_LINK_TOTALS_MS, &mah, &wh));
    CHECK_EQ(mah, (int32_t)0xFFFF2345u);
    CHECK_EQ(wh, 0x00010002u);

    /* The ESC's voltage alone. */
    sense_page_esc(&pg, true, 24.0f, false, 0.0f);
    polls(1);
    CHECK(sense_link_esc(&sl, now, &v_ok, &v, &i_ok, &a));
    CHECK(v_ok);
    CHECK(!i_ok);

    /* A read that stopped answering ages out. */
    CHECK(!sense_link_esc(&sl, now + SENSE_LINK_STALE_MS, &v_ok, &v, &i_ok,
                          &a));
    CHECK(!v_ok);
}

int main(void)
{
    RUN(nothing_is_sent_to_a_4_6_coprocessor);
    RUN(a_coprocessor_that_refuses_the_page_is_left_alone);
    RUN(the_page_is_read_first_and_only_what_differs_is_written);
    RUN(enabling_a_part_writes_the_bus_frame_and_reads_identity);
    RUN(a_part_frame_goes_with_both_parts_off);
    RUN(two_parts_swap_addresses_without_meeting);
    RUN(a_refused_frame_is_not_written_again_and_its_part_stays_off);
    RUN(pins_the_page_refuses_are_said_and_leave_the_parts_off);
    RUN(unset_pins_and_one_address_are_not_written_enabled);
    RUN(writes_wait_for_an_idle_bank);
    RUN(an_unanswered_write_is_written_again);
    RUN(a_lost_link_reads_the_page_again);
    RUN(a_part_that_does_not_answer_is_said_once);
    RUN(another_identity_is_said_once_with_what_it_read);
    RUN(a_stuck_bus_and_clipped_readings_are_said_on_their_edges);
    RUN(a_clip_on_a_channel_not_read_is_not_said);
    RUN(the_store_off_is_said_once_per_link);
    RUN(the_esc_figures_and_the_totals_come_from_the_last_read);
    return test_summary("sense_link");
}
