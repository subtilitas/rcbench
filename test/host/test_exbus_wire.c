/*
 * EX Bus packets (protocols/exbus/exbus_wire.c): the CRC-16, the channel
 * packet, the telemetry request and the reply check.
 *
 * The byte strings are the examples of the EX Bus specification, version
 * 1.21 (S1 in the module README), and packets computed from its rules for
 * cases it has no example for.  Each is held byte for byte, CRC included.
 *
 * Under test:
 *  - the CRC-16: the check value of "123456789" and every packet below;
 *  - the channel packet: the specification's 16-channel example, 1, 8 and
 *    24 channels, both second header bytes, the Packet_ID, the clamp to
 *    400..2500 us and the counts it changed, the arguments it refuses;
 *  - the request: the specification's example and Packet_IDs 0 and 1;
 *  - the reply check: the specification's reply, an empty reply, bytes
 *    before and after it, every refusal code in its order, the JETIBOX
 *    reply the bench never asks for;
 *  - the air time at both rates.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "exbus_wire.h"

/* The bytes of the hex string @p hex ("3E 01 ...") into @p out; returns
 * how many the string has. */
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

/* @p n bytes of @p got against the hex string @p want. */
static bool same_bytes(const uint8_t *got, size_t n, const char *want)
{
    uint8_t w[128];
    const unsigned len = hex_to_bytes(want, w, sizeof(w));

    if (len != n) {
        printf("    length %u, want %u\n", (unsigned)n, len);
        return false;
    }
    for (unsigned i = 0; i < len; ++i) {
        if (got[i] != w[i]) {
            printf("    byte %u: %02X, want %02X\n", i, got[i], w[i]);
            return false;
        }
    }
    return true;
}

/* The specification's 16-channel example: every channel 0x1F82. */
#define SPEC_CHANNELS                                                        \
    "3E 03 28 06 31 20"                                                      \
    " 82 1F 82 1F 82 1F 82 1F 82 1F 82 1F 82 1F 82 1F"                       \
    " 82 1F 82 1F 82 1F 82 1F 82 1F 82 1F 82 1F 82 1F"                       \
    " 4F E2"

/* The specification's telemetry reply. */
#define SPEC_REPLY                                                           \
    "3B 01 20 08 3A 18 9F 56 00 A4 51 55 EE 11 30 20 21 00 40 34 A3 28 00"   \
    " 41 00 00 51 18 00 09 91 D6"

/* The specification's JETIBOX reply: 32 characters of text. */
#define SPEC_JETIBOX                                                         \
    "3B 01 28 88 3B 20"                                                      \
    " 43 65 6E 74 72 61 6C 20 42 6F 78 20 31 30 30 3E"                       \
    " 20 20 20 34 2E 38 56 20 20 31 30 34 30 6D 41 68"                       \
    " EB DE"

static void fill(uint16_t *ch, unsigned n, uint16_t v)
{
    for (unsigned i = 0; i < n; ++i) {
        ch[i] = v;
    }
}

TEST_CASE(the_crc_16_is_kermit)
{
    const uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    uint8_t p[128];
    uint16_t crc = 0;

    CHECK_EQ(exbus_crc16(check, sizeof(check)), 0x2189);
    for (unsigned i = 0; i < sizeof(check); ++i) {
        crc = exbus_crc16_update(crc, check[i]);
    }
    CHECK_EQ(crc, 0x2189);
    CHECK_EQ(exbus_crc16(NULL, 4), 0);
    CHECK_EQ(exbus_crc16(check, 0), 0);
    /* Over a whole packet with its CRC the KERMIT CRC is 0. */
    const unsigned n = hex_to_bytes(SPEC_CHANNELS, p, sizeof(p));

    CHECK_EQ(exbus_crc16(p, n - 2u), 0xE24F);
    CHECK_EQ(exbus_crc16(p, n), 0);
}

TEST_CASE(the_specification_channel_packet_is_built_byte_for_byte)
{
    uint16_t ch[16];
    uint8_t out[EXBUS_CHANNEL_PACKET_MAX];
    unsigned clamped = 99;

    fill(ch, 16, 0x1F82);
    CHECK_EQ(exbus_build_channels(ch, 16, 0x06, false, out, &clamped), 40u);
    CHECK(same_bytes(out, 40, SPEC_CHANNELS));
    CHECK_EQ(clamped, 0u);
}

TEST_CASE(eight_channels_from_1000_to_2000_us_with_release)
{
    const uint16_t us[8] = { 1000, 1250, 1500, 1750, 2000, 1500, 1500, 1500 };
    uint16_t ch[8];
    uint8_t out[EXBUS_CHANNEL_PACKET_MAX];

    for (unsigned i = 0; i < 8u; ++i) {
        ch[i] = exbus_us_to_counts(us[i]);
    }
    CHECK_EQ(exbus_build_channels(ch, 8, 1, true, out, NULL), 24u);
    CHECK(same_bytes(out, 24,
                     "3E 01 18 01 31 10 40 1F 10 27 E0 2E B0 36 80 3E"
                     " E0 2E E0 2E E0 2E 68 A3"));
}

TEST_CASE(eight_channels_without_release_and_packet_id_0)
{
    uint16_t ch[8];
    uint8_t out[EXBUS_CHANNEL_PACKET_MAX];

    fill(ch, 8, 12000);
    CHECK_EQ(exbus_build_channels(ch, 8, 0, false, out, NULL), 24u);
    CHECK(same_bytes(out, 24,
                     "3E 03 18 00 31 10 E0 2E E0 2E E0 2E E0 2E E0 2E"
                     " E0 2E E0 2E E0 2E 59 6D"));
}

TEST_CASE(twenty_four_channels_are_56_bytes)
{
    uint16_t ch[EXBUS_MAX_CHANNELS];
    uint8_t out[EXBUS_CHANNEL_PACKET_MAX];

    fill(ch, EXBUS_MAX_CHANNELS, 12000);
    CHECK_EQ(exbus_build_channels(ch, 24, 7, true, out, NULL), 56u);
    CHECK_EQ(EXBUS_CHANNEL_PACKET_MAX, 56u);
    CHECK(same_bytes(out, 56,
                     "3E 01 38 07 31 30"
                     " E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E"
                     " E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E"
                     " E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E"
                     " C6 64"));
}

TEST_CASE(one_channel_is_10_bytes)
{
    const uint16_t ch = 12000;
    uint8_t out[EXBUS_CHANNEL_PACKET_MAX];

    CHECK_EQ(exbus_build_channels(&ch, 1, 1, true, out, NULL), 10u);
    CHECK(same_bytes(out, 10, "3E 01 0A 01 31 02 E0 2E 0E A4"));
}

TEST_CASE(channel_values_are_clamped_to_400_to_2500_us)
{
    uint16_t ch[4] = { 0, 3199, 20001, 0xFFFF };
    uint8_t out[EXBUS_CHANNEL_PACKET_MAX];
    unsigned clamped = 0;

    CHECK_EQ(exbus_build_channels(ch, 4, 0, true, out, &clamped), 16u);
    CHECK_EQ(clamped, 4u);
    CHECK_EQ(out[6] | (out[7] << 8), EXBUS_CH_MIN);
    CHECK_EQ(out[8] | (out[9] << 8), EXBUS_CH_MIN);
    CHECK_EQ(out[10] | (out[11] << 8), EXBUS_CH_MAX);
    CHECK_EQ(out[12] | (out[13] << 8), EXBUS_CH_MAX);
    CHECK_EQ(exbus_crc16(out, 16), 0);
    /* The limits themselves pass unchanged. */
    ch[0] = 3200;
    ch[1] = 20000;
    CHECK_EQ(exbus_build_channels(ch, 2, 0, true, out, &clamped), 12u);
    CHECK_EQ(clamped, 0u);
    CHECK_EQ(out[6] | (out[7] << 8), 3200);
    CHECK_EQ(out[8] | (out[9] << 8), 20000);
    /* 400 us and 2500 us, and the conversion's saturation. */
    CHECK_EQ(EXBUS_CH_MIN, 3200u);
    CHECK_EQ(EXBUS_CH_MAX, 20000u);
    CHECK_EQ(exbus_us_to_counts(0), 0);
    CHECK_EQ(exbus_us_to_counts(1500), 12000);
    CHECK_EQ(exbus_us_to_counts(8191), 65528);
    CHECK_EQ(exbus_us_to_counts(8192), 0xFFFF);
    CHECK_EQ(exbus_us_to_counts(0xFFFFFFFFu), 0xFFFF);
    CHECK_EQ(exbus_clamp_counts(3199), 3200);
    CHECK_EQ(exbus_clamp_counts(12345), 12345);
    CHECK_EQ(exbus_clamp_counts(20001), 20000);
}

TEST_CASE(a_channel_packet_the_encoder_cannot_build_is_0_bytes)
{
    uint16_t ch[EXBUS_MAX_CHANNELS + 1u];
    uint8_t out[EXBUS_CHANNEL_PACKET_MAX + 2u];
    unsigned clamped = 7;

    fill(ch, EXBUS_MAX_CHANNELS + 1u, 12000);
    memset(out, 0xAA, sizeof(out));
    CHECK_EQ(exbus_build_channels(ch, 0, 0, true, out, &clamped), 0u);
    CHECK_EQ(clamped, 0u);
    CHECK_EQ(exbus_build_channels(ch, 25, 0, true, out, NULL), 0u);
    CHECK_EQ(exbus_build_channels(NULL, 4, 0, true, out, NULL), 0u);
    CHECK_EQ(exbus_build_channels(ch, 4, 0, true, NULL, NULL), 0u);
    /* Nothing was written. */
    CHECK_EQ(out[0], 0xAA);
}

TEST_CASE(the_specification_request_and_two_more_are_built)
{
    uint8_t out[EXBUS_REQUEST_LEN];

    CHECK_EQ(exbus_build_request(0x06, out), 8u);
    CHECK(same_bytes(out, 8, "3D 01 08 06 3A 00 98 81"));
    CHECK_EQ(exbus_build_request(0, out), 8u);
    CHECK(same_bytes(out, 8, "3D 01 08 00 3A 00 41 57"));
    CHECK_EQ(exbus_build_request(1, out), 8u);
    CHECK(same_bytes(out, 8, "3D 01 08 01 3A 00 9D 0D"));
    CHECK_EQ(exbus_build_request(1, NULL), 0u);
}

TEST_CASE(the_jetibox_requests_of_the_specification_carry_a_valid_crc)
{
    /* The bench sends none of them; the CRC is the one the packets use. */
    uint8_t p[16];
    unsigned n = hex_to_bytes("3D 01 09 88 3B 01 F0 A3 24", p, sizeof(p));

    CHECK_EQ(n, 9u);
    CHECK_EQ(exbus_crc16(p, n), 0);
    n = hex_to_bytes("3D 01 09 00 3B 01 70 1D 68", p, sizeof(p));
    CHECK_EQ(exbus_crc16(p, n), 0);
}

TEST_CASE(the_specification_reply_passes_and_gives_its_block)
{
    uint8_t p[64];
    const unsigned n = hex_to_bytes(SPEC_REPLY, p, sizeof(p));
    exbus_reply_t r;

    CHECK_EQ(n, 32u);
    CHECK_EQ(exbus_reply_parse(p, n, 0x08, &r), EXBUS_RX_OK);
    CHECK_EQ(r.packet_id, 0x08);
    CHECK_EQ(r.block_len, 24);
    CHECK_EQ(r.skipped, 0);
    CHECK_EQ(r.extra, 0);
    CHECK(same_bytes(r.block, r.block_len,
                     "9F 56 00 A4 51 55 EE 11 30 20 21 00 40 34 A3 28 00"
                     " 41 00 00 51 18 00 09"));
    /* No output is fine. */
    CHECK_EQ(exbus_reply_parse(p, n, 0x08, NULL), EXBUS_RX_OK);
}

TEST_CASE(an_empty_reply_passes_with_sub_len_0)
{
    uint8_t p[16];
    const unsigned n = hex_to_bytes("3B 01 08 06 3A 00 62 99", p, sizeof(p));
    exbus_reply_t r;

    CHECK_EQ(exbus_reply_parse(p, n, 6, &r), EXBUS_RX_OK);
    CHECK_EQ(r.block_len, 0);
}

TEST_CASE(bytes_before_and_after_the_reply_are_counted)
{
    uint8_t p[64];
    exbus_reply_t r;
    unsigned n;

    /* A glitch byte, a 0x3B not followed by 0x01, then the reply, then two
     * bytes of the next. */
    p[0] = 0xFF;
    p[1] = 0x3B;
    n = 2u + hex_to_bytes(SPEC_REPLY, p + 2, sizeof(p) - 2u);
    p[n++] = 0x3E;
    p[n++] = 0x01;
    CHECK_EQ(exbus_reply_parse(p, n, 0x08, &r), EXBUS_RX_OK);
    CHECK_EQ(r.skipped, 2);
    CHECK_EQ(r.extra, 2);
    CHECK_EQ(r.block_len, 24);
}

TEST_CASE(every_refusal_has_its_code)
{
    uint8_t good[64];
    uint8_t p[64];
    exbus_reply_t r;
    const unsigned n = hex_to_bytes(SPEC_REPLY, good, sizeof(good));

    /* No byte. */
    CHECK_EQ(exbus_reply_parse(good, 0, 8, &r), EXBUS_RX_NONE);
    CHECK_EQ(exbus_reply_parse(NULL, 5, 8, &r), EXBUS_RX_NONE);
    /* No header: channel bytes, a 0x3B followed by something else. */
    p[0] = 0x3E;
    p[1] = 0x01;
    p[2] = 0x3B;
    p[3] = 0x02;
    CHECK_EQ(exbus_reply_parse(p, 4, 8, &r), EXBUS_RX_HEADER);
    CHECK_EQ(r.skipped, 4);
    /* A lone 0x3B at the end, the header alone, header and LEN. */
    CHECK_EQ(exbus_reply_parse(good, 1, 8, &r), EXBUS_RX_TRUNCATED);
    CHECK_EQ(exbus_reply_parse(good, 2, 8, &r), EXBUS_RX_TRUNCATED);
    CHECK_EQ(exbus_reply_parse(good, 3, 8, &r), EXBUS_RX_TRUNCATED);
    /* Truncated anywhere before the last byte. */
    for (unsigned k = 3; k < n; ++k) {
        CHECK_EQ(exbus_reply_parse(good, k, 8, &r), EXBUS_RX_TRUNCATED);
    }
    /* LEN below 8. */
    memcpy(p, good, n);
    p[2] = 7;
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_LENGTH);
    /* LEN above 36: the JETIBOX reply's 40, or 255. */
    p[2] = 37;
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_TOO_LONG);
    p[2] = 0xFF;
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_TOO_LONG);
    /* LEN and SUB_LEN disagree. */
    memcpy(p, good, n);
    p[5] = 0x17;
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_LENGTH);
    /* One bit of the block, and each CRC byte. */
    memcpy(p, good, n);
    p[12] ^= 0x01;
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_CRC);
    memcpy(p, good, n);
    p[n - 2u] ^= 0x80;
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_CRC);
    memcpy(p, good, n);
    p[n - 1u] ^= 0x01;
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_CRC);
    CHECK_EQ(r.block_len, 0);
    /* A DATA_ID other than telemetry, with a CRC that fits. */
    memcpy(p, good, n);
    p[4] = EXBUS_ID_CHANNELS;
    {
        const uint16_t crc = exbus_crc16(p, n - 2u);

        p[n - 2u] = (uint8_t)crc;
        p[n - 1u] = (uint8_t)(crc >> 8);
    }
    CHECK_EQ(exbus_reply_parse(p, n, 8, &r), EXBUS_RX_DATA_ID);
    /* Another request's reply. */
    CHECK_EQ(exbus_reply_parse(good, n, 9, &r), EXBUS_RX_PACKET_ID);
    CHECK_EQ(exbus_reply_parse(good, n, 7, &r), EXBUS_RX_PACKET_ID);
}

TEST_CASE(the_jetibox_reply_is_longer_than_a_telemetry_reply)
{
    uint8_t p[64];
    const unsigned n = hex_to_bytes(SPEC_JETIBOX, p, sizeof(p));

    CHECK_EQ(n, 40u);
    CHECK_EQ(exbus_crc16(p, n), 0);
    CHECK_EQ(exbus_reply_parse(p, n, 0x88, NULL), EXBUS_RX_TOO_LONG);
}

TEST_CASE(air_time_is_10_bits_a_byte)
{
    CHECK_EQ(exbus_air_us(1, 125000), 80u);
    CHECK_EQ(exbus_air_us(1, 250000), 40u);
    CHECK_EQ(exbus_air_us(56, 125000), 4480u);
    CHECK_EQ(exbus_air_us(64, 250000), 2560u);
    CHECK_EQ(exbus_air_us(3, 115200), 261u);
    CHECK_EQ(exbus_air_us(3, 0), 0u);
    CHECK_EQ(EXBUS_TX_MAX, 64u);
    CHECK_EQ(EXBUS_REPLY_MAX, 36u);
}

int main(void)
{
    RUN(the_crc_16_is_kermit);
    RUN(the_specification_channel_packet_is_built_byte_for_byte);
    RUN(eight_channels_from_1000_to_2000_us_with_release);
    RUN(eight_channels_without_release_and_packet_id_0);
    RUN(twenty_four_channels_are_56_bytes);
    RUN(one_channel_is_10_bytes);
    RUN(channel_values_are_clamped_to_400_to_2500_us);
    RUN(a_channel_packet_the_encoder_cannot_build_is_0_bytes);
    RUN(the_specification_request_and_two_more_are_built);
    RUN(the_jetibox_requests_of_the_specification_carry_a_valid_crc);
    RUN(the_specification_reply_passes_and_gives_its_block);
    RUN(an_empty_reply_passes_with_sub_len_0);
    RUN(bytes_before_and_after_the_reply_are_counted);
    RUN(every_refusal_has_its_code);
    RUN(the_jetibox_reply_is_longer_than_a_telemetry_reply);
    RUN(air_time_is_10_bits_a_byte);
    return test_summary("exbus_wire");
}
