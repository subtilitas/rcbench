/**
 * @file test_prn_tmpl.c
 * @brief The engine's templates: every checksum kind against its reference
 *        value over "123456789"; ranges at both ends of a frame, negative
 *        offsets, a range one byte past the frame; lengths; values at the
 *        top of each encoding and one past it; the frame's room; the pad.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "prn_job.h"

static prn_spec_t s;
static prn_ctx_t ctx;
static uint8_t out[PRN_FRAME_MAX];

/**
 * @brief Make the spec's token pool one run.
 * @param toks the tokens
 * @param n    their count
 * @return the run
 */
static prn_tseq_t run_of(const prn_tok_t *toks, unsigned n)
{
    memset(&s, 0, sizeof s);
    memcpy(s.tok, toks, n * sizeof toks[0]);
    s.ntok = (uint16_t)n;
    const prn_tseq_t q = { 0u, (uint8_t)n };
    return q;
}

TEST_CASE(each_checksum_kind_gives_its_reference_value)
{
    const uint8_t *p = (const uint8_t *)"123456789";
    CHECK_EQ(prn_ck(PRN_CK_SUM8, p, 9u), 0xDDu);    /* 477 mod 256 */
    CHECK_EQ(prn_ck(PRN_CK_SUM16LE, p, 9u), 0x1DDu);
    CHECK_EQ(prn_ck(PRN_CK_SUM16BE, p, 9u), 0x1DDu);
    CHECK_EQ(prn_ck(PRN_CK_XOR8, p, 9u), 0x31u);
    CHECK_EQ(prn_ck(PRN_CK_CRC8, p, 9u), 0xF4u);    /* CRC-8/SMBUS */
    CHECK_EQ(prn_ck(PRN_CK_CRC16CCITT_LE, p, 9u), 0x29B1u); /* CCITT-FALSE */
    CHECK_EQ(prn_ck(PRN_CK_CRC16CCITT_BE, p, 9u), 0x29B1u);
    CHECK_EQ(prn_ck(PRN_CK_CRC32LE, p, 9u), 0xCBF43926u);
    CHECK_EQ(prn_ck(PRN_CK_SVPAGE, p, 9u), 0u);
    CHECK_EQ(prn_ck(0xEEu, p, 9u), 0u);
    CHECK_EQ(prn_ck_width(PRN_CK_CRC32LE), 4u);
    CHECK_EQ(prn_ck_width(PRN_CK_SVPAGE), 2u);
    CHECK_EQ(prn_ck_width(0xEEu), 0u);
    CHECK_EQ(prn_enc_width(PRN_E_BE32), 4u);
    CHECK_EQ(prn_enc_width(0xEEu), 0u);
}

TEST_CASE(a_checksum_is_written_in_its_byte_order_over_its_range)
{
    /* 31 32 33, then the checksum of bytes 0 to 2 */
    const struct {
        uint8_t kind;
        uint8_t want[4];
        unsigned n;
    } k[] = {
        { PRN_CK_SUM16LE, { 0x96u, 0x00u }, 2u },
        { PRN_CK_SUM16BE, { 0x00u, 0x96u }, 2u },
        { PRN_CK_CRC16CCITT_BE, { 0x5Bu, 0xCEu }, 2u },
        { PRN_CK_CRC16CCITT_LE, { 0xCEu, 0x5Bu }, 2u },
        { PRN_CK_CRC32LE, { 0xD2u, 0x63u, 0x48u, 0x88u }, 4u },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        const prn_tok_t t[4] = {
            { PRN_T_BYTE, 0x31u, 0, 0 }, { PRN_T_BYTE, 0x32u, 0, 0 },
            { PRN_T_BYTE, 0x33u, 0, 0 }, { PRN_T_CK, k[i].kind, 0, 2 },
        };
        const prn_tseq_t q = run_of(t, 4u);
        size_t n = 0u;
        memset(&ctx, 0, sizeof ctx);
        CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
        CHECK_EQ(n, 3u + k[i].n);
        CHECK(memcmp(out + 3, k[i].want, k[i].n) == 0);
    }
}

TEST_CASE(ranges_at_both_ends_and_one_byte_past)
{
    /* AA {len:u8:0:-1} BB {ck:sum8:-3:-2} */
    prn_tok_t t[4] = {
        { PRN_T_BYTE, 0xAAu, 0, 0 }, { PRN_T_LEN, PRN_E_U8, 0, -1 },
        { PRN_T_BYTE, 0xBBu, 0, 0 }, { PRN_T_CK, PRN_CK_SUM8, -3, -2 },
    };
    prn_tseq_t q = run_of(t, 4u);
    size_t n = 0u;
    memset(&ctx, 0, sizeof ctx);
    CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    CHECK_EQ(n, 4u);
    CHECK_EQ(out[1], 4);
    CHECK_EQ(out[3], (uint8_t)(0x04u + 0xBBu));
    /* the last byte, by its offset and by -1 */
    t[3].a = 3;
    t[3].b = 3;
    q = run_of(t, 4u);
    CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    t[3].a = -4;
    t[3].b = -1;
    q = run_of(t, 4u);
    CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    /* one byte past either end, or the wrong way round */
    const int8_t bad[4][2] = { { 0, 4 }, { -5, -1 }, { 2, 1 }, { 4, 4 } };
    for (unsigned i = 0; i < 4u; ++i) {
        t[3].a = bad[i][0];
        t[3].b = bad[i][1];
        q = run_of(t, 4u);
        CHECK(!prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    }
}

TEST_CASE(values_at_the_top_of_each_encoding_and_one_past)
{
    const uint64_t top[5] = { 0xFFu, 0xFFFFu, 0xFFFFu, 0xFFFFFFFFu,
                              0xFFFFFFFFu };
    for (unsigned e = 0; e < (unsigned)PRN_E_COUNT; ++e) {
        const uint8_t enc = (uint8_t)e;
        const prn_tok_t t[2] = { { PRN_T_VAR, enc, PRN_V_C0, 0 },
                                 { PRN_T_ARG, enc, 1, 0 } };
        const prn_tseq_t q = run_of(t, 2u);
        size_t n = 0u;
        memset(&ctx, 0, sizeof ctx);
        ctx.var[PRN_V_C0] = top[enc];
        ctx.arg[1] = (uint32_t)top[enc];
        CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
        CHECK_EQ(n, 2u * prn_enc_width(enc));
        ctx.var[PRN_V_C0] = top[enc] + 1u;
        CHECK(!prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    }
    /* byte order: 0x1234 as le16 and be16, 0x12345678 as le32 and be32 */
    const prn_tok_t t[4] = {
        { PRN_T_ARG, PRN_E_LE16, 0, 0 }, { PRN_T_ARG, PRN_E_BE16, 0, 0 },
        { PRN_T_ARG, PRN_E_LE32, 1, 0 }, { PRN_T_ARG, PRN_E_BE32, 1, 0 },
    };
    const prn_tseq_t q = run_of(t, 4u);
    memset(&ctx, 0, sizeof ctx);
    ctx.arg[0] = 0x1234u;
    ctx.arg[1] = 0x12345678u;
    size_t n = 0u;
    CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    const uint8_t want[12] = { 0x34u, 0x12u, 0x12u, 0x34u, 0x78u, 0x56u,
                               0x34u, 0x12u, 0x12u, 0x34u, 0x56u, 0x78u };
    CHECK_EQ(n, 12u);
    CHECK(memcmp(out, want, 12u) == 0);
}

TEST_CASE(a_length_too_large_for_its_encoding_is_refused)
{
    /* {len:u8:0:-1} {data} with 255 and 256 bytes in all */
    const prn_tok_t t[2] = { { PRN_T_LEN, PRN_E_U8, 0, -1 },
                             { PRN_T_DATA, 0u, 0, 0 } };
    const prn_tseq_t q = run_of(t, 2u);
    static uint8_t data[300];
    memset(&ctx, 0, sizeof ctx);
    ctx.data = data;
    ctx.data_len = 254u;
    size_t n = 0u;
    CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    CHECK_EQ(out[0], 255);
    ctx.data_len = 255u;
    CHECK(!prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
}

TEST_CASE(data_is_padded_and_the_frame_kept_to_its_room)
{
    const prn_tok_t t[3] = { { PRN_T_BYTE, 0x7Eu, 0, 0 },
                             { PRN_T_DATA, 0u, 0, 0 },
                             { PRN_T_BYTE, 0x7Fu, 0, 0 } };
    const prn_tseq_t q = run_of(t, 3u);
    const uint8_t data[3] = { 1u, 2u, 3u };
    memset(&ctx, 0, sizeof ctx);
    ctx.data = data;
    ctx.data_len = 3u;
    ctx.data_pad = 2u;
    ctx.pad_byte = 0xEEu;
    size_t n = 0u;
    CHECK(prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    const uint8_t want[7] = { 0x7Eu, 1u, 2u, 3u, 0xEEu, 0xEEu, 0x7Fu };
    CHECK_EQ(n, 7u);
    CHECK(memcmp(out, want, 7u) == 0);
    CHECK(prn_tmpl_render(&s, q, &ctx, out, 7u, &n));
    CHECK(!prn_tmpl_render(&s, q, &ctx, out, 6u, &n));
    /* data asked for with none given */
    ctx.data = NULL;
    CHECK(!prn_tmpl_render(&s, q, &ctx, out, sizeof out, &n));
    /* a run past the pool, an argument or variable out of its table */
    prn_tseq_t far = { (uint16_t)(PRN_TOKENS - 1u), 2u };
    CHECK(!prn_tmpl_render(&s, far, &ctx, out, sizeof out, &n));
    const prn_tok_t bad[2] = { { PRN_T_ARG, PRN_E_U8, 4, 0 },
                               { PRN_T_VAR, PRN_E_U8, 0, 0 } };
    for (unsigned i = 0; i < 2u; ++i) {
        const prn_tseq_t one = run_of(&bad[i], 1u);
        CHECK(!prn_tmpl_render(&s, one, &ctx, out, sizeof out, &n));
    }
}

int main(void)
{
    RUN(each_checksum_kind_gives_its_reference_value);
    RUN(a_checksum_is_written_in_its_byte_order_over_its_range);
    RUN(ranges_at_both_ends_and_one_byte_past);
    RUN(values_at_the_top_of_each_encoding_and_one_past);
    RUN(a_length_too_large_for_its_encoding_is_refused);
    RUN(data_is_padded_and_the_frame_kept_to_its_room);
    return test_summary("prn_tmpl");
}
