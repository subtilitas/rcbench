/*
 * The data side of protocols/bleuart (bleuart_adv.c): the advertising data
 * parser, the scan list and the address text.
 *
 * Under test: the owner's report of the BWT901CL (0x18F0, HC-06); a 128-bit
 * UUID list; a length 0; a length past the end; 31 bytes exactly; a name of
 * 24 bytes and more; non-printable bytes; the complete name over the
 * shortened one; 9 matching devices (8 kept, strongest first); the hidden
 * count, once per address, past the remembered addresses too; the address
 * text both ways; the UUID constants against their text forms.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ble_capture.h"
#include "bleuart.h"

/* Parse a UUID's text form, 8-4-4-4-12 hex digits, into its 16 bytes least
 * significant first. */
static void uuid_bytes(const char *text, uint8_t out[16])
{
    unsigned n = 0;
    uint8_t be[16] = { 0 };
    for (const char *p = text; *p != '\0' && n < 32u; ++p) {
        int v = -1;
        if (*p >= '0' && *p <= '9') {
            v = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            v = *p - 'a' + 10;
        }
        if (v >= 0) {
            be[n / 2u] = (uint8_t)(be[n / 2u] << 4 | (unsigned)v);
            ++n;
        }
    }
    for (unsigned i = 0; i < 16u; ++i) {
        out[i] = be[15u - i];
    }
}

/* An address whose bytes are all @p v, public. */
static bleu_addr_t addr_of(uint8_t v)
{
    bleu_addr_t a = { { v, v, v, v, v, v }, BLEU_ADDR_PUBLIC };
    return a;
}

/* The 0x18F0 list alone. */
static const uint8_t k_18f0[] = { 0x03, 0x03, 0xF0, 0x18 };

TEST_CASE(the_uuids_are_the_isscs_text_forms_least_significant_byte_first)
{
    static const struct {
        const char *text;
        const uint8_t *bytes;
    } k[] = {
        { "49535343-fe7d-4ae5-8fa9-9fafd205e455", bleu_uuid_svc },
        { "49535343-1e4d-4bd9-ba61-23c647249616", bleu_uuid_tx },
        { "49535343-8841-43f4-a8d4-ecbe34729bb3", bleu_uuid_rx },
        { "49535343-aca3-481c-91ec-d85e28a60318", bleu_uuid_air },
        { "49535343-6daa-4d02-abf6-19569aca69fe", bleu_uuid_param },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        uint8_t want[16];
        uuid_bytes(k[i].text, want);
        CHECK(memcmp(k[i].bytes, want, 16) == 0);
    }
}

TEST_CASE(the_owners_report_is_listed_with_its_name)
{
    bleu_adv_t adv;
    CHECK(bleu_adv_parse(k_cap_adv, sizeof k_cap_adv, &adv));
    CHECK(adv.has_18f0);
    CHECK(!adv.has_issc);
    CHECK(!adv.cut);
    CHECK_EQ(adv.flags, 0x1A);
    CHECK_EQ(adv.name_kind, 0x09);
    CHECK_STR_EQ(adv.name, "HC-06");

    bleu_list_t l;
    bleu_list_clear(&l);
    bleu_list_report(&l, &k_cap_addr, -50, k_cap_adv, sizeof k_cap_adv);
    CHECK_EQ(l.found_n, 1);
    CHECK_EQ(l.hidden, 0);
    CHECK_EQ(l.reports, 1);
    CHECK(bleu_addr_eq(&l.found[0].addr, &k_cap_addr));
    CHECK_EQ(l.found[0].rssi, -50);
    CHECK_STR_EQ(l.found[0].name, "HC-06");
    CHECK_EQ(l.found[0].flags, BLEU_DEV_18F0 | BLEU_DEV_NAME_FULL);
}

TEST_CASE(a_128_bit_list_with_the_issc_service_matches)
{
    uint8_t ad[2 + 32];
    ad[0] = 33;
    ad[1] = 0x07;
    memset(&ad[2], 0xA5, 16);
    memcpy(&ad[18], bleu_uuid_svc, 16);
    bleu_adv_t adv;
    CHECK(bleu_adv_parse(ad, sizeof ad, &adv));
    CHECK(adv.has_issc);
    CHECK(!adv.has_18f0);

    /* The incomplete list's type matches as well. */
    ad[1] = 0x06;
    CHECK(bleu_adv_parse(ad, sizeof ad, &adv));

    /* Another UUID, or ours cut to 15 bytes, does not. */
    ad[33] ^= 0x01u;
    CHECK(!bleu_adv_parse(ad, sizeof ad, &adv));
    uint8_t cut[2 + 15];
    cut[0] = 16;
    cut[1] = 0x07;
    memcpy(&cut[2], bleu_uuid_svc, 15);
    CHECK(!bleu_adv_parse(cut, sizeof cut, &adv));

    /* The service UUID in a structure of another type is no list. */
    uint8_t other[2 + 16];
    other[0] = 17;
    other[1] = 0x21;
    memcpy(&other[2], bleu_uuid_svc, 16);
    CHECK(!bleu_adv_parse(other, sizeof other, &adv));

    /* A device listed by it carries the ISSC flag. */
    bleu_list_t l;
    bleu_list_clear(&l);
    const bleu_addr_t a = addr_of(1);
    ad[1] = 0x07;
    ad[33] ^= 0x01u;
    bleu_list_report(&l, &a, -60, ad, sizeof ad);
    CHECK_EQ(l.found_n, 1);
    CHECK_EQ(l.found[0].flags, BLEU_DEV_ISSC);
    CHECK_STR_EQ(l.found[0].name, "");
}

TEST_CASE(a_16_bit_list_is_read_in_pairs)
{
    bleu_adv_t adv;
    /* 0x18F0 second in a complete list, an odd byte after it. */
    const uint8_t two[] = { 0x06, 0x03, 0x0D, 0x18, 0xF0, 0x18, 0xAA };
    CHECK(bleu_adv_parse(two, sizeof two, &adv));
    /* The incomplete list's type. */
    const uint8_t inc[] = { 0x03, 0x02, 0xF0, 0x18 };
    CHECK(bleu_adv_parse(inc, sizeof inc, &adv));
    /* Half a UUID is none, and the bytes the other way round are 0xF018. */
    const uint8_t half[] = { 0x02, 0x03, 0xF0 };
    CHECK(!bleu_adv_parse(half, sizeof half, &adv));
    const uint8_t swapped[] = { 0x03, 0x03, 0x18, 0xF0 };
    CHECK(!bleu_adv_parse(swapped, sizeof swapped, &adv));
    /* 0x18F0 in a structure of another type is no list. */
    const uint8_t data16[] = { 0x03, 0x16, 0xF0, 0x18 };
    CHECK(!bleu_adv_parse(data16, sizeof data16, &adv));
}

TEST_CASE(a_length_0_ends_the_data)
{
    const uint8_t ad[] = { 0x02, 0x01, 0x06, 0x00, 0x03, 0x03, 0xF0, 0x18 };
    bleu_adv_t adv;
    CHECK(!bleu_adv_parse(ad, sizeof ad, &adv));
    CHECK_EQ(adv.flags, 0x06);
    CHECK(!adv.cut);
    /* Padding of zeros after the structures changes nothing. */
    uint8_t pad[31] = { 0 };
    memcpy(pad, k_cap_adv, sizeof k_cap_adv);
    CHECK(bleu_adv_parse(pad, sizeof pad, &adv));
    CHECK_STR_EQ(adv.name, "HC-06");
    CHECK(!adv.cut);
}

TEST_CASE(a_length_past_the_end_drops_the_rest)
{
    const uint8_t ad[] = { 0x03, 0x03, 0xF0, 0x18, 0x04, 0x09, 'A', 'B' };
    bleu_adv_t adv;
    CHECK(bleu_adv_parse(ad, sizeof ad, &adv));
    CHECK(adv.cut);
    CHECK_STR_EQ(adv.name, "");
    CHECK_EQ(adv.name_kind, 0);
    /* One byte short of the claimed length is cut as well; the exact
     * length is not. */
    const uint8_t one[] = { 0x03, 0x09, 'A', 'B' };
    CHECK(!bleu_adv_parse(one, 3, &adv));
    CHECK(adv.cut);
    CHECK(!bleu_adv_parse(one, sizeof one, &adv));
    CHECK(!adv.cut);
    CHECK_STR_EQ(adv.name, "AB");
    /* A length byte as the last byte has no type byte. */
    const uint8_t lone[] = { 0x03, 0x03, 0xF0, 0x18, 0x01 };
    CHECK(bleu_adv_parse(lone, sizeof lone, &adv));
    CHECK(adv.cut);
    /* A flags structure without its byte gives no flags. */
    const uint8_t noflag[] = { 0x01, 0x01 };
    CHECK(!bleu_adv_parse(noflag, sizeof noflag, &adv));
    CHECK_EQ(adv.flags, 0);
    CHECK(!adv.cut);
}

TEST_CASE(thirty_one_bytes_exactly_are_read_to_the_last)
{
    /* Flags 3, 0x18F0 4, a name of 22 bytes 24: 31 bytes. */
    uint8_t ad[31];
    memcpy(ad, (const uint8_t[]){ 0x02, 0x01, 0x06 }, 3);
    memcpy(&ad[3], k_18f0, 4);
    ad[7] = 23;
    ad[8] = 0x09;
    memcpy(&ad[9], "ABCDEFGHIJKLMNOPQRSTUV", 22);
    bleu_adv_t adv;
    CHECK(bleu_adv_parse(ad, sizeof ad, &adv));
    CHECK(!adv.cut);
    CHECK_STR_EQ(adv.name, "ABCDEFGHIJKLMNOPQRSTUV");
    /* One byte more claimed runs past the 31. */
    ad[7] = 24;
    CHECK(bleu_adv_parse(ad, sizeof ad, &adv));
    CHECK(adv.cut);
    CHECK_STR_EQ(adv.name, "");
}

TEST_CASE(a_name_of_24_bytes_or_more_is_cut_to_23)
{
    static const char name[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t n = 22; n <= 26; ++n) {
        uint8_t ad[2 + 26];
        ad[0] = (uint8_t)(n + 1u);
        ad[1] = 0x09;
        memcpy(&ad[2], name, n);
        bleu_adv_t adv;
        (void)bleu_adv_parse(ad, n + 2u, &adv);
        const size_t want = n < 23u ? n : 23u;
        CHECK_EQ(strlen(adv.name), want);
        CHECK(memcmp(adv.name, name, want) == 0);
    }
}

TEST_CASE(non_printable_bytes_show_as_question_marks)
{
    const uint8_t ad[] = { 0x08, 0x09, 0x1F, 0x7F, 0x80, 'A', ' ', '~', 0x01 };
    bleu_adv_t adv;
    (void)bleu_adv_parse(ad, sizeof ad, &adv);
    CHECK_STR_EQ(adv.name, "???A ~?");
    /* The shortened name's type is read the same way. */
    const uint8_t sh[] = { 0x03, 0x08, 0xFF, 'x' };
    (void)bleu_adv_parse(sh, sizeof sh, &adv);
    CHECK_STR_EQ(adv.name, "?x");
    CHECK_EQ(adv.name_kind, 0x08);
}

TEST_CASE(the_stations_report_lists_hc06_with_its_nul_padding_dropped)
{
    bleu_adv_t adv;
    CHECK(bleu_adv_parse(k_station_adv, sizeof k_station_adv, &adv));
    CHECK(adv.has_18f0);
    CHECK(!adv.has_issc);
    CHECK(!adv.cut);
    CHECK_EQ(adv.flags, 0x0A);
    CHECK_EQ(adv.name_kind, 0x09);
    CHECK_STR_EQ(adv.name, "HC-06");

    bleu_list_t l;
    bleu_list_clear(&l);
    bleu_list_report(&l, &k_cap_addr, STATION_RSSI, k_station_adv,
                     sizeof k_station_adv);
    CHECK_EQ(l.found_n, 1);
    CHECK_EQ(l.hidden, 0);
    CHECK_EQ(l.found[0].rssi, -82);
    CHECK_STR_EQ(l.found[0].name, "HC-06");
    CHECK_EQ(l.found[0].flags, BLEU_DEV_18F0 | BLEU_DEV_NAME_FULL);
}

TEST_CASE(a_name_ends_at_its_first_nul)
{
    const uint8_t mid[] = { 0x05, 0x09, 'A', 0x00, 'B', 'C' };
    bleu_adv_t adv;
    (void)bleu_adv_parse(mid, sizeof mid, &adv);
    CHECK_STR_EQ(adv.name, "A");
    CHECK_EQ(adv.name_kind, 0x09);
    /* A name of NULs only is empty and still the complete name. */
    const uint8_t nul[] = { 0x03, 0x09, 0x00, 0x00 };
    (void)bleu_adv_parse(nul, sizeof nul, &adv);
    CHECK_STR_EQ(adv.name, "");
    CHECK_EQ(adv.name_kind, 0x09);
    /* The NUL past the 23rd byte is not reached: the cut comes first. */
    uint8_t longer[2 + 25];
    longer[0] = 26;
    longer[1] = 0x09;
    memset(&longer[2], 'N', 24);
    longer[26] = 0x00;
    (void)bleu_adv_parse(longer, sizeof longer, &adv);
    CHECK_EQ(strlen(adv.name), 23);
    /* A complete name shorter than the shortened one before it ends where
     * it ends. */
    const uint8_t two[] = { 0x06, 0x08, 'L', 'O', 'N', 'G', '!', 0x04, 0x09,
                            'A', 'B', 0x00 };
    (void)bleu_adv_parse(two, sizeof two, &adv);
    CHECK_STR_EQ(adv.name, "AB");
}

TEST_CASE(the_complete_name_wins_over_the_shortened_one_in_either_order)
{
    const uint8_t first[] = { 0x03, 0x08, 'H', 'C', 0x06, 0x09, 'H', 'C', '-',
                              '0', '6' };
    const uint8_t last[] = { 0x06, 0x09, 'H', 'C', '-', '0', '6', 0x03, 0x08,
                             'H', 'C' };
    bleu_adv_t adv;
    (void)bleu_adv_parse(first, sizeof first, &adv);
    CHECK_STR_EQ(adv.name, "HC-06");
    CHECK_EQ(adv.name_kind, 0x09);
    (void)bleu_adv_parse(last, sizeof last, &adv);
    CHECK_STR_EQ(adv.name, "HC-06");
    CHECK_EQ(adv.name_kind, 0x09);

    /* In the list: a scan response's shortened name does not replace a
     * complete one; a complete one replaces a shortened one. */
    bleu_list_t l;
    bleu_list_clear(&l);
    const uint8_t adv_short[] = { 0x03, 0x03, 0xF0, 0x18, 0x03, 0x08, 'H',
                                  'C' };
    const uint8_t rsp_full[] = { 0x06, 0x09, 'H', 'C', '-', '0', '6' };
    const uint8_t rsp_short[] = { 0x02, 0x08, 'Z' };
    const bleu_addr_t a = addr_of(3);
    bleu_list_report(&l, &a, -60, adv_short, sizeof adv_short);
    CHECK_STR_EQ(l.found[0].name, "HC");
    CHECK_EQ(l.found[0].flags, BLEU_DEV_18F0);
    bleu_list_report(&l, &a, -61, rsp_full, sizeof rsp_full);
    CHECK_STR_EQ(l.found[0].name, "HC-06");
    CHECK_EQ(l.found[0].flags, BLEU_DEV_18F0 | BLEU_DEV_NAME_FULL);
    bleu_list_report(&l, &a, -62, rsp_short, sizeof rsp_short);
    CHECK_STR_EQ(l.found[0].name, "HC-06");
    /* A report without a name keeps it. */
    bleu_list_report(&l, &a, -63, k_18f0, sizeof k_18f0);
    CHECK_STR_EQ(l.found[0].name, "HC-06");
    CHECK_EQ(l.found[0].rssi, -63);
    CHECK_EQ(l.found_n, 1);
    CHECK_EQ(l.hidden, 0);
    CHECK_EQ(l.reports, 4);
}

TEST_CASE(nine_matching_devices_keep_the_eight_strongest_first)
{
    static const int8_t rssi[9] = { -90, -50, -70, -60, -80, -40, -85, -55,
                                    -65 };
    static const int8_t want[8] = { -40, -50, -55, -60, -65, -70, -80, -85 };
    /* In this order the ninth is stronger than the weakest row, which is
     * given up for it; reversed, the ninth is the weakest and not taken. */
    for (int rev = 0; rev < 2; ++rev) {
        bleu_list_t l;
        bleu_list_clear(&l);
        for (unsigned i = 0; i < 9u; ++i) {
            const unsigned k = rev ? 8u - i : i;
            const bleu_addr_t a = addr_of((uint8_t)(k + 1u));
            bleu_list_report(&l, &a, rssi[k], k_18f0, sizeof k_18f0);
        }
        CHECK_EQ(l.found_n, BLEU_FOUND_MAX);
        CHECK_EQ(l.hidden, 0);
        for (unsigned i = 0; i < 8u; ++i) {
            CHECK_EQ(l.found[i].rssi, want[i]);
        }
        /* The given-up device's next report, a scan response without the
         * UUID, neither lists it nor counts it hidden. */
        const bleu_addr_t weak = addr_of(1);
        const uint8_t rsp[] = { 0x02, 0x09, 'W' };
        bleu_list_report(&l, &weak, -30, rsp, sizeof rsp);
        bleu_list_report(&l, &weak, -30, k_18f0, sizeof k_18f0);
        CHECK_EQ(l.hidden, 0);
        CHECK_EQ(l.found[0].rssi, -40);
    }
}

TEST_CASE(an_equal_rssi_keeps_the_older_row_first_and_a_full_list_its_rows)
{
    bleu_list_t l;
    bleu_list_clear(&l);
    for (unsigned i = 0; i < BLEU_FOUND_MAX; ++i) {
        const bleu_addr_t a = addr_of((uint8_t)(i + 1u));
        bleu_list_report(&l, &a, -60, k_18f0, sizeof k_18f0);
    }
    /* A ninth at the same RSSI as the weakest is not taken. */
    const bleu_addr_t ninth = addr_of(9);
    bleu_list_report(&l, &ninth, -60, k_18f0, sizeof k_18f0);
    for (unsigned i = 0; i < BLEU_FOUND_MAX; ++i) {
        CHECK_EQ(l.found[i].addr.val[0], i + 1u);
    }
    /* A row that grows stronger moves up past the equal ones; one that
     * grows weaker moves down. */
    const bleu_addr_t five = addr_of(5);
    bleu_list_report(&l, &five, -59, k_18f0, sizeof k_18f0);
    CHECK_EQ(l.found[0].addr.val[0], 5);
    CHECK_EQ(l.found[1].addr.val[0], 1);
    bleu_list_report(&l, &five, -61, k_18f0, sizeof k_18f0);
    CHECK_EQ(l.found[7].addr.val[0], 5);
    CHECK_EQ(l.found[6].addr.val[0], 8);
}

TEST_CASE(an_unknown_rssi_sorts_last_and_keeps_a_known_one)
{
    bleu_list_t l;
    bleu_list_clear(&l);
    const bleu_addr_t a = addr_of(1), c = addr_of(2);
    bleu_list_report(&l, &a, 127, k_18f0, sizeof k_18f0);
    bleu_list_report(&l, &c, -100, k_18f0, sizeof k_18f0);
    CHECK_EQ(l.found[0].addr.val[0], 2);
    CHECK_EQ(l.found[1].rssi, 127);
    bleu_list_report(&l, &c, 127, k_18f0, sizeof k_18f0);
    CHECK_EQ(l.found[0].rssi, -100);
}

TEST_CASE(other_devices_are_counted_hidden_once)
{
    bleu_list_t l;
    bleu_list_clear(&l);
    const uint8_t plain[] = { 0x02, 0x01, 0x06 };
    const uint8_t name[] = { 0x04, 0x09, 'T', 'V', '!' };
    const bleu_addr_t o1 = addr_of(0x11), o2 = addr_of(0x12);
    /* An advertisement and its scan response: one device. */
    bleu_list_report(&l, &o1, -70, plain, sizeof plain);
    bleu_list_report(&l, &o1, -70, name, sizeof name);
    bleu_list_report(&l, &o2, -70, plain, sizeof plain);
    CHECK_EQ(l.hidden, 2);
    CHECK_EQ(l.found_n, 0);
    /* The same bytes from a random address are another device. */
    bleu_addr_t r = o1;
    r.type = BLEU_ADDR_RANDOM;
    bleu_list_report(&l, &r, -70, plain, sizeof plain);
    CHECK_EQ(l.hidden, 3);
    /* A hidden device that shows the UUID later is listed, not hidden. */
    bleu_list_report(&l, &o2, -65, k_18f0, sizeof k_18f0);
    CHECK_EQ(l.hidden, 2);
    CHECK_EQ(l.found_n, 1);
    CHECK(bleu_addr_eq(&l.found[0].addr, &o2));
    /* A listed device's scan response without the UUID adds its name. */
    bleu_list_report(&l, &o2, -66, name, sizeof name);
    CHECK_EQ(l.hidden, 2);
    CHECK_STR_EQ(l.found[0].name, "TV!");
    CHECK_EQ(l.reports, 6);
}

TEST_CASE(past_the_remembered_addresses_every_other_report_counts)
{
    bleu_list_t l;
    bleu_list_clear(&l);
    const uint8_t plain[] = { 0x02, 0x01, 0x06 };
    for (unsigned i = 0; i < BLEU_SEEN_MAX + 2u; ++i) {
        bleu_addr_t a = addr_of(0);
        a.val[0] = (uint8_t)i;
        a.val[1] = 0x40;
        bleu_list_report(&l, &a, -70, plain, sizeof plain);
    }
    CHECK_EQ(l.hidden, BLEU_SEEN_MAX + 2u);
    /* A remembered one counts once, one past the table every time. */
    bleu_addr_t kept = addr_of(0), lost = addr_of(0);
    kept.val[1] = 0x40;
    lost.val[0] = (uint8_t)(BLEU_SEEN_MAX + 1u);
    lost.val[1] = 0x40;
    bleu_list_report(&l, &kept, -70, plain, sizeof plain);
    CHECK_EQ(l.hidden, BLEU_SEEN_MAX + 2u);
    bleu_list_report(&l, &lost, -70, plain, sizeof plain);
    CHECK_EQ(l.hidden, BLEU_SEEN_MAX + 3u);
    /* A matching device still lists. */
    const bleu_addr_t m = addr_of(0x77);
    bleu_list_report(&l, &m, -50, k_18f0, sizeof k_18f0);
    CHECK_EQ(l.found_n, 1);
}

TEST_CASE(the_address_text_goes_both_ways)
{
    char out[BLEU_ADDR_TEXT_MAX];
    CHECK_EQ(bleu_addr_format(&k_cap_addr, false, out, sizeof out), 17);
    CHECK_STR_EQ(out, CAP_ADDR_TEXT);
    CHECK_EQ(bleu_addr_format(&k_cap_addr, true, out, sizeof out), 19);
    CHECK_STR_EQ(out, CAP_ADDR_TEXT " P");
    bleu_addr_t r = k_cap_addr;
    r.type = BLEU_ADDR_RANDOM;
    CHECK_EQ(bleu_addr_format(&r, true, out, sizeof out), 19);
    CHECK_STR_EQ(out, CAP_ADDR_TEXT " R");

    bleu_addr_t a;
    CHECK(bleu_addr_parse("00:0C:BF:08:19:29 P", &a));
    CHECK(bleu_addr_eq(&a, &k_cap_addr));
    CHECK(bleu_addr_parse("00:0c:bf:08:19:29 R", &a));
    CHECK(bleu_addr_eq(&a, &r));
    CHECK(bleu_addr_parse("FF:EE:DD:CC:BB:AA P", &a));
    CHECK_EQ(a.val[0], 0xAA);
    CHECK_EQ(a.val[5], 0xFF);

    /* The exact size fits, one byte less does not. */
    CHECK_EQ(bleu_addr_format(&k_cap_addr, true, out, 20), 19);
    CHECK_EQ(bleu_addr_format(&k_cap_addr, true, out, 19), 0);
    CHECK_EQ(bleu_addr_format(&k_cap_addr, false, out, 18), 17);
    CHECK_EQ(bleu_addr_format(&k_cap_addr, false, out, 17), 0);
    /* A type other than public and random has no letter. */
    bleu_addr_t id = k_cap_addr;
    id.type = 2;
    CHECK_EQ(bleu_addr_format(&id, true, out, sizeof out), 0);
    CHECK_EQ(bleu_addr_format(&id, false, out, sizeof out), 17);
    CHECK_EQ(bleu_addr_format(NULL, false, out, sizeof out), 0);
    CHECK_EQ(bleu_addr_format(&k_cap_addr, false, NULL, 20), 0);

    static const char *const bad[] = {
        "", "00:0C:BF:08:19:29", "00:0C:BF:08:19:29 ", "00:0C:BF:08:19:29 X",
        "00:0C:BF:08:19:29 PP", "00-0C-BF-08-19-29 P", "0:0C:BF:08:19:29 P",
        "00:0C:BF:08:19:2G P", "G0:0C:BF:08:19:29 P", "00:0C:BF:08:19:29P",
        "00:0C:BF:08:19:29 p", "00:0C:BF:08:1929 P", "00:0C:BF:08:19:@9 P",
        "00:0C:BF:08:19:/9 P", "00:0C:BF:08:19:g9 P", "00:0C:BF:08:19:`9 P",
        "00:0C:BF:08:19::9 P",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        bleu_addr_t keep = addr_of(0x5A);
        CHECK(!bleu_addr_parse(bad[i], &keep));
        CHECK_EQ(keep.val[0], 0x5A);
    }
    CHECK(!bleu_addr_parse(NULL, &a));
    CHECK(!bleu_addr_parse("00:0C:BF:08:19:29 P", NULL));
    CHECK(!bleu_addr_eq(&a, NULL));
    CHECK(!bleu_addr_eq(NULL, &a));
}

TEST_CASE(null_and_empty_input_change_nothing)
{
    bleu_adv_t adv;
    memset(&adv, 0x5A, sizeof adv);
    CHECK(!bleu_adv_parse(NULL, 0, &adv));
    CHECK_STR_EQ(adv.name, "");
    CHECK(!adv.cut);
    CHECK(!bleu_adv_parse(k_cap_adv, sizeof k_cap_adv, NULL));
    CHECK(!bleu_adv_parse(k_cap_adv, 0, &adv));
    bleu_list_clear(NULL);
    bleu_list_t l;
    bleu_list_clear(&l);
    bleu_list_report(NULL, &k_cap_addr, -50, k_cap_adv, sizeof k_cap_adv);
    bleu_list_report(&l, NULL, -50, k_cap_adv, sizeof k_cap_adv);
    CHECK_EQ(l.reports, 0);
    /* A report without data is another device. */
    bleu_list_report(&l, &k_cap_addr, -50, NULL, 0);
    CHECK_EQ(l.hidden, 1);
}

int main(void)
{
    RUN(the_uuids_are_the_isscs_text_forms_least_significant_byte_first);
    RUN(the_owners_report_is_listed_with_its_name);
    RUN(a_128_bit_list_with_the_issc_service_matches);
    RUN(a_16_bit_list_is_read_in_pairs);
    RUN(a_length_0_ends_the_data);
    RUN(a_length_past_the_end_drops_the_rest);
    RUN(thirty_one_bytes_exactly_are_read_to_the_last);
    RUN(a_name_of_24_bytes_or_more_is_cut_to_23);
    RUN(non_printable_bytes_show_as_question_marks);
    RUN(the_stations_report_lists_hc06_with_its_nul_padding_dropped);
    RUN(a_name_ends_at_its_first_nul);
    RUN(the_complete_name_wins_over_the_shortened_one_in_either_order);
    RUN(nine_matching_devices_keep_the_eight_strongest_first);
    RUN(an_equal_rssi_keeps_the_older_row_first_and_a_full_list_its_rows);
    RUN(an_unknown_rssi_sorts_last_and_keeps_a_known_one);
    RUN(other_devices_are_counted_hidden_once);
    RUN(past_the_remembered_addresses_every_other_report_counts);
    RUN(the_address_text_goes_both_ways);
    RUN(null_and_empty_input_change_nothing);
    return test_summary("bleuart_adv");
}
