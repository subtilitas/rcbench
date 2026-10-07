/*
 * The SENSE and SERVO_SENSE link pages at the coprocessor.
 *
 * Under test: the set-up as a page starts; SHUNT_CAL and ADCRANGE from the
 * INA228's shunt and range, and the INA3221's full scale from its shunt;
 * the bus refused on pins that are not one I2C block's pair, on pins the
 * board, an output or the supply holds, and while the bank is armed; every
 * value held to its range, the reserved registers to 0, the two parts to
 * two addresses; a refused write storing nothing; the pins reserved while
 * held; and a capture armed only whole, on an armed bank, on a channel the
 * INA3221 reads and for a surface on a PWM slot, and ended by a disarm.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "sense_page.h"

static outputs_t    o;
static sense_page_t pg;

#define BIT(pin) ((uint64_t)1u << (pin))

/* A bank with the heartbeat on GP3 reserved and a surface on channel 0
 * driven as PWM on GP4. */
static void fresh(void)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, BIT(3));
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    CHECK(outputs_set_role(&o, 0, OUT_ROLE_SURFACE));
    sense_page_init(&pg);
}

static uint8_t bus(uint16_t en, uint16_t sda, uint16_t scl, uint16_t khz,
                   uint64_t taken)
{
    const uint16_t r[4] = { en, sda, scl, khz };
    return sense_page_write(&pg, LINK_SN_ENABLE, 4u, r, &o, taken);
}

static uint8_t i228(uint16_t addr, uint16_t uohm, uint16_t da, uint16_t r7)
{
    const uint16_t r[4] = { addr, uohm, da, r7 };
    return sense_page_write(&pg, LINK_SN_I228_ADDR, 4u, r, &o, 0u);
}

static uint8_t i3221(uint16_t addr, uint16_t dmohm, uint16_t ch, uint16_t r11)
{
    const uint16_t r[4] = { addr, dmohm, ch, r11 };
    return sense_page_write(&pg, LINK_SN_I3221_ADDR, 4u, r, &o, 0u);
}

static uint8_t arm(uint16_t a, uint16_t hold, uint16_t move, uint16_t band)
{
    const uint16_t r[4] = { a, hold, move, band };
    return sense_servo_write(&pg, LINK_SS_CAP_ARM, 4u, r, &o);
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    sense_page_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

static uint16_t sreg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    sense_servo_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

TEST_CASE(a_page_starts_with_both_parts_off_at_the_modules_defaults)
{
    fresh();
    CHECK_EQ(reg(LINK_SN_ENABLE), 0u);
    CHECK_EQ(reg(LINK_SN_SDA_PIN), 16u);
    CHECK_EQ(reg(LINK_SN_SCL_PIN), 17u);
    CHECK_EQ(reg(LINK_SN_KHZ), 400u);
    CHECK_EQ(reg(LINK_SN_I228_ADDR), 0x45u);
    CHECK_EQ(reg(LINK_SN_I228_SHUNT_UOHM), 200u);
    CHECK_EQ(reg(LINK_SN_I228_MAX_DA), 2048u);
    CHECK_EQ(reg(LINK_SN_RESERVED_7), 0u);
    CHECK_EQ(reg(LINK_SN_I3221_ADDR), 0x40u);
    CHECK_EQ(reg(LINK_SN_I3221_SHUNT_DMOHM), 1000u);
    CHECK_EQ(reg(LINK_SN_I3221_CHANNELS), 1u);
    CHECK_EQ(reg(LINK_SN_RESERVED_11), 0u);
    /* Nothing read: no bus open, and every reading 0. */
    for (unsigned r = LINK_SN_FLAGS; r < LINK_SN_COUNT; ++r) {
        CHECK_EQ(reg(r), 0u);
    }
    for (unsigned r = 0; r < LINK_SS_CAP_ARM; ++r) {
        CHECK_EQ(sreg(r), 0u);
    }
    CHECK_EQ(sreg(LINK_SS_CAP_MOVE_MA), 100u);
    CHECK_EQ(sreg(LINK_SS_CAP_BAND_MA), 50u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);

    CHECK(!sense_page_enabled(&pg));
    CHECK_EQ(sense_page_pins(&pg), 0u);
    CHECK_EQ(sense_page_sda(&pg), 16u);
    CHECK_EQ(sense_page_scl(&pg), 17u);
    CHECK_EQ(sense_page_hz(&pg), 400000u);

    /* The defaults on their own are a set-up every check passes. */
    uint16_t cfg[LINK_SN_CONFIG_COUNT];
    sense_page_defaults(cfg);
    CHECK_EQ(memcmp(cfg, pg.sense, sizeof(cfg)), 0);
    cfg[LINK_SN_ENABLE] = LINK_SN_EN_I228 | LINK_SN_EN_I3221;
    CHECK_EQ(sense_page_write(&pg, 0u, (uint8_t)LINK_SN_CONFIG_COUNT, cfg,
                              &o, 0u), 0u);
    sense_page_defaults(NULL);
}

/* MATEK I2C-INA-BM: 200 uOhm ranged for 204.8 A is SHUNT_CAL 4096 at
 * ADCRANGE 1, the numbers the INA228's equations give. */
TEST_CASE(shunt_cal_and_adcrange_follow_from_the_shunt_and_the_range)
{
    uint8_t range = 9u;
    CHECK_EQ(sense_i228_cal(200u, 2048u, &range), 4096u);
    CHECK_EQ(range, 1u);
    /* Past 40.96 mV at that current: the wider range, about a quarter the cal. */
    CHECK_EQ(sense_i228_cal(200u, 2049u, &range), 1025u);
    CHECK_EQ(range, 0u);
    CHECK_EQ(sense_i228_cal(200u, 6553u, &range), 3277u);
    CHECK_EQ(range, 0u);
    /* 163.84 mV exactly is still inside; a step past it is not. */
    CHECK_EQ(sense_i228_cal(1000u, 1638u, &range), 4095u);
    CHECK_EQ(sense_i228_cal(16384u, 100u, &range), 4096u);
    CHECK_EQ(range, 0u);
    range = 9u;
    CHECK_EQ(sense_i228_cal(16385u, 100u, &range), 0u);
    CHECK_EQ(range, 9u);
    CHECK_EQ(sense_i228_cal(20000u, 6553u, NULL), 0u);
    /* The smallest set-up the page takes still has a calibration. */
    CHECK_EQ(sense_i228_cal(50u, 10u, &range), 5u);
    CHECK_EQ(range, 1u);
    CHECK_EQ(sense_i228_cal(0u, 2048u, NULL), 0u);
}

/* DAOKAI's R100: 163.8 mV across 0.1 Ohm is 1.638 A, and the 5 mOhm floor
 * keeps the full scale inside a register of signed mA. */
TEST_CASE(the_ina3221_full_scale_follows_from_its_shunt)
{
    CHECK_EQ(sense_i3221_full_scale_ma(1000u), 1638u);
    CHECK_EQ(sense_i3221_full_scale_ma(500u), 3276u);
    CHECK_EQ(sense_i3221_full_scale_ma(200u), 8190u);
    CHECK_EQ(sense_i3221_full_scale_ma(LINK_SN_I3221_DMOHM_MIN), 32760u);
    CHECK(sense_i3221_full_scale_ma(LINK_SN_I3221_DMOHM_MIN) <= 32767u);
    CHECK_EQ(sense_i3221_full_scale_ma(0u), 0u);
}

TEST_CASE(the_bus_is_refused_on_pins_that_are_not_one_blocks_pair)
{
    fresh();
    CHECK_EQ(bus(1u, 17u, 18u, 400u, 0u), LINK_NACK_BAD_VALUE); /* SDA odd */
    CHECK_EQ(bus(1u, 16u, 18u, 400u, 0u), LINK_NACK_BAD_VALUE); /* not +1 */
    CHECK_EQ(bus(1u, 17u, 16u, 400u, 0u), LINK_NACK_BAD_VALUE); /* swapped */
    CHECK_EQ(bus(1u, 16u, 16u, 400u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(1u, 64u, 65u, 400u, 0u), LINK_NACK_BAD_VALUE); /* no bank */
    CHECK_EQ(bus(1u, 62u, 63u, 400u, 0u), 0u);                  /* its end */
    CHECK_EQ(bus(0u, 16u, 17u, 400u, 0u), 0u);
    /* I2C0's pairs at mod 4 = 0 and I2C1's at mod 4 = 2 both serve. */
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(bus(2u, 18u, 19u, 100u, 0u), 0u);
    CHECK_EQ(sense_page_pins(&pg), BIT(18) | BIT(19));
    CHECK_EQ(sense_page_hz(&pg), 100000u);
}

TEST_CASE(the_bus_is_refused_on_pins_something_else_holds)
{
    fresh();
    CHECK_EQ(bus(1u, 2u, 3u, 400u, 0u), LINK_NACK_BAD_VALUE);   /* reserved */
    CHECK_EQ(bus(1u, 4u, 5u, 400u, 0u), LINK_NACK_BAD_VALUE);   /* an output */
    CHECK_EQ(bus(1u, 16u, 17u, 400u, BIT(17)), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(1u, 16u, 17u, 400u, BIT(16)), LINK_NACK_BAD_VALUE);
    CHECK(!sense_page_enabled(&pg));

    /* Disabled, the pins are only numbers: nothing is held, nothing is
     * checked. */
    CHECK_EQ(bus(0u, 4u, 5u, 400u, BIT(4)), 0u);
    CHECK_EQ(sense_page_pins(&pg), 0u);

    CHECK_EQ(bus(1u, 16u, 17u, 400u, BIT(8) | BIT(9)), 0u);
    CHECK(sense_page_enabled(&pg));
    CHECK_EQ(sense_page_pins(&pg), BIT(16) | BIT(17));

    /* Its own pins, reserved from the outputs once held, are still its own
     * to write again; another page's are not. */
    outputs_reserve_pins(&o, BIT(3) | sense_page_pins(&pg));
    CHECK_EQ(bus(3u, 16u, 17u, 100u, 0u), 0u);
    CHECK_EQ(bus(3u, 20u, 21u, 400u, 0u), 0u);
    outputs_reserve_pins(&o, BIT(3) | BIT(16) | BIT(17));
    CHECK_EQ(bus(3u, 16u, 17u, 400u, 0u), LINK_NACK_BAD_VALUE);
}

TEST_CASE(every_value_is_held_to_its_range)
{
    fresh();
    CHECK_EQ(bus(4u, 16u, 17u, 400u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(0u, 16u, 17u, 200u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(0u, 16u, 17u, 0u, 0u), LINK_NACK_BAD_VALUE);

    CHECK_EQ(i228(0x3Fu, 200u, 2048u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x50u, 200u, 2048u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 49u, 2048u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 20001u, 10u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 200u, 9u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 200u, 6554u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 200u, 2048u, 1u), LINK_NACK_BAD_VALUE);
    /* In range one by one, and no setting reads that current. */
    CHECK_EQ(i228(0x45u, 20000u, 6553u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x40u, 50u, 10u, 0u), 0u);
    CHECK_EQ(i228(0x4Fu, 250u, 6553u, 0u), 0u);
    CHECK_EQ(i228(0x44u, 20000u, 81u, 0u), 0u);

    CHECK_EQ(i3221(0x44u, 1000u, 1u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i3221(0x3Fu, 1000u, 1u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i3221(0x40u, 49u, 1u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i3221(0x40u, 10001u, 1u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i3221(0x40u, 1000u, 8u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i3221(0x40u, 1000u, 1u, 7u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i3221(0x43u, 50u, 7u, 0u), 0u);
    CHECK_EQ(i3221(0x40u, 10000u, 0u, 0u), 0u);     /* not enabled: fine */
    CHECK_EQ(reg(LINK_SN_I3221_CHANNELS), 0u);

    /* Enabled, it reads at least one channel. */
    CHECK_EQ(bus(2u, 16u, 17u, 400u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i3221(0x40u, 1000u, 4u, 0u), 0u);
    CHECK_EQ(bus(2u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(i3221(0x40u, 1000u, 0u, 0u), LINK_NACK_BAD_VALUE);
}

TEST_CASE(two_parts_enabled_answer_at_two_addresses)
{
    fresh();
    CHECK_EQ(i228(0x41u, 200u, 2048u, 0u), 0u);
    CHECK_EQ(i3221(0x41u, 1000u, 1u, 0u), 0u);
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);      /* one: no clash */
    CHECK_EQ(bus(2u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(bus(3u, 16u, 17u, 400u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 200u, 2048u, 0u), 0u);
    CHECK_EQ(bus(3u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(i3221(0x45u, 1000u, 1u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x41u, 200u, 2048u, 0u), LINK_NACK_BAD_VALUE);
}

/* Opening the bus again stops the readings for some milliseconds, so not
 * mid-run; writing what is in force changes nothing and is taken. */
TEST_CASE(the_set_up_does_not_change_while_the_bank_is_armed)
{
    fresh();
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    outputs_arm(&o, true, 0u);
    CHECK(outputs_driving(&o));
    CHECK_EQ(bus(0u, 16u, 17u, 400u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(1u, 16u, 17u, 100u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x44u, 200u, 2048u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(i228(0x45u, 200u, 2048u, 0u), 0u);
    outputs_arm(&o, false, 0u);
    CHECK_EQ(bus(0u, 16u, 17u, 400u, 0u), 0u);
}

/* A frame is all or nothing: a refusal in its last register keeps none of
 * the three before it. */
TEST_CASE(a_refused_write_stores_none_of_its_registers)
{
    fresh();
    CHECK_EQ(bus(1u, 20u, 21u, 300u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SN_ENABLE), 0u);
    CHECK_EQ(reg(LINK_SN_SDA_PIN), 16u);
    CHECK_EQ(reg(LINK_SN_SCL_PIN), 17u);
    CHECK_EQ(reg(LINK_SN_KHZ), 400u);
    CHECK_EQ(i228(0x41u, 300u, 1000u, 5u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SN_I228_ADDR), 0x45u);
    CHECK_EQ(reg(LINK_SN_I228_SHUNT_UOHM), 200u);
    CHECK_EQ(reg(LINK_SN_I228_MAX_DA), 2048u);
}

TEST_CASE(read_only_registers_and_the_page_end_are_refused)
{
    fresh();
    const uint16_t v[4] = { 1u, 2u, 3u, 4u };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_FLAGS, 1u, v, &o, 0u),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I3221_CHANNELS, 3u, v, &o, 0u),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_page_write(&pg, LINK_SN_COUNT - 1u, 2u, v, &o, 0u),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_page_write(&pg, 0u, 0u, v, &o, 0u), 0u);
    CHECK_EQ(sense_page_write(NULL, 0u, 1u, v, &o, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_page_write(&pg, 0u, 1u, NULL, &o, 0u),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_page_write(&pg, 0u, 1u, v, NULL, 0u),
             LINK_NACK_BAD_RANGE);

    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CH_MEAN_MA, 1u, v, &o),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CH_FLAGS, 2u, v, &o),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_BAND_MA, 2u, v, &o),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_STATE, 1u, v, &o),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_COUNT - 1u, 2u, v, &o),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 0u, v, &o), 0u);
    CHECK_EQ(sense_servo_write(NULL, LINK_SS_CAP_ARM, 1u, v, &o),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 1u, NULL, &o),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 1u, v, NULL),
             LINK_NACK_BAD_RANGE);

    /* A read off the end, or with nowhere to put it, touches nothing. */
    uint16_t out[2] = { 0xAAAAu, 0xAAAAu };
    sense_page_read(&pg, LINK_SN_COUNT - 1u, 2u, out);
    sense_servo_read(&pg, LINK_SS_COUNT - 1u, 2u, out);
    sense_page_read(NULL, 0u, 1u, out);
    sense_servo_read(NULL, 0u, 1u, out);
    CHECK_EQ(out[0], 0xAAAAu);
    sense_page_read(&pg, 0u, 1u, NULL);
    sense_servo_read(&pg, 0u, 1u, NULL);

    /* And a page that is not there holds nothing. */
    sense_page_init(NULL);
    sense_page_step(NULL, false);
    CHECK(!sense_page_enabled(NULL));
    CHECK_EQ(sense_page_pins(NULL), 0u);
    CHECK_EQ(sense_page_sda(NULL), 0u);
    CHECK_EQ(sense_page_scl(NULL), 0u);
    CHECK_EQ(sense_page_hz(NULL), 0u);
}

TEST_CASE(no_slot_binds_a_pin_the_bus_holds)
{
    fresh();
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    slots[LINK_OS_DRIVER] = LINK_DRIVER_PWM;
    slots[LINK_OS_PIN]    = 16u;
    CHECK_EQ(sense_page_slots_check(&pg, slots), 0u);   /* not held yet */
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(sense_page_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    slots[LINK_OS_PIN] = 17u;
    CHECK_EQ(sense_page_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* A slot without a driver holds no pin, and a pin past the bank is the
     * OUTPUTS page's to refuse. */
    slots[LINK_OS_DRIVER] = LINK_DRIVER_NONE;
    CHECK_EQ(sense_page_slots_check(&pg, slots), 0u);
    slots[LINK_OS_DRIVER] = LINK_DRIVER_PWM;
    slots[LINK_OS_PIN]    = 64u;
    CHECK_EQ(sense_page_slots_check(&pg, slots), 0u);
    CHECK_EQ(sense_page_slots_check(&pg, NULL), LINK_NACK_BAD_VALUE);
}

/* An INA3221 reading CH1 and CH3, and a bank armed. */
static void ready_to_capture(void)
{
    fresh();
    CHECK_EQ(i3221(0x40u, 1000u, 5u, 0u), 0u);
    CHECK_EQ(bus(2u, 16u, 17u, 400u, 0u), 0u);
    outputs_arm(&o, true, 0u);
}

TEST_CASE(a_capture_arms_whole_on_an_armed_bank)
{
    ready_to_capture();
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_ARM), LINK_SS_ARM_OF(1u, 0u));
    CHECK_EQ(sreg(LINK_SS_CAP_HOLD_MA), 900u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_ARMED);
    CHECK_EQ(LINK_SS_ARM_CH(sreg(LINK_SS_CAP_ARM)), 1u);
    CHECK_EQ(LINK_SS_ARM_OUT(sreg(LINK_SS_CAP_ARM)), 0u);

    /* Not part of a frame: an arm is all four, and a threshold alone is
     * an arm's. */
    const uint16_t two[2] = { LINK_SS_ARM_OF(3u, 0u), 900u };
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 2u, two, &o),
             LINK_NACK_BAD_VALUE);
    const uint16_t band = 20u;
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_BAND_MA, 1u, &band, &o),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(sreg(LINK_SS_CAP_BAND_MA), 50u);

    /* Another arm restarts it, its results cleared. */
    pg.servo[LINK_SS_CAP_STATE]   = (uint16_t)LINK_CAP_ARRIVED;
    pg.servo[LINK_SS_CAP_MOVE_T]  = 123u;
    pg.servo[LINK_SS_CAP_SAMPLES] = 40u;
    pg.servo[LINK_SS_CH_FLAGS]    = (uint16_t)(LINK_SS_CAP_CLIPPED
                                               | LINK_SS_CH_VALID(1u));
    CHECK_EQ(arm(LINK_SS_ARM_OF(3u, 0u), 0u, 1u, 1u), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_ARMED);
    CHECK_EQ(sreg(LINK_SS_CAP_MOVE_T), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_SAMPLES), 0u);
    CHECK_EQ(sreg(LINK_SS_CH_FLAGS), LINK_SS_CH_VALID(1u));
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 32767u, 32767u, 32767u), 0u);
}

TEST_CASE(a_capture_is_refused_what_it_cannot_time)
{
    ready_to_capture();
    /* Values. */
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u) | 0x0010u, 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(arm(LINK_SS_ARM | 0x0800u | 1u, 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(arm(1u, 900u, 100u, 50u), LINK_NACK_BAD_VALUE);   /* no bit 7 */
    CHECK_EQ(arm(LINK_SS_ARM, 900u, 100u, 50u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 32768u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 0u, 50u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 32768u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 32768u),
             LINK_NACK_BAD_VALUE);
    /* A channel the INA3221 does not read. */
    CHECK_EQ(arm(LINK_SS_ARM_OF(2u, 0u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    /* An output channel with no PWM slot, and one that is a throttle. */
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 1u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK(outputs_set_role(&o, 0, OUT_ROLE_THROTTLE));
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK(outputs_set_role(&o, 0, OUT_ROLE_SURFACE));
    /* Nothing armed, nothing to time. */
    outputs_arm(&o, false, 0u);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u),
             LINK_NACK_NOT_ARMED);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);
    CHECK_EQ(sreg(LINK_SS_CAP_ARM), 0u);

    /* The INA3221 off: no channel is read. */
    outputs_arm(&o, false, 0u);
    CHECK_EQ(bus(0u, 16u, 17u, 400u, 0u), 0u);
    outputs_arm(&o, true, 0u);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
}

TEST_CASE(a_disarm_is_never_refused_and_stores_nothing_beside_it)
{
    ready_to_capture();
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    const uint16_t off = 0u;
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 1u, &off, &o), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_ARM), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);

    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    outputs_arm(&o, false, 0u);
    CHECK_EQ(arm(0u, 40000u, 0u, 0u), 0u);           /* at the frame's head */
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);
    CHECK_EQ(sreg(LINK_SS_CAP_HOLD_MA), 900u);
    CHECK_EQ(sreg(LINK_SS_CAP_MOVE_MA), 100u);
    CHECK_EQ(sreg(LINK_SS_CAP_BAND_MA), 50u);
}

/* A capture times a move in a run; the run ending ends one that has not
 * finished, and leaves a finished one's result to be read. */
TEST_CASE(a_bank_that_stops_driving_ends_an_unfinished_capture)
{
    static const link_cap_state_t running[] = {
        LINK_CAP_ARMED, LINK_CAP_WAIT_MOVE, LINK_CAP_MOVING,
    };
    for (size_t i = 0; i < sizeof(running) / sizeof(running[0]); ++i) {
        ready_to_capture();
        CHECK_EQ(arm(LINK_SS_ARM_OF(3u, 0u), 900u, 100u, 50u), 0u);
        pg.servo[LINK_SS_CAP_STATE] = (uint16_t)running[i];
        sense_page_step(&pg, true);
        CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)running[i]);
        sense_page_step(&pg, false);
        CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);
        CHECK_EQ(sreg(LINK_SS_CAP_ARM), 0u);
    }
    static const link_cap_state_t done[] = {
        LINK_CAP_ARRIVED, LINK_CAP_AT_STOP, LINK_CAP_LATE,
    };
    for (size_t i = 0; i < sizeof(done) / sizeof(done[0]); ++i) {
        ready_to_capture();
        CHECK_EQ(arm(LINK_SS_ARM_OF(3u, 0u), 900u, 100u, 50u), 0u);
        pg.servo[LINK_SS_CAP_STATE]    = (uint16_t)done[i];
        pg.servo[LINK_SS_CAP_ARRIVE_T] = 2345u;
        sense_page_step(&pg, false);
        CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)done[i]);
        CHECK_EQ(sreg(LINK_SS_CAP_ARRIVE_T), 2345u);
    }
}

/* The register macros say what the page says. */
TEST_CASE(the_channel_flags_and_the_arm_word_pack_as_documented)
{
    CHECK_EQ(LINK_SS_CH_VALID(1u), 0x01u);
    CHECK_EQ(LINK_SS_CH_VALID(3u), 0x04u);
    CHECK_EQ(LINK_SS_CH_CLIPPED(1u), 0x10u);
    CHECK_EQ(LINK_SS_CH_CLIPPED(3u), 0x40u);
    CHECK_EQ(LINK_SS_ARM_OF(3u, 7u), 0x0783u);
    CHECK_EQ(LINK_SS_ARM_OF(3u, 7u), LINK_SS_ARM_BITS);
    CHECK_EQ(LINK_SS_ARM_OF(1u, 2u), 0x0281u);
    CHECK_EQ(LINK_SS_CH_STRIDE * LINK_SS_CHANNELS, (unsigned)LINK_SS_WINDOW);
}

int main(void)
{
    RUN(a_page_starts_with_both_parts_off_at_the_modules_defaults);
    RUN(shunt_cal_and_adcrange_follow_from_the_shunt_and_the_range);
    RUN(the_ina3221_full_scale_follows_from_its_shunt);
    RUN(the_bus_is_refused_on_pins_that_are_not_one_blocks_pair);
    RUN(the_bus_is_refused_on_pins_something_else_holds);
    RUN(every_value_is_held_to_its_range);
    RUN(two_parts_enabled_answer_at_two_addresses);
    RUN(the_set_up_does_not_change_while_the_bank_is_armed);
    RUN(a_refused_write_stores_none_of_its_registers);
    RUN(read_only_registers_and_the_page_end_are_refused);
    RUN(no_slot_binds_a_pin_the_bus_holds);
    RUN(a_capture_arms_whole_on_an_armed_bank);
    RUN(a_capture_is_refused_what_it_cannot_time);
    RUN(a_disarm_is_never_refused_and_stores_nothing_beside_it);
    RUN(a_bank_that_stops_driving_ends_an_unfinished_capture);
    RUN(the_channel_flags_and_the_arm_word_pack_as_documented);
    return test_summary("sense_page");
}
