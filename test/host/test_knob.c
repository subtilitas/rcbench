/*
 * The rotary knob: the AS5600 register decode, the relative-motion mapping
 * across the 4095 to 0 wrap, and the rules that stop a knob that is gone or
 * confused from moving anything.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "knob.h"
#include "settings.h"

/* The three reads of one poll, each as the sensor returns it. */
typedef struct {
    uint8_t status;
    uint8_t raw[KNOB_WORD_LEN];
    uint8_t mag[KNOB_WORD_LEN];
} reads_t;

static void reads(reads_t *o, uint8_t status, uint16_t raw, uint16_t magnitude)
{
    o->status = status;
    o->raw[0] = (uint8_t)(raw >> 8);
    o->raw[1] = (uint8_t)raw;
    o->mag[0] = (uint8_t)(magnitude >> 8);
    o->mag[1] = (uint8_t)magnitude;
}

static bool decode(const reads_t *o, knob_reading_t *r)
{
    return knob_decode(o->status, o->raw, o->mag, r);
}

TEST_CASE(the_register_offsets_are_the_datasheets)
{
    CHECK_EQ(KNOB_I2C_ADDR, 0x36);
    CHECK_EQ(KNOB_REG_STATUS, 0x0B);
    CHECK_EQ(KNOB_REG_RAW_ANGLE, 0x0C);
    CHECK_EQ(KNOB_REG_MAGNITUDE, 0x1B);
    CHECK_EQ(KNOB_STATUS_LEN, 1);
    CHECK_EQ(KNOB_WORD_LEN, 2);
}

TEST_CASE(decode_reads_the_twelve_bit_fields_and_the_flags)
{
    reads_t b;
    knob_reading_t r;
    reads(&b, KNOB_STATUS_MD, 0x0ABC, 0x0123);
    CHECK(decode(&b, &r));
    CHECK(r.magnet && !r.too_weak && !r.too_strong);
    CHECK_EQ(r.raw, 0x0ABC);
    CHECK_EQ(r.magnitude, 0x0123);

    /* The top four bits of each high byte are not part of the value. */
    b.raw[0] |= 0xF0u;
    b.mag[0] |= 0xF0u;
    CHECK(decode(&b, &r));
    CHECK_EQ(r.raw, 0x0ABC);
    CHECK_EQ(r.magnitude, 0x0123);

    reads(&b, KNOB_STATUS_MD, 4095, 4095);
    CHECK(decode(&b, &r));
    CHECK_EQ(r.raw, 4095);
    CHECK_EQ(r.magnitude, 4095);
}

TEST_CASE(a_reading_is_unusable_unless_the_magnet_is_right)
{
    reads_t b;
    knob_reading_t r;

    reads(&b, 0, 100, 800);                       /* no magnet          */
    CHECK(!decode(&b, &r));
    CHECK(!r.magnet);

    reads(&b, KNOB_STATUS_MD | KNOB_STATUS_ML, 100, 800);   /* too weak   */
    CHECK(!decode(&b, &r));
    CHECK(r.too_weak);

    reads(&b, KNOB_STATUS_MD | KNOB_STATUS_MH, 100, 800);   /* too strong */
    CHECK(!decode(&b, &r));
    CHECK(r.too_strong);

    reads(&b, KNOB_STATUS_MD, 100, 0);            /* nothing measured    */
    CHECK(!decode(&b, &r));

    /* Bits outside MD, ML and MH are not read as flags. */
    reads(&b, 0xC7, 100, 800);
    CHECK(!decode(&b, &r));
    reads(&b, (uint8_t)(0xC7 | KNOB_STATUS_MD), 100, 800);
    CHECK(decode(&b, &r));
}

TEST_CASE(the_first_reading_sets_a_reference_and_moves_nothing)
{
    knob_t k;
    knob_reset(&k);
    CHECK_EQ(knob_feed(&k, true, 3000), 0);
    CHECK_EQ(knob_feed(&k, true, 3010), 10);
    CHECK_EQ(knob_feed(&k, true, 3005), -5);
    CHECK_EQ(knob_feed(&k, true, 3005), 0);
}

TEST_CASE(motion_is_relative_across_the_wrap)
{
    knob_t k;
    knob_reset(&k);
    (void)knob_feed(&k, true, 4090);
    /* Up through 4095 to 0: +10, not -4086. */
    CHECK_EQ(knob_feed(&k, true, 4), 10);
    /* And back down. */
    CHECK_EQ(knob_feed(&k, true, 4090), -10);
    CHECK_EQ(knob_feed(&k, true, 0), 6);
    CHECK_EQ(knob_feed(&k, true, 4095), -1);

    /* The steps of one turn, taken 128 at a time, sum to one turn. */
    knob_reset(&k);
    (void)knob_feed(&k, true, 1000);
    int total = 0;
    for (int i = 1; i <= 32; ++i) {
        total += knob_feed(&k, true, (uint16_t)((1000 + 128 * i) % KNOB_COUNTS));
    }
    CHECK_EQ(total, KNOB_COUNTS);
}

TEST_CASE(a_reading_out_of_range_is_taken_modulo_a_turn)
{
    knob_t k;
    knob_reset(&k);
    (void)knob_feed(&k, true, 10);
    CHECK_EQ(knob_feed(&k, true, 4096 + 20), 10);
}

TEST_CASE(a_knob_that_stops_answering_holds_no_value)
{
    knob_t k;
    knob_reset(&k);
    (void)knob_feed(&k, true, 500);
    CHECK_EQ(knob_feed(&k, true, 510), 10);

    /* No answer, or an unusable reading: no motion, and the reference goes. */
    CHECK_EQ(knob_feed(&k, false, 0), 0);
    CHECK_EQ(knob_feed(&k, false, 2000), 0);
    CHECK(!k.have_ref);

    /* The knob came back somewhere else: that is a new reference, not a
     * 1490-step turn. */
    CHECK_EQ(knob_feed(&k, true, 2000), 0);
    CHECK_EQ(knob_feed(&k, true, 2003), 3);
}

TEST_CASE(a_step_over_a_quarter_turn_is_a_glitch_and_moves_nothing)
{
    knob_t k;
    knob_reset(&k);
    (void)knob_feed(&k, true, 1000);
    /* The largest step still taken, in both directions. */
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP), KNOB_MAX_STEP);
    CHECK_EQ(knob_feed(&k, true, 1000), -KNOB_MAX_STEP);
    /* One more is not. */
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP + 1), 0);
    /* And it was taken as the new reference. */
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP + 4), 3);
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP + 4 - KNOB_MAX_STEP - 1),
             0);
    /* Half a turn is ambiguous in direction and is refused the same way. */
    knob_reset(&k);
    (void)knob_feed(&k, true, 0);
    CHECK_EQ(knob_feed(&k, true, KNOB_COUNTS / 2), 0);
}

TEST_CASE(a_span_fraction_is_degrees_over_the_scale)
{
    /* A quarter turn, 90 degrees, on a 270 degree scale is a third. */
    CHECK_NEAR(knob_span_fraction(KNOB_COUNTS / 4, 270), 1.0f / 3.0f, 0.0001f);
    CHECK_NEAR(knob_span_fraction(-KNOB_COUNTS / 4, 270), -1.0f / 3.0f, 0.0001f);
    /* The scale itself is a full span. */
    CHECK_NEAR(knob_span_fraction(KNOB_COUNTS * 270 / 360, 270), 1.0f, 0.0001f);
    CHECK_NEAR(knob_span_fraction(0, 270), 0.0f, 0.0f);
    /* One step: 360/4096 degrees. */
    CHECK_NEAR(knob_span_fraction(1, 360), 1.0f / 4096.0f, 1e-7f);
}

TEST_CASE(the_scale_is_clamped_to_the_settings_range)
{
    const float lo = knob_span_fraction(100, KNOB_SCALE_DEG_MIN);
    const float hi = knob_span_fraction(100, KNOB_SCALE_DEG_MAX);
    CHECK_NEAR(knob_span_fraction(100, 1), lo, 1e-9f);
    CHECK_NEAR(knob_span_fraction(100, -50), lo, 1e-9f);
    CHECK_NEAR(knob_span_fraction(100, 100000), hi, 1e-9f);
    CHECK(lo > hi);
}

TEST_CASE(the_settings_agree_with_the_constants)
{
    settings_set_store(NULL);
    settings_init();
    const setting_def_t *en = settings_def(SET_KNOB_EN);
    const setting_def_t *sc = settings_def(SET_KNOB_SCALE);
    CHECK_EQ(en->type, SET_TYPE_BOOL);
    CHECK(!settings_get_bool(SET_KNOB_EN));            /* off by default */
    CHECK_EQ(settings_get_int(SET_KNOB_SCALE), KNOB_SCALE_DEG_DEFAULT);
    CHECK_EQ((int)sc->min, KNOB_SCALE_DEG_MIN);
    CHECK_EQ((int)sc->max, KNOB_SCALE_DEG_MAX);
    CHECK_EQ(en->cat, SET_CAT_APP);
    CHECK_EQ(sc->cat, SET_CAT_APP);

    settings_set(SET_KNOB_SCALE, 5000.0f);
    CHECK_EQ(settings_get_int(SET_KNOB_SCALE), KNOB_SCALE_DEG_MAX);
    settings_set(SET_KNOB_SCALE, 0.0f);
    CHECK_EQ(settings_get_int(SET_KNOB_SCALE), KNOB_SCALE_DEG_MIN);
    settings_adjust(SET_KNOB_EN, 1);
    CHECK(settings_get_bool(SET_KNOB_EN));
    settings_reset_all();
    CHECK(!settings_get_bool(SET_KNOB_EN));
}

int main(void)
{
    RUN(the_register_offsets_are_the_datasheets);
    RUN(decode_reads_the_twelve_bit_fields_and_the_flags);
    RUN(a_reading_is_unusable_unless_the_magnet_is_right);
    RUN(the_first_reading_sets_a_reference_and_moves_nothing);
    RUN(motion_is_relative_across_the_wrap);
    RUN(a_reading_out_of_range_is_taken_modulo_a_turn);
    RUN(a_knob_that_stops_answering_holds_no_value);
    RUN(a_step_over_a_quarter_turn_is_a_glitch_and_moves_nothing);
    RUN(a_span_fraction_is_degrees_over_the_scale);
    RUN(the_scale_is_clamped_to_the_settings_range);
    RUN(the_settings_agree_with_the_constants);
    return test_summary("knob");
}
