/*
 * The INA228's codec and driver, and the sensor bus under both drivers,
 * against a modelled part (fake_ina.h).
 *
 * Under test: register decoding, most significant byte first, 20-bit and
 * 40-bit with their signs; SHUNT_CAL and ADCRANGE from the shunt and the
 * maximum current, with the MATEK module's figures from the datasheet
 * equations; the ADC_CONFIG fields and the time a set-up takes; a part
 * identified before anything is written to it, its set-up read back; a
 * reading at the end of the range reported as a clip and never a value;
 * the accumulators and their clearing; three failures in a row taking the
 * part offline and a probe a second later bringing it back; the bus marked
 * stuck by a held line or two timeouts, and freed by a recovery.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fake_ina.h"
#include "ina228.h"
#include "sense_bus.h"

#define MATEK_UOHM   200u
#define MATEK_MAX_MA 204800u
#define MATEK_ADDR   0x45u

static fake_bus_t   fb;
static sense_bus_t  bus;
static ina228_t     d;
static fake_part_t *part;

/* A MATEK module on the bus, set up and probed. */
static void matek(void)
{
    fake_bus_init(&fb, &bus);
    part = fake_add(&fb, FAKE_INA228, MATEK_ADDR, 200e-6);
    CHECK_EQ(ina228_init(&d, &bus, MATEK_ADDR, MATEK_UOHM, MATEK_MAX_MA,
                         INA228_ADC_BENCH), INA228_SETUP_OK);
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

TEST_CASE(registers_decode_most_significant_byte_first_with_their_signs)
{
    const uint8_t b16[2] = { 0x54, 0x49 };
    const uint8_t b24[3] = { 0x12, 0x34, 0x56 };
    const uint8_t b40[5] = { 0x80, 0x00, 0x00, 0x00, 0x01 };
    CHECK_EQ(sense_be16(b16), 0x5449);
    CHECK_EQ(sense_be24(b24), 0x123456);
    CHECK(sense_be40(b40) == 0x8000000001ull);

    /* 20 bits in 23-4, bits 3-0 ignored (Tables 7-9, 7-10, 7-12). */
    CHECK_EQ(ina228_code20(0x7FFFF0u), 524287);
    CHECK_EQ(ina228_code20(0x7FFFFFu), 524287);
    CHECK_EQ(ina228_code20(0x800000u), -524288);
    CHECK_EQ(ina228_code20(0xFFFFF0u), -1);
    CHECK_EQ(ina228_code20(0x000010u), 1);
    CHECK_EQ(ina228_code20(0u), 0);

    CHECK_EQ(sense_sext64(0x8000000000ull, 40u), -549755813888ll);
    CHECK_EQ(sense_sext64(0xFFFFFFFFFFull, 40u), -1);
    CHECK_EQ(sense_sext64(0x7FFFFFFFFFull, 40u), 549755813887ll);
    CHECK_EQ(sense_sext64(0xFFFFFFFFFFFFFFFFull, 64u), -1);
    CHECK_EQ(sense_sext32(0xFFFFFFFFu, 32u), -1);
    CHECK_EQ(sense_sext32(0x80000000u, 32u), -2147483647 - 1);
    CHECK_EQ(sense_sext32(0x1FFFu, 13u), -1);

    /* DIETEMP: 7.8125 m°C a step, two's complement (Table 7-11). */
    CHECK_EQ(ina228_dietemp_mdegc(0x0C80u), 25000);     /* 3200 steps */
    CHECK_EQ(ina228_dietemp_mdegc(0xFB00u), -10000);    /* -1280 steps */
    /* VBUS: 195.3125 µV a step (Table 7-10); 85 V is 435200 steps. */
    CHECK_EQ(ina228_vbus_uv(435200), 85000000);
    CHECK_EQ(ina228_vbus_uv(16), 3125);
}

TEST_CASE(a_product_too_wide_for_63_bits_is_divided_first)
{
    /* (a / div) × mul + (a % div) × mul / div, truncated towards zero. */
    CHECK_EQ(sense_muldiv(10, 3, 4), 7);
    CHECK_EQ(sense_muldiv(-10, 3, 4), -7);
    CHECK_EQ(sense_muldiv(-1, 3, 2), -1);
    const int64_t big = (int64_t)1 << 51;
    CHECK_EQ(sense_muldiv(big, 78125u, 1024u * 800u), 214748364800000ll);
}

/* -------------------------------------------------------- calibration */

TEST_CASE(the_matek_module_calibrates_as_the_datasheet_equations_say)
{
    ina228_cal_t cal;
    CHECK_EQ(ina228_calibrate(MATEK_UOHM, MATEK_MAX_MA, &cal), INA228_SETUP_OK);
    /* 204.8 A × 200 µΩ = 40.96 mV: the narrow range, SHUNT_CAL 1024 × 4. */
    CHECK_EQ(cal.adcrange, 1);
    CHECK_EQ(cal.shunt_cal, 4096);

    /* CURRENT_LSB 390.625 µA: 256000 steps are 100 A. */
    sense_value_t v = ina228_current_ua(&cal, 256000);
    CHECK_EQ(v.clip, SENSE_CLIP_NONE);
    CHECK_EQ(v.value, 100000000);
    CHECK_EQ(ina228_current_ua(&cal, -256000).value, -100000000);
    CHECK_EQ(ina228_current_ua(&cal, 1).value, 390);     /* truncated */
    CHECK_EQ(ina228_current_ua(&cal, 8).value, 3125);

    /* Power 1.25 mW, energy 20 mJ, charge 390.625 µC a step (Eq. 5-7). */
    CHECK(ina228_power_uw(&cal, 1u) == 1250u);
    CHECK(ina228_power_uw(&cal, 0xFFFFFFu) == 20971518750ull);
    CHECK(ina228_energy_mj(&cal, 1u) == 20u);
    CHECK(ina228_energy_mj(&cal, 0xFFFFFFFFFFull) == 21990232555500ull);
    CHECK_EQ(ina228_charge_uc(&cal, 1024u), 400000);
    CHECK_EQ(ina228_charge_uc(&cal, 0xFFFFFFFC00ull), -400000);
    CHECK_EQ(ina228_charge_uc(&cal, 0x8000000000ull), -214748364800000ll);

    /* VSHUNT at ADCRANGE 1: 78.125 nV a step. */
    CHECK_EQ(ina228_vshunt_nv(&cal, 8).value, 625);
}

TEST_CASE(adcrange_follows_the_maximum_across_the_shunt)
{
    ina228_cal_t cal;
    /* One mA past 40.96 mV over 200 µΩ: the wide range. */
    CHECK_EQ(ina228_calibrate(200u, 204801u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.adcrange, 0);
    CHECK_EQ(cal.shunt_cal, 1025);                     /* 1024.005, up */
    CHECK_EQ(ina228_vshunt_nv(&cal, 2).value, 625);    /* 312.5 nV */
    /* 819.2 A over 200 µΩ is 163.84 mV, the top of the wide range. */
    CHECK_EQ(ina228_calibrate(200u, 819200u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.adcrange, 0);
    CHECK_EQ(cal.shunt_cal, 4096);
    CHECK_EQ(ina228_calibrate(200u, 819201u, &cal), INA228_SETUP_OVER_RANGE);
    /* 1000 A is the most taken, whatever the shunt: 1 µΩ reads to
     * 163840 A. */
    CHECK_EQ(ina228_calibrate(1u, 1000000u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.shunt_cal, 100);
    CHECK_EQ(ina228_current_ua(&cal, 524286).value, 999996185);
    CHECK_EQ(ina228_calibrate(1u, 1000001u, &cal), INA228_SETUP_OVER_RANGE);

    CHECK_EQ(ina228_calibrate(0u, 1000u, &cal), INA228_SETUP_NO_SHUNT);
    CHECK_EQ(ina228_calibrate(200u, 0u, &cal), INA228_SETUP_NO_MAX);
    /* 40 mA over 200 µΩ: 8 µV, SHUNT_CAL 0.8 -- the CURRENT register would
     * read 0 (§7.3.2). */
    CHECK_EQ(ina228_calibrate(200u, 49u, &cal), INA228_SETUP_UNDER_LSB);
    CHECK_EQ(ina228_calibrate(200u, 50u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.shunt_cal, 1);
}

TEST_CASE(shunt_cal_rounds_up_so_the_register_reaches_the_maximum)
{
    ina228_cal_t cal;
    /* 333 µΩ, 101 A: 33.6 mV, narrow; 101000 × 333 × 4 / 40000 = 3363.3. */
    CHECK_EQ(ina228_calibrate(333u, 101000u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.adcrange, 1);
    CHECK_EQ(cal.shunt_cal, 3364);
    /* CURRENT_LSB × 2^19 at or above the maximum (Equation 3). */
    CHECK((uint64_t)cal.shunt_cal * 40000u >= 101000ull * 333u * 4u);
    /* And the readings use the LSB that SHUNT_CAL gives: 2^19 - 2 steps
     * come to more than 101 A, less than 101.05 A. */
    const sense_value_t v = ina228_current_ua(&cal, 524286);
    CHECK_EQ(v.clip, SENSE_CLIP_NONE);
    CHECK(v.value > 101000000 && v.value < 101050000);
}

/* ---------------------------------------------------------- set-up */

TEST_CASE(adc_config_packs_its_fields_and_times_a_cycle)
{
    /* The reset value FB68h is continuous, 1052 µs each, averaging 1. */
    CHECK_EQ(ina228_adc_config(0xF, 5, 5, 5, 0), INA228_ADC_RESET);
    CHECK_EQ(ina228_adc_config(INA228_MODE_CONT_ALL, 4, 4, 0, 0), INA228_ADC_BENCH);
    CHECK_EQ(ina228_cycle_us(INA228_ADC_RESET), 3156);
    CHECK_EQ(ina228_cycle_us(INA228_ADC_BENCH), 1130);   /* 885 Hz */
    /* Averaging repeats the whole sequence: 4 × 610 µs. */
    CHECK_EQ(ina228_cycle_us(ina228_adc_config(INA228_MODE_CONT_VI, 3, 3, 0, 1)),
             2240);
    CHECK_EQ(ina228_cycle_us(ina228_adc_config(INA228_MODE_CONT_SHUNT, 7, 0, 7, 0)),
             50);
    CHECK_EQ(ina228_cycle_us(ina228_adc_config(INA228_MODE_CONT_BUS, 7, 0, 0, 7)),
             4120u * 1024u);
    CHECK_EQ(ina228_cycle_us(ina228_adc_config(0xC, 0, 0, 6, 0)), 2074);
    CHECK_EQ(ina228_cycle_us(ina228_adc_config(INA228_MODE_SHUTDOWN, 0, 0, 0, 0)), 0);
    CHECK_EQ(ina228_cycle_us(ina228_adc_config(0x8, 0, 0, 0, 0)), 0);
    CHECK_EQ(ina228_cycle_us(ina228_adc_config(0x7, 0, 0, 0, 0)), 0);
    /* Fields are masked to their widths. */
    CHECK_EQ(ina228_adc_config(0x1F, 0xF, 0, 0, 0xF), 0xFE07);
    CHECK_EQ(ina228_averages(7), 1024);
}

TEST_CASE(the_address_must_be_one_the_pins_can_give)
{
    fake_bus_init(&fb, &bus);
    CHECK_EQ(ina228_init(&d, &bus, 0x3F, MATEK_UOHM, MATEK_MAX_MA,
                         INA228_ADC_BENCH), INA228_SETUP_BAD_ADDR);
    CHECK_EQ(ina228_state(&d), SENSE_PART_UNSET);
    CHECK(!ina228_step(&d, 0));
    CHECK_EQ(ina228_init(&d, &bus, 0x50, MATEK_UOHM, MATEK_MAX_MA,
                         INA228_ADC_BENCH), INA228_SETUP_BAD_ADDR);
    CHECK_EQ(ina228_init(&d, &bus, 0x45, 0, MATEK_MAX_MA,
                         INA228_ADC_BENCH), INA228_SETUP_NO_SHUNT);
    CHECK(!ina228_step(&d, 0));
    sense_value_t v = { 7, SENSE_CLIP_NONE };
    CHECK_EQ(ina228_read_current(&d, &v), SENSE_OFFLINE);
    CHECK_EQ(v.value, 7);
    CHECK_EQ(fb.transactions, 0);              /* an unset part is never sent to */
}

TEST_CASE(a_probe_writes_the_setup_and_reads_it_back)
{
    matek();
    part->energy = 1234u;
    part->charge = 5678u;
    CHECK_EQ(ina228_state(&d), SENSE_PART_UNPROBED);
    CHECK(ina228_step(&d, 0));
    CHECK_EQ(ina228_state(&d), SENSE_PART_ONLINE);
    CHECK_EQ(part->reg[INA228_CONFIG], INA228_CONFIG_ADCRANGE);
    CHECK_EQ(part->reg[INA228_SHUNT_CAL], 4096);
    CHECK_EQ(part->reg[INA228_ADC_CONFIG], INA228_ADC_BENCH);
    CHECK(part->energy == 0u && part->charge == 0u);  /* RSTACC on the way */
    CHECK_EQ(part->writes[INA228_CONFIG], 3);
    CHECK_EQ(fb.transactions, 10);
    CHECK_EQ(fb.bad_width, 0);
    /* Online: not probed again. */
    CHECK(ina228_step(&d, 5000));
    CHECK_EQ(fb.transactions, 10);
}

TEST_CASE(nothing_is_written_to_a_part_that_is_not_an_ina228)
{
    matek();
    part->device = 0x2380u;                     /* an INA238 */
    CHECK(!ina228_step(&d, 0));
    CHECK_EQ(ina228_state(&d), SENSE_PART_WRONG_ID);
    CHECK_EQ(d.part.id_maker, 0x5449);
    CHECK_EQ(d.part.id_device, 0x2380);
    CHECK_EQ(writes_to(part), 0);

    matek();
    part->maker = 0x1408u;
    CHECK(!ina228_step(&d, 0));
    CHECK_EQ(ina228_state(&d), SENSE_PART_WRONG_ID);
    CHECK_EQ(writes_to(part), 0);

    /* Another revision of the die is an INA228 (Table 7-24, REV_ID). */
    CHECK(ina228_identity_ok(0x5449u, 0x2282u));
    CHECK(!ina228_identity_ok(0x5449u, 0x2381u));
}

TEST_CASE(a_setup_that_does_not_read_back_is_refused_and_tried_again)
{
    matek();
    part->ignore_writes = true;
    CHECK(!ina228_step(&d, 0));
    CHECK_EQ(ina228_state(&d), SENSE_PART_REFUSED);
    const unsigned t = fb.transactions;
    CHECK(!ina228_step(&d, 999));
    CHECK_EQ(fb.transactions, t);               /* not before a second */
    part->ignore_writes = false;
    CHECK(ina228_step(&d, 1000));
    CHECK_EQ(ina228_state(&d), SENSE_PART_ONLINE);
}

TEST_CASE(nothing_at_the_address_is_absent_and_probed_every_second)
{
    matek();
    part->present = false;
    CHECK(!ina228_step(&d, 0));
    CHECK_EQ(ina228_state(&d), SENSE_PART_ABSENT);
    CHECK_EQ(fb.transactions, 1);               /* one NACK, nothing more */
    CHECK_EQ(bus.errors, 1);
    CHECK(!bus.stuck);                          /* a NACK frees the bus */
    CHECK(!ina228_step(&d, 500));
    CHECK_EQ(fb.transactions, 1);
    CHECK(!ina228_step(&d, 1000));
    CHECK_EQ(fb.transactions, 2);
    part->present = true;
    CHECK(ina228_step(&d, 2000));
}

TEST_CASE(a_probe_that_fails_part_way_stops_and_leaves_the_part_absent)
{
    /* Two identity reads, five writes, three read-backs: each failing in
     * turn ends the probe there. */
    for (unsigned at = 1; at <= 10; ++at) {
        matek();
        fb.fail_with = SENSE_NACK;
        fb.fail_at   = at;
        CHECK(!ina228_step(&d, 0));
        CHECK_EQ(ina228_state(&d), SENSE_PART_ABSENT);
        CHECK_EQ(fb.transactions, at);
    }
}

/* ----------------------------------------------------------- readings */

TEST_CASE(readings_decode_as_the_part_reports_them)
{
    matek();
    part->amps[0]  = 100.0;
    part->volts[0] = 16.8;
    part->degc     = 31.25;
    CHECK(ina228_step(&d, 0));

    sense_value_t ua = { 0, SENSE_CLIP_NONE };
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_NONE);
    CHECK_EQ(ua.value, 100000000);

    sense_value_t nv = { 0, SENSE_CLIP_NONE };
    CHECK_EQ(ina228_read_vshunt(&d, &nv), SENSE_OK);
    CHECK_EQ(nv.value, 20000000);               /* 20 mV over 200 µΩ */

    int32_t uv = 0;
    CHECK_EQ(ina228_read_vbus(&d, &uv), SENSE_OK);
    CHECK_NEAR(uv, 16800000, 98);               /* half a 195.3 µV step */

    int32_t mdegc = 0;
    CHECK_EQ(ina228_read_dietemp(&d, &mdegc), SENSE_OK);
    CHECK_EQ(mdegc, 31250);

    uint64_t uw = 0;
    CHECK_EQ(ina228_read_power(&d, &uw), SENSE_OK);
    CHECK_NEAR((double)uw, 1680e6, 625);        /* 1680 W, half a step */

    part->amps[0] = -12.5;                      /* regenerating */
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ua.value, -12500000);
    CHECK_EQ(fb.bad_width, 0);
}

TEST_CASE(a_current_at_the_top_of_the_range_is_a_clip_never_a_value)
{
    matek();
    CHECK(ina228_step(&d, 0));
    sense_value_t ua = { 0, SENSE_CLIP_NONE };

    part->amps[0] = 300.0;                      /* past 204.8 A */
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_HIGH);
    CHECK_EQ(ua.value, 0);
    part->amps[0] = 204.8;                      /* at the top */
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_HIGH);
    part->amps[0] = -300.0;
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_LOW);
    CHECK_EQ(ua.value, 0);

    /* One step under the top is a value. */
    part->amps[0] = 204.8 - 2 * 390.625e-6;
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ua.clip, SENSE_CLIP_NONE);
    CHECK_NEAR(ua.value, 204799218, 1);

    sense_value_t nv = { 0, SENSE_CLIP_NONE };
    part->amps[0] = 300.0;
    CHECK_EQ(ina228_read_vshunt(&d, &nv), SENSE_OK);
    CHECK_EQ(nv.clip, SENSE_CLIP_HIGH);
    CHECK_EQ(nv.value, 0);
    part->amps[0] = -300.0;
    CHECK_EQ(ina228_read_vshunt(&d, &nv), SENSE_OK);
    CHECK_EQ(nv.clip, SENSE_CLIP_LOW);
}

TEST_CASE(the_accumulators_decode_forty_bits_and_clear)
{
    matek();
    CHECK(ina228_step(&d, 0));
    part->energy = 0xFFFFFFFFFFull;
    part->charge = 0xFFFFFFFC00ull;             /* -1024 steps */

    uint64_t mj = 0;
    int64_t  uc = 0;
    CHECK_EQ(ina228_read_energy(&d, &mj), SENSE_OK);
    CHECK(mj == 21990232555500ull);             /* 2^40 - 1 steps of 20 mJ */
    CHECK_EQ(ina228_read_charge(&d, &uc), SENSE_OK);
    CHECK_EQ(uc, -400000);

    uint16_t diag = 0;
    part->reg[INA228_DIAG_ALRT] = INA228_DIAG_MATHOF | INA228_DIAG_MEMSTAT;
    CHECK_EQ(ina228_read_diag(&d, &diag), SENSE_OK);
    CHECK_EQ(diag, INA228_DIAG_MATHOF | INA228_DIAG_MEMSTAT);

    CHECK_EQ(ina228_clear_totals(&d), SENSE_OK);
    CHECK(part->energy == 0u && part->charge == 0u);
    CHECK_EQ(part->reg[INA228_CONFIG], INA228_CONFIG_ADCRANGE);  /* RSTACC back */
    CHECK_EQ(fb.bad_width, 0);

    /* A clear whose first write fails writes nothing more. */
    part->energy  = 5u;
    fb.fail_with  = SENSE_NACK;
    fb.fail_count = 1;
    const unsigned w = part->writes[INA228_CONFIG];
    CHECK_EQ(ina228_clear_totals(&d), SENSE_NACK);
    CHECK_EQ(part->writes[INA228_CONFIG], w);
    CHECK(part->energy == 5u);
}

TEST_CASE(a_failed_read_leaves_the_output_as_it_was)
{
    matek();
    CHECK(ina228_step(&d, 0));
    fb.fail_with  = SENSE_TIMEOUT;
    fb.fail_count = 9;
    sense_value_t v = { 11, SENSE_CLIP_NONE };
    int32_t i = 12;
    uint64_t u = 13;
    int64_t s = 14;
    uint16_t g = 15;
    CHECK_EQ(ina228_read_current(&d, &v), SENSE_TIMEOUT);
    CHECK_EQ(ina228_read_vshunt(&d, &v), SENSE_TIMEOUT); /* the bus sticks */
    CHECK_EQ(v.value, 11);
    CHECK_EQ(ina228_read_vbus(&d, &i), SENSE_STUCK);     /* three failures */
    CHECK_EQ(ina228_read_vbus(&d, &i), SENSE_OFFLINE);
    CHECK_EQ(ina228_read_dietemp(&d, &i), SENSE_OFFLINE);
    CHECK_EQ(i, 12);
    CHECK_EQ(ina228_read_power(&d, &u), SENSE_OFFLINE);
    CHECK_EQ(ina228_read_energy(&d, &u), SENSE_OFFLINE);
    CHECK(u == 13u);
    CHECK_EQ(ina228_read_charge(&d, &s), SENSE_OFFLINE);
    CHECK_EQ(s, 14);
    CHECK_EQ(ina228_read_diag(&d, &g), SENSE_OFFLINE);
    CHECK_EQ(g, 15);
    CHECK_EQ(ina228_clear_totals(&d), SENSE_OFFLINE);
}

/* ------------------------------------------------------------- offline */

TEST_CASE(three_failed_reads_in_a_row_take_the_part_offline)
{
    matek();
    part->amps[0] = 1.0;
    CHECK(ina228_step(&d, 0));
    sense_value_t ua = { 0, SENSE_CLIP_NONE };

    /* Two failures and a success: still online, the run starts again. */
    fb.fail_with  = SENSE_NACK;
    fb.fail_count = 2;
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_NACK);
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_NACK);
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ina228_state(&d), SENSE_PART_ONLINE);
    fb.fail_count = 2;
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_NACK);
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_NACK);
    CHECK_EQ(ina228_state(&d), SENSE_PART_ONLINE);

    /* The third in a row. */
    CHECK(ina228_step(&d, 40));
    fb.fail_count = 1;
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_NACK);
    CHECK_EQ(ina228_state(&d), SENSE_PART_OFFLINE);
    CHECK_EQ(d.part.last_err, SENSE_NACK);

    /* Offline: nothing goes out until the probe a second on. */
    const unsigned t = fb.transactions;
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OFFLINE);
    CHECK(!ina228_step(&d, 1039));
    CHECK_EQ(fb.transactions, t);
    CHECK(ina228_step(&d, 1040));
    CHECK_EQ(ina228_state(&d), SENSE_PART_ONLINE);
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
    CHECK_EQ(ua.value, 1000000);
}

TEST_CASE(a_failed_write_counts_like_a_failed_read)
{
    matek();
    CHECK(ina228_step(&d, 0));
    fb.fail_with  = SENSE_NACK;
    fb.fail_count = 3;
    CHECK_EQ(ina228_clear_totals(&d), SENSE_NACK);
    CHECK_EQ(ina228_clear_totals(&d), SENSE_NACK);
    CHECK_EQ(ina228_clear_totals(&d), SENSE_NACK);
    CHECK_EQ(ina228_state(&d), SENSE_PART_OFFLINE);
}

/* ---------------------------------------------------------- bus stuck */

TEST_CASE(a_line_held_low_sticks_the_bus_until_a_recovery_frees_it)
{
    matek();
    CHECK(ina228_step(&d, 0));
    sense_value_t ua = { 0, SENSE_CLIP_NONE };

    fb.low = true;
    CHECK(!sense_bus_recovery_due(&bus, 10));
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_BUS_LOW);
    CHECK(bus.stuck);
    /* Nothing goes out while stuck; the reads still count as failures. */
    const unsigned t = fb.transactions;
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_STUCK);
    CHECK_EQ(fb.transactions, t);

    /* A recovery is due at once, then every 100 ms while the line stays
     * low. */
    CHECK(sense_bus_recovery_due(&bus, 10));
    sense_bus_recovered(&bus, 10);
    CHECK(!sense_bus_recovery_due(&bus, 109));
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_BUS_LOW);  /* the test */
    CHECK_EQ(fb.transactions, t + 1);
    CHECK(bus.stuck);
    CHECK_EQ(ina228_state(&d), SENSE_PART_OFFLINE);
    CHECK(!sense_bus_recovery_due(&bus, 109));
    CHECK(sense_bus_recovery_due(&bus, 110));

    /* The line released: the test after the next recovery frees the bus. */
    fb.low = false;
    sense_bus_recovered(&bus, 110);
    CHECK_EQ(bus.recoveries, 2);
    CHECK(ina228_step(&d, 1100));               /* the probe is the test */
    CHECK(!bus.stuck);
    CHECK(!sense_bus_recovery_due(&bus, 5000));
    CHECK_EQ(ina228_read_current(&d, &ua), SENSE_OK);
}

TEST_CASE(two_timeouts_in_a_row_stick_the_bus_and_one_does_not)
{
    matek();
    CHECK(ina228_step(&d, 0));
    int32_t uv = 0;
    fb.fail_with  = SENSE_TIMEOUT;
    fb.fail_count = 1;
    CHECK_EQ(ina228_read_vbus(&d, &uv), SENSE_TIMEOUT);
    CHECK(!bus.stuck);
    CHECK_EQ(ina228_read_vbus(&d, &uv), SENSE_OK);
    CHECK_EQ(bus.timeouts, 0);

    fb.fail_count = 3;
    CHECK_EQ(ina228_read_vbus(&d, &uv), SENSE_TIMEOUT);
    CHECK_EQ(ina228_read_vbus(&d, &uv), SENSE_TIMEOUT);
    CHECK(bus.stuck);
    /* A test that times out again keeps it stuck. */
    uint8_t buf[3];
    sense_bus_recovered(&bus, 0);
    CHECK(bus.trial);
    CHECK_EQ(sense_bus_read(&bus, MATEK_ADDR, INA228_VBUS, buf, 3), SENSE_TIMEOUT);
    CHECK(bus.stuck);
    CHECK_EQ(sense_bus_read(&bus, MATEK_ADDR, INA228_VBUS, buf, 3), SENSE_STUCK);
    CHECK_EQ(sense_bus_write16(&bus, MATEK_ADDR, INA228_CONFIG, 0u), SENSE_STUCK);
    /* One that is answered by a NACK frees it: the lines came back. */
    sense_bus_recovered(&bus, 100);
    fb.fail_with  = SENSE_NACK;
    fb.fail_count = 1;
    CHECK_EQ(sense_bus_read(&bus, MATEK_ADDR, INA228_VBUS, buf, 3), SENSE_NACK);
    CHECK(!bus.stuck);
    CHECK_EQ(bus.timeouts, 0);
    /* A recovery reported while the bus is free changes nothing, and a
     * fresh stick is recovered at once. */
    sense_bus_recovered(&bus, 200);
    CHECK(!bus.trial);
    fb.low = true;
    CHECK_EQ(sense_bus_read(&bus, MATEK_ADDR, INA228_VBUS, buf, 3), SENSE_BUS_LOW);
    CHECK(sense_bus_recovery_due(&bus, 201));
    fb.low = false;
    sense_bus_recovered(&bus, 201);
    CHECK_EQ(sense_bus_read(&bus, MATEK_ADDR, INA228_VBUS, buf, 3), SENSE_OK);
    CHECK(!bus.stuck);
    CHECK(!sense_bus_recovery_due(&bus, 300));
}

TEST_CASE(failed_transactions_are_counted_modulo_65536)
{
    matek();
    part->present = false;
    bus.errors = 65535u;
    CHECK(!ina228_step(&d, 0));
    CHECK_EQ(bus.errors, 0);
    CHECK(!ina228_step(&d, 1000));
    CHECK_EQ(bus.errors, 1);
}

int main(void)
{
    RUN(registers_decode_most_significant_byte_first_with_their_signs);
    RUN(a_product_too_wide_for_63_bits_is_divided_first);
    RUN(the_matek_module_calibrates_as_the_datasheet_equations_say);
    RUN(adcrange_follows_the_maximum_across_the_shunt);
    RUN(shunt_cal_rounds_up_so_the_register_reaches_the_maximum);
    RUN(adc_config_packs_its_fields_and_times_a_cycle);
    RUN(the_address_must_be_one_the_pins_can_give);
    RUN(a_probe_writes_the_setup_and_reads_it_back);
    RUN(nothing_is_written_to_a_part_that_is_not_an_ina228);
    RUN(a_setup_that_does_not_read_back_is_refused_and_tried_again);
    RUN(nothing_at_the_address_is_absent_and_probed_every_second);
    RUN(a_probe_that_fails_part_way_stops_and_leaves_the_part_absent);
    RUN(readings_decode_as_the_part_reports_them);
    RUN(a_current_at_the_top_of_the_range_is_a_clip_never_a_value);
    RUN(the_accumulators_decode_forty_bits_and_clear);
    RUN(a_failed_read_leaves_the_output_as_it_was);
    RUN(three_failed_reads_in_a_row_take_the_part_offline);
    RUN(a_failed_write_counts_like_a_failed_read);
    RUN(a_line_held_low_sticks_the_bus_until_a_recovery_frees_it);
    RUN(two_timeouts_in_a_row_stick_the_bus_and_one_does_not);
    RUN(failed_transactions_are_counted_modulo_65536);
    return test_summary("ina228");
}
