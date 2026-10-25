/*
 * The IOmcu's sensor bus wire (firmware/iomcu/src/sense_i2c.c) over a
 * modelled pair of pads (sense_i2c_stub/pico/stdlib.h).
 *
 * Under test: what a transaction comes to when the lines are low at idle.
 * Pins nothing is wired to -- no part, no pull-up -- read low; a line a
 * part holds reads low too.  The pads' own pull-ups tell them apart: a
 * line that rises on them within SENSE_I2C_PROBE_US is held by nothing,
 * and the transaction is a NACK with nothing sent, so the bus is not
 * marked stuck and no recovery runs.  One that does not rise is
 * SENSE_BUS_LOW.  The pulls are off again either way, at the limit of the
 * probe and past it.  A bus with its pull-ups sends as before.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "hardware/gpio.h"
#include "sense_bus.h"
#include "sense_i2c.h"

fake_pin_t fake_pins[NUM_BANK0_GPIOS];
uint64_t   fake_us;
unsigned   fake_wire;
int        fake_rc;
i2c_inst_t fake_i2c[2];

#define SDA 14u
#define SCL 15u

/* GP14 and GP15 as the bench found them: a probe lead on each, no part
 * and no pull-up; the pads raise them in 4us. */
static void unwired(void)
{
    memset(fake_pins, 0, sizeof(fake_pins));
    fake_us = 1000u;
    fake_wire = 0u;
    fake_rc = 0;
    fake_pins[SDA].pad_rise_us = 4u;
    fake_pins[SCL].pad_rise_us = 4u;
    CHECK(sense_i2c_open((uint8_t)SDA, (uint8_t)SCL));
}

/* The same pins with the modules' pull-ups on them. */
static void wired(void)
{
    unwired();
    fake_pins[SDA].ext_pullup = true;
    fake_pins[SCL].ext_pullup = true;
}

static bool pulls_off(void)
{
    return !fake_pins[SDA].pad_up && !fake_pins[SDA].pad_down
           && !fake_pins[SCL].pad_up && !fake_pins[SCL].pad_down;
}

TEST_CASE(pins_nothing_is_wired_to_read_as_nothing_answering)
{
    unwired();
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_NACK);
    uint8_t buf[2];
    CHECK_EQ(sense_i2c_read(NULL, 0x45u, 0x3Eu, buf, sizeof(buf)),
             SENSE_NACK);
    CHECK_EQ(sense_i2c_write(NULL, 0x45u, 0x00u, buf, sizeof(buf)),
             SENSE_NACK);
    CHECK_EQ(fake_wire, 0u);                 /* nothing was sent */
    CHECK(pulls_off());
    sense_i2c_close();
}

TEST_CASE(nothing_wired_does_not_mark_the_bus_stuck)
{
    unwired();
    sense_bus_t bus;
    const sense_i2c_t io = { sense_i2c_read, sense_i2c_write, NULL };
    sense_bus_init(&bus, &io);
    uint8_t buf[2];
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(sense_bus_read(&bus, 0x45u, 0x3Eu, buf, sizeof(buf)),
                 SENSE_NACK);
        sense_bus_note(&bus, sense_i2c_ask(0x40u));
    }
    CHECK(!bus.stuck);
    CHECK(!sense_bus_recovery_due(&bus, 2000u));
    sense_i2c_close();
}

TEST_CASE(a_line_a_part_holds_low_is_the_bus_held_low)
{
    /* With the pull-ups: SDA held. */
    wired();
    fake_pins[SDA].held_low = true;
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_BUS_LOW);
    CHECK(pulls_off());
    /* SCL held. */
    fake_pins[SDA].held_low = false;
    fake_pins[SCL].held_low = true;
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_BUS_LOW);
    CHECK(pulls_off());
    /* Without them: SCL rises on its pad, SDA is held. */
    unwired();
    fake_pins[SDA].held_low = true;
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_BUS_LOW);
    CHECK_EQ(fake_wire, 0u);
    CHECK(pulls_off());
    sense_i2c_close();
}

TEST_CASE(the_pad_pull_up_has_the_probe_time_and_no_more)
{
    /* Rising exactly at SENSE_I2C_PROBE_US: held by nothing. */
    unwired();
    fake_pins[SDA].pad_rise_us = SENSE_I2C_PROBE_US;
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_NACK);
    CHECK(pulls_off());
    /* A microsecond later, or never: held. */
    unwired();
    fake_pins[SCL].pad_rise_us = SENSE_I2C_PROBE_US + 1u;
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_BUS_LOW);
    CHECK(pulls_off());
    unwired();
    fake_pins[SDA].pad_rise_us = UINT32_MAX;
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_BUS_LOW);
    CHECK(pulls_off());
    sense_i2c_close();
}

TEST_CASE(a_bus_with_its_pull_ups_sends_as_before)
{
    wired();
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_OK);
    CHECK_EQ(fake_wire, 1u);
    CHECK(!fake_pins[SDA].pad_up && !fake_pins[SCL].pad_up);
    /* An address nobody acknowledges: the lines are back high. */
    fake_rc = PICO_ERROR_GENERIC;
    CHECK_EQ(sense_i2c_ask(0x41u), SENSE_NACK);
    fake_rc = PICO_ERROR_TIMEOUT;
    CHECK_EQ(sense_i2c_ask(0x41u), SENSE_TIMEOUT);
    /* A failed transaction with SDA held after it. */
    fake_rc = PICO_ERROR_GENERIC;
    fake_pins[SDA].held_low = true;
    CHECK_EQ(sense_i2c_ask(0x41u), SENSE_BUS_LOW);
    sense_i2c_close();
}

TEST_CASE(a_closed_bus_and_a_pair_that_is_not_one_send_nothing)
{
    unwired();
    sense_i2c_close();
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_BAD_ARG);
    CHECK(!sense_i2c_open(15u, 16u));
    CHECK_EQ(sense_i2c_ask(0x40u), SENSE_BAD_ARG);
    CHECK_EQ(fake_wire, 0u);
    /* Closed pins are pulled down, as at reset. */
    CHECK(fake_pins[SDA].pad_down && fake_pins[SCL].pad_down);
}

int main(void)
{
    RUN(pins_nothing_is_wired_to_read_as_nothing_answering);
    RUN(nothing_wired_does_not_mark_the_bus_stuck);
    RUN(a_line_a_part_holds_low_is_the_bus_held_low);
    RUN(the_pad_pull_up_has_the_probe_time_and_no_more);
    RUN(a_bus_with_its_pull_ups_sends_as_before);
    RUN(a_closed_bus_and_a_pair_that_is_not_one_send_nothing);
    return test_summary("sense_i2c");
}
