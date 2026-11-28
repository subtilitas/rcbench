/**
 * @file test_prn_image.c
 * @brief The engine's image stream with the T50M Pro file against the
 *        station's captures (test/host/fixtures/supvan, H0) and against
 *        protocols/supvan.
 *
 * Byte identity, layer 1: the pages built from the captured label's image
 * equal label-bufs.bin (12,288B), and the literal-only LZMA stream of
 * those pages equals label-lit.lz.  Layer 2: the chunk frames of the
 * captured stream label.lz equal label-dataframes.bin (1,024B); the
 * commands equal every request of label-cmds.tsv; each of the 29 writes of
 * label-frames.bin is a write the engine makes.
 *
 * Beyond the capture: pages for images of 1 to 300 lines against
 * sv_buf_fill(), chunk frames of streams of 1 to 2,000 bytes against
 * sv_chunk_build(), the speed table against sv_speed(); line packing in
 * each of its 8 forms; rows; a page head that does not fit.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "lzlit.h"
#include "prn_fixture.h"
#include "prn_job.h"
#include "supvan.h"

#define FIX "fixtures/supvan/"

static prn_spec_t s_spec;
static prn_ctx_t s_ctx;
static uint8_t s_bufs[3u * 4096u];
static uint8_t s_bits[300u * 48u];
static uint8_t s_page[8192u];
static uint8_t s_ref[8192u];
static uint8_t s_lit[LZLIT_BOUND(3u * 4096u)];
static lzlit_t s_lz;

/**
 * @brief The context a job gives a page head: margins, density, bytes a
 *        line.
 */
static void ctx_for_pages(void)
{
    memset(&s_ctx, 0, sizeof s_ctx);
    s_ctx.var[PRN_V_MLEAD] = s_spec.mlead;
    s_ctx.var[PRN_V_MTRAIL] = s_spec.mtrail;
    s_ctx.var[PRN_V_DENSITY] = s_spec.density;
    s_ctx.var[PRN_V_LINE_BYTES] = s_spec.line_bytes;
}

/**
 * @brief The captured label's image: the data of label-bufs.bin's 3
 *        buffers (gen_supvan_fixtures.py's captured_bits()).
 * @param bits the image, 224 x 48 bytes
 * @return its lines, 0 when the fixture cannot be read
 */
static uint32_t captured_bits(uint8_t *bits)
{
    if (prn_read(FIX "label-bufs.bin", (char *)s_bufs, sizeof s_bufs)
        != sizeof s_bufs) {
        return 0u;
    }
    uint32_t lines = 0u;
    for (unsigned i = 0; i < 3u; ++i) {
        const uint8_t *b = s_bufs + i * 4096u;
        const unsigned cols = (unsigned)(b[4] | (b[5] << 8));
        memcpy(bits + (size_t)lines * 48u, b + 14, (size_t)cols * 48u);
        lines += cols;
    }
    return lines;
}

TEST_CASE(the_pages_of_the_captured_image_are_label_bufs_bin)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    const uint32_t lines = captured_bits(s_bits);
    CHECK_EQ(lines, 224u);
    CHECK_EQ(prn_page_count(&s_spec, lines), 3u);
    ctx_for_pages();
    for (uint32_t i = 0; i < 3u; ++i) {
        CHECK(prn_page_fill(&s_spec, &s_ctx, s_bits, lines, i, s_page));
        CHECK(memcmp(s_page, s_bufs + i * 4096u, 4096u) == 0);
    }
    CHECK(!prn_page_fill(&s_spec, &s_ctx, s_bits, lines, 3u, s_page));
}

TEST_CASE(the_literal_stream_of_those_pages_is_label_lit_lz)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    const uint32_t lines = captured_bits(s_bits);
    CHECK(s_spec.lzma);
    ctx_for_pages();
    lzlit_begin(&s_lz, s_lit, sizeof s_lit, 3u * 4096u);
    for (uint32_t i = 0; i < 3u; ++i) {
        CHECK(prn_page_fill(&s_spec, &s_ctx, s_bits, lines, i, s_page));
        lzlit_put(&s_lz, s_page, 4096u);
    }
    const size_t n = lzlit_end(&s_lz);
    /* the file's dictionary goes into the header, as the caller does */
    for (unsigned k = 0; k < 4u; ++k) {
        s_lit[1u + k] = (uint8_t)(s_spec.lzma_dict >> (8u * k));
    }
    static uint8_t ref[LZLIT_BOUND(3u * 4096u)];
    const size_t m = prn_read(FIX "label-lit.lz", (char *)ref, sizeof ref);
    CHECK(m > 0u);
    CHECK_EQ(n, m);
    CHECK(n == m && memcmp(s_lit, ref, n) == 0);
}

TEST_CASE(the_chunk_frames_of_the_captured_stream_are_label_dataframes)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    static uint8_t lz[1024];
    static uint8_t frames[1024];
    const size_t n = prn_read(FIX "label.lz", (char *)lz, sizeof lz);
    CHECK_EQ(n, 813u);
    CHECK_EQ(prn_read(FIX "label-dataframes.bin", (char *)frames,
                      sizeof frames),
             1024u);
    CHECK_EQ(prn_frame_count(&s_spec, (uint32_t)n), 2u);
    memset(&s_ctx, 0, sizeof s_ctx);
    for (uint32_t i = 0; i < 2u; ++i) {
        size_t len = 0u;
        CHECK(prn_frame_build(&s_spec, &s_ctx, lz, (uint32_t)n, i, s_page,
                              &len));
        CHECK_EQ(len, 512u);
        CHECK(memcmp(s_page, frames + i * 512u, 512u) == 0);
    }
    size_t len = 0u;
    CHECK(!prn_frame_build(&s_spec, &s_ctx, lz, (uint32_t)n, 2u, s_page,
                           &len));
}

/**
 * @brief Render the file's `cmd` template.
 * @param a   the command byte
 * @param p0  P0
 * @param p1  P1
 * @param out 16 bytes
 * @return the length
 */
static size_t cmd(uint8_t a, uint32_t p0, uint32_t p1, uint8_t *out)
{
    memset(&s_ctx, 0, sizeof s_ctx);
    s_ctx.arg[0] = a;
    s_ctx.arg[1] = p0;
    s_ctx.arg[2] = p1;
    size_t n = 0u;
    if (!prn_tmpl_render(&s_spec, s_spec.tmpl[0].seq, &s_ctx, out,
                         PRN_CMD_MAX, &n)) {
        return 0u;
    }
    return n;
}

/**
 * @brief Read a hexadecimal text into bytes.
 * @param t   the text, NUL- or tab-ended
 * @param out the bytes
 * @return their count
 */
static size_t unhex(const char *t, uint8_t *out)
{
    size_t n = 0u;
    while (t[0] != '\0' && t[0] != '\t' && t[0] != '\n' && t[1] != '\0') {
        unsigned v = 0u;
        if (sscanf(t, "%2x", &v) != 1) {
            break;
        }
        out[n++] = (uint8_t)v;
        t += 2;
    }
    return n;
}

TEST_CASE(the_commands_are_every_request_of_label_cmds_tsv)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    FILE *f = fopen(FIX "label-cmds.tsv", "r");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    static char line[512];
    unsigned rows = 0u;
    while (fgets(line, sizeof line, f) != NULL) {
        char *t1 = strchr(line, '\t');
        const char *t2 = (t1 != NULL) ? strchr(t1 + 1, '\t') : NULL;
        if (t2 == NULL || strncmp(line, "unix_time", 9) == 0) {
            continue;
        }
        uint8_t req[64] = { 0u };
        uint8_t mine[PRN_CMD_MAX];
        const size_t n = unhex(t2 + 1, req);
        CHECK_EQ(n, 16u);
        const uint32_t p0 = (uint32_t)(req[12] | (req[13] << 8));
        const uint32_t p1 = (uint32_t)(req[14] | (req[15] << 8));
        CHECK_EQ(cmd(req[7], p0, p1, mine), 16u);
        CHECK(memcmp(mine, req, 16u) == 0);
        rows++;
    }
    fclose(f);
    CHECK_EQ(rows, 21u);
}

TEST_CASE(each_captured_write_is_a_write_the_engine_makes)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    static uint8_t rec[2048];
    static uint8_t lz[1024];
    const size_t n = prn_read(FIX "label-frames.bin", (char *)rec,
                              sizeof rec);
    const size_t zn = prn_read(FIX "label.lz", (char *)lz, sizeof lz);
    CHECK_EQ(n, 1418u);
    unsigned writes = 0u;
    unsigned frag = 0u;
    unsigned cmds = 0u;
    static uint8_t frame[PRN_FRAME_MAX];
    size_t flen = 0u;
    for (size_t i = 0; i + 2u <= n; ++writes) {
        const size_t len = (size_t)(rec[i] | (rec[i + 1u] << 8));
        const uint8_t *w = rec + i + 2u;
        i += 2u + len;
        if (len == 16u) {
            uint8_t mine[PRN_CMD_MAX];
            const uint32_t p0 = (uint32_t)(w[12] | (w[13] << 8));
            const uint32_t p1 = (uint32_t)(w[14] | (w[15] << 8));
            CHECK_EQ(cmd(w[7], p0, p1, mine), 16u);
            CHECK(memcmp(mine, w, 16u) == 0);
            cmds++;
            continue;
        }
        CHECK_EQ(len, (size_t)s_spec.frag);
        if (frag % 4u == 0u) {
            memset(&s_ctx, 0, sizeof s_ctx);
            CHECK(prn_frame_build(&s_spec, &s_ctx, lz, (uint32_t)zn,
                                  frag / 4u, frame, &flen));
        }
        CHECK(memcmp(frame + (frag % 4u) * 128u, w, 128u) == 0);
        frag++;
    }
    CHECK_EQ(writes, 29u);
    CHECK_EQ(cmds, 21u);
    CHECK_EQ(frag, 8u);
    /* BULK and BUF_FULL carry the stream's frames, length and speed */
    CHECK_EQ(prn_speed(&s_spec, (uint32_t)zn / 3u), 60);
}

/**
 * @brief The next byte of a 32-bit linear congruential generator.
 * @param x the state
 * @return the byte
 */
static uint8_t lcg(uint32_t *x)
{
    *x = *x * 1103515245u + 12345u;
    return (uint8_t)(*x >> 24);
}

TEST_CASE(pages_of_1_to_300_lines_equal_sv_buf_fill)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    uint32_t x = 7u;
    for (size_t i = 0; i < sizeof s_bits; ++i) {
        s_bits[i] = lcg(&x);
    }
    ctx_for_pages();
    unsigned bad = 0u;
    for (uint16_t lines = 1u; lines <= 300u; ++lines) {
        const uint32_t count = prn_page_count(&s_spec, lines);
        if (count != sv_buf_count(lines)) {
            bad++;
        }
        for (uint32_t i = 0; i < count; ++i) {
            (void)sv_buf_fill(s_ref, s_bits, lines, (uint16_t)i);
            if (!prn_page_fill(&s_spec, &s_ctx, s_bits, lines, i, s_page)
                || memcmp(s_page, s_ref, 4096u) != 0) {
                bad++;
            }
        }
    }
    CHECK_EQ(bad, 0u);
    CHECK_EQ(prn_page_count(&s_spec, 0u), 0u);
}

TEST_CASE(chunk_frames_of_1_to_2000_bytes_equal_sv_chunk_build)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    static uint8_t stream[2000];
    uint32_t x = 3u;
    for (size_t i = 0; i < sizeof stream; ++i) {
        stream[i] = lcg(&x);
    }
    unsigned bad = 0u;
    for (uint32_t len = 1u; len <= sizeof stream; ++len) {
        const uint32_t count = prn_frame_count(&s_spec, len);
        if (count != sv_chunk_count(len)) {
            bad++;
        }
        for (uint32_t i = 0; i < count; ++i) {
            size_t n = 0u;
            memset(&s_ctx, 0, sizeof s_ctx);
            (void)sv_chunk_build(s_ref, stream, len, (uint8_t)i);
            if (!prn_frame_build(&s_spec, &s_ctx, stream, len, i, s_page, &n)
                || n != SV_FRAME_LEN || memcmp(s_page, s_ref, n) != 0) {
                bad++;
            }
        }
    }
    CHECK_EQ(bad, 0u);
    /* 255 chunks is the most; 256 is none */
    CHECK_EQ(prn_frame_count(&s_spec, 255u * 500u), 255u);
    CHECK_EQ(prn_frame_count(&s_spec, 255u * 500u + 1u), 0u);
    CHECK_EQ(prn_frame_count(&s_spec, 0u), 0u);
}

TEST_CASE(the_speed_table_equals_sv_speed)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    unsigned bad = 0u;
    for (uint32_t mean = 0u; mean <= 4000u; ++mean) {
        bad += (prn_speed(&s_spec, mean) != sv_speed(mean)) ? 1u : 0u;
    }
    CHECK_EQ(bad, 0u);
    CHECK_EQ(prn_speed(&s_spec, 0xFFFFFFFFu), 10);
    prn_spec_t none;
    memset(&none, 0, sizeof none);
    CHECK_EQ(prn_speed(&none, 100u), 0);
}

TEST_CASE(line_packing_in_each_of_its_8_forms)
{
    prn_spec_t s;
    memset(&s, 0, sizeof s);
    s.head_dots = 12u; /* 2 bytes in the engine's form */
    s.line_bytes = 3u; /* one byte more in the file's */
    const uint8_t in[2] = { 0x01u, 0x08u }; /* dots 0 and 11 black */
    uint8_t out[3];
    const struct {
        bool msb, black0, reverse;
        uint8_t want[3];
    } k[8] = {
        { false, false, false, { 0x01u, 0x08u, 0x00u } },
        { true, false, false, { 0x80u, 0x10u, 0x00u } },
        { false, true, false, { 0xFEu, 0xF7u, 0xFFu } },
        { true, true, false, { 0x7Fu, 0xEFu, 0xFFu } },
        { false, false, true, { 0x00u, 0x08u, 0x01u } },
        { true, false, true, { 0x00u, 0x10u, 0x80u } },
        { false, true, true, { 0xFFu, 0xF7u, 0xFEu } },
        { true, true, true, { 0xFFu, 0xEFu, 0x7Fu } },
    };
    for (unsigned i = 0; i < 8u; ++i) {
        s.line_msb = k[i].msb;
        s.line_black0 = k[i].black0;
        s.line_reverse = k[i].reverse;
        prn_line_pack(&s, in, out);
        CHECK(memcmp(out, k[i].want, 3u) == 0);
    }
    /* the T50M Pro's form is the engine's: a line comes out as it went in */
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    uint8_t line[48];
    uint8_t packed[48];
    uint32_t x = 11u;
    for (unsigned i = 0; i < 48u; ++i) {
        line[i] = lcg(&x);
    }
    prn_line_pack(&s_spec, line, packed);
    CHECK(memcmp(line, packed, 48u) == 0);
}

/** @brief A printer with row frames: a notify joiner, no pages, a 12mm
 *         head. */
static const char k_rows[] =
    "rcbench printer, format 1\n"
    "id rows-test\nname ROWS TEST\nversion 1\n"
    "match name ROW\n"
    "service FF00\nchr cmd FF02 write_nr\nchr rx FF03 notify\n"
    "mtu_min 64\nconn 16 32 0 400\n"
    "head_dots 96\ndots_mm 8\nline 12 msb 1 no\n"
    "label feed top\nlabel dot0 left\nlabel align 0\nlabel margin 0 0\n"
    "roll source fixed\nroll fixed 12 30 3\nroll limits 10 12 10 200\n"
    "rows 55 55 85 {line_no:be16} {len:u8:0:-1} {data} {ck:xor8:2:-2}\n"
    "write cmd nr\nwrite data nr 20 5\n"
    "tmpl go 55 55 {a:u8}\n"
    "rx join notify 3 3\nrx key 0\nanswer 1A 3\n"
    "field 1A cover 1 bit 0\nerror cover COVER\n"
    "step go\n  send go a=1\n  within 100\n"
    "step rows\n  data\n  within 500\n"
    "job query go\njob print go rows\n"
    "verdict none\nend\n";

TEST_CASE(row_frames_wrap_each_head_line)
{
    prn_spec_t s;
    prn_perr_t e;
    CHECK(prn_parse(k_rows, sizeof k_rows - 1u, "rows-test.prn", &s, &e));
    CHECK_EQ(e.code, 0);
    uint8_t lines[36];
    for (unsigned i = 0; i < 36u; ++i) {
        lines[i] = (uint8_t)(i * 7u + 1u);
    }
    CHECK_EQ(prn_frame_count(&s, 36u), 3u);
    CHECK_EQ(prn_frame_count(&s, 35u), 0u);
    memset(&s_ctx, 0, sizeof s_ctx);
    size_t n = 0u;
    CHECK(prn_frame_build(&s, &s_ctx, lines, 36u, 2u, s_page, &n));
    /* 55 55 85, line 2 big-endian, the frame's length, the line, the XOR
     * of byte 2 to the byte before it */
    uint8_t want[19] = { 0x55u, 0x55u, 0x85u, 0x00u, 0x02u, 19u };
    memcpy(want + 6, lines + 24, 12u);
    uint8_t x = 0u;
    for (unsigned i = 2u; i < 18u; ++i) {
        x ^= want[i];
    }
    want[18] = x;
    CHECK_EQ(n, 19u);
    CHECK(memcmp(s_page, want, 19u) == 0);
    CHECK(!prn_frame_build(&s, &s_ctx, lines, 36u, 3u, s_page, &n));
    CHECK(!prn_frame_build(&s, &s_ctx, NULL, 36u, 0u, s_page, &n));
    CHECK_EQ(prn_page_count(&s, 10u), 0u);
    CHECK(!prn_page_fill(&s, &s_ctx, lines, 3u, 0u, s_page));
}

TEST_CASE(a_page_that_cannot_be_built_is_refused)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    ctx_for_pages();
    /* a density too large for its byte: the head does not render */
    s_ctx.var[PRN_V_DENSITY] = 256u;
    const uint32_t lines = captured_bits(s_bits);
    CHECK(!prn_page_fill(&s_spec, &s_ctx, s_bits, lines, 0u, s_page));
    ctx_for_pages();
    CHECK(!prn_page_fill(&s_spec, &s_ctx, NULL, lines, 0u, s_page));
    CHECK(!prn_page_fill(&s_spec, &s_ctx, s_bits, lines, 0u, NULL));
    /* a damaged spec whose lines no longer fit the page */
    prn_spec_t big = s_spec;
    big.page_lines = 86u;
    CHECK(!prn_page_fill(&big, &s_ctx, s_bits, lines, 0u, s_page));
}

int main(void)
{
    RUN(the_pages_of_the_captured_image_are_label_bufs_bin);
    RUN(the_literal_stream_of_those_pages_is_label_lit_lz);
    RUN(the_chunk_frames_of_the_captured_stream_are_label_dataframes);
    RUN(the_commands_are_every_request_of_label_cmds_tsv);
    RUN(each_captured_write_is_a_write_the_engine_makes);
    RUN(pages_of_1_to_300_lines_equal_sv_buf_fill);
    RUN(chunk_frames_of_1_to_2000_bytes_equal_sv_chunk_build);
    RUN(the_speed_table_equals_sv_speed);
    RUN(line_packing_in_each_of_its_8_forms);
    RUN(row_frames_wrap_each_head_line);
    RUN(a_page_that_cannot_be_built_is_refused);
    return test_summary("prn_image");
}
