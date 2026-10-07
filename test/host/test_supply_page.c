/*
 * The SUPPLY link page at the coprocessor.
 *
 * Under test: wiring refused on pins the board or an output holds, on one
 * pin for both, and while the output is on; an ON only with the supply
 * enabled and a live heartbeat; the output off when the heartbeat stops;
 * the pins reserved while held; what the driver last said read back; a
 * wiring change from a module that has answered held for a state read,
 * taken on off and refused otherwise.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "out_bind.h"
#include "outputs.h"
#include "pdmini.h"
#include "supply_page.h"

static outputs_t     o;
static supply_page_t pg;

static void fresh(void)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, (uint64_t)1u << 3);   /* the heartbeat */
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    supply_page_init(&pg);
}

static uint8_t wire(uint16_t en, uint16_t tx, uint16_t rx, uint16_t baud)
{
    const uint16_t r[4] = { en, tx, rx, baud };
    return supply_page_write(&pg, LINK_SP_ENABLE, 4u, r, &o, true);
}

static uint8_t command(uint16_t on, uint16_t mv, uint16_t ma, bool beat)
{
    const uint16_t r[3] = { on, mv, ma };
    return supply_page_write(&pg, LINK_SP_OUTPUT, 3u, r, &o, beat);
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    supply_page_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

TEST_CASE(wiring_is_refused_on_pins_that_are_not_free)
{
    fresh();
    CHECK_EQ(wire(1u, 3u, 9u, 1u), LINK_NACK_BAD_VALUE);    /* reserved */
    CHECK_EQ(wire(1u, 4u, 9u, 1u), LINK_NACK_BAD_VALUE);    /* an output's */
    CHECK_EQ(wire(1u, 9u, 9u, 1u), LINK_NACK_BAD_VALUE);    /* both the same */
    CHECK_EQ(wire(1u, 64u, 9u, 1u), LINK_NACK_BAD_VALUE);   /* past the bank */
    CHECK_EQ(wire(1u, 8u, 9u, 8u), LINK_NACK_BAD_VALUE);    /* no such baud */
    CHECK_EQ(wire(2u, 8u, 9u, 1u), LINK_NACK_BAD_VALUE);
    CHECK(!supply_page_enabled(&pg));
    CHECK_EQ(supply_page_pins(&pg), 0u);

    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK(supply_page_enabled(&pg));
    CHECK_EQ(supply_page_tx(&pg), 8u);
    CHECK_EQ(supply_page_rx(&pg), 9u);
    CHECK_EQ(supply_page_pins(&pg), ((uint64_t)1u << 8) | ((uint64_t)1u << 9));
    CHECK_EQ(supply_page_baud(reg(LINK_SP_BAUD)), 19200u);

    /* Its own pins, reserved from the outputs once held, are still its own
     * to write again. */
    outputs_reserve_pins(&o, ((uint64_t)1u << 3) | supply_page_pins(&pg));
    CHECK_EQ(wire(1u, 8u, 9u, 4u), 0u);
    CHECK_EQ(supply_page_baud(reg(LINK_SP_BAUD)), 115200u);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
}

TEST_CASE(wiring_is_refused_on_the_pins_the_module_keeps_or_lacks)
{
    /* The mask the coprocessor boots with, not a hand-made one. */
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, outbind_reserved_mask(
                                 (uint16_t)OUTBIND_BOARD_PICO_HEADER));
    supply_page_init(&pg);

    /* GP23 (power converter MODE), GP24 (VBUS sense), GP25 (LED1), GP29
     * (VSYS sense), and everything the RP2350A lacks. */
    static const uint16_t refused[] = { 23, 24, 25, 29, 30, 31, 32, 40, 47,
                                        48, 63 };
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        CHECK_EQ(wire(1u, refused[i], 4u, 1u), LINK_NACK_BAD_VALUE);
        CHECK_EQ(wire(1u, 4u, refused[i], 1u), LINK_NACK_BAD_VALUE);
        CHECK(!supply_page_enabled(&pg));
        CHECK_EQ(supply_page_pins(&pg), 0u);
    }
    /* The safety line and the CAN pins stay refused too. */
    CHECK_EQ(wire(1u, 3u, 4u, 1u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(wire(1u, 4u, 12u, 1u), LINK_NACK_BAD_VALUE);

    /* Free header pins still take it, the ADC pins among them. */
    CHECK_EQ(wire(1u, 4u, 5u, 1u), 0u);
    CHECK_EQ(wire(1u, 26u, 28u, 1u), 0u);
    CHECK_EQ(supply_page_pins(&pg),
             ((uint64_t)1u << 26) | ((uint64_t)1u << 28));
}

TEST_CASE(an_on_needs_the_supply_wired_and_a_heartbeat)
{
    fresh();
    CHECK_EQ(command(1u, 12000u, 1000u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(1u, 12000u, 1000u, false), LINK_NACK_NOT_ARMED);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(command(1u, 20001u, 1000u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(command(1u, 12000u, 3001u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(command(1u, 12000u, 1000u, true), 0u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 12000u);
    /* An OFF needs nothing. */
    CHECK_EQ(command(0u, 12000u, 1000u, false), 0u);
}

TEST_CASE(the_wiring_does_not_change_under_a_live_output)
{
    fresh();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(1u, 5000u, 500u, true), 0u);
    CHECK_EQ(wire(1u, 10u, 11u, 1u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(wire(0u, 8u, 9u, 1u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);                    /* the same: fine */
    CHECK_EQ(command(0u, 5000u, 500u, true), 0u);
    CHECK_EQ(wire(0u, 8u, 9u, 1u), 0u);
}

/* Nor under one the driver says may still be on, with OFF already asked:
 * read on, an ON not yet confirmed, an OFF owed to a module gone quiet, an
 * ON that went out unsettled. */
TEST_CASE(the_wiring_waits_for_an_output_that_may_still_be_on)
{
    fresh();
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(0u, 5000u, 500u, true), 0u);
    for (int k = 0; k < 4; ++k) {
        drv.st.output  = k == 0;
        drv.en_pending = k == 1;
        drv.en_for     = k == 1;
        drv.off_owed   = k == 2;
        drv.on_sent    = k == 3;
        supply_page_step(&pg, true, &drv);
        CHECK((reg(LINK_SP_FLAGS) & LINK_SP_LIVE) != 0u);
        CHECK_EQ(wire(0u, 8u, 9u, 1u), LINK_NACK_BAD_VALUE);
        CHECK_EQ(wire(1u, 10u, 11u, 1u), LINK_NACK_BAD_VALUE);
    }
    drv.on_sent = false;
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_LIVE, 0u);
    CHECK_EQ(wire(0u, 8u, 9u, 1u), 0u);
    CHECK(!pdmini_may_be_on(NULL));
}

/* An ON comes with its set points in one frame; set points alone, or an
 * OFF alone, may come by themselves. */
TEST_CASE(an_on_needs_its_whole_frame)
{
    fresh();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    const uint16_t on = 1u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_OUTPUT, 1u, &on, &o, true),
             LINK_NACK_BAD_VALUE);
    const uint16_t on_mv[2] = { 1u, 5000u };
    CHECK_EQ(supply_page_write(&pg, LINK_SP_OUTPUT, 2u, on_mv, &o, true),
             LINK_NACK_BAD_VALUE);
    const uint16_t mv = 7000u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_SET_MV, 1u, &mv, &o, true), 0u);
    const uint16_t all[7] = { 1u, 8u, 9u, 1u, 1u, 5000u, 500u };
    CHECK_EQ(supply_page_write(&pg, LINK_SP_ENABLE, 7u, all, &o, true), 0u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    /* Set points alone under a live output, and an OFF alone. */
    CHECK_EQ(supply_page_write(&pg, LINK_SP_SET_MV, 1u, &mv, &o, true), 0u);
    const uint16_t off = 0u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_OUTPUT, 1u, &off, &o, true), 0u);
}

/* No output slot is bound on a pin the supply holds. */
TEST_CASE(no_slot_binds_a_held_pin)
{
    fresh();
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    slots[LINK_OS_DRIVER] = 1u;
    slots[LINK_OS_PIN]    = 8u;
    CHECK_EQ(supply_page_slots_check(&pg, slots), 0u);   /* not wired */
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(supply_page_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    slots[LINK_OS_PIN] = 10u;
    CHECK_EQ(supply_page_slots_check(&pg, slots), 0u);
    slots[LINK_OS_STRIDE + LINK_OS_PIN] = 9u;              /* no driver */
    CHECK_EQ(supply_page_slots_check(&pg, slots), 0u);
    CHECK_EQ(supply_page_slots_check(&pg, NULL), LINK_NACK_BAD_VALUE);
}

/* Set points under the module's least are taken at its least, and read
 * back so. */
TEST_CASE(low_set_points_read_back_as_the_module_takes_them)
{
    fresh();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(0u, 0u, 10u, true), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), PDMINI_V_MIN_MV);
    CHECK_EQ(reg(LINK_SP_SET_MA), PDMINI_I_MIN_MA);
}

/* Wired with no command yet, the driver is asked for the output off and
 * for no set points, so the module's are not overwritten with zeros. */
TEST_CASE(no_set_points_go_out_before_a_command)
{
    fresh();
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    drv.want_output = true;
    supply_page_step(&pg, true, &drv);
    CHECK(!drv.want_output);
    CHECK(!drv.want_set);
    /* An OFF alone names no set points either. */
    const uint16_t off = 0u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_OUTPUT, 1u, &off, &o, true), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 0u);
    supply_page_step(&pg, true, &drv);
    CHECK(!drv.want_set);
    CHECK_EQ(command(0u, 9000u, 800u, true), 0u);
    supply_page_step(&pg, true, &drv);
    CHECK(drv.want_set);
    CHECK_EQ(drv.want_mv, 9000u);
    pdmini_want_off(NULL);
}

/* AUTO steps to the next rate after every WHO_AM_I without a valid
 * answer, wraps after the seventh, and holds the rate once a module has
 * answered; a set rate is used as it is. */
TEST_CASE(auto_baud_tries_each_rate_and_holds_the_one_that_answers)
{
    fresh();
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    CHECK_EQ(wire(1u, 8u, 9u, SUPPLY_BAUD_AUTO), 0u);
    CHECK_EQ(supply_page_rate(&pg, &drv), 1u);         /* 19200 first */
    CHECK_EQ(reg(LINK_SP_BAUD_FOUND), SUPPLY_BAUD_AUTO);
    CHECK_EQ(supply_page_uart_baud(&pg), 19200u);
    for (unsigned k = 0u; k < 7u; ++k) {
        ++drv.who_failed;
        CHECK_EQ(supply_page_rate(&pg, &drv), (uint8_t)((2u + k) % 7u));
        CHECK_EQ(supply_page_rate(&pg, &drv), (uint8_t)((2u + k) % 7u));
    }
    CHECK_EQ(supply_page_rate(&pg, NULL), 1u);          /* left as it is */
    ++drv.who_failed;                                   /* now 38400 */
    CHECK_EQ(supply_page_rate(&pg, &drv), 2u);
    drv.answered = true;
    ++drv.who_failed;                                   /* found: held */
    CHECK_EQ(supply_page_rate(&pg, &drv), 2u);
    CHECK_EQ(reg(LINK_SP_BAUD_FOUND), 2u);
    CHECK_EQ(supply_page_uart_baud(&pg), 38400u);

    /* New wiring looks again from 19200; a set rate is the rate. */
    pdmini_init(&drv, NULL, 0u);
    CHECK_EQ(wire(1u, 10u, 11u, SUPPLY_BAUD_AUTO), 0u);
    CHECK_EQ(supply_page_rate(&pg, &drv), 1u);
    CHECK_EQ(wire(1u, 10u, 11u, 4u), 0u);
    CHECK_EQ(supply_page_rate(&pg, &drv), 4u);
    CHECK_EQ(reg(LINK_SP_BAUD_FOUND), 4u);
    CHECK_EQ(wire(1u, 10u, 11u, 8u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(supply_page_rate(NULL, &drv), 1u);
    CHECK_EQ(supply_page_uart_baud(NULL), 0u);
}

/* RESET is taken alone, as 1, with the output asked off and the supply
 * wired, and passed to the driver at the next step. */
TEST_CASE(reset_is_taken_alone_with_the_output_off)
{
    fresh();
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    const uint16_t one = 1u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_RESET, 1u, &one, &o, true),
             LINK_NACK_BAD_VALUE);                     /* not wired */
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    const uint16_t two = 2u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_RESET, 1u, &two, &o, true),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(command(1u, 5000u, 500u, true), 0u);
    CHECK_EQ(supply_page_write(&pg, LINK_SP_RESET, 1u, &one, &o, true),
             LINK_NACK_BAD_VALUE);                     /* output asked on */
    CHECK_EQ(command(0u, 5000u, 500u, true), 0u);
    CHECK_EQ(supply_page_write(&pg, LINK_SP_RESET, 1u, &one, &o, true), 0u);
    CHECK(!drv.reset_owed);
    supply_page_step(&pg, true, &drv);
    CHECK(drv.reset_owed);
    CHECK_EQ(reg(LINK_SP_RESET), 0u);
}

TEST_CASE(read_only_registers_and_the_page_end_are_refused)
{
    fresh();
    const uint16_t v[2] = { 0u, 0u };
    CHECK_EQ(supply_page_write(&pg, LINK_SP_FLAGS, 1u, v, &o, true),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(supply_page_write(&pg, LINK_SP_SET_MA, 2u, v, &o, true),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(supply_page_write(&pg, LINK_SP_COUNT - 1u, 2u, v, &o, true),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(supply_page_write(NULL, 0u, 1u, v, &o, true), LINK_NACK_BAD_RANGE);
}

/* The driver is told what is asked; without a heartbeat the output goes
 * off; what the driver last said is on the page. */
TEST_CASE(the_step_passes_the_page_to_the_driver_and_back)
{
    fresh();
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(reg(LINK_SP_FLAGS), 0u);                  /* not wired: nothing */

    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(1u, 9000u, 800u, true), 0u);
    drv.st.online = true;
    drv.st.output = true;
    drv.st.mode   = PDMINI_MODE_CC;
    drv.st.v_mv   = 8990u;
    drv.st.i_ma   = 790u;
    drv.st.set_mv = 9000u;
    drv.st.set_ma = 800u;
    drv.st.in_state = 5u;
    drv.st.vin_mv   = 20000u;
    drv.st.samples  = 70000u;
    drv.st.errors   = 2u;
    supply_page_step(&pg, true, &drv);
    CHECK(drv.want_output);
    CHECK_EQ(drv.want_mv, 9000u);
    CHECK_EQ(drv.want_ma, 800u);
    const uint16_t f = reg(LINK_SP_FLAGS);
    CHECK((f & LINK_SP_ONLINE) != 0u);
    CHECK((f & LINK_SP_ON) != 0u);
    CHECK_EQ(LINK_SP_MODE(f), PDMINI_MODE_CC);
    CHECK_EQ(f & (LINK_SP_STUCK | LINK_SP_SET_STUCK), 0u);
    CHECK_EQ(reg(LINK_SP_V_MV), 8990u);
    CHECK_EQ(reg(LINK_SP_I_MA), 790u);
    CHECK_EQ(reg(LINK_SP_SET_MV_RB), 9000u);
    CHECK_EQ(reg(LINK_SP_IN_STATE), 5u);
    CHECK_EQ(reg(LINK_SP_VIN_MV), 20000u);
    CHECK_EQ(reg(LINK_SP_SAMPLES), (uint16_t)70000u);
    CHECK_EQ(reg(LINK_SP_ERRORS), 2u);

    drv.st.tripped = true;                     /* switched off by the module */
    supply_page_step(&pg, true, &drv);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_TRIPPED) != 0u);
    drv.st.tripped = false;
    drv.st.stuck = true;                       /* the output would not switch */
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(reg(LINK_SP_FLAGS) & (LINK_SP_STUCK | LINK_SP_SET_STUCK),
             LINK_SP_STUCK);
    drv.st.stuck     = false;                  /* the set points would not take */
    drv.st.set_stuck = true;
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(reg(LINK_SP_FLAGS) & (LINK_SP_STUCK | LINK_SP_SET_STUCK),
             LINK_SP_SET_STUCK);

    supply_page_step(&pg, false, &drv);                 /* the panel died */
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK(!drv.want_output);
}

/* A driver whose module has answered, told what its state read shows. */
static pdmini_t answered_driver(void)
{
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    drv.answered    = true;
    drv.identified  = true;
    drv.state_known = true;
    return drv;
}

/* Held for its read: acknowledged, the wiring in force read back, bit 8
 * set at once; the read asked at the next step; taken on off. */
TEST_CASE(a_wiring_change_waits_for_a_read_of_an_answered_module)
{
    fresh();
    pdmini_t drv = answered_driver();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);         /* nothing answered yet */
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);
    supply_page_step(&pg, true, &drv);
    CHECK(pg.answered);
    CHECK_EQ(wire(1u, 10u, 11u, 1u), 0u);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_WAIT) != 0u);
    uint16_t w[4];
    CHECK(!supply_page_wire_ready(&pg, w));
    CHECK_EQ(supply_page_wire_write(&pg, &o), LINK_NACK_BAD_VALUE);
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(pdmini_checked(&drv), PDMINI_CHECK_ASKED);
    /* An ON while it waits is refused, and so is the wiring with a
     * command in one frame. */
    CHECK_EQ(command(1u, 5000u, 500u, true), LINK_NACK_BAD_VALUE);
    const uint16_t both[7] = { 1u, 12u, 13u, 1u, 0u, 5000u, 500u };
    CHECK_EQ(supply_page_write(&pg, LINK_SP_ENABLE, 7u, both, &o, true),
             LINK_NACK_BAD_VALUE);
    drv.check = (uint8_t)PDMINI_CHECK_OFF;
    supply_page_step(&pg, true, &drv);
    CHECK(supply_page_wire_ready(&pg, w));
    CHECK_EQ(w[1], 10u);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_WAIT) != 0u);
    supply_page_follow(&pg, &drv);
    CHECK_EQ(supply_page_wire_write(&pg, &o), 0u);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 10u);
    CHECK_EQ(reg(LINK_SP_FLAGS) & (LINK_SP_WIRE_WAIT | LINK_SP_WIRE_REFUSED),
             0u);
    CHECK(!supply_page_wire_ready(NULL, w));
    CHECK(!supply_page_wire_ready(&pg, NULL));
}

/* Refused on a read that does not show it off, bit 9 until the next wiring
 * write; another change replaces the one waiting and is read for again;
 * the wiring in force written drops it. */
TEST_CASE(a_wiring_change_not_read_off_is_refused)
{
    fresh();
    pdmini_t drv = answered_driver();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(wire(1u, 10u, 11u, 1u), 0u);
    supply_page_step(&pg, true, &drv);
    drv.check = (uint8_t)PDMINI_CHECK_NOT_OFF;
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);
    CHECK_EQ(reg(LINK_SP_FLAGS) & (LINK_SP_WIRE_WAIT | LINK_SP_WIRE_REFUSED),
             LINK_SP_WIRE_REFUSED);
    supply_page_step(&pg, true, &drv);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_REFUSED) != 0u);   /* kept */

    CHECK_EQ(wire(1u, 10u, 11u, 1u), 0u);       /* again: read again */
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_WIRE_REFUSED, 0u);
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(pdmini_checked(&drv), PDMINI_CHECK_ASKED);
    CHECK_EQ(wire(1u, 12u, 13u, 1u), 0u);       /* replaced */
    supply_page_step(&pg, true, &drv);
    drv.check = (uint8_t)PDMINI_CHECK_OFF;
    supply_page_step(&pg, true, &drv);
    uint16_t w[4];
    CHECK(supply_page_wire_ready(&pg, w));
    CHECK_EQ(w[1], 12u);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);         /* the wiring in force */
    CHECK(!supply_page_wire_ready(&pg, w));
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_WIRE_WAIT, 0u);
}

/* The ready wiring is judged again as it is written: pins an output took
 * since, or an output that may be on, refuse it.  A driver started afresh
 * is asked again; no driver at all, and it is ready. */
TEST_CASE(a_ready_wiring_is_judged_again_when_written)
{
    fresh();
    pdmini_t drv = answered_driver();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(wire(1u, 10u, 11u, 1u), 0u);
    supply_page_step(&pg, true, &drv);
    drv.check = (uint8_t)PDMINI_CHECK_OFF;
    supply_page_step(&pg, true, &drv);
    drv.st.output = true;                       /* on again since */
    supply_page_follow(&pg, &drv);
    CHECK_EQ(supply_page_wire_write(&pg, &o), LINK_NACK_BAD_VALUE);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_REFUSED) != 0u);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);

    drv.st.output = false;
    supply_page_follow(&pg, &drv);
    CHECK_EQ(wire(1u, 10u, 11u, 1u), 0u);
    supply_page_step(&pg, true, &drv);
    pdmini_init(&drv, NULL, 0u);                /* rewired: forgotten */
    drv.answered    = true;
    drv.state_known = true;
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(pdmini_checked(&drv), PDMINI_CHECK_ASKED);
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 1,
                             .channels = 1, .pin = 10, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 1, &pwm));      /* pin 10 taken */
    drv.check = (uint8_t)PDMINI_CHECK_OFF;
    supply_page_step(&pg, true, &drv);
    supply_page_follow(&pg, &drv);
    CHECK_EQ(supply_page_wire_write(&pg, &o), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);

    CHECK_EQ(wire(1u, 12u, 13u, 1u), 0u);
    supply_page_step(&pg, true, NULL);          /* no driver: ready */
    uint16_t w[4];
    CHECK(supply_page_wire_ready(&pg, w));
    supply_page_wire_refuse(&pg);               /* no UART for them */
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_REFUSED) != 0u);
    supply_page_wire_refuse(NULL);
    supply_page_follow(NULL, &drv);
}

/* The driver's sag cut is on the page as bit 10. */
TEST_CASE(a_sag_cut_is_on_the_page)
{
    fresh();
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(1u, 6000u, 1000u, true), 0u);
    drv.st.sagged = true;
    supply_page_step(&pg, true, &drv);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_SAGGED) != 0u);
    drv.st.sagged = false;
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_SAGGED, 0u);
}

int main(void)
{
    RUN(wiring_is_refused_on_pins_that_are_not_free);
    RUN(wiring_is_refused_on_the_pins_the_module_keeps_or_lacks);
    RUN(an_on_needs_the_supply_wired_and_a_heartbeat);
    RUN(the_wiring_does_not_change_under_a_live_output);
    RUN(the_wiring_waits_for_an_output_that_may_still_be_on);
    RUN(an_on_needs_its_whole_frame);
    RUN(no_slot_binds_a_held_pin);
    RUN(low_set_points_read_back_as_the_module_takes_them);
    RUN(no_set_points_go_out_before_a_command);
    RUN(auto_baud_tries_each_rate_and_holds_the_one_that_answers);
    RUN(reset_is_taken_alone_with_the_output_off);
    RUN(read_only_registers_and_the_page_end_are_refused);
    RUN(the_step_passes_the_page_to_the_driver_and_back);
    RUN(a_wiring_change_waits_for_a_read_of_an_answered_module);
    RUN(a_wiring_change_not_read_off_is_refused);
    RUN(a_ready_wiring_is_judged_again_when_written);
    RUN(a_sag_cut_is_on_the_page);
    return test_summary("supply_page");
}
