/*
 * The SUPPLY link page at the IOmcu.
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

TEST_CASE(a_frozen_wiring_takes_no_write)
{
    fresh();
    supply_page_wire_freeze(&pg, true);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), LINK_NACK_BUSY);
    CHECK(!supply_page_enabled(&pg));
    /* The set points are no wiring. */
    CHECK_EQ(command(0u, 5000u, 500u, true), 0u);
    /* Reaches into the wiring from its last register. */
    const uint16_t r[2] = { 9600u, 0u };

    CHECK_EQ(supply_page_write(&pg, LINK_SP_BAUD, 2u, r, &o, true), LINK_NACK_BUSY);
    supply_page_wire_freeze(&pg, false);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    supply_page_wire_freeze(NULL, true);
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
    /* The mask the IOmcu boots with, not a hand-made one. */
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
/* A panel restarted in a hold writes the wiring it has saved: a refusal of the
 * same pins would stand on its side until new ones. */
TEST_CASE(a_frozen_wiring_takes_a_write_that_changes_nothing)
{
    fresh();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    supply_page_wire_freeze(&pg, true);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK(supply_page_enabled(&pg));
    /* Each register counts. */
    CHECK_EQ(wire(0u, 8u, 9u, 1u), LINK_NACK_BUSY);
    CHECK_EQ(wire(1u, 7u, 9u, 1u), LINK_NACK_BUSY);
    CHECK_EQ(wire(1u, 8u, 7u, 1u), LINK_NACK_BUSY);
    CHECK_EQ(wire(1u, 8u, 9u, 2u), LINK_NACK_BUSY);
    /* A frame on into OUTPUT: the wiring as held, the command by its own rules. */
    const uint16_t r[2] = { 1u, 0u };

    CHECK_EQ(supply_page_write(&pg, LINK_SP_BAUD, 2u, r, &o, true), 0u);
    CHECK_EQ(reg(LINK_SP_BAUD), 1u);
    /* A change waiting for its read is not the wiring in force. */
    supply_page_wire_freeze(&pg, false);
    pdmini_t drv = answered_driver();

    supply_page_follow(&pg, &drv);
    CHECK_EQ(wire(1u, 10u, 11u, 1u), 0u);
    supply_page_wire_freeze(&pg, true);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), LINK_NACK_BUSY);
}

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

/* The supply as a programming port sees it: answering only while wired
 * with no wiring change waiting; on only when read on at no more than
 * asked with nothing stuck; off only when read off and not maybe on. */
TEST_CASE(a_programming_port_sees_the_supply_as_the_page_last_read_it)
{
    supply_port_view_t v;

    fresh();
    pdmini_t drv = answered_driver();
    memset(&v, 0xA5, sizeof(v));
    supply_page_port_view(&pg, &v);
    CHECK(!v.online);
    CHECK(!v.on);
    CHECK(!v.asked_on);
    CHECK(v.off);                               /* nothing read, nothing on */
    supply_page_port_view(NULL, &v);
    CHECK(!v.online);
    CHECK(!v.off);
    supply_page_port_view(&pg, NULL);

    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    drv.st.online = true;
    drv.st.vin_mv = 9000u;
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(v.online);
    CHECK(v.off);
    CHECK(!v.on);
    CHECK_EQ(v.vin_mv, 9000u);
    /* Asked on and not read on yet: neither. */
    CHECK_EQ(command(1u, 5000u, 1000u, true), 0u);
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(v.asked_on);
    CHECK_EQ(v.set_mv, 5000u);
    CHECK_EQ(v.set_ma, 1000u);
    CHECK(!v.on);
    /* Read on at the old, higher voltage: not on for the port. */
    drv.st.output = true;
    drv.st.set_mv = 5001u;
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(!v.on);
    CHECK(!v.off);
    drv.st.set_mv = 0u;                         /* not read back yet */
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(!v.on);
    drv.st.set_mv = 5000u;
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(v.on);
    /* Stuck either way: not on. */
    drv.st.stuck = true;
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(!v.on);
    drv.st.stuck = false;
    drv.st.set_stuck = true;
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(!v.on);
    drv.st.set_stuck = false;
    /* Asked off and still read on: neither on nor off. */
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, true));
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(!v.asked_on);
    CHECK(!v.on);
    CHECK(!v.off);
    CHECK_EQ(v.set_mv, 5000u);                  /* an OFF leaves them */
    drv.st.output = false;
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(v.off);
    /* Not answering. */
    drv.st.online = false;
    supply_page_step(&pg, true, &drv);
    supply_page_port_view(&pg, &v);
    CHECK(!v.online);
    drv.st.online = true;
    supply_page_step(&pg, true, &drv);
    /* A wiring change waiting: the module is not the port's to command. */
    CHECK_EQ(wire(1u, 10u, 11u, 1u), 0u);
    supply_page_port_view(&pg, &v);
    CHECK(!v.online);
    CHECK(!supply_page_port_set(&pg, true, 5000u, 1000u, true));
}

/* A port's request: an OFF always, an ON at the module's limits and never
 * above the voltage of an output that is on. */
TEST_CASE(a_programming_port_switches_the_supply_and_never_raises_it)
{
    fresh();
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    /* Not wired. */
    CHECK(!supply_page_port_set(&pg, true, 5000u, 1000u, true));
    CHECK(supply_page_port_set(&pg, false, 5000u, 1000u, true));
    CHECK(!supply_page_port_set(NULL, false, 0u, 0u, true));
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    /* No heartbeat. */
    CHECK(!supply_page_port_set(&pg, true, 5000u, 1000u, false));
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 0u);
    /* The module's range, at both ends of both. */
    CHECK(!supply_page_port_set(&pg, true, PDMINI_V_MIN_MV - 1u, 1000u, true));
    CHECK(!supply_page_port_set(&pg, true, PDMINI_V_MAX_MV + 1u, 1000u, true));
    CHECK(!supply_page_port_set(&pg, true, 5000u, PDMINI_I_MIN_MA - 1u, true));
    CHECK(!supply_page_port_set(&pg, true, 5000u, PDMINI_I_MAX_MA + 1u, true));
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK(supply_page_port_set(&pg, true, PDMINI_V_MIN_MV, PDMINI_I_MIN_MA,
                               true));
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, true));
    CHECK(supply_page_port_set(&pg, true, PDMINI_V_MAX_MV, PDMINI_I_MAX_MA,
                               true));
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK_EQ(reg(LINK_SP_SET_MV), PDMINI_V_MAX_MV);
    /* It reaches the driver as a link write does. */
    supply_page_step(&pg, true, &drv);
    CHECK(drv.want_output);
    CHECK_EQ(drv.want_mv, PDMINI_V_MAX_MV);
    CHECK_EQ(drv.want_ma, PDMINI_I_MAX_MA);
    /* On: down and the same are taken, one millivolt up is not. */
    CHECK(supply_page_port_set(&pg, true, 6000u, 1000u, true));
    CHECK(supply_page_port_set(&pg, true, 6000u, 1200u, true));
    CHECK(!supply_page_port_set(&pg, true, 6001u, 1000u, true));
    CHECK_EQ(reg(LINK_SP_SET_MV), 6000u);
    CHECK_EQ(reg(LINK_SP_SET_MA), 1200u);
    /* Off without a heartbeat too, and then on at any voltage in range. */
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, false));
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 6000u);
    CHECK(supply_page_port_set(&pg, true, 7400u, 1000u, true));
    CHECK_EQ(reg(LINK_SP_SET_MV), 7400u);
}

/* While a port holds the supply the panel can switch it off, and nothing
 * else: no set point moves, up or down; FLAGS bit 11 says so at once. */
TEST_CASE(a_held_supply_takes_off_from_the_panel_only)
{
    fresh();
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(1u, 5000u, 1000u, true), 0u);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_PORT, 0u);
    supply_page_hold(&pg, true);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_PORT, LINK_SP_PORT);
    supply_page_hold(&pg, true);
    /* On and lower, on and higher, a current limit lower or higher: all
     * refused, and the page keeps what it had. */
    CHECK_EQ(command(1u, 4000u, 1000u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(command(1u, 6000u, 1000u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(command(1u, 5000u, 999u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(command(1u, 5000u, 1001u, true), LINK_NACK_BAD_VALUE);
    const uint16_t ma_only = 3000u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_SET_MA, 1u, &ma_only, &o, true),
             LINK_NACK_BAD_VALUE);
    const uint16_t mv_only = 4900u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_SET_MV, 1u, &mv_only, &o, true),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 5000u);
    CHECK_EQ(reg(LINK_SP_SET_MA), 1000u);
    /* On as it is changes nothing and is taken. */
    CHECK_EQ(command(1u, 5000u, 1000u, true), 0u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    /* An OFF is taken whatever set points come with it, and they stay. */
    CHECK_EQ(command(0u, 8000u, 3000u, true), 0u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 5000u);
    CHECK_EQ(reg(LINK_SP_SET_MA), 1000u);
    CHECK_EQ(command(0u, 4000u, 500u, true), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 5000u);
    CHECK_EQ(reg(LINK_SP_SET_MA), 1000u);
    const uint16_t off = 0u;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_OUTPUT, 1u, &off, &o, true), 0u);
    /* Off, it is not switched on again, as it was or otherwise. */
    CHECK_EQ(command(1u, 5000u, 1000u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(command(1u, 4000u, 1000u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    /* The port itself is not held by its own hold; but after the panel's
     * OFF it cannot switch the supply on again until it lets go (4.31). */
    CHECK(!supply_page_port_set(&pg, true, 5000u, 1000u, true));
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    supply_port_view_t seen;

    supply_page_port_view(&pg, &seen);
    CHECK(seen.latched);
    supply_page_hold(&pg, false);
    supply_page_hold(&pg, true);
    supply_page_port_view(&pg, &seen);
    CHECK(!seen.latched);
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    /* The bit survives a step with and without a driver. */
    pdmini_t drv;
    pdmini_init(&drv, NULL, 0u);
    supply_page_step(&pg, true, &drv);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_PORT, LINK_SP_PORT);
    supply_page_step(&pg, true, NULL);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_PORT, LINK_SP_PORT);
    /* Let go: the panel commands again. */
    supply_page_hold(&pg, false);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_PORT, 0u);
    CHECK_EQ(command(1u, 6000u, 1000u, true), 0u);
    supply_page_hold(NULL, true);
}

/* A page that is wired, on at 5.0V, and stepped once at @p t0. */
static pdmini_t on_at(uint32_t t0)
{
    pdmini_t drv;

    fresh();
    pdmini_init(&drv, NULL, 0u);
    CHECK_EQ(wire(1u, 8u, 9u, 1u), 0u);
    CHECK_EQ(command(1u, 5000u, 1000u, true), 0u);
    supply_page_port_frame(&pg, false, t0);
    supply_page_step(&pg, true, &drv);
    CHECK(drv.want_output);
    return drv;
}

/* An OFF that finds a frame of the programming port on the wire reaches
 * the driver when the frame has ended; OUTPUT reads 0 from the start. */
TEST_CASE(an_off_waits_for_the_end_of_a_frame)
{
    /* The clock's wrap is in the second pass. */
    static const uint32_t k_t0[2] = { 1000u, 0xFFFFFFFAu };

    for (unsigned w = 0; w < 2u; ++w) {
        const uint32_t t0 = k_t0[w];
        pdmini_t drv = on_at(t0);

        /* No frame out: off at once. */
        CHECK_EQ(command(0u, 5000u, 1000u, true), 0u);
        supply_page_port_frame(&pg, false, t0 + 1u);
        supply_page_step(&pg, true, &drv);
        CHECK(!drv.want_output);

        /* A frame out: the driver stays on until the frame is over. */
        drv = on_at(t0);
        supply_page_port_frame(&pg, true, t0 + 1u);
        supply_page_step(&pg, true, &drv);
        CHECK_EQ(command(0u, 5000u, 1000u, true), 0u);
        CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
        for (uint32_t d = 2u; d < 6u; ++d) {
            supply_page_port_frame(&pg, true, t0 + d);
            supply_page_step(&pg, true, &drv);
            CHECK(drv.want_output);
            CHECK_EQ(drv.want_mv, 5000u);
            CHECK_EQ(drv.want_ma, 1000u);
            CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
        }
        supply_page_port_frame(&pg, false, t0 + 6u);
        supply_page_step(&pg, true, &drv);
        CHECK(!drv.want_output);
        /* A frame after it does not switch it on again. */
        supply_page_port_frame(&pg, true, t0 + 7u);
        supply_page_step(&pg, true, &drv);
        CHECK(!drv.want_output);
    }
}

/* The wait is bounded: a frame that does not end holds the OFF for
 * SUPPLY_PAGE_OFF_WAIT_MS and no longer. */
TEST_CASE(an_off_waits_no_longer_than_its_bound)
{
    static const uint32_t k_t0[2] = { 1000u, 0xFFFFFFFAu };

    for (unsigned w = 0; w < 2u; ++w) {
        const uint32_t t0 = k_t0[w];
        pdmini_t drv = on_at(t0);

        supply_page_port_frame(&pg, true, t0);
        CHECK_EQ(command(0u, 5000u, 1000u, true), 0u);
        supply_page_step(&pg, true, &drv);
        CHECK(drv.want_output);
        supply_page_port_frame(&pg, true,
                               t0 + SUPPLY_PAGE_OFF_WAIT_MS - 1u);
        supply_page_step(&pg, true, &drv);
        CHECK(drv.want_output);
        supply_page_port_frame(&pg, true, t0 + SUPPLY_PAGE_OFF_WAIT_MS);
        supply_page_step(&pg, true, &drv);
        CHECK(!drv.want_output);
        supply_page_port_frame(&pg, true,
                               t0 + SUPPLY_PAGE_OFF_WAIT_MS + 1u);
        supply_page_step(&pg, true, &drv);
        CHECK(!drv.want_output);
    }
}

/* The heartbeat lost is an OFF like the panel's: it waits for the frame
 * too, and an ON written while it waits ends the wait. */
TEST_CASE(a_lost_heartbeat_waits_for_the_end_of_a_frame)
{
    pdmini_t drv = on_at(50u);

    supply_page_port_frame(&pg, true, 51u);
    supply_page_step(&pg, false, &drv);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK(drv.want_output);
    supply_page_port_frame(&pg, true, 52u);
    supply_page_step(&pg, false, &drv);
    CHECK(drv.want_output);
    supply_page_port_frame(&pg, false, 53u);
    supply_page_step(&pg, false, &drv);
    CHECK(!drv.want_output);

    /* Switched on again while an OFF waits: on, and no late off. */
    drv = on_at(50u);
    supply_page_port_frame(&pg, true, 51u);
    CHECK_EQ(command(0u, 5000u, 1000u, true), 0u);
    supply_page_step(&pg, true, &drv);
    CHECK(drv.want_output);
    CHECK_EQ(command(1u, 5000u, 1000u, true), 0u);
    supply_page_port_frame(&pg, false, 52u);
    supply_page_step(&pg, true, &drv);
    CHECK(drv.want_output);
    supply_page_port_frame(&pg, false, 70u);
    supply_page_step(&pg, true, &drv);
    CHECK(drv.want_output);
    supply_page_port_frame(NULL, true, 0u);
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
    RUN(a_programming_port_sees_the_supply_as_the_page_last_read_it);
    RUN(a_programming_port_switches_the_supply_and_never_raises_it);
    RUN(a_held_supply_takes_off_from_the_panel_only);
    RUN(an_off_waits_for_the_end_of_a_frame);
    RUN(a_frozen_wiring_takes_no_write);
    RUN(a_frozen_wiring_takes_a_write_that_changes_nothing);
    RUN(an_off_waits_no_longer_than_its_bound);
    RUN(a_lost_heartbeat_waits_for_the_end_of_a_frame);
    return test_summary("supply_page");
}
