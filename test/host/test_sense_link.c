/*
 * The panel's half of the SENSE and SERVO_SENSE pages, against the
 * IOmcu's half (shared/outputs/sense_page.c) and against an
 * IOmcu that speaks protocol 4.6: what is sent and when.
 *
 * Under test: nothing sent to an IOmcu older than 4.7, and the
 * operator told once when a part is enabled for one; the page read before
 * anything is written and only what differs written; both parts off before
 * a part's frame changes while both are on, so two enabled parts never
 * meet on one address on the way; a refused frame not written again until
 * the set-up changes, and a refused part left off; a set-up with unset pins
 * or one address for both written with neither part enabled; writes only on
 * an idle bank and once the set-up has rested; the identity read again
 * after a write is taken; the reads' rates; and the events a read raises --
 * a part not answering, the wrong identity, a stuck bus, a clipped
 * reading -- each once, the second of two after the gap.  What the reads
 * hold, handed to the screens, is test_sense_link_view.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sense_link_fix.h"

/* ---------------------------------------------------------- the version */

TEST_CASE(nothing_is_sent_to_a_4_6_coprocessor)
{
    fresh(6u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    /* A part enabled with an IOmcu up that cannot read it is said at
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

    /* And the same panel on a 4.7 IOmcu reads the page. */
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

/*
 * A set-up nobody published into -- every field 0 -- is no set-up: the
 * panel reads the page and writes nothing, rather than GP0 for both pins
 * and a shunt of 0.  A real one after it is written as usual.
 */
TEST_CASE(a_zeroed_setup_writes_nothing)
{
    outputs_init(&o, 0u);
    sense_page_init(&pg);
    sense_link_init(&sl);
    now = 10000u;
    minor = 7u;
    writes = reads = to_sense = idents = 0u;
    idle = true;
    sense_setup_t zero;
    memset(&zero, 0, sizeof(zero));
    sense_link_want(&sl, &zero, now);
    sense_link_came_up(&sl, minor, now);
    polls(40);
    CHECK_EQ(writes, 0u);
    CHECK_EQ(reads, 1u);              /* the page, read once */
    CHECK_EQ(sense_link_events(&sl), 0u);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], SENSE_DEFAULT_SDA);

    /* Out of range one field at a time is ignored the same way. */
    sense_setup_t w = setup_default();
    w.i228_uohm = 20001u;
    want(&w);
    w = setup_default();
    w.i3221_ch = 0u;
    want(&w);
    polls(20);
    CHECK_EQ(writes, 0u);

    w = setup_default();
    w.i228 = true;
    want(&w);
    polls(12);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
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

    /* Then SENSE's read-only registers at 20Hz and no SERVO_SENSE. */
    const unsigned before = reads;
    polls(20);
    CHECK_EQ(reads - before, 20u);

    /* The INA3221 adds SERVO_SENSE, every poll as well. */
    w.i3221 = true;
    want(&w);
    polls(12);
    const unsigned at = reads;
    polls(40);
    CHECK_EQ(reads - at, 40u + 40u);
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
    /* 20000 uOhm at 300A is 6V across the shunt: no range reads it. */
    w.i228_uohm = 20000u;
    w.i228_max_da = 3000u;
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

    /* An edit asks again: 8.0A puts 160mV across 20000 uOhm. */
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

/* A refused frame's event stands while the value is asked, through the
 * rest and the write of a value stepped back, and is over once the page
 * holds the frame as asked. */
TEST_CASE(a_part_refusal_stands_until_the_page_takes_the_frame)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i228_uohm = 20000u;
    w.i228_max_da = 3000u;                  /* 6V across the shunt */
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_I228_REFUSED);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    polls(40);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    /* Stepped to a range the shunt can read, 8.0A for 160mV: owed,
     * resting, then written. */
    w.i228_max_da = 80u;
    want(&w);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    polls(9);                               /* 450ms of the rest */
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    polls(2);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);
    CHECK_EQ(pg.sense[LINK_SN_I228_MAX_DA], 80u);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    /* Stepped back to the values the page still holds: over at once. */
    w.i228_max_da = 3000u;
    want(&w);
    polls(14);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    w.i228_max_da = 80u;
    want(&w);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    /* Not while the bank drives: the write waits, the refusal stands. */
    w.i3221 = true;
    w.i3221_dmohm = 50u;
    want(&w);
    idle = false;
    polls(20);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_I3221_REFUSED));
    idle = true;
    polls(14);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_I3221_REFUSED));
    /* With no IOmcu answering, a refusal stands. */
    sense_link_lost(&sl);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_I228_REFUSED));
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_BUS_REFUSED));
    CHECK(!sense_link_event_stands(NULL, SENSE_LINK_EV_I228_REFUSED));
}

TEST_CASE(a_pin_refusal_is_over_once_the_page_holds_the_pins_asked)
{
    fresh(7u);
    polls(2);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.sda = 2;                              /* GP3, the heartbeat, is SCL */
    w.scl = 3;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_BUS_REFUSED);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_BUS_REFUSED));
    /* Back to GP16: over once the bus frame is taken. */
    w.sda = 16;
    w.scl = 17;
    want(&w);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_BUS_REFUSED));
    polls(14);
    CHECK_EQ(pg.sense[LINK_SN_SDA_PIN], 16u);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_BUS_REFUSED));
    /* A frame the page refuses when it lets go of nothing: stands. */
    w.sda = 5;
    w.scl = 6;
    want(&w);
    polls(14);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_BUS_REFUSED));
}

TEST_CASE(unset_pins_and_one_address_are_over_with_the_edit)
{
    fresh(9u);
    sense_setup_t w = setup_default();
    w.as5600 = true;
    w.sda = -1;
    want(&w);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_PINS_UNSET));
    w.sda = 16;
    want(&w);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_PINS_UNSET));
    /* Unset pins with nothing enabled ask nothing. */
    w.as5600 = false;
    w.sda = -1;
    want(&w);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_PINS_UNSET));

    w.sda = 16;
    w.i228 = true;
    w.i3221 = true;
    w.i228_addr = 0x40u;
    want(&w);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_SAME_ADDR));
    w.i228_addr = 0x45u;
    want(&w);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_SAME_ADDR));
    /* One part off: two addresses alike are no clash. */
    w.i228_addr = 0x40u;
    w.i3221 = false;
    want(&w);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_SAME_ADDR));
    /* Events not about the set-up are left to the band. */
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_STUCK));
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_ENC_MAGNET));
}

/* A refusal handed out, put back by a band another alert took first, and
 * over before its next turn: not handed out again. */
TEST_CASE(a_refusal_put_back_and_over_is_not_handed_out)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i228_uohm = 20000u;
    w.i228_max_da = 3000u;
    want(&w);
    polls(14);
    CHECK_EQ(sense_link_event(&sl, now), SENSE_LINK_EV_I228_REFUSED);
    w.i228_uohm = 200u;
    w.i228_max_da = 2048u;
    want(&w);
    polls(14);
    sense_link_event_back(&sl, SENSE_LINK_EV_I228_REFUSED);
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS), 0u);
    /* Put back while it stands: handed out again. */
    w.i228_uohm = 20000u;
    w.i228_max_da = 3000u;
    want(&w);
    polls(14);
    const uint32_t t = now + SENSE_LINK_EVENT_GAP_MS;
    CHECK_EQ(sense_link_event(&sl, t), SENSE_LINK_EV_I228_REFUSED);
    sense_link_event_back(&sl, SENSE_LINK_EV_I228_REFUSED);
    CHECK_EQ(sense_link_event(&sl, t + SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_I228_REFUSED);
}

TEST_CASE(one_address_for_both_monitors_keeps_the_encoder_enabled)
{
    fresh(9u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    w.i228_addr = 0x40u;
    w.i3221_addr = 0x40u;
    w.as5600 = true;
    want(&w);
    CHECK_EQ(sense_link_events(&sl), SENSE_LINK_EV_SAME_ADDR);
    polls(14);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], (uint16_t)LINK_SN_EN_AS5600);
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

    /*
     * An edit that puts it right before the band has shown it: the
     * waiting alert goes, and only what still holds of the new request is
     * said.
     */
    w.i228_addr = 0x40u;
    w.i3221_addr = 0x41u;
    want(&w);
    w.i3221_addr = 0x40u;
    want(&w);
    CHECK_EQ(sl.events, SENSE_LINK_EV_SAME_ADDR);
    w.i3221_addr = 0x41u;
    want(&w);
    CHECK_EQ(sl.events, 0u);
    /* Unset pins, then set, the same way. */
    w.scl = -1;
    want(&w);
    CHECK_EQ(sl.events, SENSE_LINK_EV_PINS_UNSET);
    w.scl = 17;
    want(&w);
    CHECK_EQ(sl.events, 0u);
    /* And one the edit does not cure stays, said again. */
    w.scl = -1;
    want(&w);
    w.i228_uohm = 210u;
    want(&w);
    CHECK_EQ(sl.events, SENSE_LINK_EV_PINS_UNSET);
}

/*
 * A refusal waiting when the refused value is corrected: the corrected
 * frame goes through, and the refusal is not said after it.
 */
TEST_CASE(a_waiting_refusal_goes_with_the_value_it_refused)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i228_uohm = 20000u;
    w.i228_max_da = 3000u;
    want(&w);
    polls(14);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I228_REFUSED);
    w.i228_max_da = 80u;
    want(&w);
    CHECK_EQ(sl.events, 0u);
    polls(14);
    CHECK_EQ(sl.events, 0u);
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 20000u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228);

    /* A missing page waiting stays while a part is enabled, and goes when
     * none is. */
    fresh(6u);
    w = setup_default();
    w.i228 = true;
    want(&w);
    CHECK_EQ(sl.events, SENSE_LINK_EV_NO_PAGE);
    w.i228_uohm = 250u;
    want(&w);
    CHECK_EQ(sl.events, SENSE_LINK_EV_NO_PAGE);
    w.i228 = false;
    want(&w);
    CHECK_EQ(sl.events, 0u);
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
    /* An IOmcu that kept the set-up in flash is not written again. */
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
    /* Written at 10500ms, so the wait ends at 13000ms. */
    polls(12);
    far_flags(LINK_SN_BUS_OPEN);
    /* Not while the IOmcu may still be finding it. */
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

/*
 * The band holds one line.  Two parts that stop answering in one read are
 * both said: one event at a time, the next once the first has had the band
 * for SENSE_LINK_EVENT_GAP_MS, and none lost on the way.
 */
TEST_CASE(simultaneous_events_are_handed_out_one_at_a_time)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    want(&w);
    polls(12);
    far_flags(LINK_SN_BUS_OPEN);
    polls((int)(SENSE_LINK_GRACE_MS / 50u) + 2);
    /* Both raised by the same read, neither taken yet. */
    CHECK_EQ(sl.events, (uint16_t)(SENSE_LINK_EV_I228_SILENT
                                   | SENSE_LINK_EV_I3221_SILENT));
    CHECK_EQ(sense_link_event(&sl, now), SENSE_LINK_EV_I228_SILENT);
    CHECK_EQ(sense_link_event(&sl, now + 50u), 0u);
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS - 1u), 0u);
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_I3221_SILENT);
    CHECK_EQ(sense_link_event(&sl, now + 3u * SENSE_LINK_EVENT_GAP_MS), 0u);

    /*
     * One waiting when a new set-up is taken: it was about the old one's
     * address, and shown now it would name the new one.  It goes; a
     * refusal of what is asked now stays.
     */
    polls(5);
    CHECK_EQ(sl.events, 0u);
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I228_ONLINE);
    polls(2);
    far_flags(LINK_SN_BUS_OPEN);
    polls(1);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I228_SILENT);
    w.i228_addr = 0x44u;
    want(&w);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I228_SILENT);
    sl.events |= SENSE_LINK_EV_I3221_REFUSED;   /* of the request now */
    polls(12);
    CHECK_EQ(pg.sense[LINK_SN_I228_ADDR], 0x44u);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I3221_REFUSED);

    /* The most pressing first, whatever order they were raised in. */
    sl.events = (uint16_t)(SENSE_LINK_EV_STORE_OFF | SENSE_LINK_EV_STUCK
                           | SENSE_LINK_EV_NO_PAGE);
    const uint32_t t = now + 10u * SENSE_LINK_EVENT_GAP_MS;
    CHECK_EQ(sense_link_event(&sl, t), SENSE_LINK_EV_NO_PAGE);
    CHECK_EQ(sense_link_event(&sl, t + SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_STUCK);
    CHECK_EQ(sense_link_event(&sl, t + 2u * SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_STORE_OFF);

    /* A lost link drops what its IOmcu said and keeps what the
     * settings say. */
    sl.events = (uint16_t)(SENSE_LINK_EV_I228_SILENT
                           | SENSE_LINK_EV_PINS_UNSET);
    sense_link_lost(&sl);
    CHECK_EQ(sl.events, SENSE_LINK_EV_PINS_UNSET);
    CHECK_EQ(sense_link_event(NULL, t), 0u);
}

/*
 * Two channels that clip in one window -- a synchronised pair -- are each
 * said, one turn of the band each, CH2 then CH3.
 */
TEST_CASE(channels_that_clip_together_are_each_said)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i3221 = true;
    w.i3221_ch = 0x07u;
    want(&w);
    polls(14);
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I3221_ONLINE);
    pg.servo[LINK_SS_CH_FLAGS] = (uint16_t)(LINK_SS_CH_CLIPPED(2)
                                            | LINK_SS_CH_CLIPPED(3));
    polls(4);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_event(&sl, now), SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_clipped_channel(&sl), 2u);
    CHECK_EQ(sense_link_event(&sl, now + 50u), 0u);
    polls(4);                               /* still clipped: no new edge */
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_clipped_channel(&sl), 3u);
    CHECK_EQ(sl.events, 0u);
    CHECK_EQ(sense_link_event(&sl, now + 3u * SENSE_LINK_EVENT_GAP_MS), 0u);

    /* A new set-up drops the ones still waiting. */
    pg.servo[LINK_SS_CH_FLAGS] = 0u;
    polls(4);
    pg.servo[LINK_SS_CH_FLAGS] = (uint16_t)(LINK_SS_CH_CLIPPED(1)
                                            | LINK_SS_CH_CLIPPED(2));
    polls(4);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I3221_CLIPPED);
    w.i3221_dmohm = 500u;
    want(&w);
    polls(14);
    CHECK_EQ(sl.clip_pending, 0u);
    CHECK_EQ(sl.events & SENSE_LINK_EV_I3221_CLIPPED, 0u);
}

/*
 * What an event says is what the read that raised it saw.  A wrong
 * identity keeps the ID it was raised with, a part not answering the
 * address the scan found then; and a part that answers as itself before
 * the band shows either drops it.
 */
TEST_CASE(a_waiting_event_says_what_its_read_saw)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    want(&w);
    polls(14);
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I228_ONLINE | LINK_SN_I3221_ID_WRONG);
    pg.sense[LINK_SN_I3221_ID] = 0x1408u;
    polls(1);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I3221_WRONG);
    /* Another read shows another ID; the event says the first. */
    pg.sense[LINK_SN_I3221_ID] = 0x0000u;
    polls(2);
    CHECK_EQ(sense_link_event_id(&sl, SENSE_LINK_INA3221), 0x1408u);
    CHECK_EQ(sense_link_id(&sl, SENSE_LINK_INA3221), 0x0000u);

    /* The right part answers before the band shows it: the event goes. */
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I228_ONLINE | LINK_SN_I3221_ONLINE);
    polls(1);
    CHECK_EQ(sl.events, 0u);

    /* Not answering, with 0x44 answering in that read's scan. */
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I3221_ONLINE);
    pg.sense[LINK_SN_PRESENT] = (uint16_t)((1u << 0) | (1u << 4));
    polls(1);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I228_SILENT);
    CHECK_EQ(sense_link_event_found(&sl, SENSE_LINK_INA228), 0x44u);
    /* The scan moves on; the event keeps what it found. */
    pg.sense[LINK_SN_PRESENT] = (uint16_t)((1u << 0) | (1u << 7));
    polls(2);
    CHECK_EQ(sense_link_found(&sl, SENSE_LINK_INA228), 0x47u);
    CHECK_EQ(sense_link_event_found(&sl, SENSE_LINK_INA228), 0x44u);
    /* And it answers again before it is shown: dropped. */
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I228_ONLINE | LINK_SN_I3221_ONLINE);
    polls(1);
    CHECK_EQ(sl.events, 0u);
    CHECK_EQ(sense_link_event_id(NULL, SENSE_LINK_INA228), 0u);
    CHECK_EQ(sense_link_event_found(NULL, SENSE_LINK_INA228), 0u);
}

/*
 * An event handed out and replaced on the alert slot before a frame took
 * it -- a NACKed control write's alert in the same pass -- goes back, with
 * what it was raised with, and is handed out again after the gap.  Its
 * read will not raise it again on its own.
 */
TEST_CASE(an_event_that_never_reached_the_band_goes_back)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    w.i3221_ch = 0x07u;
    want(&w);
    polls(14);
    far_flags(LINK_SN_BUS_OPEN | LINK_SN_I3221_ONLINE);
    pg.sense[LINK_SN_PRESENT] = (uint16_t)((1u << 0) | (1u << 4));
    polls((int)(SENSE_LINK_GRACE_MS / 50u) + 2);
    CHECK_EQ(sl.events, SENSE_LINK_EV_I228_SILENT);
    uint32_t ev = sense_link_event(&sl, now);
    CHECK_EQ(ev, SENSE_LINK_EV_I228_SILENT);
    polls(20);
    CHECK_EQ(sl.events, 0u);                  /* told: not raised again */
    sense_link_event_back(&sl, ev);
    CHECK_EQ(sense_link_event(&sl, now), 0u); /* the gap still runs */
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_I228_SILENT);
    CHECK_EQ(sense_link_event_found(&sl, SENSE_LINK_INA228), 0x44u);

    /* A clipped channel goes back ahead of the one still waiting. */
    pg.servo[LINK_SS_CH_FLAGS] = (uint16_t)(LINK_SS_CH_CLIPPED(2)
                                            | LINK_SS_CH_CLIPPED(3));
    polls(4);
    uint32_t t = now + 2u * SENSE_LINK_EVENT_GAP_MS;
    ev = sense_link_event(&sl, t);
    CHECK_EQ(ev, SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_clipped_channel(&sl), 2u);
    sense_link_event_back(&sl, ev);
    t += SENSE_LINK_EVENT_GAP_MS;
    CHECK_EQ(sense_link_event(&sl, t), SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_clipped_channel(&sl), 2u);
    t += SENSE_LINK_EVENT_GAP_MS;
    CHECK_EQ(sense_link_event(&sl, t), SENSE_LINK_EV_I3221_CLIPPED);
    CHECK_EQ(sense_link_clipped_channel(&sl), 3u);
    CHECK_EQ(sl.events, 0u);

    /* Once the link has gone, only what the settings say goes back. */
    sense_link_lost(&sl);
    sense_link_event_back(&sl, SENSE_LINK_EV_I228_SILENT);
    CHECK_EQ(sl.events, 0u);
    sense_link_event_back(&sl, SENSE_LINK_EV_PINS_UNSET);
    CHECK_EQ(sl.events, SENSE_LINK_EV_PINS_UNSET);
    sense_link_event_back(NULL, SENSE_LINK_EV_STUCK);
}

/* The second of two events waits SENSE_LINK_EVENT_GAP_MS from the first
 * and not 1ms less, wherever on the clock the first was handed out. */
static void the_second_event_waits_the_gap(uint32_t t0)
{
    fresh(7u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    want(&w);
    polls(12);
    far_flags(LINK_SN_BUS_OPEN);
    polls((int)(SENSE_LINK_GRACE_MS / 50u) + 2);
    CHECK_EQ(sl.events, (uint16_t)(SENSE_LINK_EV_I228_SILENT
                                   | SENSE_LINK_EV_I3221_SILENT));
    CHECK_EQ(sense_link_event(&sl, t0), SENSE_LINK_EV_I228_SILENT);
    CHECK_EQ(sense_link_event(&sl, t0 + 1u), 0u);
    CHECK_EQ(sense_link_event(&sl, t0 + SENSE_LINK_EVENT_GAP_MS - 1u), 0u);
    CHECK_EQ(sense_link_event(&sl, t0 + SENSE_LINK_EVENT_GAP_MS),
             SENSE_LINK_EV_I3221_SILENT);
}

TEST_CASE(an_event_waits_its_gap_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(the_second_event_waits_the_gap,
                                  SENSE_LINK_EVENT_GAP_MS / 2u);
}

int main(void)
{
    RUN(nothing_is_sent_to_a_4_6_coprocessor);
    RUN(a_coprocessor_that_refuses_the_page_is_left_alone);
    RUN(the_page_is_read_first_and_only_what_differs_is_written);
    RUN(a_zeroed_setup_writes_nothing);
    RUN(enabling_a_part_writes_the_bus_frame_and_reads_identity);
    RUN(a_part_frame_goes_with_both_parts_off);
    RUN(two_parts_swap_addresses_without_meeting);
    RUN(a_refused_frame_is_not_written_again_and_its_part_stays_off);
    RUN(pins_the_page_refuses_are_said_and_leave_the_parts_off);
    RUN(a_part_refusal_stands_until_the_page_takes_the_frame);
    RUN(a_pin_refusal_is_over_once_the_page_holds_the_pins_asked);
    RUN(unset_pins_and_one_address_are_over_with_the_edit);
    RUN(a_refusal_put_back_and_over_is_not_handed_out);
    RUN(one_address_for_both_monitors_keeps_the_encoder_enabled);
    RUN(unset_pins_and_one_address_are_not_written_enabled);
    RUN(a_waiting_refusal_goes_with_the_value_it_refused);
    RUN(writes_wait_for_an_idle_bank);
    RUN(an_unanswered_write_is_written_again);
    RUN(a_lost_link_reads_the_page_again);
    RUN(a_part_that_does_not_answer_is_said_once);
    RUN(another_identity_is_said_once_with_what_it_read);
    RUN(a_stuck_bus_and_clipped_readings_are_said_on_their_edges);
    RUN(a_clip_on_a_channel_not_read_is_not_said);
    RUN(simultaneous_events_are_handed_out_one_at_a_time);
    RUN(channels_that_clip_together_are_each_said);
    RUN(a_waiting_event_says_what_its_read_saw);
    RUN(an_event_that_never_reached_the_band_goes_back);
    RUN(an_event_waits_its_gap_across_the_tick_wrap);
    return test_summary("sense_link");
}
