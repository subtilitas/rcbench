/*
 * The guard before the driver: every row of its table refuses on the path
 * of a draft (oy_guard_approve) and on the path of a frame
 * (oy_guard_frame), with the reason of that row.  A row taken out of the
 * guard fails a case here.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "openyge.h"
#include "openyge_guard.h"
#include "openyge_map.h"

static uint16_t g_t[OPENYGE_MAX_PARAMS];
static uint8_t g_f[OPENYGE_MAX_FRAME];

/* A table a writable ESC holds: count 32, firmware V1.03555. */
static void good_table(uint16_t *t)
{
    static const uint16_t good[32] = {
        32, 2, 74, 0, 0, 5, 5, 1, 1, 2, 1, 5712, 0x9483u, 0x0001u, 0x1234, 0,
        0, 1100, 800, 20, 5, 12, 120, 5, 20, 0, 0, 12000, 0, 0, 0, 0,
    };
    memset(t, 0, OPENYGE_MAX_PARAMS * sizeof(t[0]));
    memcpy(t, good, sizeof(good));
}

/* A guard that has read the good table of the sim. */
static void ready(oy_guard_t *g)
{
    memset(g, 0, sizeof(*g));
    oy_guard_init(g);
    good_table(g_t);
    oy_guard_table(g, g_t, 32, false);
}

/* Approve the read copy with one index changed. */
static oy_ref_t approve_one(oy_guard_t *g, uint16_t index, uint16_t value,
                            uint16_t *bad)
{
    uint16_t d[OPENYGE_MAX_PARAMS];
    memcpy(d, g->orig, sizeof(d));
    d[index] = value;
    return oy_guard_approve(g, d, g->count, g->read_no, g->read_crc, bad);
}

/* Make the CRC of an @p n-byte frame in g_f again after an edit. */
static void recrc(size_t n)
{
    const uint16_t crc = openyge_crc(OPENYGE_CRC_INIT, g_f, n - 2u);
    g_f[n - 2u] = (uint8_t)(crc & 0xFFu);
    g_f[n - 1u] = (uint8_t)(crc >> 8);
}

/* A write frame of index and value. */
static size_t wframe(uint16_t index, uint16_t value)
{
    return openyge_build_param_write(g_f, sizeof(g_f), 1, 7, index, value);
}

TEST_CASE(a_request_passes_before_any_table)
{
    oy_guard_t g;
    memset(&g, 0, sizeof(g));
    oy_guard_init(&g);
    const size_t n = openyge_build_telemetry_request(g_f, sizeof(g_f), 1, 0);
    CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_NONE);

    /* A request with a payload that is not zero is no request. */
    const uint8_t ctl[4] = { 21, 0, 13, 0 };
    const size_t m = openyge_encode(g_f, sizeof(g_f), OPENYGE_FT_TELE_REQ, 1,
                                    0, ctl, sizeof(ctl));
    CHECK_EQ(oy_guard_frame(&g, g_f, m), OY_REF_REQUEST);
}

TEST_CASE(only_requests_and_writes_pass_the_allow_list)
{
    oy_guard_t g;
    ready(&g);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
    const uint8_t tele[OPENYGE_TELEMETRY_BYTES] = { 0 };
    static const uint8_t ESC_TYPES[] = { OPENYGE_FT_TELE_AUTO,
                                         OPENYGE_FT_TELE_RESP,
                                         OPENYGE_FT_WRITE_PARAM_RESP };
    for (size_t k = 0; k < sizeof(ESC_TYPES); ++k) {
        const size_t n = openyge_encode(g_f, sizeof(g_f), ESC_TYPES[k], 1, 0,
                                        tele, sizeof(tele));
        CHECK(n > 0u);
        CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_TYPE);
    }
    /* Types the codec does not know: 0x01, 0x06 and 0x81. */
    static const uint8_t ODD[] = { 0x01, 0x06, 0x81 };
    for (size_t k = 0; k < sizeof(ODD); ++k) {
        const size_t n = wframe(21, 13);
        g_f[2] = ODD[k];
        CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_TYPE);
    }
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_NONE);
}

TEST_CASE(a_frame_out_of_form_is_refused)
{
    oy_guard_t g;
    ready(&g);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
    size_t n = wframe(21, 13);
    CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_NONE);

    CHECK_EQ(oy_guard_frame(&g, g_f, 5), OY_REF_FORM);       /* short */
    CHECK_EQ(oy_guard_frame(&g, g_f, n - 1u), OY_REF_FORM);
    CHECK_EQ(oy_guard_frame(&g, g_f, n + 1u), OY_REF_FORM);
    /* Each field broken alone, the CRC made again over it. */
    g_f[0] = 0xA4;
    recrc(n);
    CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_FORM);       /* sync */
    n = wframe(21, 13);
    g_f[1] = 2;
    recrc(n);
    CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_FORM);       /* legacy */
    n = wframe(21, 13);
    g_f[3] = 13;
    recrc(n);
    CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_FORM);       /* length */
    n = wframe(21, 13);
    g_f[5] = 0x01;
    recrc(n);
    CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_FORM);       /* master bit */
    n = wframe(21, 13);
    g_f[n - 1u] ^= 1u;
    CHECK_EQ(oy_guard_frame(&g, g_f, n), OY_REF_FORM);       /* CRC */
    CHECK_EQ(oy_guard_frame(&g, NULL, n), OY_REF_ARG);
    CHECK_EQ(oy_guard_frame(NULL, g_f, n), OY_REF_ARG);
}

TEST_CASE(a_write_needs_a_complete_table_this_session)
{
    oy_guard_t g;
    ready(&g);
    oy_guard_init(&g);   /* a new session forgets the table */
    good_table(g_t);
    g_t[21] = 13;
    CHECK_EQ(oy_guard_approve(&g, g_t, 32, g.read_no, g.read_crc, NULL),
             OY_REF_NO_TABLE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_NO_TABLE);
}

TEST_CASE(a_draft_from_another_read_is_refused)
{
    oy_guard_t g;
    ready(&g);
    uint16_t d[OPENYGE_MAX_PARAMS];
    memcpy(d, g.orig, sizeof(d));
    d[21] = 13;
    const uint16_t no = g.read_no, crc = g.read_crc;
    CHECK_EQ(oy_guard_approve(&g, d, 32, (uint16_t)(no - 1u), crc, NULL),
             OY_REF_OTHER_READ);
    CHECK_EQ(oy_guard_approve(&g, d, 32, no, (uint16_t)(crc ^ 1u), NULL),
             OY_REF_OTHER_READ);
    CHECK_EQ(oy_guard_approve(&g, d, 32, no, crc, NULL), OY_REF_NONE);

    /* Read again, the same values: the old read number no longer serves. */
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(g.approved, false);
    CHECK_EQ(oy_guard_approve(&g, d, 32, no, crc, NULL), OY_REF_OTHER_READ);
    CHECK_EQ(oy_guard_approve(&g, d, 32, g.read_no, g.read_crc, NULL),
             OY_REF_NONE);
    /* A draft of another length. */
    CHECK_EQ(oy_guard_approve(&g, d, 31, g.read_no, g.read_crc, NULL),
             OY_REF_ARG);
    CHECK_EQ(oy_guard_approve(&g, NULL, 32, g.read_no, g.read_crc, NULL),
             OY_REF_ARG);
}

TEST_CASE(the_read_number_wraps)
{
    oy_guard_t g;
    ready(&g);
    g.read_no = 0xFFFEu;
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(g.read_no, 0xFFFFu);
    uint16_t d[OPENYGE_MAX_PARAMS];
    memcpy(d, g.orig, sizeof(d));
    d[21] = 13;
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(g.read_no, 0u);
    CHECK_EQ(oy_guard_approve(&g, d, 32, 0xFFFFu, g.read_crc, NULL),
             OY_REF_OTHER_READ);
    CHECK_EQ(oy_guard_approve(&g, d, 32, 0u, g.read_crc, NULL), OY_REF_NONE);
    /* A new session keeps counting, so a draft of the last one is old. */
    oy_guard_init(&g);
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(g.read_no, 1u);
}

TEST_CASE(identity_indices_are_never_written)
{
    static const uint16_t ID[] = { 0, 11, 12, 13, 14, 15 };
    for (size_t k = 0; k < sizeof(ID) / sizeof(ID[0]); ++k) {
        oy_guard_t g;
        ready(&g);
        uint16_t bad = 0xFFFF;
        CHECK_EQ(approve_one(&g, ID[k], (uint16_t)(g.orig[ID[k]] + 1u), &bad),
                 OY_REF_IDENTITY);
        CHECK_EQ(bad, ID[k]);
        CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
        CHECK_EQ(oy_guard_frame(&g, g_f, wframe(ID[k], 1)), OY_REF_IDENTITY);
    }
}

TEST_CASE(the_telemetry_type_is_never_written)
{
    oy_guard_t g;
    ready(&g);
    uint16_t bad = 0;
    CHECK_EQ(approve_one(&g, 25, 1, &bad), OY_REF_TELEMETRY);
    CHECK_EQ(bad, 25u);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(25, 1)), OY_REF_TELEMETRY);
}

TEST_CASE(raw_indices_are_not_editable)
{
    static const uint16_t RAWS[] = { 4, 16, 19, 20, 28, 29, 30, 31 };
    for (size_t k = 0; k < sizeof(RAWS) / sizeof(RAWS[0]); ++k) {
        oy_guard_t g;
        ready(&g);
        uint16_t bad = 0;
        CHECK_EQ(approve_one(&g, RAWS[k], 1, &bad), OY_REF_NOT_EDITABLE);
        CHECK_EQ(bad, RAWS[k]);
        CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
        CHECK_EQ(oy_guard_frame(&g, g_f, wframe(RAWS[k], 1)),
                 OY_REF_NOT_EDITABLE);
    }
}

TEST_CASE(an_index_at_or_above_the_count_is_refused)
{
    oy_guard_t g;
    ready(&g);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
    static const uint16_t PAST[] = { 32, 33, 63, 64, 0xFFFF };
    for (size_t k = 0; k < sizeof(PAST) / sizeof(PAST[0]); ++k) {
        CHECK_EQ(oy_guard_frame(&g, g_f, wframe(PAST[k], 1)),
                 OY_REF_PAST_COUNT);
    }
}

TEST_CASE(a_value_outside_the_set_is_refused)
{
    oy_guard_t g;
    ready(&g);
    uint16_t bad = 0;
    CHECK_EQ(approve_one(&g, 21, 0, &bad), OY_REF_VALUE);
    CHECK_EQ(bad, 21u);
    CHECK_EQ(approve_one(&g, 3, 16, &bad), OY_REF_VALUE);
    CHECK_EQ(approve_one(&g, 2, 85, &bad), OY_REF_VALUE);
    CHECK_EQ(approve_one(&g, 27, 0, &bad), OY_REF_VALUE);
    CHECK_EQ(approve_one(&g, 21, 255, NULL), OY_REF_NONE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 256)), OY_REF_VALUE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(10, 2)), OY_REF_VALUE);
    /* BEC 100 needs flag bit 3 in the approved draft, and it is clear. */
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(2, 100)), OY_REF_VALUE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 255)), OY_REF_NONE);
}

TEST_CASE(bec_to_120_needs_flag_bit_3_as_read)
{
    oy_guard_t g;
    ready(&g);
    g_t[26] = OY_FLAG_BEC12V;
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(approve_one(&g, 2, 120, NULL), OY_REF_NONE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(2, 120)), OY_REF_NONE);
    CHECK_EQ(approve_one(&g, 2, 121, NULL), OY_REF_VALUE);
    /* Clearing bit 3 in the same draft is a flag change, refused first. */
    uint16_t d[OPENYGE_MAX_PARAMS];
    memcpy(d, g.orig, sizeof(d));
    d[2] = 84;
    d[26] = 0;
    CHECK_EQ(oy_guard_approve(&g, d, 32, g.read_no, g.read_crc, NULL),
             OY_REF_FLAG_BITS);
}

TEST_CASE(a_flag_write_changes_bits_0_and_1_only)
{
    oy_guard_t g;
    ready(&g);
    g_t[26] = 0x00F4u;
    oy_guard_table(&g, g_t, 32, false);
    for (unsigned bit = 2; bit < 16u; ++bit) {
        uint16_t bad = 0;
        const uint16_t v = (uint16_t)(0x00F4u ^ (1u << bit));
        CHECK_EQ(approve_one(&g, 26, v, &bad), OY_REF_FLAG_BITS);
        CHECK_EQ(bad, 26u);
    }
    CHECK_EQ(approve_one(&g, 26, 0x00F7u, NULL), OY_REF_NONE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(26, 0x00F7u)), OY_REF_NONE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(26, 0x00FFu)), OY_REF_FLAG_BITS);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(26, 0x0007u)), OY_REF_FLAG_BITS);
}

TEST_CASE(a_table_of_another_count_legacy_or_old_firmware_is_read_only)
{
    oy_guard_t g;
    static const uint16_t COUNTS[] = { 1, 31, 33, 64 };
    for (size_t k = 0; k < sizeof(COUNTS) / sizeof(COUNTS[0]); ++k) {
        ready(&g);
        g_t[0] = COUNTS[k];
        oy_guard_table(&g, g_t, COUNTS[k], false);
        CHECK_EQ(g.ro, OY_REF_RO_COUNT);
        CHECK_EQ(oy_guard_approve(&g, g_t, COUNTS[k], g.read_no, g.read_crc,
                                  NULL), OY_REF_RO_COUNT);
        CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_RO_COUNT);
    }
    ready(&g);
    oy_guard_table(&g, g_t, 32, true);
    CHECK_EQ(g.ro, OY_REF_RO_LEGACY);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_RO_LEGACY);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_RO_LEGACY);

    /* Firmware V1.03546 is one below the first with parameter access. */
    ready(&g);
    g_t[12] = (uint16_t)(103546u & 0xFFFFu);
    g_t[13] = (uint16_t)(103546u >> 16);
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(g.ro, OY_REF_RO_FIRMWARE);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_RO_FIRMWARE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_RO_FIRMWARE);
    g_t[12] = (uint16_t)(103547u & 0xFFFFu);
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(g.ro, OY_REF_NONE);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
}

TEST_CASE(a_write_frame_needs_the_approved_value)
{
    oy_guard_t g;
    ready(&g);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_NOT_APPROVED);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 14)), OY_REF_NOT_APPROVED);
    /* An index the draft leaves as read is not written either. */
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(22, 120)), OY_REF_NOT_APPROVED);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_NONE);
    oy_guard_done(&g);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_NOT_APPROVED);
    /* A refused draft leaves nothing approved. */
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
    CHECK_EQ(approve_one(&g, 21, 0, NULL), OY_REF_VALUE);
    CHECK_EQ(oy_guard_frame(&g, g_f, wframe(21, 13)), OY_REF_NOT_APPROVED);
}

TEST_CASE(a_value_read_outside_its_set_goes_back_as_read)
{
    oy_guard_t g;
    ready(&g);
    g_t[5] = 10;     /* a gain the other source allows */
    g_t[3] = 16;     /* the timing code written as 0 */
    oy_guard_table(&g, g_t, 32, false);
    CHECK_EQ(approve_one(&g, 21, 13, NULL), OY_REF_NONE);
    CHECK_EQ(approve_one(&g, 5, 9, NULL), OY_REF_NONE);
    CHECK_EQ(approve_one(&g, 5, 11, NULL), OY_REF_VALUE);
}

TEST_CASE(the_table_crc_is_xmodem_over_little_endian_values)
{
    const uint16_t t[2] = { 0x3132u, 0x0033u };
    const uint8_t b[4] = { 0x32, 0x31, 0x33, 0x00 };
    CHECK_EQ(oy_guard_table_crc(t, 2), openyge_crc(OPENYGE_CRC_INIT, b, 4));
    CHECK_EQ(oy_guard_table_crc(t, 0), OPENYGE_CRC_INIT);
    CHECK_EQ(oy_guard_table_crc(NULL, 2), OPENYGE_CRC_INIT);
    /* A table that cannot be held is not taken. */
    oy_guard_t g;
    ready(&g);
    const uint16_t no = g.read_no;
    oy_guard_table(&g, g_t, 0, false);
    oy_guard_table(&g, g_t, 65, false);
    oy_guard_table(&g, NULL, 32, false);
    CHECK_EQ(g.read_no, no);
    oy_guard_init(NULL);
    oy_guard_done(NULL);
    oy_guard_table(NULL, g_t, 32, false);
}

int main(void)
{
    RUN(a_request_passes_before_any_table);
    RUN(only_requests_and_writes_pass_the_allow_list);
    RUN(a_frame_out_of_form_is_refused);
    RUN(a_write_needs_a_complete_table_this_session);
    RUN(a_draft_from_another_read_is_refused);
    RUN(the_read_number_wraps);
    RUN(identity_indices_are_never_written);
    RUN(the_telemetry_type_is_never_written);
    RUN(raw_indices_are_not_editable);
    RUN(an_index_at_or_above_the_count_is_refused);
    RUN(a_value_outside_the_set_is_refused);
    RUN(bec_to_120_needs_flag_bit_3_as_read);
    RUN(a_flag_write_changes_bits_0_and_1_only);
    RUN(a_table_of_another_count_legacy_or_old_firmware_is_read_only);
    RUN(a_write_frame_needs_the_approved_value);
    RUN(a_value_read_outside_its_set_goes_back_as_read);
    RUN(the_table_crc_is_xmodem_over_little_endian_values);
    return test_summary("openyge_guard");
}
