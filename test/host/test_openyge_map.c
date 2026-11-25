/*
 * The field map of OpenYGE ESCs (electronic speed controllers): access per
 * index exactly as docs/OpenYGE.md section 5.3 states it, and every
 * writable field at its minimum, its maximum and one past.  The expected
 * figures are written out here from the page, not read from the map.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "openyge_map.h"

/* docs/OpenYGE.md 5.3, column Access, index 0 to 31. */
static const uint8_t ACCESS[32] = {
    OY_ACC_IDENTITY,                        /* 0 count            */
    OY_ACC_CHECK, OY_ACC_CHECK,             /* 1 mode, 2 BEC      */
    OY_ACC_EDIT, OY_ACC_RAW,                /* 3 timing, 4 start  */
    OY_ACC_EDIT, OY_ACC_EDIT, OY_ACC_EDIT,  /* 5, 6 gains, 7      */
    OY_ACC_CHECK, OY_ACC_EDIT, OY_ACC_EDIT, /* 8, 9, 10           */
    OY_ACC_IDENTITY, OY_ACC_IDENTITY, OY_ACC_IDENTITY,  /* 11-13 */
    OY_ACC_IDENTITY, OY_ACC_IDENTITY,       /* 14-15 serial       */
    OY_ACC_RAW, OY_ACC_EDIT, OY_ACC_EDIT,   /* 16, 17, 18         */
    OY_ACC_RAW, OY_ACC_RAW,                 /* 19, 20             */
    OY_ACC_EDIT, OY_ACC_EDIT, OY_ACC_EDIT, OY_ACC_EDIT, /* 21-24  */
    OY_ACC_TELEMETRY,                       /* 25                 */
    OY_ACC_CHECK, OY_ACC_CHECK,             /* 26 flags, 27 limit */
    OY_ACC_RAW, OY_ACC_RAW, OY_ACC_RAW, OY_ACC_RAW, /* 28-31      */
};

TEST_CASE(every_index_has_the_access_of_the_spec)
{
    for (uint16_t i = 0; i < 32u; ++i) {
        const oy_field_t *f = oy_map_field(i);
        CHECK(f != NULL);
        if (f != NULL && f->access != ACCESS[i]) {
            T_FAIL("index %u: access %u, the spec says %u", (unsigned)i,
                   (unsigned)f->access, (unsigned)ACCESS[i]);
        }
        const bool w = ACCESS[i] == OY_ACC_CHECK || ACCESS[i] == OY_ACC_EDIT;
        CHECK_EQ(oy_map_writable(i), w);
    }
    CHECK(oy_map_field(32) == NULL);
    CHECK(oy_map_field(63) == NULL);
    CHECK_EQ(oy_map_writable(32), false);
    CHECK_EQ(oy_map_writable(0xFFFF), false);
    /* Index 26: bit 0 CHECK, bit 1 edit, the rest kept. */
    CHECK_EQ(oy_map_field(26)->kind, OY_KIND_FLAGS);
    CHECK_EQ(oy_map_field(26)->edit_bits, 0x0003u);
    CHECK_EQ(oy_map_field(26)->check_bits, 0x0001u);
}

/* Writable sets of docs/OpenYGE.md 5.3, column Writable. */
typedef struct {
    uint16_t index, lo, hi;
} span_t;

static const span_t SPANS[] = {
    { 1, 0, 6 },      { 2, 55, 84 },    { 3, 0, 19 },     { 5, 1, 9 },
    { 6, 1, 9 },      { 7, 0, 3 },      { 8, 0, 2 },      { 9, 0, 5 },
    { 10, 0, 3 },     { 17, 900, 1900 }, { 18, 600, 1500 }, { 21, 1, 255 },
    { 22, 0, 1800 },  { 23, 0, 26 },    { 24, 0, 31 },    { 27, 1, 65500 },
};

TEST_CASE(every_writable_field_takes_its_limits_and_refuses_one_past)
{
    for (size_t k = 0; k < sizeof(SPANS) / sizeof(SPANS[0]); ++k) {
        const span_t *s = &SPANS[k];
        if (!oy_map_value_ok(s->index, s->lo, 0xFFFFu, 0u)
            || !oy_map_value_ok(s->index, s->hi, 0xFFFFu, 0u)) {
            T_FAIL("index %u refuses a limit %u or %u", (unsigned)s->index,
                   (unsigned)s->lo, (unsigned)s->hi);
        }
        if (s->lo > 0u
            && oy_map_value_ok(s->index, (uint16_t)(s->lo - 1u), 0u, 0u)) {
            T_FAIL("index %u takes %u", (unsigned)s->index,
                   (unsigned)(s->lo - 1u));
        }
        if (oy_map_value_ok(s->index, (uint16_t)(s->hi + 1u), 0u, 0u)) {
            T_FAIL("index %u takes %u", (unsigned)s->index,
                   (unsigned)(s->hi + 1u));
        }
    }
    CHECK_EQ(oy_map_value_ok(27, 65535u, 0u, 0u), false);
    CHECK_EQ(oy_map_value_ok(27, 0u, 1u, 0u), false);
}

TEST_CASE(choice_sets_keep_their_gaps)
{
    /* Timing: 0 to 6 and 17 to 19; 16 is never written. */
    for (uint16_t v = 0; v <= 20u; ++v) {
        const bool want = v <= 6u || (v >= 17u && v <= 19u);
        if (oy_map_value_ok(3, v, 0xFFFFu, 0u) != want) {
            T_FAIL("timing %u: %s", (unsigned)v, want ? "refused" : "taken");
        }
    }
    /* Freewheel: 0, 1 and 3; 2 is unused. */
    CHECK(oy_map_value_ok(10, 0, 1, 0));
    CHECK(oy_map_value_ok(10, 1, 0, 0));
    CHECK_EQ(oy_map_value_ok(10, 2, 0, 0), false);
    CHECK(oy_map_value_ok(10, 3, 0, 0));
    CHECK_EQ(oy_map_value_ok(10, 4, 0, 0), false);
    /* Gains 1 to 9: neither 0 (one source) nor 10 (the other). */
    CHECK_EQ(oy_map_value_ok(5, 0, 5, 0), false);
    CHECK_EQ(oy_map_value_ok(6, 10, 5, 0), false);
}

TEST_CASE(bec_goes_to_120_only_while_flag_bit_3_is_set)
{
    CHECK_EQ(oy_map_value_ok(2, 85, 74, 0x0000u), false);
    CHECK_EQ(oy_map_value_ok(2, 85, 74, 0x0007u), false);
    CHECK(oy_map_value_ok(2, 85, 74, OY_FLAG_BEC12V));
    CHECK(oy_map_value_ok(2, 120, 74, OY_FLAG_BEC12V));
    CHECK_EQ(oy_map_value_ok(2, 121, 74, OY_FLAG_BEC12V), false);
    CHECK_EQ(oy_map_value_ok(2, 123, 74, 0xFFFFu), false);
    CHECK_EQ(oy_map_value_ok(2, 54, 74, OY_FLAG_BEC12V), false);
}

TEST_CASE(flags_change_in_bits_0_and_1_only)
{
    const uint16_t was = 0x00F4u;   /* bit 2 and bits 4 to 7 set */

    CHECK(oy_map_value_ok(26, was, was, 0));
    CHECK(oy_map_value_ok(26, (uint16_t)(was | 0x0003u), was, 0));
    CHECK(oy_map_value_ok(26, (uint16_t)(was | 0x0001u), was, 0));
    CHECK(oy_map_value_ok(26, (uint16_t)(was | 0x0002u), was, 0));
    for (unsigned bit = 2; bit < 16u; ++bit) {
        const uint16_t v = (uint16_t)(was ^ (1u << bit));
        if (oy_map_value_ok(26, v, was, 0)) {
            T_FAIL("flags take a change of bit %u", bit);
        }
    }
}

TEST_CASE(read_only_fields_take_no_value)
{
    static const uint16_t RO[] = { 0, 4, 11, 12, 13, 14, 15, 16, 19, 20, 25,
                                   28, 29, 30, 31, 32, 63, 64, 0xFFFF };
    for (size_t k = 0; k < sizeof(RO) / sizeof(RO[0]); ++k) {
        for (uint32_t v = 0; v <= 0xFFFFu; v += 0x1111u) {
            if (oy_map_value_ok(RO[k], (uint16_t)v, (uint16_t)(v + 1u), 0)) {
                T_FAIL("index %u takes %u", (unsigned)RO[k], (unsigned)v);
            }
        }
    }
}

TEST_CASE(names_of_types_and_choices)
{
    CHECK_STR_EQ(oy_map_type_name(848), "35 LVT BEC");
    CHECK_STR_EQ(oy_map_type_name(5712), "165 HVT");
    CHECK_STR_EQ(oy_map_type_name(16464), "Opto 405");
    CHECK(oy_map_type_name(0) == NULL);
    CHECK(oy_map_type_name(849) == NULL);
    CHECK_STR_EQ(oy_map_choice_name(1, 0), "FREE");
    CHECK_STR_EQ(oy_map_choice_name(1, 6), "AIRPLANE F3A");
    CHECK(oy_map_choice_name(1, 7) == NULL);
    CHECK_STR_EQ(oy_map_choice_name(3, 19), "AUTO EXTREME");
    CHECK(oy_map_choice_name(3, 7) == NULL);
    CHECK(oy_map_choice_name(3, 20) == NULL);
    CHECK(oy_map_choice_name(10, 2) == NULL);
    CHECK_STR_EQ(oy_map_choice_name(9, 5), "3.4V");
    CHECK(oy_map_choice_name(21, 1) == NULL);
    CHECK(oy_map_choice_name(40, 0) == NULL);
    CHECK_EQ(oy_map_u32(0x9483u, 0x0001u), 103555u);
    CHECK_EQ(oy_map_u32(0xFFFFu, 0xFFFFu), 0xFFFFFFFFu);
}

int main(void)
{
    RUN(every_index_has_the_access_of_the_spec);
    RUN(every_writable_field_takes_its_limits_and_refuses_one_past);
    RUN(choice_sets_keep_their_gaps);
    RUN(bec_goes_to_120_only_while_flag_bit_3_is_set);
    RUN(flags_change_in_bits_0_and_1_only);
    RUN(read_only_fields_take_no_value);
    RUN(names_of_types_and_choices);
    return test_summary("openyge_map");
}
