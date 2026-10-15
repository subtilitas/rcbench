/*
 * S.BUS2 telemetry slots: the slot IDs, the footers, the cut of a window's
 * bytes into records and their assignment to slots, the table over many
 * windows, the sensor map and the values.
 *
 * The records are the captures of the step 5 research (sbus.md, section
 * 9.2) where one exists, and records shaped as those are elsewhere: 3 bytes
 * 120 us apart, slots 660 us or 700 us apart from 2000 us after the footer.
 * A capture of the owner's sensors replaces the shaped ones when it exists.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "sbus2.h"

/* One record on the wire. */
typedef struct {
    uint8_t id;
    uint8_t d0;
    uint8_t d1;
    uint32_t at_us;     /* first byte's start after the footer */
} rec_t;

/* The bytes of @p n records, 120 us apart within a record. */
static void build(sbus2_capture_t *cap, const rec_t *r, unsigned n)
{
    memset(cap, 0, sizeof(*cap));
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t b[3] = { r[i].id, r[i].d0, r[i].d1 };

        for (unsigned k = 0; k < 3u; ++k) {
            sbus2_rx_byte_t *x = &cap->byte[cap->n++];

            x->data = b[k];
            x->t_ns = (r[i].at_us + 120u * k) * 1000u;
            x->parity_ok = true;
            x->stop_ok = true;
        }
    }
}

static uint32_t slot_start(unsigned k, unsigned spacing)
{
    return 2000u + k * spacing;
}

/* --------------------------------------------------------------- the IDs */

TEST_CASE(the_slot_ids_of_all_32_slots_are_the_5_bit_number_backwards)
{
    /* Slots 0 to 15 as captured, 16 to 31 by the same rule. */
    static const uint8_t k_ids[SBUS2_SLOTS] = {
        0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
        0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
        0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
        0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
    };
    unsigned named = 0;

    for (unsigned s = 0; s < SBUS2_SLOTS; ++s) {
        CHECK_EQ(sbus2_slot_id(s), k_ids[s]);
        CHECK_EQ(sbus2_id_slot(k_ids[s]), (int)s);
        CHECK_EQ(sbus2_slot_id(s + SBUS2_SLOTS), k_ids[s]);
    }
    /* Every other byte names no slot, the S.BUS header among them. */
    for (unsigned b = 0; b < 256u; ++b) {
        const int s = sbus2_id_slot((uint8_t)b);

        if (s >= 0) {
            ++named;
            CHECK_EQ(b & 7u, 3u);
            CHECK_EQ(sbus2_slot_id((unsigned)s), b);
        }
    }
    CHECK_EQ(named, SBUS2_SLOTS);
    CHECK_EQ(sbus2_id_slot(0x0F), -1);
}

TEST_CASE(the_four_footers_select_the_four_groups_and_no_other_byte_does)
{
    static const uint8_t k_footers[4] = { 0x04, 0x14, 0x24, 0x34 };
    unsigned groups = 0;

    for (unsigned g = 0; g < 4u; ++g) {
        CHECK_EQ(sbus2_footer(g), k_footers[g]);
        CHECK_EQ(sbus2_footer(g + 4u), k_footers[g]);
        CHECK_EQ(sbus2_footer_group(k_footers[g]), (int)g);
    }
    for (unsigned b = 0; b < 256u; ++b) {
        if (sbus2_footer_group((uint8_t)b) >= 0) {
            ++groups;
        }
    }
    CHECK_EQ(groups, 4u);
    CHECK_EQ(sbus2_footer_group(0x00), -1);
    CHECK_EQ(sbus2_footer_group(0x44), -1);
    CHECK_EQ(sbus2_footer_group(0x0F), -1);
}

/* ------------------------------------------------------------ the windows */

/* A record in every slot of every group at @p spacing; all 8 are taken
 * with their data and their time. */
static void all_slots_at(unsigned spacing)
{
    for (unsigned g = 0; g < SBUS2_GROUPS; ++g) {
        rec_t r[SBUS2_GROUP_SLOTS];
        sbus2_capture_t cap;
        sbus2_window_t w;

        for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
            r[k].id = sbus2_slot_id(g * 8u + k);
            r[k].d0 = (uint8_t)(0x10u * g + k);
            r[k].d1 = (uint8_t)(0xF0u - k);
            r[k].at_us = slot_start(k, spacing);
        }
        build(&cap, r, SBUS2_GROUP_SLOTS);
        CHECK(sbus2_window_decode(NULL, sbus2_footer(g), &cap, &w));
        CHECK_EQ(w.group, g);
        CHECK_EQ(w.taken, 8u);
        CHECK_EQ(w.duplicate + w.wrong_id + w.early + w.late + w.short_rec
                     + w.bad_byte, 0u);
        for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
            CHECK(w.slot[k].taken);
            CHECK_EQ(w.slot[k].data[0], r[k].d0);
            CHECK_EQ(w.slot[k].data[1], r[k].d1);
            CHECK_EQ(w.slot[k].t_us, r[k].at_us);
        }
    }
}

TEST_CASE(every_slot_is_assigned_at_660_us_spacing)
{
    all_slots_at(660u);
}

TEST_CASE(every_slot_is_assigned_at_700_us_spacing)
{
    all_slots_at(700u);
}

TEST_CASE(a_timing_set_to_700_us_reads_700_us_and_660_us_slots)
{
    sbus2_timing_t t;

    sbus2_timing_default(&t);
    t.slot_us = 700u;
    t.early_us = 300u;       /* slot 7 of 660 us spacing is 280 us early */
    t.late_us = 380u;
    CHECK(sbus2_timing_ok(&t));
    for (unsigned spacing = 660; spacing <= 700u; spacing += 40u) {
        rec_t r[8];
        sbus2_capture_t cap;
        sbus2_window_t w;

        for (unsigned k = 0; k < 8u; ++k) {
            r[k].id = sbus2_slot_id(k);
            r[k].d0 = (uint8_t)k;
            r[k].d1 = 0;
            r[k].at_us = slot_start(k, spacing);
        }
        build(&cap, r, 8);
        CHECK(sbus2_window_decode(&t, SBUS2_FOOTER_0, &cap, &w));
        CHECK_EQ(w.taken, 8u);
    }
}

TEST_CASE(the_research_captures_land_in_their_slots_with_their_values)
{
    /* sbus.md 9.2: slot 0 two voltage records (one per window), slot 1 the
     * temperature sensor, slot 2 the injected temperature. */
    static const rec_t k_rx[3] = {
        { 0x03, 0xC0, 0x33, 2000 },
        { 0x83, 0x81, 0x80, 2660 },
        { 0x43, 0x7E, 0x80, 3320 },
    };
    sbus2_capture_t cap;
    sbus2_window_t w;
    sbus2_map_t map;
    sbus2_value_t v;
    char text[SBUS2_VALUE_TEXT];

    build(&cap, k_rx, 3);
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 3u);
    sbus2_map_default(&map);

    sbus2_value(map.slot[0].decode, w.slot[0].data, &v);
    CHECK_EQ(v.unit, SBUS2_UNIT_DECIVOLT);
    CHECK_EQ(v.value, 51);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "5.1 V");

    sbus2_value(map.slot[1].decode, w.slot[1].data, &v);
    CHECK_EQ(v.unit, SBUS2_UNIT_DEGC);
    CHECK_EQ(v.value, 29);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "29 C");

    /* Slot 2 is the RPM sensor's by default: raw, until the map says the
     * temperature sensor sits there. */
    sbus2_value(map.slot[2].decode, w.slot[2].data, &v);
    CHECK_EQ(v.unit, SBUS2_UNIT_RAW);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "7E 80");
    CHECK(sbus2_map_set(&map, 2, SBUS2_SENSOR_TEMP, 0, SBUS2_DEC_TEMP));
    sbus2_value(map.slot[2].decode, w.slot[2].data, &v);
    CHECK_EQ(v.value, 26);

    /* The external voltage record of the next window. */
    static const rec_t k_ext = { 0x03, 0xC4, 0x4F, 2000 };
    build(&cap, &k_ext, 1);
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    sbus2_value(map.slot[0].decode, w.slot[0].data, &v);
    CHECK_EQ(v.value, 79);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "7.9 V");
}

TEST_CASE(the_gps_capture_is_slots_8_to_15_shown_raw)
{
    static const rec_t k_gps[8] = {
        { 0x13, 0x53, 0x1A, 2000 }, { 0x93, 0x71, 0xD0, 2700 },
        { 0x53, 0xDE, 0xE2, 3400 }, { 0xD3, 0xAF, 0xD7, 4100 },
        { 0x33, 0xBF, 0x00, 4800 }, { 0xB3, 0x2E, 0x30, 5500 },
        { 0x73, 0xBC, 0xBB, 6200 }, { 0xF3, 0xE2, 0xAE, 6900 },
    };
    static const char *const k_text[8] = {
        "53 1A", "71 D0", "DE E2", "AF D7", "BF 00", "2E 30", "BC BB",
        "E2 AE",
    };
    sbus2_capture_t cap;
    sbus2_window_t w;
    sbus2_map_t map;
    sbus2_table_t tab;

    build(&cap, k_gps, 8);
    /* Under footer 0x04 every one of them is of another group. */
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 0u);
    CHECK_EQ(w.wrong_id, 8u);

    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_1, &cap, &w));
    CHECK_EQ(w.taken, 8u);
    sbus2_map_default(&map);
    sbus2_table_reset(&tab);
    sbus2_table_update(&tab, &w);
    for (unsigned k = 0; k < 8u; ++k) {
        sbus2_value_t v;
        char text[SBUS2_VALUE_TEXT];

        CHECK_EQ(map.slot[8u + k].sensor, SBUS2_SENSOR_GPS);
        CHECK_EQ(map.slot[8u + k].part, k);
        CHECK(sbus2_slot_live(&tab, 8u + k));
        sbus2_value(map.slot[8u + k].decode, tab.slot[8u + k].data, &v);
        CHECK_EQ(v.unit, SBUS2_UNIT_RAW);
        CHECK_EQ(v.value, (int32_t)(k_gps[k].d0 * 256u + k_gps[k].d1));
        sbus2_value_text(&v, text, sizeof(text));
        CHECK_STR_EQ(text, k_text[k]);
    }
}

TEST_CASE(missing_records_leave_their_slots_empty_and_age_in_the_table)
{
    static const rec_t k_two[2] = {
        { 0x83, 0x81, 0x80, 2660 },     /* slot 1 */
        { 0xA3, 0x12, 0x34, 5300 },     /* slot 5 */
    };
    static const rec_t k_one = { 0x83, 0x82, 0x80, 2660 };
    sbus2_capture_t cap;
    sbus2_window_t w;
    sbus2_table_t tab;

    sbus2_table_reset(&tab);
    build(&cap, k_two, 2);
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 2u);
    for (unsigned k = 0; k < 8u; ++k) {
        CHECK_EQ(w.slot[k].taken, k == 1u || k == 5u);
    }
    sbus2_table_update(&tab, &w);
    CHECK(sbus2_slot_live(&tab, 1));
    CHECK(sbus2_slot_live(&tab, 5));
    CHECK(!sbus2_slot_live(&tab, 0));
    CHECK(!sbus2_slot_live(&tab, 9));

    /* Slot 5 goes quiet; windows of another group do not age it. */
    build(&cap, &k_one, 1);
    for (unsigned i = 0; i < SBUS2_LIVE_WINDOWS; ++i) {
        sbus2_window_t other;
        sbus2_capture_t none;

        memset(&none, 0, sizeof(none));
        CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_2, &none, &other));
        sbus2_table_update(&tab, &other);
        CHECK(sbus2_slot_live(&tab, 5));

        CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
        sbus2_table_update(&tab, &w);
        CHECK_EQ(tab.slot[5].age, i + 1u);
        CHECK_EQ(tab.slot[5].missed, i + 1u);
        CHECK_EQ(sbus2_slot_live(&tab, 5), i + 1u < SBUS2_LIVE_WINDOWS);
        CHECK(sbus2_slot_live(&tab, 1));
    }
    /* The last record stays readable after it went stale. */
    CHECK(tab.slot[5].valid);
    CHECK_EQ(tab.slot[5].data[0], 0x12);
    CHECK_EQ(tab.slot[1].data[0], 0x82);
    CHECK_EQ(tab.slot[1].records, 1u + SBUS2_LIVE_WINDOWS);
    CHECK_EQ(tab.windows, 1u + 2u * SBUS2_LIVE_WINDOWS);
    /* A slot never seen is never missed. */
    CHECK_EQ(tab.slot[0].missed, 0u);

    /* The age stops at 255. */
    for (unsigned i = 0; i < 300u; ++i) {
        sbus2_table_update(&tab, &w);
    }
    CHECK_EQ(tab.slot[5].age, 255u);
}

TEST_CASE(a_duplicated_record_keeps_the_first_and_is_counted)
{
    const uint32_t s3 = slot_start(3, 660);
    const rec_t r[3] = {
        { 0xC3, 0x01, 0x02, s3 },
        { 0xC3, 0x03, 0x04, s3 + 380u },   /* still slot 3's window */
        { 0xC3, 0x05, 0x06, s3 + 400u + 360u },  /* past it: late */
    };
    sbus2_capture_t cap;
    sbus2_window_t w;

    build(&cap, r, 3);
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 1u);
    CHECK_EQ(w.duplicate, 1u);
    CHECK_EQ(w.late, 1u);
    CHECK_EQ(w.slot[3].data[0], 0x01);
    CHECK_EQ(w.slot[3].data[1], 0x02);
}

TEST_CASE(a_wrong_slot_id_is_refused_and_counted)
{
    const rec_t r[4] = {
        { 0x13, 0x01, 0x02, slot_start(0, 660) },  /* slot 8: group 1 */
        { 0x55, 0x01, 0x02, slot_start(1, 660) },  /* no slot ID at all */
        { 0x0F, 0x01, 0x02, slot_start(2, 660) },  /* the S.BUS header */
        { 0xC3, 0x01, 0x02, slot_start(3, 660) },  /* slot 3, right */
    };
    sbus2_capture_t cap;
    sbus2_window_t w;

    build(&cap, r, 4);
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.wrong_id, 3u);
    CHECK_EQ(w.taken, 1u);
    CHECK(w.slot[3].taken);
    CHECK(!w.slot[0].taken);
    CHECK(!w.slot[1].taken);
    CHECK(!w.slot[2].taken);
}

TEST_CASE(a_record_outside_its_slots_window_is_early_or_late)
{
    /* Slot 4's window is [start - 200 us, start + 400 us). */
    const uint32_t s4 = slot_start(4, 660);
    static const struct {
        int32_t off_us;
        unsigned taken, early, late;
    } k_cases[] = {
        { -201, 0, 1, 0 }, { -200, 1, 0, 0 }, { 0, 1, 0, 0 },
        { 280, 1, 0, 0 },     /* 700 us spacing at slot 7 */
        { 399, 1, 0, 0 }, { 400, 0, 0, 1 }, { 660, 0, 0, 1 },
        { -660, 0, 1, 0 },    /* slot 3's time with slot 4's ID */
    };

    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i) {
        const rec_t r = { 0x23, 0xAA, 0x55,
                          (uint32_t)((int32_t)s4 + k_cases[i].off_us) };
        sbus2_capture_t cap;
        sbus2_window_t w;

        build(&cap, &r, 1);
        CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
        if (w.taken != k_cases[i].taken || w.early != k_cases[i].early
            || w.late != k_cases[i].late) {
            T_FAIL("offset %d us: taken %u early %u late %u",
                   (int)k_cases[i].off_us, w.taken, w.early, w.late);
        }
    }
}

TEST_CASE(short_records_and_bad_bytes_are_refused_and_counted)
{
    sbus2_capture_t cap;
    sbus2_window_t w;
    const uint32_t s0 = slot_start(0, 660);
    const uint32_t s2 = slot_start(2, 660);
    const uint32_t s5 = slot_start(5, 660);

    /* Slot 0: two bytes, then nothing.  Slot 2: one noise byte.  Slot 5: a
     * record with a parity error in its second byte. */
    memset(&cap, 0, sizeof(cap));
    const struct {
        uint8_t d;
        uint32_t at;
        bool par;
    } k_bytes[] = {
        { 0x03, s0, true }, { 0x11, s0 + 120u, true },
        { 0x43, s2, true },
        { 0xA3, s5, true }, { 0x22, s5 + 120u, false }, { 0x33, s5 + 240u, true },
    };
    for (size_t i = 0; i < sizeof(k_bytes) / sizeof(k_bytes[0]); ++i) {
        sbus2_rx_byte_t *b = &cap.byte[cap.n++];

        b->data = k_bytes[i].d;
        b->t_ns = k_bytes[i].at * 1000u;
        b->parity_ok = k_bytes[i].par;
        b->stop_ok = true;
    }
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.short_rec, 2u);
    CHECK_EQ(w.bad_byte, 1u);
    CHECK_EQ(w.taken, 0u);

    /* A stop bit error refuses a record the same way. */
    const rec_t r = { 0xA3, 0x01, 0x02, s5 };
    build(&cap, &r, 1);
    cap.byte[2].stop_ok = false;
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.bad_byte, 1u);
    CHECK_EQ(w.taken, 0u);
}

TEST_CASE(the_byte_gap_splits_records_only_above_200_us)
{
    sbus2_capture_t cap;
    sbus2_window_t w;
    const uint32_t s1 = slot_start(1, 660);
    const rec_t r = { 0x83, 0x81, 0x80, s1 };

    /* 200 us between the bytes: one record. */
    build(&cap, &r, 1);
    cap.byte[1].t_ns = (s1 + 200u) * 1000u;
    cap.byte[2].t_ns = (s1 + 400u) * 1000u;
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 1u);

    /* 1 ns more before the last byte: a short record and a stray byte. */
    cap.byte[2].t_ns = (s1 + 400u) * 1000u + 1u;
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 0u);
    CHECK_EQ(w.short_rec, 2u);

    /* A byte stamped before the one ahead of it starts a record. */
    build(&cap, &r, 1);
    cap.byte[1].t_ns = s1 * 1000u - 1u;
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 0u);
}

TEST_CASE(back_to_back_records_of_neighbouring_slots_stay_apart)
{
    /* The fourth byte follows the third 120 us later: a record has 3 bytes,
     * so it starts the next one even without a gap. */
    sbus2_capture_t cap;
    sbus2_window_t w;
    sbus2_timing_t t;
    rec_t r[2] = {
        { 0x03, 0x01, 0x02, 2000 },
        { 0x83, 0x03, 0x04, 2360 },
    };

    sbus2_timing_default(&t);
    t.slot_us = 360u;
    t.early_us = 0u;
    t.late_us = 360u;
    t.byte_gap_us = 121u;
    CHECK(!sbus2_timing_ok(&t));   /* the gap cannot part two records */
    build(&cap, r, 2);
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK_EQ(w.taken, 1u);          /* slot 1 at 2360 us is early */
    CHECK_EQ(w.early, 1u);
    CHECK_EQ(w.slot[0].data[1], 0x02);
}

TEST_CASE(an_overflowed_capture_is_read_and_counted)
{
    sbus2_capture_t cap;
    sbus2_window_t w;
    sbus2_table_t tab;
    const rec_t r = { 0x03, 0x01, 0x02, 2000 };

    build(&cap, &r, 1);
    cap.overflow = true;
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
    CHECK(w.overflow);
    CHECK_EQ(w.taken, 1u);
    sbus2_table_reset(&tab);
    sbus2_table_update(&tab, &w);
    CHECK_EQ(tab.overflow, 1u);

    /* A count above the array is read as the array. */
    cap.n = 200;
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &w));
}

TEST_CASE(a_window_is_refused_for_a_footer_that_is_no_sbus2_footer)
{
    sbus2_capture_t cap;
    sbus2_window_t w;
    sbus2_timing_t t;

    memset(&cap, 0, sizeof(cap));
    memset(&w, 0xA5, sizeof(w));
    CHECK(!sbus2_window_decode(NULL, 0x00, &cap, &w));
    CHECK_EQ(w.taken, 0u);
    CHECK(!sbus2_window_decode(NULL, SBUS2_FOOTER_0, NULL, &w));
    CHECK(!sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, NULL));
    sbus2_timing_default(&t);
    t.slot_us = 0;
    CHECK(!sbus2_window_decode(&t, SBUS2_FOOTER_0, &cap, &w));
    sbus2_table_update(NULL, &w);
    sbus2_table_reset(NULL);
    CHECK(!sbus2_slot_live(NULL, 0));
}

TEST_CASE(the_timing_check_refuses_overlap_a_bad_gap_and_a_late_last_slot)
{
    sbus2_timing_t t;

    sbus2_timing_default(&t);
    CHECK_EQ(t.slot0_us, 2000u);
    CHECK_EQ(t.slot_us, 660u);
    CHECK(sbus2_timing_ok(&t));
    CHECK(!sbus2_timing_ok(NULL));
    sbus2_timing_default(NULL);

    t.late_us = 461u;                    /* 200 + 461 > 660 */
    CHECK(!sbus2_timing_ok(&t));
    t.late_us = 460u;
    CHECK(sbus2_timing_ok(&t));

    sbus2_timing_default(&t);
    t.early_us = 2001u;
    t.slot_us = 2500u;
    CHECK(!sbus2_timing_ok(&t));          /* before the footer ends */

    sbus2_timing_default(&t);
    t.byte_gap_us = 120u;
    CHECK(!sbus2_timing_ok(&t));
    t.byte_gap_us = 419u;                 /* 419 + 240 < 660 */
    CHECK(sbus2_timing_ok(&t));
    t.byte_gap_us = 420u;
    CHECK(!sbus2_timing_ok(&t));

    /* Slot 7 has to end inside the 8 ms window: 2000 + 7 x 660 + 400 +
     * 360 = 7380 us. */
    sbus2_timing_default(&t);
    t.slot0_us = 2620u;                   /* ends at 8000 */
    CHECK(sbus2_timing_ok(&t));
    t.slot0_us = 2621u;
    CHECK(!sbus2_timing_ok(&t));
}

/* --------------------------------------------------- the map and the values */

TEST_CASE(the_default_map_puts_each_sensor_at_its_futaba_default_slot)
{
    static const struct {
        unsigned slot;
        unsigned sensor;
        unsigned part;
        unsigned decode;
    } k_want[SBUS2_SLOTS] = {
        { 0, SBUS2_SENSOR_RECEIVER, 0, SBUS2_DEC_VOLT },
        { 1, SBUS2_SENSOR_TEMP, 0, SBUS2_DEC_TEMP },
        { 2, SBUS2_SENSOR_RPM, 0, SBUS2_DEC_RAW },
        { 3, SBUS2_SENSOR_ALTITUDE, 0, SBUS2_DEC_RAW },
        { 4, SBUS2_SENSOR_ALTITUDE, 1, SBUS2_DEC_RAW },
        { 5, SBUS2_SENSOR_ALTITUDE, 2, SBUS2_DEC_RAW },
        { 6, SBUS2_SENSOR_VOLTAGE, 0, SBUS2_DEC_RAW },
        { 7, SBUS2_SENSOR_VOLTAGE, 1, SBUS2_DEC_RAW },
        { 8, SBUS2_SENSOR_GPS, 0, 0 }, { 9, SBUS2_SENSOR_GPS, 1, 0 },
        { 10, SBUS2_SENSOR_GPS, 2, 0 }, { 11, SBUS2_SENSOR_GPS, 3, 0 },
        { 12, SBUS2_SENSOR_GPS, 4, 0 }, { 13, SBUS2_SENSOR_GPS, 5, 0 },
        { 14, SBUS2_SENSOR_GPS, 6, 0 }, { 15, SBUS2_SENSOR_GPS, 7, 0 },
        { 16, SBUS2_SENSOR_SERVO, 0, 0 }, { 17, SBUS2_SENSOR_SERVO, 1, 0 },
        { 18, SBUS2_SENSOR_SERVO, 2, 0 }, { 19, SBUS2_SENSOR_SERVO, 3, 0 },
        { 20, SBUS2_SENSOR_SERVO, 4, 0 }, { 21, SBUS2_SENSOR_SERVO, 5, 0 },
        { 22, SBUS2_SENSOR_NONE, 0, 0 }, { 23, SBUS2_SENSOR_NONE, 0, 0 },
        { 24, SBUS2_SENSOR_CURRENT, 0, 0 }, { 25, SBUS2_SENSOR_CURRENT, 1, 0 },
        { 26, SBUS2_SENSOR_CURRENT, 2, 0 },
        { 27, SBUS2_SENSOR_NONE, 0, 0 }, { 28, SBUS2_SENSOR_NONE, 0, 0 },
        { 29, SBUS2_SENSOR_NONE, 0, 0 }, { 30, SBUS2_SENSOR_NONE, 0, 0 },
        { 31, SBUS2_SENSOR_NONE, 0, 0 },
    };
    sbus2_map_t map;

    sbus2_map_default(&map);
    for (unsigned s = 0; s < SBUS2_SLOTS; ++s) {
        CHECK_EQ(k_want[s].slot, s);
        CHECK_EQ(map.slot[s].sensor, k_want[s].sensor);
        CHECK_EQ(map.slot[s].part, k_want[s].part);
        CHECK_EQ(map.slot[s].decode, k_want[s].decode);
    }
    /* Every sensor of 8 slots or fewer lies in one group. */
    for (unsigned s = 0; s < SBUS2_SLOTS; ++s) {
        if (map.slot[s].sensor != SBUS2_SENSOR_NONE) {
            const unsigned first = s - map.slot[s].part;

            CHECK_EQ(first / 8u, s / 8u);
        }
    }
    CHECK_EQ(sbus2_sensor_slots(SBUS2_SENSOR_NONE), 0u);
    CHECK_EQ(sbus2_sensor_slots(SBUS2_SENSOR_GPS), 8u);
    CHECK_EQ(sbus2_sensor_slots(SBUS2_SENSOR_SERVO), 6u);
    CHECK_EQ(sbus2_sensor_slots(SBUS2_SENSOR_COUNT), 0u);
    sbus2_map_default(NULL);
}

TEST_CASE(the_map_is_editable_per_slot_within_what_each_sensor_has)
{
    sbus2_map_t map;
    sbus2_map_t before;

    sbus2_map_default(&map);
    CHECK(sbus2_map_set(&map, 31, SBUS2_SENSOR_TEMP, 0, SBUS2_DEC_TEMP));
    CHECK_EQ(map.slot[31].sensor, SBUS2_SENSOR_TEMP);
    CHECK_EQ(map.slot[31].decode, SBUS2_DEC_TEMP);
    CHECK(sbus2_map_set(&map, 1, SBUS2_SENSOR_TEMP, 0, SBUS2_DEC_RAW));
    CHECK_EQ(map.slot[1].decode, SBUS2_DEC_RAW);
    CHECK(sbus2_map_set(&map, 22, SBUS2_SENSOR_NONE, 0, SBUS2_DEC_VOLT));
    CHECK(sbus2_map_set(&map, 15, SBUS2_SENSOR_GPS, 7, SBUS2_DEC_RAW));

    before = map;
    CHECK(!sbus2_map_set(&map, 32, SBUS2_SENSOR_TEMP, 0, 0));
    CHECK(!sbus2_map_set(&map, 3, SBUS2_SENSOR_COUNT, 0, 0));
    CHECK(!sbus2_map_set(&map, 3, SBUS2_SENSOR_TEMP, 0, SBUS2_DEC_COUNT));
    CHECK(!sbus2_map_set(&map, 3, SBUS2_SENSOR_TEMP, 1, 0));
    CHECK(!sbus2_map_set(&map, 3, SBUS2_SENSOR_GPS, 8, 0));
    CHECK(!sbus2_map_set(&map, 3, SBUS2_SENSOR_NONE, 1, 0));
    CHECK(!sbus2_map_set(NULL, 3, SBUS2_SENSOR_TEMP, 0, 0));
    CHECK_EQ(memcmp(&map, &before, sizeof(map)), 0);
}

TEST_CASE(values_and_their_text_for_each_encoding_and_unknown_ones_raw)
{
    sbus2_value_t v;
    char text[SBUS2_VALUE_TEXT];
    const uint8_t cold[2] = { 0x58, 0x80 };      /* 88 - 100 = -12 C */
    const uint8_t zero[2] = { 0x00, 0x00 };
    const uint8_t full[2] = { 0xFF, 0xFF };

    sbus2_value(SBUS2_DEC_TEMP, cold, &v);
    CHECK_EQ(v.value, -12);
    CHECK_EQ(sbus2_value_text(&v, text, sizeof(text)), 5u);
    CHECK_STR_EQ(text, "-12 C");

    sbus2_value(SBUS2_DEC_TEMP, zero, &v);
    CHECK_EQ(v.value, -100);
    sbus2_value(SBUS2_DEC_TEMP, full, &v);
    CHECK_EQ(v.value, 155);

    sbus2_value(SBUS2_DEC_VOLT, zero, &v);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "0.0 V");
    sbus2_value(SBUS2_DEC_VOLT, full, &v);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "25.5 V");

    /* An encoding the module does not know reads raw. */
    sbus2_value(SBUS2_DEC_COUNT, cold, &v);
    CHECK_EQ(v.unit, SBUS2_UNIT_RAW);
    CHECK_EQ(v.value, 0x5880);
    CHECK_EQ(v.raw[0], 0x58);
    CHECK_EQ(v.raw[1], 0x80);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "58 80");
    sbus2_value(SBUS2_DEC_RAW, zero, &v);
    sbus2_value_text(&v, text, sizeof(text));
    CHECK_STR_EQ(text, "00 00");

    /* The widest values a caller can hand in fit. */
    v.unit = SBUS2_UNIT_DECIVOLT;
    v.value = INT32_MIN;
    CHECK_EQ(sbus2_value_text(&v, text, sizeof(text)), 14u);
    CHECK_STR_EQ(text, "-214748364.8 V");
    v.unit = SBUS2_UNIT_DEGC;
    v.value = INT32_MIN;
    CHECK_EQ(sbus2_value_text(&v, text, sizeof(text)), 13u);
    CHECK_STR_EQ(text, "-2147483648 C");

    /* A buffer below SBUS2_VALUE_TEXT gets an empty string. */
    CHECK_EQ(sbus2_value_text(&v, text, SBUS2_VALUE_TEXT - 1u), 0u);
    CHECK_STR_EQ(text, "");
    CHECK_EQ(sbus2_value_text(&v, NULL, sizeof(text)), 0u);
    CHECK_EQ(sbus2_value_text(&v, text, 0), 0u);
    CHECK_EQ(sbus2_value_text(NULL, text, sizeof(text)), 0u);
    CHECK_STR_EQ(text, "");
    sbus2_value(SBUS2_DEC_RAW, NULL, &v);
    CHECK_EQ(v.value, 0);
    sbus2_value(SBUS2_DEC_RAW, zero, NULL);
}

int main(void)
{
    RUN(the_slot_ids_of_all_32_slots_are_the_5_bit_number_backwards);
    RUN(the_four_footers_select_the_four_groups_and_no_other_byte_does);
    RUN(every_slot_is_assigned_at_660_us_spacing);
    RUN(every_slot_is_assigned_at_700_us_spacing);
    RUN(a_timing_set_to_700_us_reads_700_us_and_660_us_slots);
    RUN(the_research_captures_land_in_their_slots_with_their_values);
    RUN(the_gps_capture_is_slots_8_to_15_shown_raw);
    RUN(missing_records_leave_their_slots_empty_and_age_in_the_table);
    RUN(a_duplicated_record_keeps_the_first_and_is_counted);
    RUN(a_wrong_slot_id_is_refused_and_counted);
    RUN(a_record_outside_its_slots_window_is_early_or_late);
    RUN(short_records_and_bad_bytes_are_refused_and_counted);
    RUN(the_byte_gap_splits_records_only_above_200_us);
    RUN(back_to_back_records_of_neighbouring_slots_stay_apart);
    RUN(an_overflowed_capture_is_read_and_counted);
    RUN(a_window_is_refused_for_a_footer_that_is_no_sbus2_footer);
    RUN(the_timing_check_refuses_overlap_a_bad_gap_and_a_late_last_slot);
    RUN(the_default_map_puts_each_sensor_at_its_futaba_default_slot);
    RUN(the_map_is_editable_per_slot_within_what_each_sensor_has);
    RUN(values_and_their_text_for_each_encoding_and_unknown_ones_raw);
    return test_summary("sbus2");
}
