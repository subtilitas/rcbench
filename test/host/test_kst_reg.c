/*
 * The KST register image: the field table, the conversions between a raw
 * value and the displayed one, the layout fingerprint and the limits.
 *
 * Every limit is held at its value and one step beyond it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_limits.h"
#include "kst_reg.h"
#include "kst_sim.h"

#define RULE(r) KST_RULE_BIT(r)

static int32_t shown(const kst_image_t *img, kst_field_id_t id)
{
    int32_t d = -1;

    if (!kst_field_display(id, kst_field_get(img, id), &d)) {
        return -1;
    }
    return d;
}

static kst_image_t with(kst_field_id_t id, unsigned raw)
{
    kst_image_t img = sim_bench_image();

    if (!kst_field_edit(&img, id, (uint16_t)raw)) {
        memset(&img, 0xFF, sizeof(img));
    }
    return img;
}

/* The bench image with Neutral at exactly 1500.0 us. */
static kst_image_t centred(void)
{
    return with(KST_F_NEUTRAL, 3455);
}

static bool breaks(const kst_image_t *img, kst_rule_t rule)
{
    return (kst_limits_image(img) & RULE(rule)) != 0u;
}

static bool asks(const kst_image_t *backup, const kst_image_t *target,
                 kst_rule_t rule)
{
    return (kst_limits_edit(backup, target, ~(kst_field_set_t)0, NULL)
            & RULE(rule)) != 0u;
}

/* --- the field table ---------------------------------------------------------- */

TEST_CASE(the_bench_image_decodes_to_what_the_vendor_tool_shows)
{
    const kst_image_t img = sim_bench_image();

    CHECK_EQ(shown(&img, KST_F_DEAD_BAND), 20);        /* 2.0 us */
    CHECK_EQ(shown(&img, KST_F_DUTY), 96);             /* 96 % */
    CHECK_EQ(shown(&img, KST_F_BOOST), 3000);          /* 300 us */
    CHECK_EQ(shown(&img, KST_F_STRETCH), 1);
    CHECK_EQ(shown(&img, KST_F_LOCK), 1);
    CHECK_EQ(shown(&img, KST_F_PULSE_LOWER), 9000);    /* 900 us */
    CHECK_EQ(shown(&img, KST_F_NEUTRAL), 15002);       /* 1500.2 us */
    CHECK_EQ(shown(&img, KST_F_PULSE_UPPER), 21000);   /* 2100 us */
    CHECK_EQ(shown(&img, KST_F_UNCONT_POS), 15000);    /* 1500 us */
    CHECK_EQ(shown(&img, KST_F_UNCONT_TIME), 20);      /* 2.0 s */
    CHECK_EQ(shown(&img, KST_F_LEFT_ANGLE), 150);
    CHECK_EQ(shown(&img, KST_F_RIGHT_ANGLE), 150);
    CHECK_EQ(shown(&img, KST_F_SPD), 10);              /* 1.0 % */
    CHECK_EQ(shown(&img, KST_F_20K_SEL), 1);
    CHECK_EQ(shown(&img, KST_F_SOFT_START), 1);
    CHECK_EQ(shown(&img, KST_F_SPD_SEL), 0);
    CHECK_EQ(shown(&img, KST_F_ALLOW_UNCONT), 0);
    CHECK_EQ(shown(&img, KST_F_REVERSION), 0);
    CHECK_EQ(shown(&img, KST_F_GYRO), 0);
    CHECK_EQ(shown(&img, KST_F_PROT1_EN), 0);
    CHECK_EQ(shown(&img, KST_F_PROT1_CANCLE), 0);
    CHECK_EQ(shown(&img, KST_F_PROT1_TIME), 1);        /* 0.1 s */
    CHECK_EQ(shown(&img, KST_F_PROT1_PWM), 0);
    CHECK_EQ(shown(&img, KST_F_PROT_POT_ERR), 0);
    CHECK_EQ(shown(&img, KST_F_PROT_PWM_ERR), 0);
    CHECK_EQ(shown(&img, KST_F_POT_SAM_TIMES), 0);
    CHECK_EQ(shown(&img, KST_F_NODE_ADDR), 0x10);
    CHECK_EQ(shown(&img, KST_F_DUTY_COPY), 0xF5);
    CHECK_EQ(shown(&img, KST_F_R06), 0x04);
    CHECK_EQ(shown(&img, KST_F_R14), 0x01);
    CHECK(kst_fingerprint(&img, NULL));
    CHECK(kst_limits_image(&img) == 0u);
}

TEST_CASE(every_bit_of_the_32_registers_belongs_to_exactly_one_field)
{
    for (unsigned reg = 0; reg < KST_REG_COUNT; ++reg) {
        unsigned seen = 0;

        for (unsigned i = 0; i < KST_F_COUNT; ++i) {
            const unsigned mask = kst_field_reg_mask((kst_field_id_t)i,
                                                     (uint8_t)reg);

            CHECK_EQ(seen & mask, 0);
            seen |= mask;
        }
        CHECK_EQ(seen, 0xFF);
    }
    CHECK_EQ(KST_F_COUNT, 44);
    CHECK(KST_F_COUNT <= 64);
}

TEST_CASE(a_field_row_names_its_place_and_width)
{
    const kst_field_t *f = kst_field(KST_F_PULSE_UPPER);

    CHECK_STR_EQ(f->name, "Pulse Upper");
    CHECK_EQ(f->lo_reg, 0x08);
    CHECK_EQ(f->hi_reg, 0x09);
    CHECK_EQ(f->hi_shift, 4);
    CHECK_EQ(f->unit, KST_UNIT_TENTH_US);
    CHECK_EQ(kst_field_raw_max(KST_F_PULSE_UPPER), 4095);
    CHECK_EQ(kst_field_raw_max(KST_F_NEUTRAL), 8191);
    CHECK_EQ(kst_field_raw_max(KST_F_PROT1_TIME), 1023);
    CHECK_EQ(kst_field_raw_max(KST_F_DEAD_BAND), 63);
    CHECK_EQ(kst_field_raw_max(KST_F_STRETCH), 7);
    CHECK_EQ(kst_field_raw_max(KST_F_GYRO), 1);
    CHECK_EQ(kst_field_reg_mask(KST_F_PULSE_UPPER, 0x09), 0xF0);
    CHECK_EQ(kst_field_reg_mask(KST_F_PULSE_LOWER, 0x09), 0x0F);
    CHECK_EQ(kst_field_reg_mask(KST_F_PROT1_TIME, 0x0D), 0x30);
    CHECK_EQ(kst_field_reg_mask(KST_F_UNCONT_POS, 0x0D), 0x0F);
    CHECK_EQ(kst_field_reg_mask(KST_F_PULSE_UPPER, 0x07), 0x00);
    CHECK_STR_EQ(kst_field(KST_F_R00)->name, "");
    CHECK_STR_EQ(kst_field(KST_F_PROT1_CANCLE)->name, "prot1_cancle");
    CHECK_EQ(kst_field(KST_F_SPD)->unit, KST_UNIT_TENTH_PERCENT);
    CHECK_EQ(kst_field(KST_F_STRETCH)->unit, KST_UNIT_FACTOR);
    CHECK_EQ(kst_field(KST_F_PROT_POT_ERR)->unit, KST_UNIT_COUNT_OF);
}

TEST_CASE(an_id_outside_the_table_has_no_field)
{
    kst_image_t img = sim_bench_image();
    const kst_image_t before = img;
    int32_t d = 7;
    uint16_t raw = 7;

    CHECK(kst_field(KST_F_COUNT) == NULL);
    CHECK(kst_field((kst_field_id_t)200) == NULL);
    CHECK_EQ(kst_field_raw_max(KST_F_COUNT), 0);
    CHECK_EQ(kst_field_reg_mask(KST_F_COUNT, 0x01), 0);
    CHECK_EQ(kst_field_get(&img, KST_F_COUNT), 0);
    CHECK_EQ(kst_field_get(NULL, KST_F_DUTY), 0);
    CHECK(!kst_field_set(&img, KST_F_COUNT, 0));
    CHECK(!kst_field_set(NULL, KST_F_DUTY, 0));
    CHECK(!kst_field_edit(NULL, KST_F_DUTY, 0));
    CHECK(!kst_field_display(KST_F_COUNT, 0, &d));
    CHECK(!kst_field_display(KST_F_DUTY, 0, NULL));
    CHECK(!kst_field_raw_from_display(KST_F_COUNT, 0, &raw));
    CHECK(!kst_field_raw_from_display(KST_F_DUTY, 0, NULL));
    CHECK_EQ(d, 7);
    CHECK_EQ(raw, 7);
    CHECK_EQ(memcmp(&img, &before, sizeof(img)), 0);
}

TEST_CASE(the_access_classes_are_the_ones_decided)
{
    static const kst_field_id_t editable[] = {
        KST_F_DUTY, KST_F_BOOST, KST_F_STRETCH, KST_F_SOFT_START,
        KST_F_SPD_SEL, KST_F_DEAD_BAND, KST_F_ALLOW_UNCONT,
        KST_F_PULSE_LOWER, KST_F_PULSE_UPPER, KST_F_NEUTRAL,
        KST_F_REVERSION, KST_F_UNCONT_POS, KST_F_UNCONT_TIME,
        KST_F_LEFT_ANGLE, KST_F_RIGHT_ANGLE, KST_F_SPD,
    };
    static const kst_field_id_t locked[] = {
        KST_F_20K_SEL, KST_F_LOCK, KST_F_PROT1_CANCLE, KST_F_PROT1_EN,
        KST_F_PROT1_TIME, KST_F_PROT1_PWM, KST_F_PROT_POT_ERR,
        KST_F_POT_SAM_TIMES, KST_F_PROT_PWM_ERR, KST_F_GYRO,
    };
    unsigned n_editable = 0;
    unsigned n_locked = 0;
    unsigned n_never = 0;

    for (unsigned i = 0; i < sizeof(editable) / sizeof(editable[0]); ++i) {
        CHECK_EQ(kst_field(editable[i])->access, KST_ACCESS_EDITABLE);
    }
    for (unsigned i = 0; i < sizeof(locked) / sizeof(locked[0]); ++i) {
        CHECK_EQ(kst_field(locked[i])->access, KST_ACCESS_LOCKED);
    }
    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_t *f = kst_field((kst_field_id_t)i);

        n_editable += f->access == KST_ACCESS_EDITABLE ? 1u : 0u;
        n_locked += f->access == KST_ACCESS_LOCKED ? 1u : 0u;
        n_never += f->access == KST_ACCESS_NEVER ? 1u : 0u;
        /* Bits without a name have no edit path. */
        if (f->name[0] == '\0') {
            CHECK_EQ(f->access, KST_ACCESS_NEVER);
        }
    }
    CHECK_EQ(n_editable, 16);
    CHECK_EQ(n_locked, 10);
    CHECK_EQ(n_never, 18);
    CHECK_EQ(kst_field(KST_F_NODE_ADDR)->access, KST_ACCESS_NEVER);
    CHECK_EQ(kst_field(KST_F_DUTY_COPY)->access, KST_ACCESS_NEVER);
}

TEST_CASE(setting_a_field_changes_its_bits_and_no_other)
{
    for (unsigned pattern = 0; pattern < 3u; ++pattern) {
        kst_image_t base = sim_bench_image();

        if (pattern == 1u) {
            memset(&base, 0x00, sizeof(base));
        } else if (pattern == 2u) {
            memset(&base, 0xFF, sizeof(base));
        }
        for (unsigned i = 0; i < KST_F_COUNT; ++i) {
            const kst_field_id_t id = (kst_field_id_t)i;
            const unsigned max = kst_field_raw_max(id);

            for (unsigned raw = 0; raw <= max; ++raw) {
                kst_image_t img = base;

                CHECK(kst_field_set(&img, id, (uint16_t)raw));
                CHECK_EQ(kst_field_get(&img, id), raw);
                for (unsigned reg = 0; reg < KST_REG_COUNT; ++reg) {
                    const unsigned mask = kst_field_reg_mask(id, (uint8_t)reg);

                    CHECK_EQ((img.r[reg] ^ base.r[reg]) & ~mask & 0xFFu, 0);
                }
            }
            /* One beyond the width is refused and changes nothing. */
            if (max < 0xFFFFu) {
                kst_image_t img = base;

                CHECK(!kst_field_set(&img, id, (uint16_t)(max + 1u)));
                CHECK_EQ(memcmp(&img, &base, sizeof(img)), 0);
            }
        }
    }
}

TEST_CASE(an_edit_of_one_field_leaves_every_other_field_at_its_raw_value)
{
    const kst_image_t base = sim_bench_image();

    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const unsigned max = kst_field_raw_max(id);

        for (unsigned raw = 0; raw <= max; ++raw) {
            kst_image_t img = base;

            CHECK(kst_field_edit(&img, id, (uint16_t)raw));
            for (unsigned k = 0; k < KST_F_COUNT; ++k) {
                const kst_field_id_t other = (kst_field_id_t)k;

                if (other == id
                    || (id == KST_F_DUTY && other == KST_F_DUTY_COPY)) {
                    continue;
                }
                CHECK_EQ(kst_field_get(&img, other),
                         kst_field_get(&base, other));
            }
        }
    }
}

TEST_CASE(an_edit_of_max_duty_writes_the_copy_and_a_set_does_not)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, KST_F_DUTY, 200));
    CHECK_EQ(img.r[0x02], 200);
    CHECK_EQ(img.r[0x01], 200);
    CHECK(kst_field_set(&img, KST_F_DUTY, 100));
    CHECK_EQ(img.r[0x02], 100);
    CHECK_EQ(img.r[0x01], 200);
    CHECK(kst_field_edit(&img, KST_F_BOOST, 7));
    CHECK_EQ(img.r[0x01], 200);
}

/* --- conversions -------------------------------------------------------------- */

static int32_t disp(kst_field_id_t id, unsigned raw)
{
    int32_t d = -1;

    return kst_field_display(id, (uint16_t)raw, &d) ? d : -1;
}

static int32_t raw_of(kst_field_id_t id, int32_t display)
{
    uint16_t raw = 0;

    return kst_field_raw_from_display(id, display, &raw) ? (int32_t)raw : -1;
}

TEST_CASE(max_duty_is_percent_of_255_with_the_vendor_rounding)
{
    CHECK_EQ(disp(KST_F_DUTY, 0), 0);
    CHECK_EQ(disp(KST_F_DUTY, 245), 96);
    CHECK_EQ(disp(KST_F_DUTY, 250), 98);
    CHECK_EQ(disp(KST_F_DUTY, 255), 100);
    CHECK_EQ(disp(KST_F_DUTY, 127), 50);
    CHECK_EQ(disp(KST_F_DUTY, 128), 50);
    CHECK_EQ(disp(KST_F_DUTY, 26), 10);
    /* 50 % is written as 127: the tie goes down. */
    CHECK_EQ(raw_of(KST_F_DUTY, 50), 127);
    CHECK_EQ(raw_of(KST_F_DUTY, 96), 245);
    CHECK_EQ(raw_of(KST_F_DUTY, 98), 250);
    CHECK_EQ(raw_of(KST_F_DUTY, 100), 255);
    CHECK_EQ(raw_of(KST_F_DUTY, 0), 0);
    CHECK_EQ(raw_of(KST_F_DUTY, 101), -1);
    CHECK_EQ(raw_of(KST_F_DUTY, -1), -1);
    /* Every percent value survives the way to raw and back. */
    for (int32_t d = 0; d <= 100; ++d) {
        CHECK_EQ(disp(KST_F_DUTY, (unsigned)raw_of(KST_F_DUTY, d)), d);
    }
}

TEST_CASE(boost_is_50_us_plus_12_5_us_per_count)
{
    CHECK_EQ(disp(KST_F_BOOST, 0), 500);
    CHECK_EQ(disp(KST_F_BOOST, 1), 625);
    CHECK_EQ(disp(KST_F_BOOST, 20), 3000);
    CHECK_EQ(disp(KST_F_BOOST, 236), 30000);
    CHECK_EQ(disp(KST_F_BOOST, 255), 32375);
    CHECK_EQ(raw_of(KST_F_BOOST, 499), -1);
    CHECK_EQ(raw_of(KST_F_BOOST, 500), 0);
    /* Half a step, 6.25 us, is the tie: 562 rounds down, 563 up. */
    CHECK_EQ(raw_of(KST_F_BOOST, 562), 0);
    CHECK_EQ(raw_of(KST_F_BOOST, 563), 1);
    CHECK_EQ(raw_of(KST_F_BOOST, 32375), 255);
    CHECK_EQ(raw_of(KST_F_BOOST, 32437), 255);
    CHECK_EQ(raw_of(KST_F_BOOST, 32438), -1);
    for (unsigned raw = 0; raw < 256u; ++raw) {
        CHECK_EQ(raw_of(KST_F_BOOST, disp(KST_F_BOOST, raw)), (int32_t)raw);
    }
}

TEST_CASE(stretch_is_a_power_of_two_up_to_32)
{
    static const int32_t factor[] = { 1, 2, 4, 8, 16, 32 };

    for (unsigned i = 0; i < 6u; ++i) {
        CHECK_EQ(disp(KST_F_STRETCH, i), factor[i]);
        CHECK_EQ(raw_of(KST_F_STRETCH, factor[i]), (int32_t)i);
    }
    /* Index 6 and 7: what the servo makes of them is not established. */
    CHECK_EQ(disp(KST_F_STRETCH, 6), -1);
    CHECK_EQ(disp(KST_F_STRETCH, 7), -1);
    CHECK_EQ(raw_of(KST_F_STRETCH, 0), -1);
    CHECK_EQ(raw_of(KST_F_STRETCH, 3), -1);
    CHECK_EQ(raw_of(KST_F_STRETCH, 64), -1);
}

TEST_CASE(dead_band_falls_0_2_us_per_count)
{
    CHECK_EQ(disp(KST_F_DEAD_BAND, 55), 20);
    CHECK_EQ(disp(KST_F_DEAD_BAND, 60), 10);
    CHECK_EQ(disp(KST_F_DEAD_BAND, 63), 4);
    CHECK_EQ(disp(KST_F_DEAD_BAND, 0), 130);
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 20), 55);
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 10), 60);
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 4), 63);
    /* 6 bits hold no value below 0.4 us or above 13.0 us. */
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 3), -1);
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 0), -1);
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 130), 0);
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 131), -1);
    /* An odd tenth rounds to the wider band above it. */
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 19), 55);
    CHECK_EQ(raw_of(KST_F_DEAD_BAND, 21), 54);
    for (unsigned raw = 0; raw < 64u; ++raw) {
        CHECK_EQ(raw_of(KST_F_DEAD_BAND, disp(KST_F_DEAD_BAND, raw)),
                 (int32_t)raw);
    }
}

TEST_CASE(pulse_widths_are_0_8_us_per_count)
{
    static const kst_field_id_t ids[] = {
        KST_F_PULSE_LOWER, KST_F_PULSE_UPPER, KST_F_UNCONT_POS,
    };

    for (unsigned k = 0; k < 3u; ++k) {
        CHECK_EQ(disp(ids[k], 625), 5000);
        CHECK_EQ(disp(ids[k], 1875), 15000);
        CHECK_EQ(disp(ids[k], 3125), 25000);
        CHECK_EQ(raw_of(ids[k], 15000), 1875);
        CHECK_EQ(raw_of(ids[k], 15003), 1875);
        CHECK_EQ(raw_of(ids[k], 15004), 1876);
        CHECK_EQ(raw_of(ids[k], 32760), 4095);
        CHECK_EQ(raw_of(ids[k], 32763), 4095);
        CHECK_EQ(raw_of(ids[k], 32764), -1);
        for (unsigned raw = 0; raw < 4096u; ++raw) {
            CHECK_EQ(raw_of(ids[k], disp(ids[k], raw)), (int32_t)raw);
        }
    }
}

TEST_CASE(neutral_is_810_us_plus_409_2048_us_per_count)
{
    CHECK_EQ(disp(KST_F_NEUTRAL, 0), 8100);
    CHECK_EQ(disp(KST_F_NEUTRAL, 3456), 15002);
    CHECK_EQ(disp(KST_F_NEUTRAL, 3455), 15000);
    CHECK_EQ(disp(KST_F_NEUTRAL, 2754), 13600);
    CHECK_EQ(disp(KST_F_NEUTRAL, 4156), 16400);
    CHECK_EQ(disp(KST_F_NEUTRAL, 8191), 24458);
    CHECK_EQ(raw_of(KST_F_NEUTRAL, 8099), -1);
    CHECK_EQ(raw_of(KST_F_NEUTRAL, 8100), 0);
    CHECK_EQ(raw_of(KST_F_NEUTRAL, 15000), 3455);
    CHECK_EQ(raw_of(KST_F_NEUTRAL, 15002), 3456);
    CHECK_EQ(raw_of(KST_F_NEUTRAL, 24458), 8191);
    CHECK_EQ(raw_of(KST_F_NEUTRAL, 24460), -1);
    /* 13 bits: every raw value has its own displayed value. */
    for (unsigned raw = 0; raw < 8192u; ++raw) {
        CHECK_EQ(raw_of(KST_F_NEUTRAL, disp(KST_F_NEUTRAL, raw)),
                 (int32_t)raw);
        if (raw > 0u) {
            CHECK(disp(KST_F_NEUTRAL, raw) > disp(KST_F_NEUTRAL, raw - 1u));
        }
    }
}

TEST_CASE(the_protection_fields_decode_and_have_no_way_back)
{
    CHECK_EQ(disp(KST_F_PROT1_TIME, 1), 1);
    CHECK_EQ(disp(KST_F_PROT1_TIME, 1000), 1000);
    CHECK_EQ(disp(KST_F_PROT1_TIME, 1001), 1000);
    CHECK_EQ(disp(KST_F_PROT1_TIME, 1023), 1000);
    CHECK_EQ(disp(KST_F_PROT1_PWM, 0), 0);
    CHECK_EQ(disp(KST_F_PROT1_PWM, 255), 97);
    CHECK_EQ(disp(KST_F_PROT1_PWM, 128), 49);
    for (int32_t d = 0; d <= 1000; ++d) {
        CHECK_EQ(raw_of(KST_F_PROT1_TIME, d), -1);
        CHECK_EQ(raw_of(KST_F_PROT1_PWM, d), -1);
    }
}

TEST_CASE(the_error_counts_are_the_1_bits_upward_from_bit_0)
{
    CHECK_EQ(disp(KST_F_PROT_PWM_ERR, 0x00), 0);
    CHECK_EQ(disp(KST_F_PROT_PWM_ERR, 0x01), 1);
    CHECK_EQ(disp(KST_F_PROT_PWM_ERR, 0x07), 3);
    CHECK_EQ(disp(KST_F_PROT_PWM_ERR, 0xFF), 8);
    /* A gap ends the count. */
    CHECK_EQ(disp(KST_F_PROT_PWM_ERR, 0x0B), 2);
    CHECK_EQ(disp(KST_F_PROT_PWM_ERR, 0xFE), 0);
    CHECK_EQ(disp(KST_F_PROT_POT_ERR, 0x0FFF), 12);
    CHECK_EQ(disp(KST_F_PROT_POT_ERR, 0x01FF), 9);
    for (int32_t n = 0; n <= 8; ++n) {
        CHECK_EQ(disp(KST_F_PROT_PWM_ERR,
                      (unsigned)raw_of(KST_F_PROT_PWM_ERR, n)), n);
    }
    for (int32_t n = 0; n <= 12; ++n) {
        CHECK_EQ(disp(KST_F_PROT_POT_ERR,
                      (unsigned)raw_of(KST_F_PROT_POT_ERR, n)), n);
    }
    CHECK_EQ(raw_of(KST_F_PROT_PWM_ERR, 9), -1);
    CHECK_EQ(raw_of(KST_F_PROT_POT_ERR, 13), -1);
}

TEST_CASE(fields_without_a_conversion_show_the_raw_value)
{
    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *f = kst_field(id);
        const unsigned max = kst_field_raw_max(id);

        if (f->unit != KST_UNIT_RAW && f->unit != KST_UNIT_BOOL
            && id != KST_F_SPD && id != KST_F_UNCONT_TIME) {
            continue;
        }
        for (unsigned raw = 0; raw <= max; ++raw) {
            CHECK_EQ(disp(id, raw), (int32_t)raw);
            CHECK_EQ(raw_of(id, (int32_t)raw), (int32_t)raw);
        }
        CHECK_EQ(raw_of(id, (int32_t)max + 1), -1);
        CHECK_EQ(raw_of(id, -1), -1);
        CHECK_EQ(disp(id, max + 1u), -1);
    }
    CHECK_EQ(raw_of(KST_F_SPD, 65536), -1);
}

TEST_CASE(decoding_and_encoding_every_field_of_an_image_gives_the_image_back)
{
    /* The fields whose displayed value has a way back to one raw value. */
    kst_image_t images[3];

    images[0] = sim_bench_image();
    images[1] = sim_bench_image();
    images[2] = sim_bench_image();
    CHECK(kst_field_edit(&images[1], KST_F_PULSE_LOWER, 625));
    CHECK(kst_field_edit(&images[1], KST_F_PULSE_UPPER, 3125));
    CHECK(kst_field_edit(&images[1], KST_F_NEUTRAL, 4156));
    CHECK(kst_field_edit(&images[1], KST_F_STRETCH, 5));
    CHECK(kst_field_edit(&images[1], KST_F_BOOST, 236));
    CHECK(kst_field_edit(&images[2], KST_F_DEAD_BAND, 63));
    CHECK(kst_field_edit(&images[2], KST_F_SPD, 1000));
    CHECK(kst_field_edit(&images[2], KST_F_SPD_SEL, 1));
    for (unsigned k = 0; k < 3u; ++k) {
        kst_image_t img = images[k];

        for (unsigned pass = 0; pass < 4u; ++pass) {
            for (unsigned i = 0; i < KST_F_COUNT; ++i) {
                const kst_field_id_t id = (kst_field_id_t)i;
                int32_t d;
                uint16_t raw;

                if (!kst_field_display(id, kst_field_get(&img, id), &d)
                    || !kst_field_raw_from_display(id, d, &raw)) {
                    continue;
                }
                /* Max. Duty 96 % is raw 245 here; the copy stays as read. */
                CHECK(kst_field_set(&img, id, raw));
            }
            CHECK_EQ(memcmp(&img, &images[k], sizeof(img)), 0);
        }
    }
}

/* --- fingerprint -------------------------------------------------------------- */

static kst_fingerprint_t fp_of(unsigned reg, unsigned value, bool *ok)
{
    kst_image_t img = sim_bench_image();
    kst_fingerprint_t fp;

    img.r[reg] = (uint8_t)value;
    *ok = kst_fingerprint(&img, &fp);
    return fp;
}

TEST_CASE(the_measured_layout_passes_the_fingerprint)
{
    const kst_image_t img = sim_bench_image();
    kst_image_t other = img;
    kst_fingerprint_t fp;

    memset(&fp, 0xFF, sizeof(fp));
    CHECK(kst_fingerprint(&img, &fp));
    CHECK_EQ(fp.rules, 0);
    CHECK_EQ(fp.regs, 0);
    /* Settings do not enter it. */
    CHECK(kst_field_edit(&other, KST_F_DUTY, 30));
    CHECK(kst_field_edit(&other, KST_F_STRETCH, 5));
    CHECK(kst_field_edit(&other, KST_F_GYRO, 1));
    CHECK(kst_field_edit(&other, KST_F_PROT1_EN, 1));
    CHECK(kst_field_edit(&other, KST_F_PROT1_CANCLE, 1));
    CHECK(kst_field_edit(&other, KST_F_REVERSION, 1));
    CHECK(kst_field_edit(&other, KST_F_NODE_ADDR, 0));
    CHECK(kst_fingerprint(&other, &fp));
    CHECK_EQ(fp.rules, 0);
}

TEST_CASE(each_fingerprint_rule_fails_alone_and_names_its_register)
{
    bool ok = true;
    kst_fingerprint_t fp;

    fp = fp_of(0x00, 0x01, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_R00);
    CHECK_EQ(fp.regs, 1u << 0x00);

    fp = fp_of(0x01, 0xF4, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_DUTY_COPY);
    CHECK_EQ(fp.regs, (1u << 0x01) | (1u << 0x02));
    fp = fp_of(0x02, 0xF6, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_DUTY_COPY);

    fp = fp_of(0x06, 0x05, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_R06);
    CHECK_EQ(fp.regs, 1u << 0x06);
    fp = fp_of(0x06, 0x00, &ok);
    CHECK(!ok);

    fp = fp_of(0x1E, 0x26, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_R1E);
    CHECK_EQ(fp.regs, 1u << 0x1E);

    fp = fp_of(0x04, 0x48 | 0x10, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_R04_B54);
    CHECK_EQ(fp.regs, 1u << 0x04);
    fp = fp_of(0x04, 0x48 | 0x20, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_R04_B54);

    fp = fp_of(0x0A, 0x0D | 0x20, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_R0A_B5_B7);
    CHECK_EQ(fp.regs, 1u << 0x0A);
    fp = fp_of(0x0A, 0x0D | 0x80, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_R0A_B5_B7);
    /* Reversion, bit 6, is a setting. */
    fp = fp_of(0x0A, 0x0D | 0x40, &ok);
    CHECK(ok);

    for (unsigned bit = 0; bit < 8u; ++bit) {
        fp = fp_of(0x12, 1u << bit, &ok);
        if (bit == 3u || bit == 6u) {
            CHECK(ok);
        } else {
            CHECK(!ok);
            CHECK_EQ(fp.rules, 1u << KST_FP_R12_UNNAMED);
            CHECK_EQ(fp.regs, 1u << 0x12);
        }
    }

    for (unsigned bit = 1; bit < 8u; ++bit) {
        fp = fp_of(0x1F, 1u << bit, &ok);
        CHECK(!ok);
        CHECK_EQ(fp.rules, 1u << KST_FP_R1F_B71);
        CHECK_EQ(fp.regs, 1u << 0x1F);
    }
    fp = fp_of(0x1F, 0x01, &ok);
    CHECK(ok);

    fp = fp_of(0x04, 0x48 | 0x05, &ok);
    CHECK(ok);
    fp = fp_of(0x04, 0x48 | 0x06, &ok);
    CHECK(!ok);
    CHECK_EQ(fp.rules, 1u << KST_FP_STRETCH_INDEX);
    CHECK_EQ(fp.regs, 1u << 0x04);
    fp = fp_of(0x04, 0x48 | 0x07, &ok);
    CHECK(!ok);
}

TEST_CASE(a_foreign_image_lists_every_rule_and_register_it_breaks)
{
    kst_image_t img;
    kst_fingerprint_t fp;

    memset(&img, 0xFF, sizeof(img));
    CHECK(!kst_fingerprint(&img, &fp));
    /* 01 equals 02 in an image of FF: every rule but that one. */
    CHECK_EQ(fp.rules, ((1u << KST_FP_COUNT) - 1u) & ~(1u << KST_FP_DUTY_COPY));
    CHECK_EQ(fp.regs, (1u << 0x00) | (1u << 0x04) | (1u << 0x06) | (1u << 0x0A)
                          | (1u << 0x12) | (1u << 0x1E) | (1u << 0x1F));
    CHECK(!kst_fingerprint(&img, NULL));

    memset(&img, 0x00, sizeof(img));
    CHECK(!kst_fingerprint(&img, &fp));
    CHECK_EQ(fp.rules, 1u << KST_FP_R06);

    CHECK(!kst_fingerprint(NULL, &fp));
    CHECK_EQ(fp.rules, (1u << KST_FP_COUNT) - 1u);
    CHECK_EQ(fp.regs, 0xFFFFFFFFu);
    CHECK(!kst_fingerprint(NULL, NULL));
}

/* --- hard limits -------------------------------------------------------------- */

TEST_CASE(rule_numbers_are_fixed)
{
    CHECK_EQ(KST_H_DUTY_HIGH, 0);
    CHECK_EQ(KST_H_GYRO_MODE, 21);
    CHECK_EQ(KST_H_LOCKED, 22);
    CHECK_EQ(KST_H_NEVER, 23);
    CHECK_EQ(KST_S_DUTY_RAISED, 24);
    CHECK_EQ(KST_S_NARROW_BAND_GAIN, 47);
    CHECK_EQ(KST_S_HIGH_DUTY_TRAVEL, 48);
    CHECK_EQ(KST_RULE_COUNT, 49);
    CHECK(KST_RULES_HARD == 0xFFFFFFu);
    CHECK(kst_limits_image(NULL) == KST_RULES_HARD);
    CHECK(kst_limits_edit(NULL, NULL, 0, NULL) == KST_RULES_HARD);
}

TEST_CASE(max_duty_is_held_between_10_and_98_percent)
{
    kst_image_t img = with(KST_F_DUTY, KST_DUTY_RAW_MAX);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_DUTY, KST_DUTY_RAW_MAX + 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_DUTY_HIGH));
    img = with(KST_F_DUTY, 255);
    CHECK(kst_limits_image(&img) == RULE(KST_H_DUTY_HIGH));
    img = with(KST_F_DUTY, KST_DUTY_RAW_MIN);
    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_DUTY, KST_DUTY_RAW_MIN - 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_DUTY_LOW));
    img = with(KST_F_DUTY, 0);
    CHECK(kst_limits_image(&img) == RULE(KST_H_DUTY_LOW));
}

TEST_CASE(the_duty_copy_follows_max_duty)
{
    kst_image_t img = sim_bench_image();

    img.r[0x01] = 0xF4;
    CHECK(kst_limits_image(&img) == RULE(KST_H_DUTY_COPY));
    img.r[0x01] = 0xF5;
    img.r[0x02] = 0xF4;
    CHECK(kst_limits_image(&img) == RULE(KST_H_DUTY_COPY));
}

TEST_CASE(boost_ends_at_3000_us)
{
    kst_image_t img = with(KST_F_BOOST, KST_BOOST_RAW_MAX);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_BOOST, KST_BOOST_RAW_MAX + 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_BOOST_HIGH));
    img = with(KST_F_BOOST, 0);
    CHECK(kst_limits_image(&img) == 0u);
}

TEST_CASE(stretch_index_6_and_7_are_refused)
{
    kst_image_t img = with(KST_F_STRETCH, KST_STRETCH_INDEX_MAX);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_STRETCH, 6);
    CHECK(kst_limits_image(&img) == RULE(KST_H_STRETCH_INDEX));
    img = with(KST_F_STRETCH, 7);
    CHECK(kst_limits_image(&img) == RULE(KST_H_STRETCH_INDEX));
}

TEST_CASE(spd_ends_at_100_percent_and_starts_at_5_with_spd_sel_on)
{
    kst_image_t img = with(KST_F_SPD, KST_SPD_RAW_MAX);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_SPD, KST_SPD_RAW_MAX + 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_SPD_HIGH));
    img = with(KST_F_SPD, 4095);
    CHECK(kst_limits_image(&img) == RULE(KST_H_SPD_HIGH));

    /* With spd_sel off the value has no lower bound. */
    img = with(KST_F_SPD, 0);
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_SPD_SEL, 1));
    CHECK(kst_limits_image(&img) == RULE(KST_H_SPD_SEL_LOW_SPD));
    CHECK(kst_field_edit(&img, KST_F_SPD, KST_SPD_RAW_MIN_ON - 1u));
    CHECK(kst_limits_image(&img) == RULE(KST_H_SPD_SEL_LOW_SPD));
    CHECK(kst_field_edit(&img, KST_F_SPD, KST_SPD_RAW_MIN_ON));
    CHECK(kst_limits_image(&img) == 0u);
}

TEST_CASE(uncontrolled_pos_stays_in_500_to_2500_us_and_between_the_end_points)
{
    kst_image_t img = sim_bench_image();

    /* At both end points. */
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, 1125));
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, 1124));
    CHECK(kst_limits_image(&img) == RULE(KST_H_UNCONT_POS_SPAN));
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, 2625));
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, 2626));
    CHECK(kst_limits_image(&img) == RULE(KST_H_UNCONT_POS_SPAN));

    /* At the ends of the range, with the end points there too. */
    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, KST_PULSE_RAW_MIN));
    CHECK(kst_field_edit(&img, KST_F_PULSE_UPPER, KST_PULSE_RAW_MAX));
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, KST_PULSE_RAW_MIN));
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, KST_PULSE_RAW_MIN - 1u));
    CHECK(kst_limits_image(&img)
          == (RULE(KST_H_UNCONT_POS_LOW) | RULE(KST_H_UNCONT_POS_SPAN)));
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, KST_PULSE_RAW_MAX));
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, KST_PULSE_RAW_MAX + 1u));
    CHECK(kst_limits_image(&img)
          == (RULE(KST_H_UNCONT_POS_HIGH) | RULE(KST_H_UNCONT_POS_SPAN)));
}

TEST_CASE(uncontrolled_time_is_held_between_0_4_and_13_s)
{
    kst_image_t img = with(KST_F_UNCONT_TIME, KST_UNCONT_TIME_RAW_MIN);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_UNCONT_TIME, KST_UNCONT_TIME_RAW_MIN - 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_UNCONT_TIME_LOW));
    img = with(KST_F_UNCONT_TIME, 0);
    CHECK(kst_limits_image(&img) == RULE(KST_H_UNCONT_TIME_LOW));
    img = with(KST_F_UNCONT_TIME, KST_UNCONT_TIME_RAW_MAX);
    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_UNCONT_TIME, KST_UNCONT_TIME_RAW_MAX + 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_UNCONT_TIME_HIGH));
}

TEST_CASE(the_end_points_stay_in_500_to_2500_us)
{
    kst_image_t img = with(KST_F_PULSE_LOWER, KST_PULSE_RAW_MIN);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_PULSE_LOWER, KST_PULSE_RAW_MIN - 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_LOWER_LOW));
    img = with(KST_F_PULSE_UPPER, KST_PULSE_RAW_MAX);
    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_PULSE_UPPER, KST_PULSE_RAW_MAX + 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_UPPER_HIGH));
}

TEST_CASE(an_end_point_keeps_100_us_from_neutral)
{
    kst_image_t img = centred();

    CHECK(kst_limits_image(&img) == 0u);
    /* Lower 1400.0 us: 100.0 us below Neutral. */
    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, 1750));
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, 1751));
    CHECK(kst_limits_image(&img) == RULE(KST_H_LOWER_MARGIN));
    /* Lower above Neutral breaks it as well. */
    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, 1876));
    CHECK(breaks(&img, KST_H_LOWER_MARGIN));

    img = centred();
    CHECK(kst_field_edit(&img, KST_F_PULSE_UPPER, 2000));
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_PULSE_UPPER, 1999));
    CHECK(kst_limits_image(&img) == RULE(KST_H_UPPER_MARGIN));

    /* Neutral is compared in its own unit: one count of it, 0.2 us, decides. */
    img = centred();
    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, 1750));
    CHECK(kst_field_edit(&img, KST_F_NEUTRAL, 3454));
    CHECK(kst_limits_image(&img) == RULE(KST_H_LOWER_MARGIN));
}

TEST_CASE(the_end_points_keep_400_us_from_each_other)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, 1625));
    CHECK(kst_field_edit(&img, KST_F_PULSE_UPPER, 2125));
    CHECK(kst_limits_image(&img) == 0u);
    CHECK(kst_field_edit(&img, KST_F_PULSE_UPPER, 2124));
    CHECK(kst_limits_image(&img) == RULE(KST_H_SPAN));
    CHECK(kst_field_edit(&img, KST_F_PULSE_UPPER, 2125));
    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, 1626));
    CHECK(kst_limits_image(&img) == RULE(KST_H_SPAN));
}

TEST_CASE(neutral_is_held_between_1360_and_1640_us)
{
    kst_image_t img = with(KST_F_NEUTRAL, KST_NEUTRAL_RAW_MIN);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_NEUTRAL, KST_NEUTRAL_RAW_MIN - 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_NEUTRAL_LOW));
    img = with(KST_F_NEUTRAL, KST_NEUTRAL_RAW_MAX);
    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_NEUTRAL, KST_NEUTRAL_RAW_MAX + 1u);
    CHECK(kst_limits_image(&img) == RULE(KST_H_NEUTRAL_HIGH));
}

TEST_CASE(an_angle_of_0_and_gyro_mode_are_refused)
{
    kst_image_t img = with(KST_F_LEFT_ANGLE, 1);

    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_LEFT_ANGLE, 0);
    CHECK(kst_limits_image(&img) == RULE(KST_H_LEFT_ZERO));
    img = with(KST_F_LEFT_ANGLE, 255);
    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_RIGHT_ANGLE, 1);
    CHECK(kst_limits_image(&img) == 0u);
    img = with(KST_F_RIGHT_ANGLE, 0);
    CHECK(kst_limits_image(&img) == RULE(KST_H_RIGHT_ZERO));
    img = with(KST_F_GYRO, 1);
    CHECK(kst_limits_image(&img) == RULE(KST_H_GYRO_MODE));
}

/* --- access -------------------------------------------------------------------- */

TEST_CASE(a_locked_field_is_refused_until_it_is_unlocked)
{
    const kst_image_t backup = sim_bench_image();

    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *f = kst_field(id);
        kst_image_t target = backup;
        kst_field_set_t refused = 0;
        kst_rules_t rules;

        if (f->access != KST_ACCESS_LOCKED) {
            continue;
        }
        CHECK(kst_field_set(&target, id, kst_field_get(&backup, id) ^ 1u));
        rules = kst_limits_edit(&backup, &target, 0, &refused);
        CHECK((rules & RULE(KST_H_LOCKED)) != 0u);
        CHECK((rules & RULE(KST_H_NEVER)) == 0u);
        CHECK(refused == KST_FIELD_BIT(id));
        /* Another field's unlock does not lift it. */
        rules = kst_limits_edit(&backup, &target, ~KST_FIELD_BIT(id), &refused);
        CHECK((rules & RULE(KST_H_LOCKED)) != 0u);
        rules = kst_limits_edit(&backup, &target, KST_FIELD_BIT(id), &refused);
        CHECK((rules & RULE(KST_H_LOCKED)) == 0u);
        CHECK(refused == 0u);
    }
}

TEST_CASE(a_field_with_no_edit_path_is_refused_whatever_is_unlocked)
{
    const kst_image_t backup = sim_bench_image();

    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *f = kst_field(id);
        kst_image_t target = backup;
        kst_field_set_t refused = 0;
        kst_rules_t rules;

        if (f->access != KST_ACCESS_NEVER || id == KST_F_DUTY_COPY) {
            continue;
        }
        CHECK(kst_field_set(&target, id, kst_field_get(&backup, id) ^ 1u));
        rules = kst_limits_edit(&backup, &target, ~(kst_field_set_t)0,
                                &refused);
        CHECK((rules & RULE(KST_H_NEVER)) != 0u);
        CHECK((rules & RULE(KST_H_LOCKED)) == 0u);
        CHECK(refused == KST_FIELD_BIT(id));
    }
}

TEST_CASE(an_editable_field_and_the_duty_copy_pass_the_access_check)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t target = backup;
    kst_field_set_t refused = 1;

    CHECK(kst_limits_edit(&backup, &backup, 0, &refused) == 0u);
    CHECK(refused == 0u);
    /* Lowering Max. Duty moves 02 and 01. */
    CHECK(kst_field_edit(&target, KST_F_DUTY, 200));
    CHECK(kst_limits_edit(&backup, &target, 0, &refused) == 0u);
    CHECK(refused == 0u);
    /* The copy on its own is no edit: the image rule catches it. */
    target = backup;
    target.r[0x01] = 200;
    CHECK(kst_limits_edit(&backup, &target, 0, &refused)
          == RULE(KST_H_DUTY_COPY));
    CHECK(refused == 0u);
    /* A NULL place for the refused set is allowed. */
    target = backup;
    CHECK(kst_field_set(&target, KST_F_NODE_ADDR, 0));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == RULE(KST_H_NEVER));
    refused = 1;
    CHECK(kst_limits_edit(NULL, &target, 0, &refused) == KST_RULES_HARD);
    CHECK(refused == 0u);
}

TEST_CASE(a_hard_rule_of_the_target_is_reported_by_the_edit_check)
{
    const kst_image_t backup = sim_bench_image();
    const kst_image_t target = with(KST_F_DUTY, 251);

    CHECK((kst_limits_edit(&backup, &target, 0, NULL) & KST_RULES_HARD)
          == RULE(KST_H_DUTY_HIGH));
}

/* --- soft limits --------------------------------------------------------------- */

TEST_CASE(an_unchanged_image_asks_nothing)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_limits_edit(&img, &img, 0, NULL) == 0u);
    /* A servo that already sits beyond a soft limit: nothing either. */
    CHECK(kst_field_edit(&img, KST_F_DUTY, 100));
    CHECK(kst_field_edit(&img, KST_F_DEAD_BAND, 63));
    CHECK(kst_field_edit(&img, KST_F_STRETCH, 3));
    CHECK(kst_field_edit(&img, KST_F_UNCONT_TIME, 5));
    CHECK(kst_field_edit(&img, KST_F_LEFT_ANGLE, 10));
    CHECK(kst_field_edit(&img, KST_F_RIGHT_ANGLE, 10));
    CHECK(kst_field_edit(&img, KST_F_ALLOW_UNCONT, 1));
    CHECK(kst_field_edit(&img, KST_F_UNCONT_POS, 2600));
    CHECK(kst_field_edit(&img, KST_F_SPD_SEL, 1));
    CHECK(kst_field_edit(&img, KST_F_SPD, 100));
    CHECK(kst_field_edit(&img, KST_F_PULSE_LOWER, 1700));
    CHECK(kst_field_edit(&img, KST_F_PULSE_UPPER, 2630));
    CHECK(kst_limits_edit(&img, &img, 0, NULL) == 0u);
    /* And an edit of a field no rule reads leaves them silent. */
    {
        kst_image_t target = img;

        CHECK(kst_field_edit(&target, KST_F_REVERSION, 1));
        CHECK(kst_limits_edit(&img, &target, 0, NULL)
              == RULE(KST_S_REVERSION_CHANGED));
    }
}

TEST_CASE(max_duty_asks_when_raised_and_below_half)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t target = with(KST_F_DUTY, 246);

    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_DUTY_RAISED));
    target = with(KST_F_DUTY, 244);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    target = with(KST_F_DUTY, KST_DUTY_RAW_HALF);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    target = with(KST_F_DUTY, KST_DUTY_RAW_HALF - 1u);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_DUTY_BELOW_HALF));
    /* Raised and still below half: both. */
    {
        const kst_image_t low = with(KST_F_DUTY, 100);

        target = with(KST_F_DUTY, 101);
        CHECK(kst_limits_edit(&low, &target, 0, NULL)
              == (RULE(KST_S_DUTY_RAISED) | RULE(KST_S_DUTY_BELOW_HALF)));
    }
}

TEST_CASE(boost_stretch_and_soft_start_ask_in_their_direction)
{
    const kst_image_t backup = with(KST_F_STRETCH, 2);
    kst_image_t target = backup;

    CHECK(kst_field_edit(&target, KST_F_BOOST, 21));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_BOOST_RAISED));
    CHECK(kst_field_edit(&target, KST_F_BOOST, 19));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);

    target = backup;
    CHECK(kst_field_edit(&target, KST_F_STRETCH, 3));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_STRETCH_RAISED));
    CHECK(kst_field_edit(&target, KST_F_STRETCH, 1));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);

    target = backup;
    CHECK(kst_field_edit(&target, KST_F_SOFT_START, 0));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_SOFT_START_OFF));
    /* Switching it on asks nothing. */
    CHECK(kst_limits_edit(&target, &backup, 0, NULL) == 0u);
}

TEST_CASE(the_speed_limit_asks_when_switched_on_and_below_30_percent)
{
    const kst_image_t backup = with(KST_F_SPD, 500);
    kst_image_t target = backup;
    kst_image_t on;

    CHECK(kst_field_edit(&target, KST_F_SPD_SEL, 1));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_SPD_SEL_ON));
    on = target;
    /* Switching it off asks nothing. */
    CHECK(kst_limits_edit(&on, &backup, 0, NULL) == 0u);

    CHECK(kst_field_edit(&target, KST_F_SPD, KST_SPD_RAW_SLOW));
    CHECK(kst_limits_edit(&on, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_SPD, KST_SPD_RAW_SLOW - 1u));
    CHECK(kst_limits_edit(&on, &target, 0, NULL) == RULE(KST_S_SPD_SLOW));
    /* With spd_sel off the value limits nothing. */
    target = backup;
    CHECK(kst_field_edit(&target, KST_F_SPD, 100));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    /* Switching on over a low value asks both. */
    CHECK(kst_field_edit(&target, KST_F_SPD_SEL, 1));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_SPD_SEL_ON) | RULE(KST_S_SPD_SLOW)));
}

TEST_CASE(dead_band_asks_when_narrowed_and_below_1_us)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t target = with(KST_F_DEAD_BAND, 56);

    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_DEAD_BAND_NARROWED));
    target = with(KST_F_DEAD_BAND, 54);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    target = with(KST_F_DEAD_BAND, KST_DEAD_BAND_RAW_1US);
    CHECK(!asks(&backup, &target, KST_S_DEAD_BAND_BELOW_1US));
    target = with(KST_F_DEAD_BAND, KST_DEAD_BAND_RAW_1US + 1u);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_DEAD_BAND_NARROWED)
              | RULE(KST_S_DEAD_BAND_BELOW_1US)));
    /* Widened and still below 1.0 us. */
    {
        const kst_image_t narrow = with(KST_F_DEAD_BAND, 63);

        target = with(KST_F_DEAD_BAND, 62);
        CHECK(kst_limits_edit(&narrow, &target, 0, NULL)
              == RULE(KST_S_DEAD_BAND_BELOW_1US));
    }
}

TEST_CASE(a_narrow_dead_band_with_gain_asks_once_more)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t target = with(KST_F_DEAD_BAND, KST_DEAD_BAND_RAW_1US + 1u);

    /* Stretch 1, Boost as it is: no. */
    CHECK(!asks(&backup, &target, KST_S_NARROW_BAND_GAIN));
    CHECK(kst_field_edit(&target, KST_F_STRETCH, 1));
    CHECK(asks(&backup, &target, KST_S_NARROW_BAND_GAIN));
    CHECK(kst_field_edit(&target, KST_F_STRETCH, 0));
    CHECK(kst_field_edit(&target, KST_F_BOOST, 21));
    CHECK(asks(&backup, &target, KST_S_NARROW_BAND_GAIN));
    /* At 1.0 us the combination does not ask. */
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, KST_DEAD_BAND_RAW_1US));
    CHECK(kst_field_edit(&target, KST_F_STRETCH, 5));
    CHECK(!asks(&backup, &target, KST_S_NARROW_BAND_GAIN));
    /* A servo that has it already: only on a change of one of the three. */
    {
        kst_image_t has = with(KST_F_DEAD_BAND, 63);
        kst_image_t next;

        CHECK(kst_field_edit(&has, KST_F_STRETCH, 2));
        next = has;
        CHECK(kst_field_edit(&next, KST_F_LEFT_ANGLE, 100));
        CHECK(!asks(&has, &next, KST_S_NARROW_BAND_GAIN));
        CHECK(kst_field_edit(&next, KST_F_STRETCH, 1));
        CHECK(asks(&has, &next, KST_S_NARROW_BAND_GAIN));
    }
}

TEST_CASE(the_failsafe_asks_when_enabled_far_out_or_short)
{
    const kst_image_t backup = centred();
    kst_image_t target = backup;
    kst_image_t on;

    CHECK(kst_field_edit(&target, KST_F_ALLOW_UNCONT, 1));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_ALLOW_UNCONT_ON));
    on = target;
    CHECK(kst_limits_edit(&on, &backup, 0, NULL) == 0u);

    /* 300.0 us above Neutral, then one count more. */
    CHECK(kst_field_edit(&target, KST_F_UNCONT_POS, 2250));
    CHECK(kst_limits_edit(&on, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_UNCONT_POS, 2251));
    CHECK(kst_limits_edit(&on, &target, 0, NULL)
          == RULE(KST_S_UNCONT_POS_FAR));
    CHECK(kst_field_edit(&target, KST_F_UNCONT_POS, 1500));
    CHECK(kst_limits_edit(&on, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_UNCONT_POS, 1499));
    CHECK(kst_limits_edit(&on, &target, 0, NULL)
          == RULE(KST_S_UNCONT_POS_FAR));
    /* With the failsafe off the position asks nothing. */
    target = backup;
    CHECK(kst_field_edit(&target, KST_F_UNCONT_POS, 2251));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    /* Switching it on over a far position asks both. */
    CHECK(kst_field_edit(&target, KST_F_ALLOW_UNCONT, 1));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_ALLOW_UNCONT_ON) | RULE(KST_S_UNCONT_POS_FAR)));

    target = backup;
    CHECK(kst_field_edit(&target, KST_F_UNCONT_TIME,
                         KST_UNCONT_TIME_RAW_SHORT));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_UNCONT_TIME,
                         KST_UNCONT_TIME_RAW_SHORT - 1u));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_UNCONT_TIME_SHORT));
}

TEST_CASE(an_end_point_asks_within_300_us_of_neutral_and_100_us_from_its_backup)
{
    const kst_image_t backup = centred();
    kst_image_t target = backup;

    /* Lower 900 us: 100.0 us up and down. */
    CHECK(kst_field_edit(&target, KST_F_PULSE_LOWER, 1250));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_PULSE_LOWER, 1251));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_LOWER_MOVED));
    CHECK(kst_field_edit(&target, KST_F_PULSE_LOWER, 1000));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_PULSE_LOWER, 999));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_LOWER_MOVED));
    /* 300.0 us below Neutral. */
    CHECK(kst_field_edit(&target, KST_F_PULSE_LOWER, 1500));
    CHECK(!asks(&backup, &target, KST_S_LOWER_NEAR));
    CHECK(kst_field_edit(&target, KST_F_PULSE_LOWER, 1501));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_LOWER_MOVED) | RULE(KST_S_LOWER_NEAR)));

    target = backup;
    CHECK(kst_field_edit(&target, KST_F_PULSE_UPPER, 2750));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_PULSE_UPPER, 2751));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_UPPER_MOVED));
    CHECK(kst_field_edit(&target, KST_F_PULSE_UPPER, 2500));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_PULSE_UPPER, 2499));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_UPPER_MOVED));
    CHECK(kst_field_edit(&target, KST_F_PULSE_UPPER, 2250));
    CHECK(!asks(&backup, &target, KST_S_UPPER_NEAR));
    CHECK(kst_field_edit(&target, KST_F_PULSE_UPPER, 2249));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_UPPER_MOVED) | RULE(KST_S_UPPER_NEAR)));
}

TEST_CASE(neutral_asks_more_than_20_us_from_its_backup)
{
    const kst_image_t backup = centred();
    kst_image_t target = backup;

    CHECK_EQ(disp(KST_F_NEUTRAL, 3555), 15200);
    CHECK_EQ(disp(KST_F_NEUTRAL, 3556), 15202);
    CHECK_EQ(disp(KST_F_NEUTRAL, 3355), 14800);
    CHECK_EQ(disp(KST_F_NEUTRAL, 3354), 14798);
    CHECK(kst_field_edit(&target, KST_F_NEUTRAL, 3555));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_NEUTRAL, 3556));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_NEUTRAL_MOVED));
    CHECK(kst_field_edit(&target, KST_F_NEUTRAL, 3355));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    CHECK(kst_field_edit(&target, KST_F_NEUTRAL, 3354));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_NEUTRAL_MOVED));
    /* Neutral moved towards an end point that stays: the margin asks. */
    {
        kst_image_t near = backup;

        CHECK(kst_field_edit(&near, KST_F_PULSE_UPPER, 2251));
        target = near;
        CHECK(kst_field_edit(&target, KST_F_NEUTRAL, 3460));
        CHECK(kst_limits_edit(&near, &target, 0, NULL)
              == RULE(KST_S_UPPER_NEAR));
    }
}

TEST_CASE(reversion_the_angles_and_the_mode_ask)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t target = with(KST_F_REVERSION, 1);

    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_REVERSION_CHANGED));
    CHECK(kst_limits_edit(&target, &backup, 0, NULL)
          == RULE(KST_S_REVERSION_CHANGED));

    /* Max. Duty is 96 % on this servo: more travel asks twice. */
    target = with(KST_F_LEFT_ANGLE, 151);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_LEFT_RAISED) | RULE(KST_S_HIGH_DUTY_TRAVEL)));
    target = with(KST_F_RIGHT_ANGLE, 151);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_RIGHT_RAISED) | RULE(KST_S_HIGH_DUTY_TRAVEL)));
    target = with(KST_F_LEFT_ANGLE, 149);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    target = with(KST_F_LEFT_ANGLE, KST_ANGLE_RAW_SMALL);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    target = with(KST_F_LEFT_ANGLE, KST_ANGLE_RAW_SMALL - 1u);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_LEFT_SMALL));
    target = with(KST_F_RIGHT_ANGLE, KST_ANGLE_RAW_SMALL);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) == 0u);
    target = with(KST_F_RIGHT_ANGLE, KST_ANGLE_RAW_SMALL - 1u);
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_RIGHT_SMALL));

    target = with(KST_F_GYRO, 1);
    CHECK(kst_limits_edit(&backup, &target, KST_FIELD_BIT(KST_F_GYRO), NULL)
          == (RULE(KST_S_GYRO_CHANGED) | RULE(KST_H_GYRO_MODE)));
}

TEST_CASE(more_travel_above_90_percent_duty_asks)
{
    kst_image_t backup = with(KST_F_DUTY, KST_DUTY_RAW_90);
    kst_image_t target = backup;

    CHECK(kst_field_edit(&target, KST_F_LEFT_ANGLE, 151));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_LEFT_RAISED));
    CHECK(kst_field_edit(&target, KST_F_DUTY, KST_DUTY_RAW_90 + 1u));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == (RULE(KST_S_LEFT_RAISED) | RULE(KST_S_DUTY_RAISED)
              | RULE(KST_S_HIGH_DUTY_TRAVEL)));
    /* High duty alone, the angles as they are: no. */
    CHECK(kst_field_edit(&target, KST_F_LEFT_ANGLE, 150));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL)
          == RULE(KST_S_DUTY_RAISED));
}

TEST_CASE(soft_rules_do_not_enter_the_hard_set)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t target = backup;

    CHECK(kst_field_edit(&target, KST_F_DUTY, 250));
    CHECK(kst_field_edit(&target, KST_F_BOOST, 236));
    CHECK(kst_field_edit(&target, KST_F_STRETCH, 5));
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 63));
    CHECK(kst_field_edit(&target, KST_F_LEFT_ANGLE, 255));
    CHECK(kst_field_edit(&target, KST_F_RIGHT_ANGLE, 255));
    CHECK(kst_limits_edit(&backup, &target, 0, NULL) != 0u);
    CHECK((kst_limits_edit(&backup, &target, 0, NULL) & KST_RULES_HARD) == 0u);
    CHECK(kst_limits_image(&target) == 0u);
}

int main(void)
{
    RUN(the_bench_image_decodes_to_what_the_vendor_tool_shows);
    RUN(every_bit_of_the_32_registers_belongs_to_exactly_one_field);
    RUN(a_field_row_names_its_place_and_width);
    RUN(an_id_outside_the_table_has_no_field);
    RUN(the_access_classes_are_the_ones_decided);
    RUN(setting_a_field_changes_its_bits_and_no_other);
    RUN(an_edit_of_one_field_leaves_every_other_field_at_its_raw_value);
    RUN(an_edit_of_max_duty_writes_the_copy_and_a_set_does_not);
    RUN(max_duty_is_percent_of_255_with_the_vendor_rounding);
    RUN(boost_is_50_us_plus_12_5_us_per_count);
    RUN(stretch_is_a_power_of_two_up_to_32);
    RUN(dead_band_falls_0_2_us_per_count);
    RUN(pulse_widths_are_0_8_us_per_count);
    RUN(neutral_is_810_us_plus_409_2048_us_per_count);
    RUN(the_protection_fields_decode_and_have_no_way_back);
    RUN(the_error_counts_are_the_1_bits_upward_from_bit_0);
    RUN(fields_without_a_conversion_show_the_raw_value);
    RUN(decoding_and_encoding_every_field_of_an_image_gives_the_image_back);
    RUN(the_measured_layout_passes_the_fingerprint);
    RUN(each_fingerprint_rule_fails_alone_and_names_its_register);
    RUN(a_foreign_image_lists_every_rule_and_register_it_breaks);
    RUN(rule_numbers_are_fixed);
    RUN(max_duty_is_held_between_10_and_98_percent);
    RUN(the_duty_copy_follows_max_duty);
    RUN(boost_ends_at_3000_us);
    RUN(stretch_index_6_and_7_are_refused);
    RUN(spd_ends_at_100_percent_and_starts_at_5_with_spd_sel_on);
    RUN(uncontrolled_pos_stays_in_500_to_2500_us_and_between_the_end_points);
    RUN(uncontrolled_time_is_held_between_0_4_and_13_s);
    RUN(the_end_points_stay_in_500_to_2500_us);
    RUN(an_end_point_keeps_100_us_from_neutral);
    RUN(the_end_points_keep_400_us_from_each_other);
    RUN(neutral_is_held_between_1360_and_1640_us);
    RUN(an_angle_of_0_and_gyro_mode_are_refused);
    RUN(a_locked_field_is_refused_until_it_is_unlocked);
    RUN(a_field_with_no_edit_path_is_refused_whatever_is_unlocked);
    RUN(an_editable_field_and_the_duty_copy_pass_the_access_check);
    RUN(a_hard_rule_of_the_target_is_reported_by_the_edit_check);
    RUN(an_unchanged_image_asks_nothing);
    RUN(max_duty_asks_when_raised_and_below_half);
    RUN(boost_stretch_and_soft_start_ask_in_their_direction);
    RUN(the_speed_limit_asks_when_switched_on_and_below_30_percent);
    RUN(dead_band_asks_when_narrowed_and_below_1_us);
    RUN(a_narrow_dead_band_with_gain_asks_once_more);
    RUN(the_failsafe_asks_when_enabled_far_out_or_short);
    RUN(an_end_point_asks_within_300_us_of_neutral_and_100_us_from_its_backup);
    RUN(neutral_asks_more_than_20_us_from_its_backup);
    RUN(reversion_the_angles_and_the_mode_ask);
    RUN(more_travel_above_90_percent_duty_asks);
    RUN(soft_rules_do_not_enter_the_hard_set);
    return test_summary("kst_reg");
}
