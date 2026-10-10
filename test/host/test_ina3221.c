/*
 * The INA3221's codec and driver, against a modelled part (fake_ina.h).
 *
 * Under test: the 13-bit registers and the datasheet's own example; the
 * Configuration fields, with CH1 in bit 14; the time a set-up takes; the
 * DAOKAI module's 0.1 Ω shunts at 400 µA a step, and a reading at the top
 * of their 1.64 A reported as a clip, never a value; a part that answers
 * 1408h refused before anything is written to it; the configuration read
 * back; a channel the set-up does not convert never read; three failures
 * in a row taking the part offline, a probe a second later bringing it
 * back; the INA228 and the INA3221 on one bus; the current an end code
 * stands for; and the Configuration read back -- a part on its reset value
 * taken offline and probed a second later, one corrupted read no reset, a
 * failed read a failed transaction.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fake_ina.h"
#include "ina228.h"
#include "ina3221.h"
#include "sense_bus.h"

#define DAOKAI_UOHM 100000u     /* R100: 0.1 Ω */
#define DAOKAI_ADDR 0x40u

static fake_bus_t   fb;
static sense_bus_t  bus;
static ina3221_t    d;
static fake_part_t *part;

static void daokai(uint16_t config)
{
    fake_bus_init(&fb, &bus);
    part = fake_add(&fb, FAKE_INA3221, DAOKAI_ADDR, 0.1);
    CHECK_EQ(ina3221_init(&d, &bus, DAOKAI_ADDR, DAOKAI_UOHM, config),
             INA3221_SETUP_OK);
}

static unsigned writes_to(const fake_part_t *p)
{
    unsigned n = 0;
    for (size_t i = 0; i < 256; ++i) {
        n += p->writes[i];
    }
    return n;
}

/* -------------------------------------------------------------- codec */

TEST_CASE(registers_hold_thirteen_bits_in_15_to_3)
{
    CHECK_EQ(ina3221_code13(0x7FF8u), 4095);
    CHECK_EQ(ina3221_code13(0x8000u), -4096);
    CHECK_EQ(ina3221_code13(0x0008u), 1);
    CHECK_EQ(ina3221_code13(0x0007u), 0);       /* bits 2-0 reserved */
    CHECK_EQ(ina3221_code13(0xFFF8u), -1);
    /* The datasheet's example: -80 mV is C180h (§8.2.2). */
    CHECK_EQ(ina3221_code13(0xC180u), -2000);
    CHECK_EQ(ina3221_bus_mv(ina3221_code13(0x7FF8u)), 32760);  /* §8.2.3 */
    CHECK_EQ(ina3221_bus_mv(625), 5000);
}

TEST_CASE(the_daokai_shunt_reads_400_microamps_a_step_to_1638_milliamps)
{
    sense_value_t v = ina3221_current_ua(DAOKAI_UOHM, 1);
    CHECK_EQ(v.clip, SENSE_CLIP_NONE);
    CHECK_EQ(v.value, 400);
    CHECK_EQ(ina3221_current_ua(DAOKAI_UOHM, 4094).value, 1637600);
    CHECK_EQ(ina3221_current_ua(DAOKAI_UOHM, -4095).value, -1638000);

    v = ina3221_current_ua(DAOKAI_UOHM, 4095);
    CHECK_EQ(v.clip, SENSE_CLIP_HIGH);
    CHECK_EQ(v.value, 0);
    v = ina3221_current_ua(DAOKAI_UOHM, -4096);
    CHECK_EQ(v.clip, SENSE_CLIP_LOW);
    CHECK_EQ(v.value, 0);

    /* 0.05 Ω: 800 µA a step; 100 µΩ, the smallest taken: 400 mA. */
    CHECK_EQ(ina3221_current_ua(50000u, 1).value, 800);
    CHECK_EQ(ina3221_current_ua(INA3221_SHUNT_MIN_UOHM, 4094).value, 1637600000);
}

/* ------------------------------------------------------------ set-up */

TEST_CASE(channel_one_is_bit_14_and_the_fields_pack)
{
    CHECK_EQ(ina3221_config(1u, 0, 0, 0, INA3221_MODE_CONT), INA3221_CONFIG_BENCH_CH1);
    CHECK_EQ(ina3221_config(7u, 0, 0, 0, INA3221_MODE_CONT), INA3221_CONFIG_BENCH_ALL);
    CHECK_EQ(ina3221_config(2u, 0, 0, 0, 0), INA3221_CONFIG_CH2EN);
    CHECK_EQ(ina3221_config(4u, 0, 0, 0, 0), INA3221_CONFIG_CH3EN);
    /* The power-on value: all channels, averaging 1, 1.1 ms, continuous. */
    CHECK_EQ(ina3221_config(7u, 0, 4, 4, 7), INA3221_CONFIG_RESET);
    CHECK_EQ(ina3221_config(0u, 0xF, 0xF, 0xF, 0xF), 0x0FFF);
    CHECK_EQ(ina3221_channels(INA3221_CONFIG_RESET), 7);
    CHECK_EQ(ina3221_channels(INA3221_CONFIG_BENCH_CH1), 1);
    CHECK_EQ(ina3221_channels(INA3221_CONFIG_CH3EN), 4);
}

TEST_CASE(a_cycle_is_each_channels_shunt_and_bus_in_turn)
{
    CHECK_EQ(ina3221_cycle_us(INA3221_CONFIG_BENCH_CH1), 280);   /* 3.57 kHz */
    CHECK_EQ(ina3221_cycle_us(INA3221_CONFIG_BENCH_ALL), 840);   /* 1.19 kHz */
    CHECK_EQ(ina3221_cycle_us(INA3221_CONFIG_RESET), 6600);
    CHECK_EQ(ina3221_cycle_us(ina3221_config(7u, 0, 0, 0, INA3221_MODE_SHUNT_CONT)),
             420);
    CHECK_EQ(ina3221_cycle_us(ina3221_config(1u, 0, 7, 0, INA3221_MODE_BUS_CONT)),
             8244);
    CHECK_EQ(ina3221_cycle_us(ina3221_config(7u, 0, 0, 0, 3)), 0);  /* one shot */
    CHECK_EQ(ina3221_cycle_us(ina3221_config(7u, 0, 0, 0, 4)), 0);  /* down */
    CHECK_EQ(ina3221_cycle_us(ina3221_config(7u, 0, 0, 0, 0)), 0);
    CHECK_EQ(ina3221_ct_us(1), 204);
}

TEST_CASE(a_setup_the_part_cannot_take_is_refused_and_never_sent)
{
    fake_bus_init(&fb, &bus);
    CHECK_EQ(ina3221_init(&d, &bus, 0x44u, DAOKAI_UOHM, INA3221_CONFIG_BENCH_CH1),
             INA3221_SETUP_BAD_ADDR);
    CHECK_EQ(ina3221_init(&d, &bus, 0x3Fu, DAOKAI_UOHM, INA3221_CONFIG_BENCH_CH1),
             INA3221_SETUP_BAD_ADDR);
    CHECK_EQ(ina3221_init(&d, &bus, 0x43u, 99u, INA3221_CONFIG_BENCH_CH1),
             INA3221_SETUP_NO_SHUNT);
    CHECK_EQ(ina3221_init(&d, &bus, 0x43u, DAOKAI_UOHM, 0x0007u),
             INA3221_SETUP_NO_CHANNEL);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_UNSET);
    CHECK(!ina3221_step(&d, 0));
    CHECK(!ina3221_step(&d, 5000));
    CHECK_EQ(fb.transactions, 0);
}

TEST_CASE(a_probe_writes_the_configuration_and_reads_it_back)
{
    daokai(INA3221_CONFIG_BENCH_CH1);
    CHECK(ina3221_step(&d, 0));
    CHECK_EQ(ina3221_state(&d), SENSE_PART_ONLINE);
    CHECK_EQ(part->reg[INA3221_CONFIG], INA3221_CONFIG_BENCH_CH1);
    CHECK_EQ(fb.transactions, 4);
    CHECK_EQ(fb.bad_width, 0);
    CHECK(ina3221_step(&d, 9000));
    CHECK_EQ(fb.transactions, 4);

    /* A reset bit in the set-up is never sent: it would undo the rest. */
    daokai(INA3221_CONFIG_BENCH_ALL | INA3221_CONFIG_RST);
    CHECK(ina3221_step(&d, 0));
    CHECK_EQ(part->reg[INA3221_CONFIG], INA3221_CONFIG_BENCH_ALL);
}

TEST_CASE(a_part_that_answers_1408h_is_not_an_ina3221)
{
    /* The DAOKAI unit one buyer read: 1408h, 0000h, configuration 1101h. */
    daokai(INA3221_CONFIG_BENCH_CH1);
    part->maker  = 0x1408u;
    part->device = 0x0000u;
    part->reg[INA3221_CONFIG] = 0x1101u;
    CHECK(!ina3221_step(&d, 0));
    CHECK_EQ(ina3221_state(&d), SENSE_PART_WRONG_ID);
    CHECK_EQ(d.part.id_maker, 0x1408);
    CHECK_EQ(d.part.id_device, 0x0000);
    CHECK_EQ(writes_to(part), 0);
    CHECK_EQ(part->reg[INA3221_CONFIG], 0x1101);

    /* Probed again a second on, and still not used. */
    CHECK(!ina3221_step(&d, 999));
    CHECK_EQ(fb.transactions, 2);
    CHECK(!ina3221_step(&d, 1000));
    CHECK_EQ(fb.transactions, 4);
    CHECK_EQ(writes_to(part), 0);

    /* The right maker and another die is refused as well. */
    CHECK(!ina3221_identity_ok(0x5449u, 0x3221u));
    CHECK(ina3221_identity_ok(0x5449u, 0x3220u));
}

TEST_CASE(a_configuration_that_does_not_read_back_is_refused)
{
    daokai(INA3221_CONFIG_BENCH_CH1);
    part->ignore_writes = true;
    part->reg[INA3221_CONFIG] = 0x1101u;
    CHECK(!ina3221_step(&d, 0));
    CHECK_EQ(ina3221_state(&d), SENSE_PART_REFUSED);
    sense_value_t ua = { 3, SENSE_CLIP_NONE };
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OFFLINE);
    CHECK_EQ(ua.value, 3);
}

TEST_CASE(a_probe_that_fails_part_way_stops_there)
{
    for (unsigned at = 1; at <= 4; ++at) {
        daokai(INA3221_CONFIG_BENCH_CH1);
        fb.fail_with = SENSE_TIMEOUT;
        fb.fail_at   = at;
        CHECK(!ina3221_step(&d, 0));
        CHECK_EQ(ina3221_state(&d), SENSE_PART_ABSENT);
        CHECK_EQ(fb.transactions, at);
    }
}

/* ----------------------------------------------------------- readings */

TEST_CASE(each_channel_reads_its_own_current_and_voltage)
{
    daokai(INA3221_CONFIG_BENCH_ALL);
    part->amps[0]  = 0.5;
    part->amps[1]  = -0.2;
    part->amps[2]  = 1.2;
    part->volts[0] = 5.0;
    part->volts[1] = 6.0;
    part->volts[2] = 7.4;
    CHECK(ina3221_step(&d, 0));

    sense_value_t ua = { 0, SENSE_CLIP_NONE };
    int32_t mv = 0;
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    CHECK_EQ(ua.value, 500000);
    CHECK_EQ(ina3221_read_current(&d, 2, &ua), SENSE_OK);
    CHECK_EQ(ua.value, -200000);
    CHECK_EQ(ina3221_read_current(&d, 3, &ua), SENSE_OK);
    CHECK_EQ(ua.value, 1200000);
    CHECK_EQ(ina3221_read_bus(&d, 1, &mv), SENSE_OK);
    CHECK_EQ(mv, 5000);
    CHECK_EQ(ina3221_read_bus(&d, 2, &mv), SENSE_OK);
    CHECK_EQ(mv, 6000);
    CHECK_EQ(ina3221_read_bus(&d, 3, &mv), SENSE_OK);
    CHECK_EQ(mv, 7400);
    CHECK_EQ(part->reads[INA3221_SHUNT1 + 2], 1);
    CHECK_EQ(part->reads[INA3221_BUS1 + 4], 1);
    CHECK_EQ(fb.bad_width, 0);
}

TEST_CASE(a_current_at_the_top_of_1638_milliamps_is_a_clip_never_a_value)
{
    daokai(INA3221_CONFIG_BENCH_CH1);
    CHECK(ina3221_step(&d, 0));
    sense_value_t ua = { 0, SENSE_CLIP_NONE };

    part->amps[0] = 2.5;                        /* a stalled servo */
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_HIGH);
    CHECK_EQ(ua.value, 0);
    part->amps[0] = 1.638;                      /* the top code itself */
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_HIGH);
    part->amps[0] = 1.6376;                     /* a step under it */
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_NONE);
    CHECK_EQ(ua.value, 1637600);
    part->amps[0] = -2.0;
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_LOW);
    CHECK_EQ(ua.value, 0);
}

TEST_CASE(a_channel_the_setup_does_not_convert_is_not_read)
{
    daokai(INA3221_CONFIG_BENCH_CH1);
    CHECK(ina3221_step(&d, 0));
    const unsigned t = fb.transactions;
    sense_value_t ua = { 9, SENSE_CLIP_NONE };
    int32_t mv = 9;
    CHECK_EQ(ina3221_read_current(&d, 2, &ua), SENSE_BAD_ARG);
    CHECK_EQ(ina3221_read_current(&d, 0, &ua), SENSE_BAD_ARG);
    CHECK_EQ(ina3221_read_current(&d, 4, &ua), SENSE_BAD_ARG);
    CHECK_EQ(ina3221_read_bus(&d, 3, &mv), SENSE_BAD_ARG);
    CHECK_EQ(ua.value, 9);
    CHECK_EQ(mv, 9);
    CHECK_EQ(fb.transactions, t);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_ONLINE);  /* not counted */

    /* Shunt only: the bus registers hold nothing fresh. */
    daokai(ina3221_config(1u, 0, 0, 0, INA3221_MODE_SHUNT_CONT));
    CHECK(ina3221_step(&d, 0));
    CHECK_EQ(ina3221_read_bus(&d, 1, &mv), SENSE_BAD_ARG);
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    daokai(ina3221_config(1u, 0, 0, 0, INA3221_MODE_BUS_CONT));
    CHECK(ina3221_step(&d, 0));
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_BAD_ARG);
    CHECK_EQ(ina3221_read_bus(&d, 1, &mv), SENSE_OK);
}

TEST_CASE(mask_enable_is_read_and_its_flags_clear)
{
    daokai(INA3221_CONFIG_BENCH_CH1);
    CHECK(ina3221_step(&d, 0));
    part->reg[INA3221_MASK_ENABLE] = INA3221_ME_CF1 | INA3221_ME_TCF | INA3221_ME_CVRF;
    uint16_t me = 0;
    CHECK_EQ(ina3221_read_flags(&d, &me), SENSE_OK);
    CHECK_EQ(me, INA3221_ME_CF1 | INA3221_ME_TCF | INA3221_ME_CVRF);
    CHECK_EQ(ina3221_read_flags(&d, &me), SENSE_OK);
    CHECK_EQ(me, INA3221_ME_TCF);               /* TCF holds until reset */
}

/* ------------------------------------------------------------- offline */

TEST_CASE(three_failed_reads_in_a_row_take_the_part_offline)
{
    daokai(INA3221_CONFIG_BENCH_CH1);
    part->amps[0] = 0.25;
    CHECK(ina3221_step(&d, 100));
    sense_value_t ua = { 0, SENSE_CLIP_NONE };
    int32_t mv = 0;
    fb.fail_with  = SENSE_NACK;
    fb.fail_count = 3;
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_NACK);
    CHECK_EQ(ina3221_read_bus(&d, 1, &mv), SENSE_NACK);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_ONLINE);
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_NACK);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_OFFLINE);
    uint16_t me = 0;
    CHECK_EQ(ina3221_read_flags(&d, &me), SENSE_OFFLINE);
    CHECK(!ina3221_step(&d, 1099));
    CHECK(ina3221_step(&d, 1100));
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    CHECK_EQ(ua.value, 250000);
}

/* ---------------------------------------------------------- one bus */

/* The ends of the range as currents: codes 4095 and -4096 of 40 µV. */
TEST_CASE(an_end_code_is_1638_milliamps_on_the_daokai_shunt)
{
    CHECK_EQ(ina3221_end_ua(DAOKAI_UOHM, SENSE_CLIP_HIGH), 1638000);
    CHECK_EQ(ina3221_end_ua(DAOKAI_UOHM, SENSE_CLIP_LOW), -1638400);
    CHECK_EQ(ina3221_end_ua(DAOKAI_UOHM, SENSE_CLIP_NONE), 0);
    CHECK_EQ(ina3221_end_ua(5000u, SENSE_CLIP_HIGH), 32760000);  /* 5 mΩ */
    CHECK_EQ(ina3221_end_ua(INA3221_SHUNT_MIN_UOHM, SENSE_CLIP_LOW),
             -1638400000);
    CHECK_EQ(ina3221_end_ua(0u, SENSE_CLIP_HIGH), 0);
    /* One step past the last code that is a value. */
    CHECK_EQ(ina3221_end_ua(DAOKAI_UOHM, SENSE_CLIP_HIGH)
             - ina3221_current_ua(DAOKAI_UOHM, 4094).value, 400);
}

/* The Configuration read back: as written, the part stays; the reset
 * value twice in a row takes it offline until the next probe sets it up. */
TEST_CASE(a_configuration_lost_to_a_reset_takes_the_part_offline)
{
    daokai(INA3221_CONFIG_BENCH_CH1);
    bool lost = true;
    /* Not probed yet: nothing is sent. */
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_OFFLINE);
    CHECK(!lost);
    CHECK_EQ(fb.transactions, 0u);
    CHECK(ina3221_step(&d, 0));
    unsigned sent = fb.transactions;
    lost = true;
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_OK);
    CHECK(!lost);
    CHECK_EQ(fb.transactions, sent + 1u);        /* one read when it holds */
    CHECK_EQ(ina3221_state(&d), SENSE_PART_ONLINE);

    /* One read comes back wrong: read again, and no reset. */
    part->glitch_reg   = INA3221_CONFIG;
    part->glitch_value = INA3221_CONFIG_RESET;
    part->glitch_n     = 1u;
    sent = fb.transactions;
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_OK);
    CHECK(!lost);
    CHECK_EQ(fb.transactions, sent + 2u);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_ONLINE);

    /* The read fails on the wire: a failed transaction, no reset. */
    fb.fail_with  = SENSE_NACK;
    fb.fail_count = 1u;
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_NACK);
    CHECK(!lost);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_ONLINE);
    /* The second read fails after a first that differed. */
    part->glitch_n = 1u;
    fb.fail_at = fb.transactions + 2u;
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_NACK);
    CHECK(!lost);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_ONLINE);
    fb.fail_at = 0u;

    /* The part resets itself. */
    (void)ina3221_step(&d, 5000);
    fake_reset3221(part);
    sent = fb.transactions;
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_OK);
    CHECK(lost);
    CHECK_EQ(fb.transactions, sent + 2u);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_OFFLINE);
    sense_value_t v = { 7, SENSE_CLIP_NONE };
    CHECK_EQ(ina3221_read_current(&d, 1u, &v), SENSE_OFFLINE);
    lost = true;
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_OFFLINE);
    CHECK(!lost);
    /* Probed SENSE_RETRY_MS after the read that found it, and not before;
     * the probe writes the set-up again. */
    CHECK(!ina3221_step(&d, 5999));
    CHECK_EQ(part->reg[INA3221_CONFIG], INA3221_CONFIG_RESET);
    CHECK(ina3221_step(&d, 6000));
    CHECK_EQ(part->reg[INA3221_CONFIG], INA3221_CONFIG_BENCH_CH1);
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_OK);
    CHECK(!lost);

    /* Across the millisecond count's wrap. */
    (void)ina3221_step(&d, 0xFFFFFF00u);
    fake_reset3221(part);
    CHECK_EQ(ina3221_verify(&d, &lost), SENSE_OK);
    CHECK(lost);
    CHECK(!ina3221_step(&d, 0xFFFFFF00u + 999u));
    CHECK(ina3221_step(&d, 0xFFFFFF00u + 1000u));    /* 744 after the wrap */

    /* A part that is not online is left as it is. */
    sense_part_t p;
    sense_part_init(&p, &bus, DAOKAI_ADDR);
    sense_part_lost(&p);
    CHECK_EQ(p.state, SENSE_PART_UNPROBED);
}

TEST_CASE(both_parts_share_one_bus_and_one_stuck_line)
{
    fake_bus_init(&fb, &bus);
    fake_part_t *servo = fake_add(&fb, FAKE_INA3221, 0x40u, 0.1);
    fake_part_t *esc   = fake_add(&fb, FAKE_INA228, 0x45u, 200e-6);
    servo->amps[0] = 0.75;
    esc->amps[0]   = 42.0;
    ina228_t e;
    CHECK_EQ(ina3221_init(&d, &bus, 0x40u, DAOKAI_UOHM, INA3221_CONFIG_BENCH_CH1),
             INA3221_SETUP_OK);
    CHECK_EQ(ina228_init(&e, &bus, 0x45u, 200u, 204800u, INA228_ADC_BENCH),
             INA228_SETUP_OK);
    CHECK(ina3221_step(&d, 0));
    CHECK(ina228_step(&e, 0));

    sense_value_t ua = { 0, SENSE_CLIP_NONE };
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_OK);
    CHECK_EQ(ua.value, 750000);
    CHECK_EQ(ina228_read_current(&e, &ua), SENSE_OK);
    CHECK_EQ(ua.value, 42000000);

    /* SDA held low stops both; each part counts the reads it lost. */
    fb.low = true;
    CHECK_EQ(ina228_read_current(&e, &ua), SENSE_BUS_LOW);
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_STUCK);
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_STUCK);
    CHECK_EQ(ina3221_read_current(&d, 1, &ua), SENSE_STUCK);
    CHECK_EQ(ina3221_state(&d), SENSE_PART_OFFLINE);
    CHECK_EQ(ina228_state(&e), SENSE_PART_ONLINE);
    CHECK_EQ(d.part.last_err, SENSE_STUCK);

    /* Released and recovered: the next probe is the test, and both come
     * back. */
    fb.low = false;
    CHECK(sense_bus_recovery_due(&bus, 50));
    sense_bus_recovered(&bus, 50);
    CHECK(ina3221_step(&d, 1000));
    CHECK(!bus.stuck);
    CHECK_EQ(ina228_read_current(&e, &ua), SENSE_OK);
    CHECK_EQ(ua.value, 42000000);
}

int main(void)
{
    RUN(registers_hold_thirteen_bits_in_15_to_3);
    RUN(the_daokai_shunt_reads_400_microamps_a_step_to_1638_milliamps);
    RUN(channel_one_is_bit_14_and_the_fields_pack);
    RUN(a_cycle_is_each_channels_shunt_and_bus_in_turn);
    RUN(a_setup_the_part_cannot_take_is_refused_and_never_sent);
    RUN(a_probe_writes_the_configuration_and_reads_it_back);
    RUN(a_part_that_answers_1408h_is_not_an_ina3221);
    RUN(a_configuration_that_does_not_read_back_is_refused);
    RUN(a_probe_that_fails_part_way_stops_there);
    RUN(each_channel_reads_its_own_current_and_voltage);
    RUN(a_current_at_the_top_of_1638_milliamps_is_a_clip_never_a_value);
    RUN(a_channel_the_setup_does_not_convert_is_not_read);
    RUN(mask_enable_is_read_and_its_flags_clear);
    RUN(three_failed_reads_in_a_row_take_the_part_offline);
    RUN(an_end_code_is_1638_milliamps_on_the_daokai_shunt);
    RUN(a_configuration_lost_to_a_reset_takes_the_part_offline);
    RUN(both_parts_share_one_bus_and_one_stuck_line);
    return test_summary("ina3221");
}
