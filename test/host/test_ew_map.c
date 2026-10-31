/*
 * The field maps (protocols/escwire/ew_map*.c).
 *
 * Under test: the count of editable fields per firmware and layout; every
 * field at both ends of its values and one past; a text for each kind of field,
 * the raw fallback; which offsets are named in the older layouts; the check of
 * a draft: only editable fields may differ, only to their values.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ew_map.h"

static int editable(uint8_t fw, uint8_t layout)
{
    int n = 0;

    for (size_t i = 0;; ++i) {
        const ew_field_t *f = ew_map_nth(fw, layout, i);

        if (f == NULL) {
            return n;
        }
        n += f->access == EW_ACC_EDIT;
    }
}

TEST_CASE(the_editable_fields_per_layout)
{
    CHECK_EQ(editable(EW_FW_S, 33u), 12);
    CHECK_EQ(editable(EW_FW_BJ, 208u), 15);
    CHECK_EQ(editable(EW_FW_AM, 4u), 40);
    CHECK_EQ(editable(EW_FW_AM, 3u), 39);
    CHECK_EQ(editable(EW_FW_AM, 2u), 0);
    CHECK_EQ(editable(EW_FW_NONE, 0u), 0);
}

TEST_CASE(which_layouts_are_written)
{
    for (unsigned l = 0u; l < 256u; ++l) {
        CHECK_EQ(ew_map_writable(EW_FW_S, (uint8_t)l), l == 33u);
        CHECK_EQ(ew_map_writable(EW_FW_BJ, (uint8_t)l), l == 208u);
        CHECK_EQ(ew_map_writable(EW_FW_AM, (uint8_t)l), l == 3u || l == 4u);
        CHECK(!ew_map_writable(EW_FW_NONE, (uint8_t)l));
    }
    CHECK_EQ(ew_map_len(EW_FW_S), 0x70u);
    CHECK_EQ(ew_map_len(EW_FW_BJ), 255u);
    CHECK_EQ(ew_map_len(EW_FW_AM), 192u);
    CHECK_EQ(ew_map_len(EW_FW_NONE), 0u);
}

TEST_CASE(every_field_at_both_ends_and_one_past)
{
    static const uint8_t k_fw[] = { EW_FW_S, EW_FW_BJ, EW_FW_AM };

    for (size_t k = 0; k < sizeof(k_fw); ++k) {
        size_t n = 0u;
        const ew_field_t *t = ew_map_table(k_fw[k], &n);

        CHECK(n > 0u);
        for (size_t i = 0; i < n; ++i) {
            const ew_field_t *f = &t[i];
            char text[32];

            CHECK(f->name != NULL && f->name[0] != '\0');
            CHECK(f->lo <= f->hi);
            CHECK(f->from <= f->to);
            CHECK(ew_field_ok(f, f->lo));
            CHECK(ew_field_ok(f, f->hi));
            if (f->lo > 0u && !(f->has_alt && f->alt == f->lo - 1u)) {
                CHECK(!ew_field_ok(f, (uint8_t)(f->lo - 1u)));
            }
            if (f->hi < 255u && !(f->has_alt && f->alt == f->hi + 1u)) {
                CHECK(!ew_field_ok(f, (uint8_t)(f->hi + 1u)));
            }
            CHECK(ew_field_text(f, f->lo, text, sizeof(text)) > 0u);
            CHECK(ew_field_text(f, f->hi, text, sizeof(text)) > 0u);
        }
    }
    CHECK(!ew_field_ok(NULL, 0u));
}

static const char *text_of(uint8_t fw, uint8_t layout, uint8_t off, uint8_t raw)
{
    static char s[32];

    (void)ew_field_text(ew_map_find(fw, layout, off), raw, s, sizeof(s));
    return s;
}

TEST_CASE(a_value_as_the_operator_reads_it)
{
    CHECK_STR_EQ(text_of(EW_FW_S, 33u, 0x09u, 1u), "0.031");
    CHECK_STR_EQ(text_of(EW_FW_S, 33u, 0x09u, 13u), "1.50");
    CHECK_STR_EQ(text_of(EW_FW_S, 33u, 0x09u, 14u), "raw 14");
    CHECK_STR_EQ(text_of(EW_FW_S, 33u, 0x0Bu, 4u), "BIDIRECTIONAL REVERSED");
    CHECK_STR_EQ(text_of(EW_FW_S, 33u, 0x19u, 37u), "1148us");
    CHECK_STR_EQ(text_of(EW_FW_S, 33u, 0x23u, 7u), "140C");
    CHECK_STR_EQ(text_of(EW_FW_S, 32u, 0x23u, 1u), "ON");
    CHECK_STR_EQ(text_of(EW_FW_S, 32u, 0x23u, 2u), "raw 2");
    CHECK_STR_EQ(text_of(EW_FW_BJ, 208u, 0x10u, 255u), "100%");
    CHECK_STR_EQ(text_of(EW_FW_BJ, 208u, 0x10u, 0u), "0%");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x08u, 1u), "0.5V");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x08u, 100u), "50V");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x17u, 10u), "0deg");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x17u, 11u), "0.9375deg");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x17u, 26u), "15deg");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x17u, 42u), "30deg");
    /* The old format reads raw, and a stored 0 has no number. */
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x17u, 3u), "raw 3");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x1Au, 0u), "20");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x1Au, 255u), "10220");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x20u, 0u), "750us");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x21u, 255u), "2260us");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x22u, 126u), "1500us");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x25u, 0u), "2.5V");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x25u, 100u), "3.5V");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x25u, 5u), "2.55V");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x2Bu, 255u), "OFF");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x2Bu, 90u), "90C");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x2Bu, 141u), "raw 141");
    CHECK_STR_EQ(text_of(EW_FW_AM, 4u, 0x2Cu, 0u), "OFF");
}

TEST_CASE(a_number_with_decimals_and_a_sign)
{
    const ew_field_t f = { .name = "T", .unit = "V", .mul = 5, .add = -20,
                           .dec = 2u, .lo = 0u, .hi = 255u };
    const ew_field_t g = { .name = "T", .unit = "", .dec = 1u, .lo = 0u,
                           .hi = 255u };
    char s[32];

    (void)ew_field_text(&f, 5u, s, sizeof(s));
    CHECK_STR_EQ(s, "0.05V");
    (void)ew_field_text(&f, 4u, s, sizeof(s));
    CHECK_STR_EQ(s, "0V");
    (void)ew_field_text(&f, 1u, s, sizeof(s));
    CHECK_STR_EQ(s, "-0.15V");
    (void)ew_field_text(&f, 44u, s, sizeof(s));
    CHECK_STR_EQ(s, "2V");
    (void)ew_field_text(&g, 25u, s, sizeof(s));
    CHECK_STR_EQ(s, "2.5");
}

TEST_CASE(a_text_stays_in_its_buffer)
{
    const ew_field_t *f = ew_map_find(EW_FW_S, 33u, 0x0Bu);
    char s[8];

    CHECK_EQ(ew_field_text(f, 1u, s, 0u), 0u);
    memset(s, 'x', sizeof(s));
    CHECK_EQ(ew_field_text(f, 4u, s, 4u), 3u);
    CHECK_STR_EQ(s, "BID");
    CHECK_EQ(s[4], 'x');
    f = ew_map_find(EW_FW_AM, 4u, 0x17u);
    CHECK_EQ(ew_field_text(f, 42u, s, 2u), 1u);
    CHECK_EQ(ew_field_text(f, 3u, s, 3u), 2u);
    CHECK_STR_EQ(s, "ra");
}

TEST_CASE(the_older_bluejay_layouts_name_their_own_bytes)
{
    CHECK_STR_EQ(ew_map_find(EW_FW_BJ, 200u, 0x04u)->name, "STARTUP BOOST");
    CHECK_STR_EQ(ew_map_find(EW_FW_BJ, 201u, 0x04u)->name, "MIN STARTUP POWER");
    CHECK(ew_map_find(EW_FW_BJ, 200u, 0x07u) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 201u, 0x07u) != NULL);
    CHECK_STR_EQ(ew_map_find(EW_FW_BJ, 200u, 0x09u)->name, "STARTUP POWER");
    CHECK_STR_EQ(ew_map_find(EW_FW_BJ, 201u, 0x09u)->name, "POWER SLOPE");
    CHECK(ew_map_find(EW_FW_BJ, 201u, 0x0Au) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 203u, 0x0Au) != NULL);
    CHECK(ew_map_find(EW_FW_BJ, 203u, 0x10u) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 204u, 0x10u) != NULL);
    CHECK(ew_map_find(EW_FW_BJ, 200u, 0x24u) != NULL);
    CHECK(ew_map_find(EW_FW_BJ, 201u, 0x24u) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 204u, 0x29u) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 206u, 0x29u) != NULL);
    CHECK(ew_map_find(EW_FW_BJ, 206u, 0x2Au) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 207u, 0x2Au) != NULL);
    CHECK(ew_map_find(EW_FW_BJ, 207u, 0x06u) != NULL);
    CHECK(ew_map_find(EW_FW_BJ, 208u, 0x06u) == NULL);
    /* Layout 33 has no stick programming and no throttle bytes. */
    CHECK(ew_map_find(EW_FW_BJ, 33u, 0x0Fu) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 33u, 0x19u) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 33u, 0x1Au) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 33u, 0x21u) == NULL);
    CHECK(ew_map_find(EW_FW_BJ, 33u, 0x0Bu) != NULL);
    /* 0x0D of AM32 is a field of revision 4 only. */
    CHECK(ew_map_find(EW_FW_AM, 3u, 0x0Du) == NULL);
    CHECK(ew_map_find(EW_FW_AM, 4u, 0x0Du) != NULL);
    CHECK(ew_map_find(EW_FW_NONE, 4u, 0x0Du) == NULL);
}

TEST_CASE(what_is_kept_is_not_a_field_that_edits)
{
    static const uint8_t k_bj_kept[] = { 0x00, 0x01, 0x02, 0x03, 0x06, 0x0A,
                                         0x0D, 0x0E, 0x2A, 0x40, 0x50, 0x60,
                                         0x70, 0xFB, 0xFE };
    static const uint8_t k_am_kept[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x0E,
                                         0x10, 0x30, 0xAF, 0xB0, 0xBF };

    for (size_t i = 0; i < sizeof(k_bj_kept); ++i) {
        const ew_field_t *f = ew_map_find(EW_FW_BJ, 208u, k_bj_kept[i]);

        CHECK(f == NULL || f->access == EW_ACC_SHOW);
    }
    for (size_t i = 0; i < sizeof(k_am_kept); ++i) {
        const ew_field_t *f = ew_map_find(EW_FW_AM, 4u, k_am_kept[i]);

        CHECK(f == NULL || f->access == EW_ACC_SHOW);
    }
    /* The fields the warning names. */
    CHECK(ew_map_find(EW_FW_AM, 4u, 0x09u)->flags & EW_FF_WARN);
    CHECK(ew_map_find(EW_FW_AM, 4u, 0x2Eu)->flags & EW_FF_WARN);
    CHECK(ew_map_find(EW_FW_BJ, 208u, 0x29u)->flags & EW_FF_WARN);
    CHECK(ew_map_find(EW_FW_S, 33u, 0x23u)->flags & EW_FF_WARN);
    CHECK(!(ew_map_find(EW_FW_S, 33u, 0x15u)->flags & EW_FF_WARN));
}

TEST_CASE(the_check_of_a_draft)
{
    uint8_t a[192];
    uint8_t b[192];
    uint16_t off = 0u;

    memset(a, 1, sizeof(a));
    memcpy(b, a, sizeof(b));
    CHECK_EQ(ew_image_check(EW_FW_AM, 4u, a, b, sizeof(a), &off), EW_IMG_OK);
    b[0x18] = 48u;
    CHECK_EQ(ew_image_check(EW_FW_AM, 4u, a, b, sizeof(a), &off), EW_IMG_OK);
    b[0x18] = 49u;
    CHECK_EQ(ew_image_check(EW_FW_AM, 4u, a, b, sizeof(a), &off), EW_IMG_VALUE);
    CHECK_EQ(off, 0x18u);
    b[0x18] = 1u;
    b[0x30] = 0u;
    CHECK_EQ(ew_image_check(EW_FW_AM, 4u, a, b, sizeof(a), &off),
             EW_IMG_NOT_EDITABLE);
    CHECK_EQ(off, 0x30u);
    b[0x30] = 1u;
    b[0x02] = 9u;
    CHECK_EQ(ew_image_check(EW_FW_AM, 4u, a, b, sizeof(a), NULL),
             EW_IMG_NOT_EDITABLE);
    b[0x02] = 1u;
    /* 0x0D: an edit of revision 4, kept on revision 3. */
    b[0x0D] = 9u;
    CHECK_EQ(ew_image_check(EW_FW_AM, 4u, a, b, sizeof(a), NULL), EW_IMG_OK);
    CHECK_EQ(ew_image_check(EW_FW_AM, 3u, a, b, sizeof(a), NULL),
             EW_IMG_NOT_EDITABLE);
    /* A stored value no field has stays when it does not change. */
    memset(a, 0xEE, sizeof(a));
    memcpy(b, a, sizeof(b));
    CHECK_EQ(ew_image_check(EW_FW_AM, 4u, a, b, sizeof(a), NULL), EW_IMG_OK);
}

int main(void)
{
    RUN(the_editable_fields_per_layout);
    RUN(which_layouts_are_written);
    RUN(every_field_at_both_ends_and_one_past);
    RUN(a_value_as_the_operator_reads_it);
    RUN(a_number_with_decimals_and_a_sign);
    RUN(a_text_stays_in_its_buffer);
    RUN(the_older_bluejay_layouts_name_their_own_bytes);
    RUN(what_is_kept_is_not_a_field_that_edits);
    RUN(the_check_of_a_draft);
    return test_summary("ew_map");
}
