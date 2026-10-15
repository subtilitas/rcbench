/*
 * EX telemetry as an EX Bus reply carries it (protocols/exbus/
 * exbus_telem.c): the block check, the three packet kinds, the sensor
 * table and the values as text.
 *
 * The byte strings are the examples of the EX Bus specification, version
 * 1.21, and of the EX telemetry specification, version 1.07 (S1 and S2 in
 * the module README).  Blocks for the data types the examples leave out
 * are built here from S2's rules; their CRC8 comes from exbus_crc8(),
 * which the examples and the check value hold.
 *
 * Under test:
 *  - the CRC8: the check value of "123456789" and each example's CRC8;
 *  - the examples: S1's data packet of 5 values and S2's data and text
 *    packets, every field;
 *  - each data type: 6, 14, 22 and 30 bits, signed and with every
 *    decimal-point position, the largest magnitudes, time, date, both
 *    coordinates and both hemisphere bits, the reserved types kept raw and
 *    stepped over, identifier 0 and the identifier byte after it;
 *  - messages; the 10 items the longest block holds;
 *  - each refusal: short, long, mark, length, CRC, kind 3, an item that
 *    runs into the CRC8, text and message lengths that disagree;
 *  - the table: labels and values in either order, another serial number,
 *    no room, messages;
 *  - the text: each form, a buffer too short, no buffer.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "exbus_telem.h"

static unsigned hex_to_bytes(const char *hex, uint8_t *out, unsigned room)
{
    unsigned n = 0;
    unsigned v = 0;
    unsigned digits = 0;

    for (const char *p = hex; *p != '\0'; ++p) {
        const char c = *p;
        unsigned d;

        if (c >= '0' && c <= '9') {
            d = (unsigned)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            d = (unsigned)(c - 'A' + 10);
        } else {
            continue;
        }
        v = (v << 4) | d;
        if (++digits == 2u) {
            if (n < room) {
                out[n] = (uint8_t)v;
            }
            ++n;
            v = 0;
            digits = 0;
        }
    }
    return n;
}

/* A block of @p kind from manufacturer 0xA401, device 0x0102, with the
 * items in the hex string @p items, the CRC8 computed. */
static unsigned block(uint8_t kind, const char *items, uint8_t *out)
{
    uint8_t it[64];
    const unsigned n_items = hex_to_bytes(items, it, sizeof(it));
    const unsigned n = EXBUS_TM_FRAME + n_items;

    out[0] = 0x9F;
    out[1] = (uint8_t)((kind << 6) | ((n - 2u) & 0x3Fu));
    out[2] = 0x01;
    out[3] = 0xA4;
    out[4] = 0x02;
    out[5] = 0x01;
    out[6] = 0x00;
    memcpy(out + 7, it, n_items);
    out[n - 1u] = exbus_crc8(out + 1, n - 2u);
    return n;
}

/* The one value of the data block with the item @p item. */
static exbus_tm_value_t one(const char *item)
{
    uint8_t b[64];
    exbus_tm_packet_t p;
    const unsigned n = block(EXBUS_TM_DATA, item, b);
    exbus_tm_value_t v;

    memset(&v, 0, sizeof(v));
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    CHECK_EQ(p.n_values, 1);
    if (p.n_values == 1u) {
        v = p.values[0];
    }
    return v;
}

static const char *text_of(const exbus_tm_value_t *v)
{
    static char buf[40];

    (void)exbus_tm_format(v, buf, sizeof(buf));
    return buf;
}

TEST_CASE(the_crc8_has_polynomial_7_and_initial_value_0)
{
    const uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    CHECK_EQ(exbus_crc8(check, sizeof(check)), 0xF4);
    CHECK_EQ(exbus_crc8(check, 0), 0);
    CHECK_EQ(exbus_crc8(NULL, 3), 0);
}

TEST_CASE(the_ex_bus_example_block_gives_five_values)
{
    uint8_t b[32];
    exbus_tm_packet_t p;
    const unsigned n = hex_to_bytes(
        "9F 56 00 A4 51 55 EE 11 30 20 21 00 40 34 A3 28 00 41 00 00 51 18"
        " 00 09", b, sizeof(b));

    CHECK_EQ(n, 24u);
    CHECK_EQ(exbus_crc8(b + 1, n - 2u), 0x09);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    CHECK_EQ(p.kind, EXBUS_TM_DATA);
    CHECK_EQ(p.manufacturer, 0xA400);
    CHECK_EQ(p.device, 0x5551);
    /* The example's reserved byte is not 0; it is kept, not refused. */
    CHECK_EQ(p.reserved, 0xEE);
    CHECK_EQ(p.n_values, 5);
    CHECK_EQ(p.values[0].id, 1);
    CHECK_EQ(p.values[0].type, 1);
    CHECK_EQ(p.values[0].size, 2);
    CHECK_EQ(p.values[0].value, 48);
    CHECK_EQ(p.values[0].decimals, 1);
    CHECK_STR_EQ(text_of(&p.values[0]), "4.8");
    CHECK_EQ(p.values[1].id, 2);
    CHECK_EQ(p.values[1].value, 0);
    CHECK_EQ(p.values[1].decimals, 2);
    CHECK_STR_EQ(text_of(&p.values[1]), "0.00");
    CHECK_EQ(p.values[2].id, 3);
    CHECK_EQ(p.values[2].type, 4);
    CHECK_EQ(p.values[2].size, 3);
    CHECK_EQ(p.values[2].value, 10403);
    CHECK_STR_EQ(text_of(&p.values[2]), "10403");
    CHECK_EQ(p.values[3].id, 4);
    CHECK_STR_EQ(text_of(&p.values[3]), "0");
    CHECK_EQ(p.values[4].id, 5);
    CHECK_STR_EQ(text_of(&p.values[4]), "24");
}

TEST_CASE(the_telemetry_example_data_block_gives_100_0_and_27)
{
    uint8_t b[32];
    exbus_tm_packet_t p;
    const unsigned n = hex_to_bytes("9F 4C A1 A8 5D 55 00 11 E8 23 21 1B 00 F4",
                                    b, sizeof(b));

    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    CHECK_EQ(p.kind, EXBUS_TM_DATA);
    CHECK_EQ(p.manufacturer, 0xA8A1);
    CHECK_EQ(p.device, 0x555D);
    CHECK_EQ(p.reserved, 0);
    CHECK_EQ(p.n_values, 2);
    CHECK_EQ(p.values[0].id, 1);
    CHECK_EQ(p.values[0].value, 1000);
    CHECK_STR_EQ(text_of(&p.values[0]), "100.0");
    CHECK_EQ(p.values[1].id, 2);
    CHECK_STR_EQ(text_of(&p.values[1]), "27");
}

TEST_CASE(the_telemetry_example_text_block_names_identifier_2)
{
    uint8_t b[32];
    exbus_tm_packet_t p;
    const unsigned n = hex_to_bytes(
        "9F 0F A1 A8 5D 55 00 02 2A 54 65 6D 70 2E B0 43 28", b, sizeof(b));

    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    CHECK_EQ(p.kind, EXBUS_TM_TEXT);
    CHECK_EQ(p.manufacturer, 0xA8A1);
    CHECK_EQ(p.text_id, 2);
    CHECK_EQ(p.label_len, 5);
    CHECK_EQ(p.unit_len, 2);
    CHECK_STR_EQ(p.label, "Temp.");
    /* The unit's bytes as sent: 0xB0 and 'C'. */
    CHECK_EQ((uint8_t)p.unit[0], 0xB0);
    CHECK_EQ(p.unit[1], 'C');
    CHECK_EQ(p.unit[2], '\0');
}

TEST_CASE(a_6_bit_value_is_one_byte_with_sign_and_point)
{
    exbus_tm_value_t v = one("10 BF");

    CHECK_EQ(v.type, 0);
    CHECK_EQ(v.size, 1);
    CHECK_EQ(v.form, EXBUS_TM_NUMBER);
    CHECK_EQ(v.negative, 1);
    CHECK_EQ(v.decimals, 1);
    CHECK_EQ(v.value, -31);
    CHECK_STR_EQ(text_of(&v), "-3.1");
    v = one("10 1F");
    CHECK_STR_EQ(text_of(&v), "31");
    v = one("10 61");
    CHECK_STR_EQ(text_of(&v), "0.001");
}

TEST_CASE(a_14_bit_value_reaches_8191)
{
    exbus_tm_value_t v = one("F1 FF 1F");

    CHECK_EQ(v.id, 15);
    CHECK_EQ(v.value, 8191);
    CHECK_STR_EQ(text_of(&v), "8191");
    v = one("11 FF FF");
    CHECK_EQ(v.value, -8191);
    CHECK_EQ(v.decimals, 3);
    CHECK_STR_EQ(text_of(&v), "-8.191");
    v = one("11 05 C0");
    CHECK_STR_EQ(text_of(&v), "-0.05");
}

TEST_CASE(a_22_bit_value_reaches_2097151)
{
    exbus_tm_value_t v = one("24 FF FF 1F");

    CHECK_EQ(v.size, 3);
    CHECK_EQ(v.value, 2097151);
    v = one("24 39 30 E0");
    CHECK_EQ(v.value, -12345);
    CHECK_EQ(v.decimals, 3);
    CHECK_STR_EQ(text_of(&v), "-12.345");
}

TEST_CASE(a_30_bit_value_reaches_536870911)
{
    exbus_tm_value_t v = one("38 FF FF FF 1F");

    CHECK_EQ(v.size, 4);
    CHECK_EQ(v.value, 536870911);
    CHECK_STR_EQ(text_of(&v), "536870911");
    v = one("38 FF FF FF BF");
    CHECK_EQ(v.value, -536870911);
    CHECK_STR_EQ(text_of(&v), "-53687091.1");
}

TEST_CASE(type_5_is_a_time_or_a_date_by_the_lowest_point_bit)
{
    /* 12:05:09: seconds, minutes, hours. */
    exbus_tm_value_t v = one("45 09 05 0C");

    CHECK_EQ(v.form, EXBUS_TM_TIME);
    CHECK_EQ(v.part[0], 12);
    CHECK_EQ(v.part[1], 5);
    CHECK_EQ(v.part[2], 9);
    CHECK_STR_EQ(text_of(&v), "12:05:09");
    /* 2026-10-15: day, month, year less 2000 with point bit 5 set. */
    v = one("45 0F 0A 3A");
    CHECK_EQ(v.form, EXBUS_TM_DATE);
    CHECK_EQ(v.part[0], 26);
    CHECK_EQ(v.part[1], 10);
    CHECK_EQ(v.part[2], 15);
    CHECK_STR_EQ(text_of(&v), "2026-10-15");
}

TEST_CASE(type_9_is_a_coordinate_with_its_hemisphere)
{
    /* Latitude north, south; longitude east, west. */
    exbus_tm_value_t v = one("59 D2 04 00 00");

    CHECK_EQ(v.form, EXBUS_TM_LATITUDE);
    CHECK_EQ(v.negative, 0);
    CHECK_EQ(v.value, 1234);
    CHECK_STR_EQ(text_of(&v), "N raw 1234");
    v = one("59 D2 04 00 40");
    CHECK_EQ(v.form, EXBUS_TM_LATITUDE);
    CHECK_STR_EQ(text_of(&v), "S raw 1234");
    v = one("59 D2 04 00 20");
    CHECK_EQ(v.form, EXBUS_TM_LONGITUDE);
    CHECK_STR_EQ(text_of(&v), "E raw 1234");
    v = one("59 FF FF FF 7F");
    CHECK_EQ(v.form, EXBUS_TM_LONGITUDE);
    CHECK_EQ(v.value, 0x1FFFFFFF);
    CHECK_STR_EQ(text_of(&v), "W raw 536870911");
    /* Bit 7 is part of no field the specification names for type 9. */
    CHECK_EQ(v.raw[3], 0x7F);
}

TEST_CASE(reserved_types_are_kept_raw_and_stepped_over)
{
    uint8_t b[64];
    exbus_tm_packet_t p;
    /* Types 2 (2 bytes), 6 (3), 10 (4), 12 (5), then a 6-bit value. */
    const unsigned n = block(EXBUS_TM_DATA,
                             "12 AA BB 26 01 02 03 3A 04 05 06 07"
                             " 4C 08 09 0A 0B 0C 50 05", b);
    static const uint8_t size[5] = { 2, 3, 4, 5, 1 };

    CHECK_EQ(n, 28u);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    CHECK_EQ(p.n_values, 5);
    for (unsigned i = 0; i < 4u; ++i) {
        CHECK_EQ(p.values[i].form, EXBUS_TM_RAW);
        CHECK_EQ(p.values[i].size, size[i]);
        CHECK_EQ(p.values[i].id, i + 1u);
    }
    CHECK_EQ(p.values[3].raw[4], 0x0C);
    CHECK_STR_EQ(text_of(&p.values[0]), "raw AA BB");
    CHECK_STR_EQ(text_of(&p.values[3]), "raw 08 09 0A 0B 0C");
    CHECK_EQ(p.values[4].id, 5);
    CHECK_EQ(p.values[4].size, size[4]);
    CHECK_STR_EQ(text_of(&p.values[4]), "5");
    /* Every type, its size. */
    for (unsigned t = 0; t < 16u; ++t) {
        char item[32];
        const unsigned want = t == 0u ? 1u : (t >> 2) + 2u;

        snprintf(item, sizeof(item), "1%X 00 00 00 00 00", t);
        item[2u + 3u * want] = '\0';
        const exbus_tm_value_t v = one(item);

        CHECK_EQ(v.size, want);
    }
}

TEST_CASE(identifier_0_takes_the_identifier_from_the_next_byte)
{
    exbus_tm_value_t v = one("01 14 2C 01");

    CHECK_EQ(v.id, 20);
    CHECK_EQ(v.type, 1);
    CHECK_EQ(v.value, 300);
    v = one("00 FF 05");
    CHECK_EQ(v.id, 255);
    CHECK_EQ(v.value, 5);
}

TEST_CASE(the_longest_block_holds_10_values)
{
    uint8_t b[64];
    exbus_tm_packet_t p;
    const unsigned n = block(EXBUS_TM_DATA,
                             "10 01 20 02 30 03 40 04 50 05"
                             " 60 06 70 07 80 08 90 09 A0 0A", b);

    CHECK_EQ(n, EXBUS_TM_BLOCK_MAX);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    CHECK_EQ(p.n_values, EXBUS_TM_MAX_VALUES);
    CHECK_EQ(p.values[9].id, 10);
    CHECK_EQ(p.values[9].value, 10);
}

TEST_CASE(a_message_has_type_class_and_text)
{
    uint8_t b[64];
    exbus_tm_packet_t p;
    /* Type 7, class 2 (warning), 5 bytes "Low V". */
    const unsigned n = block(EXBUS_TM_MESSAGE, "07 45 4C 6F 77 20 56", b);

    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    CHECK_EQ(p.kind, EXBUS_TM_MESSAGE);
    CHECK_EQ(p.msg_type, 7);
    CHECK_EQ(p.msg_class, 2);
    CHECK_EQ(p.msg_len, 5);
    CHECK_STR_EQ(p.message, "Low V");
}

TEST_CASE(every_refusal_has_its_code)
{
    uint8_t good[64];
    uint8_t b[64];
    exbus_tm_packet_t p;
    const unsigned n = block(EXBUS_TM_DATA, "11 30 20", good);

    CHECK_EQ(exbus_tm_parse(good, n, &p), EXBUS_TM_OK);
    CHECK_EQ(exbus_tm_parse(NULL, n, &p), EXBUS_TM_ERR_ARG);
    CHECK_EQ(exbus_tm_parse(good, n, NULL), EXBUS_TM_ERR_ARG);
    CHECK_EQ(exbus_tm_parse(good, 7, &p), EXBUS_TM_ERR_SHORT);
    CHECK_EQ(exbus_tm_parse(good, 29, &p), EXBUS_TM_ERR_TOO_LONG);
    /* The mark's high nibble is free, its low nibble is not. */
    memcpy(b, good, n);
    b[0] = 0x0F;
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    b[0] = 0x9E;
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_ERR_MARK);
    /* L one off either way. */
    CHECK_EQ(exbus_tm_parse(good, n - 1u, &p), EXBUS_TM_ERR_LENGTH);
    memcpy(b, good, n);
    b[n] = 0;
    CHECK_EQ(exbus_tm_parse(b, n + 1u, &p), EXBUS_TM_ERR_LENGTH);
    /* One bit anywhere the CRC8 covers, and the CRC8 itself. */
    for (unsigned i = 2; i < n; ++i) {
        memcpy(b, good, n);
        b[i] ^= 0x10;
        CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_ERR_CRC);
    }
    CHECK_EQ(p.n_values, 0);
    /* Kind 3. */
    {
        const unsigned k = block(3, "11 30 20", b);

        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_KIND);
    }
    /* An item that runs into the CRC8: a 14-bit value with one byte, an
     * identifier 0 without its byte. */
    {
        unsigned k = block(EXBUS_TM_DATA, "11 30", b);

        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_ITEM);
        k = block(EXBUS_TM_DATA, "11 30 20 01", b);
        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_ITEM);
        CHECK_EQ(p.n_values, 0);
    }
    /* A text whose lengths say 6 bytes, with 5 and with 7. */
    {
        unsigned k = block(EXBUS_TM_TEXT, "02 21 41 42 43 44", b);

        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_ITEM);
        k = block(EXBUS_TM_TEXT, "02 21 41 42 43 44 45 46", b);
        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_ITEM);
        k = block(EXBUS_TM_TEXT, "02 21 41 42 43 44 45", b);
        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_OK);
        CHECK_STR_EQ(p.label, "ABCD");
        CHECK_STR_EQ(p.unit, "E");
        k = block(EXBUS_TM_TEXT, "02", b);
        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_ITEM);
    }
    /* A message whose length disagrees, and one without its length. */
    {
        unsigned k = block(EXBUS_TM_MESSAGE, "07 45 4C 6F 77 20", b);

        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_ITEM);
        k = block(EXBUS_TM_MESSAGE, "07", b);
        CHECK_EQ(exbus_tm_parse(b, k, &p), EXBUS_TM_ERR_ITEM);
    }
}

TEST_CASE(the_table_joins_labels_and_values_in_either_order)
{
    exbus_tm_table_t t;
    exbus_tm_packet_t p;
    uint8_t b[64];
    unsigned n;
    const exbus_tm_sensor_t *s;

    exbus_tm_table_init(&t);
    /* A value before its label. */
    n = block(EXBUS_TM_DATA, "11 30 20 21 1B 00", b);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 1000);
    s = exbus_tm_table_find(&t, 1);
    CHECK(s != NULL);
    if (s != NULL) {
        CHECK_EQ(s->has_value, 1);
        CHECK_EQ(s->has_label, 0);
        CHECK_EQ(s->value_us, 1000u);
        CHECK_EQ(s->updates, 1u);
    }
    /* The label, then the device name (identifier 0). */
    n = block(EXBUS_TM_TEXT, "01 2A 56 6F 6C 74 2E 20 56", b);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 2000);
    n = block(EXBUS_TM_TEXT, "00 20 4D 42 61 72", b);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 3000);
    s = exbus_tm_table_find(&t, 1);
    CHECK(s != NULL);
    if (s != NULL) {
        CHECK_EQ(s->has_label, 1);
        CHECK_STR_EQ(s->label, "Volt.");
        CHECK_STR_EQ(s->unit, " V");
        CHECK_STR_EQ(text_of(&s->value), "4.8");
    }
    s = exbus_tm_table_find(&t, 0);
    CHECK(s != NULL && s->has_label == 1u && !strcmp(s->label, "MBar"));
    CHECK(exbus_tm_table_find(&t, 3) == NULL);
    CHECK_EQ(t.n, 3);
    CHECK_EQ(t.packets, 3u);
    CHECK_EQ(t.manufacturer, 0xA401);
    CHECK_EQ(t.device, 0x0102);
    /* A label before its value. */
    n = block(EXBUS_TM_TEXT, "05 18 54 65 6D", b);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 4000);
    s = exbus_tm_table_find(&t, 5);
    CHECK(s != NULL && s->has_label == 1u && s->has_value == 0u);
    /* A second value of identifier 1. */
    n = block(EXBUS_TM_DATA, "11 31 20", b);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 5000);
    s = exbus_tm_table_find(&t, 1);
    CHECK(s != NULL && s->updates == 2u && s->value_us == 5000u);
    /* A message. */
    n = block(EXBUS_TM_MESSAGE, "01 82 48 69", b);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 6000);
    CHECK_EQ(t.has_message, 1);
    CHECK_EQ(t.msg_class, 4);
    CHECK_STR_EQ(t.message, "Hi");
    CHECK_EQ(t.message_us, 6000u);
    /* Safe without arguments. */
    exbus_tm_table_update(NULL, &p, 0);
    exbus_tm_table_update(&t, NULL, 0);
    exbus_tm_table_init(NULL);
    CHECK(exbus_tm_table_find(NULL, 1) == NULL);
}

TEST_CASE(another_serial_number_clears_the_table)
{
    exbus_tm_table_t t;
    exbus_tm_packet_t p;
    uint8_t b[64];
    unsigned n = block(EXBUS_TM_DATA, "11 30 20", b);

    exbus_tm_table_init(&t);
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 1);
    CHECK_EQ(t.n, 1);
    n = hex_to_bytes("9F 4C A1 A8 5D 55 00 11 E8 23 21 1B 00 F4", b,
                     sizeof(b));
    CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
    exbus_tm_table_update(&t, &p, 2);
    CHECK_EQ(t.device_changes, 1u);
    CHECK_EQ(t.manufacturer, 0xA8A1);
    CHECK_EQ(t.n, 2);
    CHECK_EQ(t.packets, 1u);
    {
        const exbus_tm_sensor_t *s = exbus_tm_table_find(&t, 1);

        CHECK(s != NULL && s->value.value == 1000);
    }
}

TEST_CASE(a_full_table_drops_new_identifiers_and_counts_them)
{
    exbus_tm_table_t t;
    exbus_tm_packet_t p;
    uint8_t b[64];

    exbus_tm_table_init(&t);
    /* 40 identifiers, 5 a block, through identifier 0. */
    for (unsigned base = 0; base < 40u; base += 5u) {
        char items[64];

        snprintf(items, sizeof(items),
                 "00 %02X 01 00 %02X 01 00 %02X 01 00 %02X 01 00 %02X 01",
                 base + 1u, base + 2u, base + 3u, base + 4u, base + 5u);
        const unsigned n = block(EXBUS_TM_DATA, items, b);

        CHECK_EQ(exbus_tm_parse(b, n, &p), EXBUS_TM_OK);
        exbus_tm_table_update(&t, &p, base);
    }
    CHECK_EQ(t.n, EXBUS_TM_MAX_SENSORS);
    CHECK_EQ(t.full, 8u);
    CHECK(exbus_tm_table_find(&t, 32) != NULL);
    CHECK(exbus_tm_table_find(&t, 33) == NULL);
}

TEST_CASE(the_text_is_cut_to_the_buffer_and_its_length_returned)
{
    exbus_tm_value_t v = one("11 39 E0");
    char buf[4];

    CHECK_EQ(exbus_tm_format(&v, buf, sizeof(buf)), 6u);
    CHECK_STR_EQ(buf, "-0.");
    CHECK_EQ(exbus_tm_format(&v, buf, 1), 6u);
    CHECK_STR_EQ(buf, "");
    CHECK_EQ(exbus_tm_format(&v, NULL, 10), 6u);
    CHECK_EQ(exbus_tm_format(NULL, buf, sizeof(buf)), 0u);
    CHECK_STR_EQ(buf, "");
    CHECK_STR_EQ(text_of(&v), "-0.057");
}

int main(void)
{
    RUN(the_crc8_has_polynomial_7_and_initial_value_0);
    RUN(the_ex_bus_example_block_gives_five_values);
    RUN(the_telemetry_example_data_block_gives_100_0_and_27);
    RUN(the_telemetry_example_text_block_names_identifier_2);
    RUN(a_6_bit_value_is_one_byte_with_sign_and_point);
    RUN(a_14_bit_value_reaches_8191);
    RUN(a_22_bit_value_reaches_2097151);
    RUN(a_30_bit_value_reaches_536870911);
    RUN(type_5_is_a_time_or_a_date_by_the_lowest_point_bit);
    RUN(type_9_is_a_coordinate_with_its_hemisphere);
    RUN(reserved_types_are_kept_raw_and_stepped_over);
    RUN(identifier_0_takes_the_identifier_from_the_next_byte);
    RUN(the_longest_block_holds_10_values);
    RUN(a_message_has_type_class_and_text);
    RUN(every_refusal_has_its_code);
    RUN(the_table_joins_labels_and_values_in_either_order);
    RUN(another_serial_number_clears_the_table);
    RUN(a_full_table_drops_new_identifiers_and_counts_them);
    RUN(the_text_is_cut_to_the_buffer_and_its_length_returned);
    return test_summary("exbus_telem");
}
