/*
 * The SENSE and SERVO_SENSE link pages at the coprocessor.
 *
 * Under test: the set-up as a page starts; an INA228 shunt and maximum
 * taken exactly when the driver's ina228_calibrate() takes them, and the
 * INA3221's full scale from its shunt; the bus refused on pins that are
 * not one I2C block's pair, on pins the
 * board, an output or the supply holds, and while the bank is armed; every
 * value held to its range, the reserved registers to 0, the two parts to
 * two addresses; a refused write storing nothing; the pins reserved while
 * held; and a capture armed only whole, on an armed bank, on CH1 while the
 * INA3221 reads it and for a surface on a PWM slot, and ended by a disarm.
 * Core 1's side: the generations; the order built from the page; a
 * snapshot published into both pages, rounded and held to the registers,
 * and only under the set-up and the capture order in force; the ESC's own
 * telemetry; BENCH from the INA228 at its existing scales while it is the
 * source, and to the end of a run it was online in; the capability bits.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ina228.h"

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
    sense_page_bound(&pg, 0x01u);            /* the silicon bound slot 0 */
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

/* One rule, the driver's: inside the page's own ranges, a shunt and a
 * maximum are taken exactly when ina228_calibrate() takes them. */
TEST_CASE(the_ina228_set_up_is_taken_when_the_driver_calibrates_it)
{
    static const uint16_t uohm[] = {
        50u, 81u, 82u, 100u, 200u, 250u, 1000u, 16384u, 16385u, 20000u,
    };
    static const uint16_t da[] = {
        10u, 20u, 21u, 100u, 2048u, 2049u, 2500u, 3000u,
    };
    unsigned taken = 0u;
    unsigned refused = 0u;
    for (size_t i = 0; i < sizeof(uohm) / sizeof(uohm[0]); ++i) {
        for (size_t k = 0; k < sizeof(da) / sizeof(da[0]); ++k) {
            fresh();
            ina228_cal_t cal;
            const bool ok = ina228_calibrate(uohm[i], (uint32_t)da[k] * 100u,
                                             &cal) == INA228_SETUP_OK;
            CHECK_EQ(i228(0x45u, uohm[i], da[k], 0u),
                     ok ? 0u : LINK_NACK_BAD_VALUE);
            if (ok) {
                ++taken;
                CHECK_EQ(cal.shunt_cal, INA228_SHUNT_CAL_ADC);
            } else {
                ++refused;
            }
        }
    }
    /* Both answers occur, so the grid holds the rule's edges. */
    CHECK(taken > 0u);
    CHECK(refused > 0u);

    /* MATEK I2C-INA-BM: 200 uOhm at 204.8 A is 40.96 mV, the top of
     * ADCRANGE 1, and 204.9 A moves to ADCRANGE 0 at the same SHUNT_CAL. */
    fresh();
    CHECK_EQ(i228(0x45u, 200u, 2048u, 0u), 0u);
    CHECK_EQ(i228(0x45u, 200u, 2049u, 0u), 0u);
    /* 163.84 mV exactly is inside; a step past it is not. */
    CHECK_EQ(i228(0x45u, 16384u, 100u, 0u), 0u);
    CHECK_EQ(i228(0x45u, 16385u, 100u, 0u), LINK_NACK_BAD_VALUE);
    /* The page's 300 A on its smallest shunts stays inside ADCRANGE 1:
     * 24.3 mV across 81 uOhm, so the 2000 A refusal of ADCRANGE 0 below
     * 81.92 uOhm is never reached from the page. */
    CHECK_EQ(i228(0x45u, 50u, 3000u, 0u), 0u);
    CHECK_EQ(i228(0x45u, 81u, 3000u, 0u), 0u);
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
    CHECK_EQ(bus(2u, 18u, 19u, 400u, 0u), 0u);
    CHECK_EQ(sense_page_pins(&pg), BIT(18) | BIT(19));
    CHECK_EQ(sense_page_hz(&pg), 400000u);
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
    CHECK_EQ(bus(3u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(bus(3u, 20u, 21u, 400u, 0u), 0u);
    outputs_reserve_pins(&o, BIT(3) | BIT(16) | BIT(17));
    CHECK_EQ(bus(3u, 16u, 17u, 400u, 0u), LINK_NACK_BAD_VALUE);
}

TEST_CASE(every_value_is_held_to_its_range)
{
    fresh();
    CHECK_EQ(bus(4u, 16u, 17u, 400u, 0u), LINK_NACK_BAD_VALUE);
    /* 400 kHz and no other clock: the 1 ms schedule needs it. */
    CHECK_EQ(bus(0u, 16u, 17u, 100u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(0u, 16u, 17u, 200u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(0u, 16u, 17u, 1000u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(bus(0u, 16u, 17u, 0u, 0u), LINK_NACK_BAD_VALUE);

    CHECK_EQ(i228(0x3Fu, 200u, 2048u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x50u, 200u, 2048u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 49u, 2048u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 20001u, 10u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 200u, 9u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 200u, 3001u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x45u, 200u, 2048u, 1u), LINK_NACK_BAD_VALUE);
    /* In range one by one, and no setting reads that current. */
    CHECK_EQ(i228(0x45u, 20000u, 3000u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(i228(0x40u, 50u, 10u, 0u), 0u);
    CHECK_EQ(i228(0x4Fu, 250u, 3000u, 0u), 0u);
    CHECK_EQ(i228(0x44u, 20000u, 81u, 0u), 0u);
    CHECK_EQ(i228(0x44u, 20000u, 82u, 0u), LINK_NACK_BAD_VALUE);

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
    CHECK_EQ(bus(1u, 20u, 21u, 400u, 0u), LINK_NACK_BAD_VALUE);
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

/* The bus moved from GP16/GP17 to GP20/GP21: until core 1 says it has let
 * the old pins go, a slot on one of them is refused, as one on a pin the
 * SUPPLY page holds is -- not taken and left unbound.  The held pins come
 * with a snapshot of the old set-up as well. */
TEST_CASE(no_slot_binds_a_pin_core_1_still_holds)
{
    fresh();
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    sense_snap_t s;
    memset(&s, 0, sizeof(s));
    s.cfg_gen = pg.cfg_gen;
    s.open = true;
    s.held = BIT(16) | BIT(17);
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(bus(1u, 20u, 21u, 400u, 0u), 0u);
    CHECK_EQ(sense_page_pins(&pg), BIT(20) | BIT(21));
    CHECK_EQ(sense_page_held(&pg), BIT(16) | BIT(17) | BIT(20) | BIT(21));
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    slots[LINK_OS_DRIVER] = LINK_DRIVER_PWM;
    slots[LINK_OS_PIN]    = 16u;
    CHECK_EQ(sense_page_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* Core 1 still on the old set-up, still holding: refused. */
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(sense_page_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* Core 1 on the new set-up, the old pins let go: taken. */
    s.cfg_gen = pg.cfg_gen;
    s.held = BIT(20) | BIT(21);
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(sense_page_slots_check(&pg, slots), 0u);
    CHECK_EQ(sense_page_held(&pg), BIT(20) | BIT(21));
    CHECK_EQ(sense_page_held(NULL), 0u);
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
    const uint16_t two[2] = { LINK_SS_ARM_OF(1u, 0u), 900u };
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
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 0u, 1u, 1u), 0u);
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
    /* CH3, which it reads: a capture is CH1's only, the others read at
     * 50 Hz. */
    CHECK_EQ(arm(LINK_SS_ARM_OF(3u, 0u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);
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

/* A PWM slot the bank holds and the silicon did not bind -- its compare
 * register taken by another pin -- renders no frame: no capture arms on
 * it.  A page told nothing counts no slot bound. */
TEST_CASE(a_capture_is_refused_on_a_slot_the_silicon_did_not_bind)
{
    ready_to_capture();
    sense_page_bound(&pg, 0x02u);            /* slot 1 bound, slot 0 not */
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);
    sense_page_bound(&pg, 0x01u);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);

    sense_page_init(&pg);                    /* told nothing yet */
    outputs_arm(&o, false, 0u);
    CHECK_EQ(i3221(0x40u, 1000u, 1u, 0u), 0u);
    CHECK_EQ(bus(2u, 16u, 17u, 400u, 0u), 0u);
    outputs_arm(&o, true, 0u);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    sense_page_bound(NULL, 0x01u);           /* nothing, no crash */
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
        CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
        pg.servo[LINK_SS_CAP_STATE] = (uint16_t)running[i];
        sense_page_step(&pg, true);
        CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)running[i]);
        sense_page_step(&pg, false);
        CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);
        CHECK_EQ(sreg(LINK_SS_CAP_ARM), 0u);
    }
    static const link_cap_state_t done[] = {
        LINK_CAP_ARRIVED, LINK_CAP_AT_STOP, LINK_CAP_LATE, LINK_CAP_UNSEEN,
        LINK_CAP_LOST,
    };
    for (size_t i = 0; i < sizeof(done) / sizeof(done[0]); ++i) {
        ready_to_capture();
        CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
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

/* ------------------------------------------- core 1's order and its view */

/* cfg_gen moves with a change of the set-up and nothing else; cap_gen
 * with an arm, a disarm and a capture a stopped bank ends. */
TEST_CASE(the_generations_move_with_what_they_cover)
{
    fresh();
    const uint16_t c0 = pg.cfg_gen;
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(pg.cfg_gen, (uint16_t)(c0 + 1u));
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);          /* in force */
    CHECK_EQ(pg.cfg_gen, (uint16_t)(c0 + 1u));
    CHECK_EQ(bus(1u, 17u, 18u, 400u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(pg.cfg_gen, (uint16_t)(c0 + 1u));

    ready_to_capture();
    const uint16_t g0 = pg.cap_gen;
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    CHECK_EQ(pg.cap_gen, (uint16_t)(g0 + 1u));
    CHECK_EQ(arm(LINK_SS_ARM_OF(2u, 0u), 900u, 100u, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(pg.cap_gen, (uint16_t)(g0 + 1u));
    CHECK(!sense_page_step(&pg, true));
    CHECK(sense_page_step(&pg, false));                 /* ends it */
    CHECK_EQ(pg.cap_gen, (uint16_t)(g0 + 2u));
    CHECK(!sense_page_step(&pg, false));                /* once */
    CHECK_EQ(pg.cap_gen, (uint16_t)(g0 + 2u));
    const uint16_t off = 0u;
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 1u, &off, &o), 0u);
    CHECK_EQ(pg.cap_gen, (uint16_t)(g0 + 3u));
}

/* The order core 1 runs is the page in its own units. */
TEST_CASE(the_order_carries_the_page)
{
    ready_to_capture();
    outputs_arm(&o, false, 0u);
    CHECK_EQ(i228(0x44u, 250u, 1500u, 0u), 0u);
    CHECK_EQ(bus(3u, 16u, 17u, 400u, 0u), 0u);
    outputs_arm(&o, true, 0u);
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    sense_cmd_t c;
    memset(&c, 0xA5, sizeof(c));
    c.run_gen  = 7u;
    c.edge_set = true;
    c.edge_us  = 1234u;
    sense_page_cmd(&pg, &c);
    CHECK_EQ(c.cfg_gen, pg.cfg_gen);
    CHECK_EQ(c.sda, 16u);
    CHECK_EQ(c.scl, 17u);
    CHECK(c.parts.ina228_en);
    CHECK_EQ(c.parts.ina228_addr, 0x44u);
    CHECK_EQ(c.parts.ina228_shunt_uohm, 250u);
    CHECK_EQ(c.parts.ina228_max_ma, 150000u);
    CHECK(c.parts.ina3221_en);
    CHECK_EQ(c.parts.ina3221_addr, 0x40u);
    CHECK_EQ(c.parts.ina3221_shunt_uohm, 100000u);
    CHECK_EQ(c.parts.ina3221_channels, 5u);
    CHECK_EQ(c.cap_gen, pg.cap_gen);
    CHECK(c.cap_on);
    CHECK_EQ(c.cap.rise_ua, SENSE_CAP_RISE_AUTO);
    CHECK_EQ(c.cap.hold_ua, 900000);
    CHECK_EQ(c.cap.move_ua, 100000);
    CHECK_EQ(c.cap.band_ua, 50000);
    /* The caller's fields are left alone. */
    CHECK_EQ(c.run_gen, 7u);
    CHECK(c.edge_set);
    CHECK_EQ(c.edge_us, 1234u);

    const uint16_t off = 0u;
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 1u, &off, &o), 0u);
    sense_page_cmd(&pg, &c);
    CHECK(!c.cap_on);
    CHECK_EQ(c.cap_gen, pg.cap_gen);

    sense_page_cmd(NULL, &c);                /* nothing, no crash */
    sense_page_cmd(&pg, NULL);
}

/* A snapshot of both parts online, under the page's set-up and capture
 * order. */
static sense_snap_t snapshot(void)
{
    sense_snap_t s;
    memset(&s, 0, sizeof(s));
    s.cfg_gen = pg.cfg_gen;
    s.cap_gen = pg.cap_gen;
    s.run_gen = 3u;
    s.open = true;
    s.held = BIT(16) | BIT(17);
    s.present = 0x0021u;
    s.i228 = SENSE_PART_ONLINE;
    s.i3221 = SENSE_PART_ONLINE;
    s.i228_maker = 0x5449u;
    s.i228_device = 0x2281u;
    s.i3221_maker = 0x5449u;
    s.i3221_die = 0x3220u;
    s.errors = 12u;
    s.have_temp = true;
    s.temp_mdegc = 25450;
    s.have_diag = true;
    s.diag = 0x0001u;
    s.have_win = true;
    s.win[SENSE_SRC_CH1] = (sense_window_t){ .number = 77u, .n_i = 50u,
        .n_v = 3u, .i_mean_ua = 120499, .i_min_ua = 100000,
        .i_max_ua = 950500, .v_mean_uv = 5999500, .v_min_uv = 5800000 };
    s.win[SENSE_SRC_CH2] = (sense_window_t){ .number = 77u, .n_i = 2u,
        .clip_hi = true, .i_mean_ua = -1500, .i_max_ua = -1499 };
    s.win[SENSE_SRC_CH3] = (sense_window_t){ .number = 77u };
    s.win[SENSE_SRC_INA228] = (sense_window_t){ .number = 77u, .n_i = 25u,
        .n_v = 25u, .i_mean_ua = 12000000, .v_mean_uv = 16800000 };
    s.run.have_v = true;
    s.run.v_min_uv = 16000000;
    s.run.have_i = true;
    s.run.i_max_ua = 30000000;
    s.run.have_p = true;
    s.run.p_max_uw = 480000000;
    s.run.totals_ok = true;
    s.run.charge_uc = 3600000;              /* 1 mAh */
    s.run.energy_mj = 36000;                /* 0.01 Wh */
    s.cap_state = SENSE_CAP_ARRIVED;
    s.cap_seq = 4u;
    s.cap_move_t = 25u;
    s.cap_arrive_t = 6700u;
    s.cap_peak_ua = 951499;
    s.cap_mean_ua = -2500;
    s.cap_samples = 670u;
    return s;
}

TEST_CASE(a_snapshot_fills_the_read_only_registers)
{
    ready_to_capture();
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    sense_snap_t s = snapshot();
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(reg(LINK_SN_FLAGS),
             (uint16_t)(LINK_SN_BUS_OPEN | LINK_SN_I228_ONLINE
                        | LINK_SN_I228_ID_OK | LINK_SN_I3221_ONLINE
                        | LINK_SN_I3221_ID_OK));
    CHECK_EQ(reg(LINK_SN_PRESENT), 0x0021u);
    CHECK_EQ(reg(LINK_SN_I228_ID), 0x2281u);
    CHECK_EQ(reg(LINK_SN_I3221_ID), 0x3220u);
    CHECK_EQ(reg(LINK_SN_ERRORS), 12u);
    CHECK_EQ(reg(LINK_SN_I228_TEMP_DC), 255u);          /* 25.45 C */
    CHECK_EQ(reg(LINK_SN_I228_DIAG), 0x0001u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_LO), 100u);        /* 1.00 mAh */
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_HI), 0u);
    CHECK_EQ(reg(LINK_SN_I228_ENERGY_LO), 1u);          /* 0.01 Wh */
    CHECK_EQ(reg(LINK_SN_I228_ENERGY_HI), 0u);
    /* The windows, rounded to mA and mV. */
    CHECK_EQ(sreg(LINK_SS_CH_MEAN_MA), 120u);
    CHECK_EQ(sreg(LINK_SS_CH_MAX_MA), 951u);
    CHECK_EQ(sreg(LINK_SS_CH_MEAN_MV), 6000u);
    CHECK_EQ(sreg(LINK_SS_CH_MIN_MV), 5800u);
    CHECK_EQ((int16_t)sreg(LINK_SS_CH_STRIDE + LINK_SS_CH_MEAN_MA), -2);
    CHECK_EQ((int16_t)sreg(LINK_SS_CH_STRIDE + LINK_SS_CH_MAX_MA), -1);
    CHECK_EQ(sreg(LINK_SS_WINDOW), 77u);
    CHECK_EQ(sreg(LINK_SS_CH_FLAGS),
             (uint16_t)(LINK_SS_CH_VALID(1u) | LINK_SS_CH_VALID(2u)
                        | LINK_SS_CH_CLIPPED(2u)));
    /* The capture, of the order in force. */
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_ARRIVED);
    CHECK_EQ(sreg(LINK_SS_CAP_SEQ), 4u);
    CHECK_EQ(sreg(LINK_SS_CAP_MOVE_T), 25u);
    CHECK_EQ(sreg(LINK_SS_CAP_ARRIVE_T), 6700u);
    CHECK_EQ(sreg(LINK_SS_CAP_PEAK_MA), 951u);
    CHECK_EQ((int16_t)sreg(LINK_SS_CAP_MEAN_MA), -3);
    CHECK_EQ(sreg(LINK_SS_CAP_SAMPLES), 670u);
    s.cap_clipped = true;
    sense_page_publish(&pg, &s, 3u);
    CHECK((sreg(LINK_SS_CH_FLAGS) & LINK_SS_CAP_CLIPPED) != 0u);

    /* An earlier capture order: the capture's registers as the page set
     * them, the channels still published, the clip bit kept. */
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_ARMED);
    s.cap_clipped = false;
    s.win[SENSE_SRC_CH1].number = 78u;
    s.win[SENSE_SRC_CH2].clip_hi = false;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_ARMED);
    CHECK_EQ(sreg(LINK_SS_CAP_SAMPLES), 0u);
    CHECK_EQ(sreg(LINK_SS_WINDOW), 78u);
    CHECK_EQ(sreg(LINK_SS_CH_FLAGS),
             (uint16_t)(LINK_SS_CH_VALID(1u) | LINK_SS_CH_VALID(2u)));

    /* Another run, or the totals not the run's: no charge, no energy. */
    s = snapshot();
    sense_page_publish(&pg, &s, 4u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_LO), 0u);
    CHECK_EQ(reg(LINK_SN_I228_ENERGY_LO), 0u);
    s.run.charge_uc = -3600000;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ((int16_t)reg(LINK_SN_I228_CHARGE_LO), -100);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_HI), 0xFFFFu);
    s.run.totals_ok = false;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_LO), 0u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_HI), 0u);

    /* An earlier set-up publishes nothing. */
    s = snapshot();
    s.cfg_gen = (uint16_t)(pg.cfg_gen - 1u);
    s.errors = 99u;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(reg(LINK_SN_ERRORS), 12u);
    sense_page_publish(NULL, &s, 3u);
    sense_page_publish(&pg, NULL, 3u);
}

/* FLAGS from each part's state, the bus and the clips; values held to
 * their registers. */
TEST_CASE(the_flags_and_the_ranges_follow_the_snapshot)
{
    ready_to_capture();
    sense_snap_t s = snapshot();
    s.stuck = true;
    s.i228 = SENSE_PART_OFFLINE;           /* stopped answering */
    s.i3221 = SENSE_PART_WRONG_ID;
    s.i3221_maker = 0x1408u;
    s.i3221_die = 0u;
    s.have_temp = false;
    s.have_diag = false;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(reg(LINK_SN_FLAGS),
             (uint16_t)(LINK_SN_BUS_OPEN | LINK_SN_BUS_STUCK
                        | LINK_SN_I228_ID_OK | LINK_SN_I3221_ID_WRONG));
    CHECK_EQ(reg(LINK_SN_I228_TEMP_DC), 0u);
    CHECK_EQ(reg(LINK_SN_I228_DIAG), 0u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_LO), 0u);          /* not online */

    s = snapshot();
    s.win[SENSE_SRC_INA228].clip_lo = true;
    sense_page_publish(&pg, &s, 3u);
    CHECK((reg(LINK_SN_FLAGS) & LINK_SN_I228_CLIPPED) != 0u);
    s = snapshot();
    s.run.i_clipped = true;
    sense_page_publish(&pg, &s, 3u);
    CHECK((reg(LINK_SN_FLAGS) & LINK_SN_I228_CLIPPED) != 0u);

    /* Held to the registers. */
    s = snapshot();
    s.temp_mdegc = -40050;
    s.win[SENSE_SRC_CH1].i_mean_ua = 40000000;
    s.win[SENSE_SRC_CH1].i_max_ua = -40000000;
    s.win[SENSE_SRC_CH1].v_mean_uv = 70000000;
    s.win[SENSE_SRC_CH1].v_min_uv = -8000;
    s.cap_move_t = 70000u;
    s.cap_samples = 70000u;
    s.run.charge_uc = INT64_MAX / 2;
    s.run.energy_mj = UINT64_MAX / 2;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ((int16_t)reg(LINK_SN_I228_TEMP_DC), -401);
    CHECK_EQ(sreg(LINK_SS_CH_MEAN_MA), 32767u);
    CHECK_EQ((int16_t)sreg(LINK_SS_CH_MAX_MA), -32767);
    CHECK_EQ(sreg(LINK_SS_CH_MEAN_MV), 65535u);
    CHECK_EQ(sreg(LINK_SS_CH_MIN_MV), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_MOVE_T), 65535u);
    CHECK_EQ(sreg(LINK_SS_CAP_SAMPLES), 65535u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_HI), 0x7FFFu);
    CHECK_EQ(reg(LINK_SN_I228_ENERGY_HI), 0xFFFFu);
    s.run.charge_uc = -(INT64_MAX / 2);
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_HI), 0x8000u);
    CHECK_EQ(reg(LINK_SN_I228_CHARGE_LO), 0x0001u);

    /* No window yet: the channels read 0 and are not valid. */
    s = snapshot();
    s.have_win = false;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(sreg(LINK_SS_CH_MEAN_MA), 0u);
    CHECK_EQ(sreg(LINK_SS_WINDOW), 0u);
    CHECK_EQ(sreg(LINK_SS_CH_FLAGS) & 0x7Fu, 0u);

    /* The bus closed: every flag clear. */
    memset(&s, 0, sizeof(s));
    s.cfg_gen = pg.cfg_gen;
    s.cap_gen = pg.cap_gen;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(reg(LINK_SN_FLAGS), 0u);
    CHECK_EQ(reg(LINK_SN_PRESENT), 0u);
}

/* A new set-up forgets what the old one read: until a snapshot of the new
 * set-up arrives, no part reads online and no value of the old shunt or
 * address shows.  The ESC's telemetry and the capture count stay; a write
 * of the set-up in force, or a refused one, clears nothing. */
TEST_CASE(a_new_set_up_forgets_what_the_old_one_read)
{
    ready_to_capture();
    CHECK_EQ(arm(LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u), 0u);
    sense_snap_t s = snapshot();
    s.cap_clipped = true;
    sense_page_publish(&pg, &s, 3u);
    sense_page_esc(&pg, true, 16.0f, true, 70.0f);
    CHECK(reg(LINK_SN_FLAGS) != 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_ARRIVED);
    outputs_arm(&o, false, 0u);
    (void)sense_page_step(&pg, false);           /* finished: kept */

    /* The set-up in force again, and a refused one: nothing cleared. */
    CHECK_EQ(i3221(0x40u, 1000u, 5u, 0u), 0u);
    CHECK_EQ(i3221(0x44u, 1000u, 5u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SN_ERRORS), 12u);
    CHECK_EQ(sreg(LINK_SS_CH_MEAN_MA), 120u);

    /* Another shunt: everything core 1 fills reads 0. */
    CHECK_EQ(i3221(0x40u, 500u, 5u, 0u), 0u);
    for (unsigned r = LINK_SN_FLAGS; r < LINK_SN_ESC_VOLTAGE_CV; ++r) {
        CHECK_EQ(reg(r), 0u);
    }
    for (unsigned r = 0; r <= LINK_SS_CH_FLAGS; ++r) {
        CHECK_EQ(sreg(r), 0u);
    }
    CHECK_EQ(sreg(LINK_SS_CAP_ARM), 0u);
    CHECK_EQ(sreg(LINK_SS_CAP_STATE), (uint16_t)LINK_CAP_IDLE);
    for (unsigned r = LINK_SS_CAP_MOVE_T; r < LINK_SS_COUNT; ++r) {
        CHECK_EQ(sreg(r), 0u);
    }
    CHECK_EQ(sreg(LINK_SS_CAP_SEQ), 4u);
    CHECK_EQ(reg(LINK_SN_ESC_VOLTAGE_CV), 1600u);
    CHECK_EQ(reg(LINK_SN_ESC_FLAGS),
             (uint16_t)(LINK_SN_ESC_VOLTAGE_OK | LINK_SN_ESC_CURRENT_OK));
    /* The capability is the set-up's, which still enables the INA3221. */
    CHECK_EQ(sense_page_caps(&pg), (uint16_t)LINK_CAP_SERVO_SENSE);

    /* The old set-up's snapshot changes nothing; the new one's fills. */
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(reg(LINK_SN_FLAGS), 0u);
    s.cfg_gen = pg.cfg_gen;
    sense_page_publish(&pg, &s, 3u);
    CHECK(reg(LINK_SN_FLAGS) != 0u);
}

TEST_CASE(the_escs_own_telemetry_goes_to_its_registers)
{
    fresh();
    sense_page_esc(&pg, true, 16.84f, true, 12.346f);
    CHECK_EQ(reg(LINK_SN_ESC_VOLTAGE_CV), 1684u);
    CHECK_EQ(reg(LINK_SN_ESC_CURRENT_CA), 1235u);
    CHECK_EQ(reg(LINK_SN_ESC_FLAGS),
             (uint16_t)(LINK_SN_ESC_VOLTAGE_OK | LINK_SN_ESC_CURRENT_OK));
    sense_page_esc(&pg, true, 900.0f, false, 12.0f);
    CHECK_EQ(reg(LINK_SN_ESC_VOLTAGE_CV), 65535u);
    CHECK_EQ(reg(LINK_SN_ESC_CURRENT_CA), 0u);
    CHECK_EQ(reg(LINK_SN_ESC_FLAGS), (uint16_t)LINK_SN_ESC_VOLTAGE_OK);
    sense_page_esc(&pg, true, -1.0f, true, 0.0f);
    CHECK_EQ(reg(LINK_SN_ESC_VOLTAGE_CV), 0u);
    sense_page_esc(NULL, true, 1.0f, true, 1.0f);
}

/* A bench_state the ESC's telemetry filled. */
static bench_state_t esc_bench(void)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.voltage = 16.0f;
    b.current = 70.0f;
    b.power = 1120.0f;
    b.voltage_min = 15.0f;
    b.current_max = 72.0f;
    b.power_max = 1200.0f;
    b.rpm = 9000.0f;
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                         | LINK_BN_RPM_OK);
    return b;
}

TEST_CASE(bench_carries_the_ina228_while_it_answers)
{
    fresh();
    CHECK_EQ(i228(0x45u, 200u, 2048u, 0u), 0u);
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    sense_snap_t s = snapshot();
    bench_state_t b = esc_bench();
    sense_page_bench(&pg, &s, 3u, false, &b);
    CHECK(pg.sensed);
    CHECK_EQ(b.flags, (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                                 | LINK_BN_RPM_OK | LINK_BN_SENSED
                                 | LINK_BN_TOTALS_OK));
    CHECK_NEAR(b.voltage, 16.8, 1e-4);
    CHECK_NEAR(b.current, 12.0, 1e-4);
    CHECK_NEAR(b.power, 201.6, 1e-2);
    CHECK_NEAR(b.voltage_min, 16.0, 1e-4);
    CHECK_NEAR(b.current_max, 30.0, 1e-4);
    CHECK_NEAR(b.power_max, 480.0, 1e-3);
    CHECK_NEAR(b.charge_mah, 1.0, 1e-5);
    CHECK_NEAR(b.energy_wh, 0.01, 1e-6);
    CHECK_NEAR(b.rpm, 9000.0, 1e-3);                     /* the ESC's */

    /* Through the BENCH page's own encoding: the existing scales. */
    uint16_t regs[LINK_BN_COUNT];
    bench_state_to_regs(&b, regs);
    CHECK_EQ(regs[LINK_BN_VOLTAGE_CV], 1680u);
    CHECK_EQ(regs[LINK_BN_CURRENT_CA], 1200u);
    CHECK_EQ(regs[LINK_BN_POWER_W], 202u);
    CHECK_EQ(regs[LINK_BN_CHARGE_MAH], 1u);
    CHECK_EQ(regs[LINK_BN_VOLT_MIN_CV], 1600u);
    CHECK_EQ(regs[LINK_BN_CURR_MAX_CA], 3000u);
    CHECK_EQ(regs[LINK_BN_POWER_MAX_W], 480u);

    /* Core 1 has not started this run yet: the peaks are the live
     * readings, and there are no totals. */
    b = esc_bench();
    sense_page_bench(&pg, &s, 4u, true, &b);
    CHECK_NEAR(b.voltage_min, 16.8, 1e-4);
    CHECK_NEAR(b.current_max, 12.0, 1e-4);
    CHECK_NEAR(b.power_max, 201.6, 1e-2);
    CHECK((b.flags & LINK_BN_TOTALS_OK) == 0u);
    CHECK_NEAR(b.charge_mah, 0.0, 1e-9);

    /* A window with no voltage: no voltage, no power. */
    s.win[SENSE_SRC_INA228].n_v = 0u;
    b = esc_bench();
    sense_page_bench(&pg, &s, 3u, true, &b);
    CHECK_EQ(b.flags & (LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK),
             (uint16_t)LINK_BN_CURRENT_OK);
    CHECK_NEAR(b.power, 0.0, 1e-9);

    /* The part drops out in the run: the fields go empty, the run's peaks
     * stay, and the ESC does not take over. */
    s = snapshot();
    s.i228 = SENSE_PART_OFFLINE;
    s.run.totals_ok = false;
    b = esc_bench();
    sense_page_bench(&pg, &s, 3u, true, &b);
    CHECK(pg.sensed);
    CHECK_EQ(b.flags, (uint16_t)(LINK_BN_RPM_OK | LINK_BN_SENSED));
    CHECK_NEAR(b.voltage, 0.0, 1e-9);
    CHECK_NEAR(b.current, 0.0, 1e-9);
    CHECK_NEAR(b.current_max, 30.0, 1e-4);
    /* The run over, still gone: the ESC's numbers are BENCH's again. */
    b = esc_bench();
    sense_page_bench(&pg, &s, 3u, false, &b);
    CHECK(!pg.sensed);
    CHECK_NEAR(b.current, 70.0, 1e-4);
    CHECK_EQ(b.flags, (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                                 | LINK_BN_RPM_OK));
    /* Gone before a run starts: never the source in it. */
    sense_page_bench(&pg, &s, 3u, true, &b);
    CHECK(!pg.sensed);

    /* Online again, then the INA228 switched off: not the source. */
    s = snapshot();
    sense_page_bench(&pg, &s, 3u, false, &b);
    CHECK(pg.sensed);
    CHECK_EQ(bus(0u, 16u, 17u, 400u, 0u), 0u);
    s.cfg_gen = pg.cfg_gen;
    b = esc_bench();
    sense_page_bench(&pg, &s, 3u, true, &b);
    CHECK(!pg.sensed);
    CHECK_NEAR(b.voltage, 16.0, 1e-4);

    /* A snapshot of an earlier set-up is not an INA228 online. */
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    s = snapshot();
    s.cfg_gen = (uint16_t)(pg.cfg_gen - 1u);
    b = esc_bench();
    sense_page_bench(&pg, &s, 3u, false, &b);
    CHECK(!pg.sensed);
    sense_page_bench(NULL, &s, 3u, false, &b);
    sense_page_bench(&pg, NULL, 3u, false, &b);
    sense_page_bench(&pg, &s, 3u, false, NULL);
}

/* The capability bits are the set-up's, not the parts' state: a part that
 * answers or stops answering moves none; an enable written moves them. */
TEST_CASE(the_capabilities_follow_the_set_up)
{
    fresh();
    CHECK_EQ(sense_page_caps(&pg), 0u);
    CHECK_EQ(bus(2u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(sense_page_caps(&pg), (uint16_t)LINK_CAP_SERVO_SENSE);
    sense_snap_t s = snapshot();
    s.i3221 = SENSE_PART_ABSENT;
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(sense_page_caps(&pg), (uint16_t)LINK_CAP_SERVO_SENSE);
    s.i3221 = SENSE_PART_ONLINE;                /* and the INA228 online */
    sense_page_publish(&pg, &s, 3u);
    CHECK_EQ(sense_page_caps(&pg), (uint16_t)LINK_CAP_SERVO_SENSE);
    CHECK_EQ(bus(3u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(sense_page_caps(&pg),
             (uint16_t)(LINK_CAP_PACK_SENSE | LINK_CAP_SERVO_SENSE));
    CHECK_EQ(bus(1u, 16u, 17u, 400u, 0u), 0u);
    CHECK_EQ(sense_page_caps(&pg), (uint16_t)LINK_CAP_PACK_SENSE);
    CHECK_EQ(sense_page_caps(NULL), 0u);
}

int main(void)
{
    RUN(a_page_starts_with_both_parts_off_at_the_modules_defaults);
    RUN(the_ina228_set_up_is_taken_when_the_driver_calibrates_it);
    RUN(the_ina3221_full_scale_follows_from_its_shunt);
    RUN(the_bus_is_refused_on_pins_that_are_not_one_blocks_pair);
    RUN(the_bus_is_refused_on_pins_something_else_holds);
    RUN(every_value_is_held_to_its_range);
    RUN(two_parts_enabled_answer_at_two_addresses);
    RUN(the_set_up_does_not_change_while_the_bank_is_armed);
    RUN(a_refused_write_stores_none_of_its_registers);
    RUN(read_only_registers_and_the_page_end_are_refused);
    RUN(no_slot_binds_a_pin_the_bus_holds);
    RUN(no_slot_binds_a_pin_core_1_still_holds);
    RUN(a_capture_arms_whole_on_an_armed_bank);
    RUN(a_capture_is_refused_what_it_cannot_time);
    RUN(a_capture_is_refused_on_a_slot_the_silicon_did_not_bind);
    RUN(a_disarm_is_never_refused_and_stores_nothing_beside_it);
    RUN(a_bank_that_stops_driving_ends_an_unfinished_capture);
    RUN(the_channel_flags_and_the_arm_word_pack_as_documented);
    RUN(the_generations_move_with_what_they_cover);
    RUN(the_order_carries_the_page);
    RUN(a_snapshot_fills_the_read_only_registers);
    RUN(the_flags_and_the_ranges_follow_the_snapshot);
    RUN(a_new_set_up_forgets_what_the_old_one_read);
    RUN(the_escs_own_telemetry_goes_to_its_registers);
    RUN(bench_carries_the_ina228_while_it_answers);
    RUN(the_capabilities_follow_the_set_up);
    return test_summary("sense_page");
}
