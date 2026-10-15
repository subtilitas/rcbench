/*
 * Protocol 2.0 packets of ROBOTIS Dynamixel servos
 * (protocols/dynamixel/dxl_packet.c): the CRC, the encoder with byte
 * stuffing, the parser with destuffing.
 *
 * The vectors are the packets the ROBOTIS e-Manual's Protocol 2.0 page
 * prints, each CRC recomputed by an independent program in the research
 * (dynamixel.md, section 9).  The stuffing cases have no vendor vector;
 * they are built here.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "dxl_packet.h"

/* Hex text to bytes; returns the count. */
static size_t hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;

    while (*s != '\0' && n < cap) {
        char *end;
        const unsigned long v = strtoul(s, &end, 16);

        if (end == s) {
            break;
        }
        out[n++] = (uint8_t)v;
        s = end;
    }
    return n;
}

typedef struct {
    const char *what;
    const char *bytes;
} vector_t;

/* The 20 distinct packets of the e-Manual (the Reboot status is the Write
 * status, byte for byte). */
static const vector_t k_vectors[] = {
    { "ping ID 1", "FF FF FD 00 01 03 00 01 19 4E" },
    { "ping reply XM430-W210", "FF FF FD 00 01 07 00 55 00 06 04 26 65 5D" },
    { "read ID 1 addr 132 len 4", "FF FF FD 00 01 07 00 02 84 00 04 00 1D 15" },
    { "read reply 166", "FF FF FD 00 01 08 00 55 00 A6 00 00 00 8C C0" },
    { "write goal 512", "FF FF FD 00 01 09 00 03 74 00 00 02 00 00 CA 89" },
    { "write status", "FF FF FD 00 01 04 00 55 00 A1 0C" },
    { "reg write 200", "FF FF FD 00 01 09 00 04 68 00 C8 00 00 00 AE 8E" },
    { "action", "FF FF FD 00 01 03 00 05 02 CE" },
    { "sync read", "FF FF FD 00 FE 09 00 82 84 00 04 00 01 02 CE FA" },
    { "sync read reply ID 2", "FF FF FD 00 02 08 00 55 00 1F 08 00 00 BA BE" },
    { "sync write", "FF FF FD 00 FE 11 00 83 74 00 04 00 01 96 00 00 00 02 AA 00 00 00 82 87" },
    { "fast sync read", "FF FF FD 00 FE 0A 00 8A 84 00 04 00 03 07 04 20 F2" },
    { "fast sync read reply segment", "FF FF FD 00 FE 19 00 55 00 03 A6 00 00 00 84 08" },
    { "bulk read", "FF FF FD 00 FE 0D 00 92 01 90 00 02 00 02 92 00 01 00 1A 05" },
    { "reboot", "FF FF FD 00 01 03 00 08 2F 4E" },
    { "factory reset option 1", "FF FF FD 00 01 04 00 06 01 A1 E6" },
    { "clear multi-turn", "FF FF FD 00 01 08 00 10 01 44 58 4C 22 B1 DC" },
    { "bulk write", "FF FF FD 00 FE 10 00 93 01 20 00 02 00 A0 00 02 1F 00 01 00 50 B7 68" },
    { "fast bulk read", "FF FF FD 00 FE 12 00 9A 03 84 00 04 00 07 7C 00 02 00 04 92 00 01 00 DA 2D" },
    { "backup", "FF FF FD 00 01 08 00 20 01 43 54 52 4C 16 F5" },
};
#define N_VECTORS (sizeof(k_vectors) / sizeof(k_vectors[0]))

static uint8_t buf[512];
static uint8_t want[512];

/* @p got bytes of buf equal the vector @p v. */
static void check_bytes(size_t got, const char *v)
{
    const size_t n = hex(v, want, sizeof(want));

    CHECK_EQ(got, n);
    if (got == n && memcmp(buf, want, n) != 0) {
        T_FAIL("bytes differ from %s", v);
    }
}

TEST_CASE(every_vector_s_crc_is_its_last_two_bytes)
{
    CHECK_EQ(N_VECTORS, 20u);
    for (size_t i = 0; i < N_VECTORS; ++i) {
        const size_t n = hex(k_vectors[i].bytes, buf, sizeof(buf));
        const uint16_t crc = dxl_crc(0, buf, n - 2u);

        if (crc != dxl_get_u16(&buf[n - 2u])) {
            T_FAIL("%s: crc %04X", k_vectors[i].what, crc);
        }
    }
}

TEST_CASE(the_crc_continues_across_calls_and_of_nothing_is_its_start)
{
    const size_t n = hex(k_vectors[1].bytes, buf, sizeof(buf));

    CHECK_EQ(dxl_crc(dxl_crc(0, buf, 5), &buf[5], n - 7u),
             dxl_crc(0, buf, n - 2u));
    CHECK_EQ(dxl_crc(0x1234u, buf, 0), 0x1234u);
    CHECK_EQ(dxl_crc(0x1234u, NULL, 3), 0x1234u);
    /* The worked check of the research: FF FF FD 00 01 03 00 01 -> 0x4E19. */
    const size_t m = hex("FF FF FD 00 01 03 00 01", buf, sizeof(buf));
    CHECK_EQ(dxl_crc(0, buf, m), 0x4E19u);
}

TEST_CASE(the_instructions_encode_to_the_vectors)
{
    const uint8_t goal512[4] = { 0x00, 0x02, 0x00, 0x00 };
    const uint8_t vel200[4] = { 0xC8, 0x00, 0x00, 0x00 };
    const uint8_t ids12[2] = { 1, 2 };
    const uint8_t sync_data[8] = { 0x96, 0, 0, 0, 0xAA, 0, 0, 0 };

    check_bytes(dxl_ping(1, buf, sizeof(buf)), k_vectors[0].bytes);
    check_bytes(dxl_read(1, 132, 4, buf, sizeof(buf)), k_vectors[2].bytes);
    check_bytes(dxl_write(1, 116, goal512, 4, buf, sizeof(buf)),
                k_vectors[4].bytes);
    check_bytes(dxl_reg_write(1, 104, vel200, 4, buf, sizeof(buf)),
                k_vectors[6].bytes);
    check_bytes(dxl_action(1, buf, sizeof(buf)), k_vectors[7].bytes);
    check_bytes(dxl_sync_read(132, 4, ids12, 2, buf, sizeof(buf)),
                k_vectors[8].bytes);
    check_bytes(dxl_sync_write(116, 4, ids12, sync_data, 2, buf, sizeof(buf)),
                k_vectors[10].bytes);
    check_bytes(dxl_reboot(1, buf, sizeof(buf)), k_vectors[14].bytes);
    check_bytes(dxl_factory_reset(1, DXL_RESET_ALL_BUT_ID,
                                  DXL_FACTORY_RESET_CONFIRM, buf,
                                  sizeof(buf)),
                k_vectors[15].bytes);

    /* The rest through the generic encoder, parameters as printed. */
    {
        const uint8_t p[7] = { 0x84, 0x00, 0x04, 0x00, 3, 7, 4 };
        check_bytes(dxl_encode(DXL_ID_BROADCAST, DXL_INS_FAST_SYNC_READ, p,
                               sizeof(p), buf, sizeof(buf)),
                    k_vectors[11].bytes);
    }
    {
        const uint8_t p[10] = { 0x01, 0x90, 0x00, 0x02, 0x00,
                                0x02, 0x92, 0x00, 0x01, 0x00 };
        check_bytes(dxl_encode(DXL_ID_BROADCAST, DXL_INS_BULK_READ, p,
                               sizeof(p), buf, sizeof(buf)),
                    k_vectors[13].bytes);
    }
    {
        const uint8_t p[5] = { 0x01, 0x44, 0x58, 0x4C, 0x22 };
        check_bytes(dxl_encode(1, DXL_INS_CLEAR, p, sizeof(p), buf,
                               sizeof(buf)),
                    k_vectors[16].bytes);
    }
    {
        const uint8_t p[13] = { 0x01, 0x20, 0x00, 0x02, 0x00, 0xA0, 0x00,
                                0x02, 0x1F, 0x00, 0x01, 0x00, 0x50 };
        check_bytes(dxl_encode(DXL_ID_BROADCAST, DXL_INS_BULK_WRITE, p,
                               sizeof(p), buf, sizeof(buf)),
                    k_vectors[17].bytes);
    }
    {
        const uint8_t p[15] = { 0x03, 0x84, 0x00, 0x04, 0x00, 0x07, 0x7C,
                                0x00, 0x02, 0x00, 0x04, 0x92, 0x00, 0x01,
                                0x00 };
        check_bytes(dxl_encode(DXL_ID_BROADCAST, DXL_INS_FAST_BULK_READ, p,
                               sizeof(p), buf, sizeof(buf)),
                    k_vectors[18].bytes);
    }
    {
        const uint8_t p[5] = { 0x01, 0x43, 0x54, 0x52, 0x4C };
        check_bytes(dxl_encode(1, DXL_INS_BACKUP, p, sizeof(p), buf,
                               sizeof(buf)),
                    k_vectors[19].bytes);
    }
}

TEST_CASE(the_status_packets_parse_and_encode_back)
{
    static const struct {
        unsigned vector;
        uint8_t id;
        uint16_t n;
        const char *params;
    } k[] = {
        { 1, 1, 3, "06 04 26" },
        { 3, 1, 4, "A6 00 00 00" },
        { 5, 1, 0, "" },
        { 9, 2, 4, "1F 08 00 00" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        dxl_packet_t p;
        size_t used = 0;
        uint8_t params[8];
        const size_t np = hex(k[i].params, params, sizeof(params));
        const size_t n = hex(k_vectors[k[i].vector].bytes, buf, sizeof(buf));

        CHECK_EQ(dxl_parse(buf, n, &p, &used), DXL_RX_OK);
        CHECK_EQ(used, n);
        CHECK_EQ(p.wire_len, n);
        CHECK_EQ(p.id, k[i].id);
        CHECK_EQ(p.instr, DXL_INS_STATUS);
        CHECK_EQ(p.error, 0);
        CHECK_EQ(p.n_params, k[i].n);
        CHECK_EQ(np, k[i].n);
        CHECK(memcmp(p.params, params, np) == 0);
        check_bytes(dxl_encode_status(k[i].id, 0, params, np, buf,
                                      sizeof(buf)),
                    k_vectors[k[i].vector].bytes);
    }
    /* The ping reply's fields: model 0x0406 = 1030, firmware 0x26. */
    {
        dxl_packet_t p;
        const size_t n = hex(k_vectors[1].bytes, buf, sizeof(buf));

        CHECK_EQ(dxl_parse(buf, n, &p, NULL), DXL_RX_OK);
        CHECK_EQ(dxl_get_u16(p.params), 1030);
        CHECK_EQ(p.params[2], 0x26);
    }
}

TEST_CASE(every_instruction_vector_parses_as_an_instruction)
{
    for (size_t i = 0; i < N_VECTORS; ++i) {
        dxl_packet_t p;
        const size_t n = hex(k_vectors[i].bytes, buf, sizeof(buf));
        const dxl_rx_t r = dxl_parse(buf, n, &p, NULL);

        if (i == 12u) {
            /* The fast sync read reply's first segment: Length 0x19 says
             * 25 bytes follow the header, 9 do. */
            CHECK_EQ(r, DXL_RX_MORE);
            continue;
        }
        if (r != DXL_RX_OK) {
            T_FAIL("%s: %d", k_vectors[i].what, (int)r);
            continue;
        }
        CHECK_EQ(p.instr, buf[7]);
        CHECK_EQ(p.id, buf[4]);
        /* No vector holds FF FF FD: the parameters are the bytes as sent. */
        const size_t head = p.instr == DXL_INS_STATUS ? 9u : 8u;

        CHECK_EQ(p.n_params, n - head - 2u);
        CHECK(memcmp(p.params, &buf[head], p.n_params) == 0);
    }
}

/* Encode @p params as a WRITE-shaped packet, check the stuffed bytes are
 * @p stuffed (the body after the instruction), parse back. */
static void stuff_round_trip(const char *params, const char *stuffed)
{
    uint8_t p[DXL_PARAM_MAX];
    uint8_t s[DXL_PARAM_MAX * 2];
    const size_t np = hex(params, p, sizeof(p));
    const size_t ns = hex(stuffed, s, sizeof(s));
    const size_t n = dxl_encode(1, DXL_INS_WRITE, p, np, buf, sizeof(buf));
    dxl_packet_t out;
    size_t used = 0;

    CHECK_EQ(n, DXL_HEADER_SIZE + 1u + ns + DXL_CRC_SIZE);
    CHECK_EQ(dxl_get_u16(&buf[5]), 1u + ns + DXL_CRC_SIZE);
    CHECK(memcmp(&buf[8], s, ns) == 0);
    CHECK_EQ(dxl_get_u16(&buf[n - 2u]), dxl_crc(0, buf, n - 2u));
    CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_OK);
    CHECK_EQ(used, n);
    CHECK_EQ(out.n_params, np);
    CHECK(memcmp(out.params, p, np) == 0);
    {
        uint8_t body[DXL_PARAM_MAX + 1];

        body[0] = DXL_INS_WRITE;
        memcpy(&body[1], p, np);
        CHECK_EQ(dxl_stuffed_size(body, np + 1u), 1u + ns);
    }
}

TEST_CASE(ff_ff_fd_in_the_parameters_gets_an_fd_after_it)
{
    /* In the middle. */
    stuff_round_trip("10 00 FF FF FD 22", "10 00 FF FF FD FD 22");
    /* At the end: the inserted FD is the last byte before the CRC. */
    stuff_round_trip("10 00 FF FF FD", "10 00 FF FF FD FD");
    /* At the start of the parameters. */
    stuff_round_trip("FF FF FD 00", "FF FF FD FD 00");
    /* Twice in a row, and an FD that is data after a stuffed triple. */
    stuff_round_trip("FF FF FD FF FF FD", "FF FF FD FD FF FF FD FD");
    stuff_round_trip("FF FF FD FD", "FF FF FD FD FD");
    /* Three FF: the triple starts at the second. */
    stuff_round_trip("FF FF FF FD", "FF FF FF FD FD");
    /* Near misses stay as they are. */
    stuff_round_trip("FF FD FF FF 00 FD", "FF FD FF FF 00 FD");
}

TEST_CASE(a_triple_across_the_err_byte_and_the_parameters_is_stuffed)
{
    const uint8_t p[2] = { 0xFF, 0xFD };
    dxl_packet_t out;
    const size_t n = dxl_encode_status(3, 0xFF, p, 2, buf, sizeof(buf));

    /* 55 FF | FF FD -> 55 FF FF FD FD */
    CHECK_EQ(n, DXL_HEADER_SIZE + 5u + DXL_CRC_SIZE);
    CHECK_EQ(buf[11], 0xFD);
    CHECK_EQ(dxl_parse(buf, n, &out, NULL), DXL_RX_OK);
    CHECK_EQ(out.error, 0xFF);
    CHECK_EQ(out.n_params, 2);
    CHECK_EQ(out.params[0], 0xFF);
    CHECK_EQ(out.params[1], 0xFD);
}

TEST_CASE(a_stuffed_packet_read_in_pieces_is_more_until_its_last_byte)
{
    const uint8_t p[6] = { 0x84, 0xFF, 0xFF, 0xFD, 0xFF, 0x01 };
    const size_t n = dxl_encode_status(1, 0, p, sizeof(p), buf, sizeof(buf));

    /* Every split, the one between FF FF FD and its FD included. */
    for (size_t cut = 0; cut < n; ++cut) {
        dxl_packet_t out;
        size_t used = 99;

        CHECK_EQ(dxl_parse(buf, cut, &out, &used), DXL_RX_MORE);
        CHECK_EQ(used, 0u);
    }
    {
        dxl_packet_t out;

        CHECK_EQ(dxl_parse(buf, n, &out, NULL), DXL_RX_OK);
        CHECK_EQ(out.n_params, 6);
        CHECK(memcmp(out.params, p, 6) == 0);
    }
}

TEST_CASE(random_parameters_survive_the_round_trip_at_every_length)
{
    uint32_t seed = 12345u;

    for (unsigned round = 0; round < 2000u; ++round) {
        uint8_t p[DXL_PARAM_MAX] = { 0 };
        const size_t np = round % (DXL_PARAM_MAX + 1u);
        dxl_packet_t out;
        size_t used = 0;

        for (size_t i = 0; i < np; ++i) {
            seed = seed * 1103515245u + 12345u;
            /* Mostly FF and FD, so triples are common. */
            const unsigned r = (seed >> 16) % 4u;
            p[i] = r == 0u ? 0xFFu : r == 1u ? 0xFDu : r == 2u ? 0xFFu
                                                               : (uint8_t)(seed >> 8);
        }
        const size_t n = dxl_encode_status(7, 0, p, np, buf, sizeof(buf));

        CHECK(n > 0u);
        CHECK(n <= DXL_WIRE_MAX);
        CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_OK);
        CHECK_EQ(used, n);
        CHECK_EQ(out.n_params, np);
        if (memcmp(out.params, p, np) != 0) {
            T_FAIL("round %u differs", round);
            return;
        }
    }
}

TEST_CASE(the_longest_packet_fits_dxl_wire_max_exactly)
{
    uint8_t p[DXL_PARAM_MAX];
    dxl_packet_t out;

    /* ERR FF, then FF FD FF FF FD ...: a triple ends at every third body
     * byte, 43 of them in 130. */
    for (size_t i = 0; i < sizeof(p); ++i) {
        p[i] = (i % 3u) == 1u ? 0xFDu : 0xFFu;
    }
    const size_t n = dxl_encode_status(1, 0xFF, p, sizeof(p), buf,
                                       sizeof(buf));

    CHECK_EQ(n, DXL_WIRE_MAX);
    CHECK_EQ(DXL_WIRE_MAX, 182u);
    CHECK_EQ(dxl_parse(buf, n, &out, NULL), DXL_RX_OK);
    CHECK_EQ(out.n_params, DXL_PARAM_MAX);
    CHECK(memcmp(out.params, p, sizeof(p)) == 0);
    /* One byte short of room: refused, nothing half written counts. */
    CHECK_EQ(dxl_encode_status(1, 0xFF, p, sizeof(p), buf, n - 1u), 0u);
    CHECK_EQ(dxl_encode_status(1, 0xFF, p, sizeof(p), buf, n), n);
}

TEST_CASE(garbage_before_the_header_is_skipped_and_counted_in_used)
{
    uint8_t in[64];
    const size_t n = hex(k_vectors[5].bytes, buf, sizeof(buf));
    dxl_packet_t out;
    size_t used = 0;

    /* FF FF FD FD is a stuffed sequence, not a header; FF FF FD 01 neither. */
    const size_t g = hex("00 FF FF FD FD 12 FF FF FD 01 FF", in, sizeof(in));

    memcpy(&in[g], buf, n);
    CHECK_EQ(dxl_parse(in, g + n, &out, &used), DXL_RX_OK);
    CHECK_EQ(used, g + n);
    CHECK_EQ(out.wire_len, n);
    CHECK_EQ(out.id, 1);
    /* Garbage alone: more, and used points at a cut header start. */
    CHECK_EQ(dxl_parse(in, g, &out, &used), DXL_RX_MORE);
    CHECK_EQ(used, g - 1u);
    CHECK_EQ(dxl_parse(in, 6, &out, &used), DXL_RX_MORE);
    CHECK_EQ(used, 6u);
}

TEST_CASE(a_wrong_crc_a_short_length_and_an_oversized_one_are_refused)
{
    dxl_packet_t out;
    size_t used = 0;
    size_t n = hex(k_vectors[3].bytes, buf, sizeof(buf));

    /* Every single bit of the packet after the header flipped. */
    for (size_t i = 7; i < n; ++i) {
        for (unsigned b = 0; b < 8u; ++b) {
            buf[i] ^= (uint8_t)(1u << b);
            CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_ERR_CRC);
            CHECK_EQ(used, n);
            buf[i] ^= (uint8_t)(1u << b);
        }
    }
    /* Length 2: no room for an instruction. */
    n = hex("FF FF FD 00 01 02 00 55 00", buf, sizeof(buf));
    CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_ERR_LENGTH);
    CHECK_EQ(used, 7u);
    /* A status packet with Length 3 has no ERR byte. */
    n = dxl_encode(1, DXL_INS_STATUS, NULL, 0, buf, sizeof(buf));
    CHECK_EQ(n, 0u);
    buf[0] = 0xFF;
    buf[1] = 0xFF;
    buf[2] = 0xFD;
    buf[3] = 0x00;
    buf[4] = 1;
    buf[5] = 3;
    buf[6] = 0;
    buf[7] = 0x55;
    dxl_put_u16(&buf[8], dxl_crc(0, buf, 8));
    CHECK_EQ(dxl_parse(buf, 10, &out, &used), DXL_RX_ERR_LENGTH);
    /* Length beyond the longest packet: refused at the header, before the
     * bytes come. */
    n = hex("FF FF FD 00 01 FF 00", buf, sizeof(buf));
    CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_ERR_OVERSIZED);
    CHECK_EQ(used, 7u);
    dxl_put_u16(&buf[5], (uint16_t)(DXL_WIRE_MAX - DXL_HEADER_SIZE));
    CHECK_EQ(dxl_parse(buf, 7, &out, &used), DXL_RX_MORE);
    dxl_put_u16(&buf[5], (uint16_t)(DXL_WIRE_MAX - DXL_HEADER_SIZE + 1u));
    CHECK_EQ(dxl_parse(buf, 7, &out, &used), DXL_RX_ERR_OVERSIZED);
}

TEST_CASE(more_parameters_than_dxl_param_max_after_destuffing_are_oversized)
{
    /* 131 body bytes without a triple: instruction, ERR, 129 parameters. */
    const size_t len = 2u + DXL_PARAM_MAX + 1u + DXL_CRC_SIZE;
    dxl_packet_t out;

    memset(buf, 0x11, sizeof(buf));
    buf[0] = 0xFF;
    buf[1] = 0xFF;
    buf[2] = 0xFD;
    buf[3] = 0x00;
    buf[4] = 1;
    dxl_put_u16(&buf[5], (uint16_t)len);
    buf[7] = 0x55;
    buf[8] = 0;
    dxl_put_u16(&buf[DXL_HEADER_SIZE + len - 2u],
                dxl_crc(0, buf, DXL_HEADER_SIZE + len - 2u));
    CHECK_EQ(dxl_parse(buf, DXL_HEADER_SIZE + len, &out, NULL),
             DXL_RX_ERR_OVERSIZED);
}

TEST_CASE(a_triple_without_its_fd_is_a_stuffing_error)
{
    dxl_packet_t out;
    size_t used = 0;
    /* 55 00 FF FF FD 01: the FD that must follow is missing. */
    size_t n = hex("FF FF FD 00 01 08 00 55 00 FF FF FD 01", buf,
                   sizeof(buf));

    dxl_put_u16(&buf[n], dxl_crc(0, buf, n));
    n += 2u;
    CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_ERR_STUFFING);
    CHECK_EQ(used, n);
    /* The triple as the last body byte. */
    n = hex("FF FF FD 00 01 07 00 55 00 FF FF FD", buf, sizeof(buf));
    dxl_put_u16(&buf[n], dxl_crc(0, buf, n));
    n += 2u;
    CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_ERR_STUFFING);
}

TEST_CASE(ids_253_and_255_are_refused_on_both_sides)
{
    dxl_packet_t out;
    size_t used = 0;

    CHECK_EQ(dxl_ping(253, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_ping(255, buf, sizeof(buf)), 0u);
    CHECK(dxl_ping(252, buf, sizeof(buf)) > 0u);
    CHECK(dxl_ping(0, buf, sizeof(buf)) > 0u);
    CHECK(dxl_ping(DXL_ID_BROADCAST, buf, sizeof(buf)) > 0u);
    CHECK_EQ(dxl_encode_status(DXL_ID_BROADCAST, 0, NULL, 0, buf,
                               sizeof(buf)),
             0u);
    const size_t n = dxl_ping(1, buf, sizeof(buf));

    buf[4] = 253;
    dxl_put_u16(&buf[n - 2u], dxl_crc(0, buf, n - 2u));
    CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_ERR_ID);
    CHECK_EQ(used, 7u);
    buf[4] = 255;
    CHECK_EQ(dxl_parse(buf, n, &out, &used), DXL_RX_ERR_ID);
}

TEST_CASE(the_encoders_refuse_what_they_cannot_send)
{
    uint8_t p[DXL_PARAM_MAX + 1];

    memset(p, 0, sizeof(p));
    /* Factory reset: never through the generic encoder, never without the
     * confirmation, never broadcast, only the 3 options. */
    CHECK_EQ(dxl_encode(1, DXL_INS_FACTORY_RESET, p, 1, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_factory_reset(1, DXL_RESET_ALL, 0, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_factory_reset(1, DXL_RESET_ALL, DXL_FACTORY_RESET_CONFIRM ^ 1u,
                               buf, sizeof(buf)),
             0u);
    CHECK_EQ(dxl_factory_reset(DXL_ID_BROADCAST, DXL_RESET_ALL,
                               DXL_FACTORY_RESET_CONFIRM, buf, sizeof(buf)),
             0u);
    CHECK_EQ(dxl_factory_reset(1, 0x03, DXL_FACTORY_RESET_CONFIRM, buf,
                               sizeof(buf)),
             0u);
    CHECK(dxl_factory_reset(1, DXL_RESET_ALL, DXL_FACTORY_RESET_CONFIRM, buf,
                            sizeof(buf))
          > 0u);
    CHECK(dxl_factory_reset(1, DXL_RESET_ALL_BUT_ID_BAUD,
                            DXL_FACTORY_RESET_CONFIRM, buf, sizeof(buf))
          > 0u);
    /* A status packet is not an instruction. */
    CHECK_EQ(dxl_encode(1, DXL_INS_STATUS, NULL, 0, buf, sizeof(buf)), 0u);
    /* Lengths. */
    CHECK_EQ(dxl_encode(1, DXL_INS_WRITE, p, DXL_PARAM_MAX + 1u, buf,
                        sizeof(buf)),
             0u);
    CHECK(dxl_encode(1, DXL_INS_WRITE, p, DXL_PARAM_MAX, buf, sizeof(buf))
          > 0u);
    CHECK_EQ(dxl_encode(1, DXL_INS_WRITE, NULL, 1, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_encode(1, DXL_INS_PING, NULL, 0, NULL, 10), 0u);
    CHECK_EQ(dxl_ping(1, buf, 9), 0u);
    CHECK_EQ(dxl_ping(1, buf, 10), 10u);
    CHECK_EQ(dxl_read(1, 0, 0, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_read(1, 0, DXL_READ_MAX + 1u, buf, sizeof(buf)), 0u);
    CHECK(dxl_read(1, 0, DXL_READ_MAX, buf, sizeof(buf)) > 0u);
    CHECK_EQ(dxl_read(DXL_ID_BROADCAST, 0, 4, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_write(1, 0, p, 0, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_write(1, 0, NULL, 1, buf, sizeof(buf)), 0u);
    CHECK_EQ(dxl_write(1, 0, p, DXL_WRITE_MAX + 1u, buf, sizeof(buf)), 0u);
    CHECK(dxl_write(1, 0, p, DXL_WRITE_MAX, buf, sizeof(buf)) > 0u);
    CHECK_EQ(dxl_reg_write(1, 0, p, DXL_WRITE_MAX + 1u, buf, sizeof(buf)), 0u);
    /* Sync: IDs 0 to 252 only, a length, data within one packet. */
    {
        const uint8_t bad[2] = { 1, DXL_ID_BROADCAST };
        const uint8_t ids[2] = { 1, 2 };

        CHECK_EQ(dxl_sync_read(132, 4, bad, 2, buf, sizeof(buf)), 0u);
        CHECK_EQ(dxl_sync_read(132, 0, ids, 2, buf, sizeof(buf)), 0u);
        CHECK_EQ(dxl_sync_read(132, 4, ids, 0, buf, sizeof(buf)), 0u);
        CHECK_EQ(dxl_sync_read(132, 4, NULL, 2, buf, sizeof(buf)), 0u);
        CHECK_EQ(dxl_sync_write(116, 4, bad, p, 2, buf, sizeof(buf)), 0u);
        CHECK_EQ(dxl_sync_write(116, 0, ids, p, 2, buf, sizeof(buf)), 0u);
        CHECK_EQ(dxl_sync_write(116, 4, ids, NULL, 2, buf, sizeof(buf)), 0u);
        CHECK_EQ(dxl_sync_write(116, 62, ids, p, 2, buf, sizeof(buf)), 0u);
        CHECK(dxl_sync_write(116, 61, ids, p, 2, buf, sizeof(buf)) > 0u);
    }
    {
        dxl_packet_t out;

        CHECK_EQ(dxl_parse(NULL, 3, &out, NULL), DXL_RX_ERR_ARG);
        CHECK_EQ(dxl_parse(buf, 3, NULL, NULL), DXL_RX_ERR_ARG);
        CHECK_EQ(dxl_stuffed_size(NULL, 3), 0u);
    }
}

TEST_CASE(the_err_byte_splits_into_alert_and_number)
{
    CHECK(dxl_err_alert(0x80));
    CHECK(!dxl_err_alert(0x7F));
    CHECK_EQ(dxl_err_number(0x80), DXL_ERRNO_NONE);
    CHECK_EQ(dxl_err_number(0x87), DXL_ERRNO_ACCESS);
    CHECK_EQ(dxl_err_number(0x03), DXL_ERRNO_CRC);
}

TEST_CASE(little_endian_fields_round_trip)
{
    uint8_t b[4];

    dxl_put_u32(b, 0x89ABCDEFu);
    CHECK_EQ(b[0], 0xEF);
    CHECK_EQ(b[3], 0x89);
    CHECK_EQ(dxl_get_u32(b), 0x89ABCDEFu);
    dxl_put_u16(b, 0xA55Au);
    CHECK_EQ(b[0], 0x5A);
    CHECK_EQ(dxl_get_u16(b), 0xA55Au);
}

int main(void)
{
    RUN(every_vector_s_crc_is_its_last_two_bytes);
    RUN(the_crc_continues_across_calls_and_of_nothing_is_its_start);
    RUN(the_instructions_encode_to_the_vectors);
    RUN(the_status_packets_parse_and_encode_back);
    RUN(every_instruction_vector_parses_as_an_instruction);
    RUN(ff_ff_fd_in_the_parameters_gets_an_fd_after_it);
    RUN(a_triple_across_the_err_byte_and_the_parameters_is_stuffed);
    RUN(a_stuffed_packet_read_in_pieces_is_more_until_its_last_byte);
    RUN(random_parameters_survive_the_round_trip_at_every_length);
    RUN(the_longest_packet_fits_dxl_wire_max_exactly);
    RUN(garbage_before_the_header_is_skipped_and_counted_in_used);
    RUN(a_wrong_crc_a_short_length_and_an_oversized_one_are_refused);
    RUN(more_parameters_than_dxl_param_max_after_destuffing_are_oversized);
    RUN(a_triple_without_its_fd_is_a_stuffing_error);
    RUN(ids_253_and_255_are_refused_on_both_sides);
    RUN(the_encoders_refuse_what_they_cannot_send);
    RUN(the_err_byte_splits_into_alert_and_number);
    RUN(little_endian_fields_round_trip);
    return test_summary("dxl_packet");
}
