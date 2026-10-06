/*
 * The SUPPLY link page at the coprocessor.
 *
 * Under test: wiring refused on pins the board or an output holds, on one
 * pin for both, and while the output is on; an ON only with the supply
 * enabled and a live heartbeat; the output off when the heartbeat stops;
 * the pins reserved while held; what the driver last said read back.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
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
    CHECK_EQ(wire(1u, 8u, 9u, 7u), LINK_NACK_BAD_VALUE);    /* no such baud */
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

int main(void)
{
    RUN(wiring_is_refused_on_pins_that_are_not_free);
    RUN(an_on_needs_the_supply_wired_and_a_heartbeat);
    RUN(the_wiring_does_not_change_under_a_live_output);
    RUN(the_wiring_waits_for_an_output_that_may_still_be_on);
    RUN(an_on_needs_its_whole_frame);
    RUN(no_slot_binds_a_held_pin);
    RUN(low_set_points_read_back_as_the_module_takes_them);
    RUN(read_only_registers_and_the_page_end_are_refused);
    RUN(the_step_passes_the_page_to_the_driver_and_back);
    return test_summary("supply_page");
}
