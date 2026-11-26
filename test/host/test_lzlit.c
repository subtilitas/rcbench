/**
 * @file test_lzlit.c
 * @brief protocols/supvan's literal-only LZMA encoder against golden
 *        streams of tools/lzlit.py (test/host/fixtures/supvan/lzlit-*.bin,
 *        and label-lit.lz of the captured print buffers label-bufs.bin),
 *        byte for byte, fed whole and in uneven pieces; the header; the
 *        overflow answer at one byte short and at the exact size; the
 *        bound on input that grows.
 *
 * test/tools/test_lzlit.py decodes the encoder's output with Python's
 * lzma, an independent decoder.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "lzlit.h"

/** @brief The encoder's state: 12KiB, off the stack. */
static lzlit_t s_z;

/**
 * @brief Read a fixture of test/host/fixtures/supvan.
 * @param name the file's name
 * @param n    set to its length
 * @return its bytes, malloc'd, or NULL
 */
static uint8_t *fixture(const char *name, size_t *n)
{
    char path[128];
    (void)snprintf(path, sizeof path, "fixtures/supvan/%s", name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    uint8_t *b = malloc(65536);
    *n = (b != NULL) ? fread(b, 1, 65536, f) : 0u;
    (void)fclose(f);
    return b;
}

/**
 * @brief The bytes tools/gen_supvan_fixtures.py's lcg_bytes() gives.
 * @param out  the bytes
 * @param n    their count
 * @param seed the start value
 */
static void lcg(uint8_t *out, size_t n, uint32_t seed)
{
    uint32_t x = seed;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1103515245u + 12345u;
        out[i] = (uint8_t)(x >> 24);
    }
}

/**
 * @brief Encode @p in whole, and again in pieces of 1 to 4,096 bytes.
 * @param in    the input
 * @param n     its length
 * @param out   the stream
 * @param cap   its size
 * @param piece 0: one put; else the first piece's length
 * @return the stream's length, 0 on overflow
 */
static size_t encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap,
                     size_t piece)
{
    lzlit_begin(&s_z, out, cap, (uint32_t)n);
    if (piece == 0u) {
        lzlit_put(&s_z, in, n);
    } else {
        size_t o = 0u;
        size_t k = piece;
        while (o < n) {
            const size_t m = (n - o < k) ? n - o : k;
            lzlit_put(&s_z, in + o, m);
            lzlit_put(&s_z, in + o + m, 0u);
            o += m;
            k = (k * 7u) % 4096u + 1u;
        }
    }
    return lzlit_end(&s_z);
}

/**
 * @brief Compare the encoder with one golden stream, whole and in pieces.
 * @param name the fixture
 * @param in   the input it encodes
 * @param n    its length
 * @return 0 when both match
 */
static int against(const char *name, const uint8_t *in, size_t n)
{
    size_t gn = 0u;
    uint8_t *gold = fixture(name, &gn);
    if (gold == NULL || gn == 0u) {
        free(gold);
        return 1;
    }
    const size_t cap = LZLIT_BOUND(n);
    uint8_t *out = malloc(LZLIT_BOUND(65536u));
    int bad = (out == NULL);
    for (size_t piece = 0u; piece <= 4097u && !bad; piece += 1366u) {
        const size_t m = encode(in, n, out, cap, piece);
        bad = (m != gn) || memcmp(out, gold, gn) != 0;
    }
    free(out);
    free(gold);
    return bad;
}

TEST_CASE(the_empty_input_and_one_byte_match_lzlit_py)
{
    const uint8_t zero = 0u;
    CHECK_EQ(against("lzlit-empty.bin", &zero, 0u), 0);
    CHECK_EQ(against("lzlit-one.bin", &zero, 1u), 0);
}

TEST_CASE(zeros_and_a_pattern_match_lzlit_py)
{
    uint8_t *in = calloc(4096u * 3u, 1);
    REQUIRE_ALLOC(in);
    CHECK_EQ(against("lzlit-zeros.bin", in, 4096u * 3u), 0);
    for (unsigned i = 0; i < 4096u; ++i) {
        in[i] = (uint8_t)i;
    }
    CHECK_EQ(against("lzlit-pattern.bin", in, 8192u), 0);
    free(in);
}

TEST_CASE(random_bytes_match_lzlit_py)
{
    uint8_t in[5000];
    lcg(in, sizeof in, 1u);
    CHECK_EQ(against("lzlit-random.bin", in, sizeof in), 0);
}

TEST_CASE(the_print_buffers_of_the_test_label_match_lzlit_py)
{
    size_t n = 0u;
    uint8_t *bufs = fixture("label-bufs.bin", &n);
    REQUIRE_ALLOC(bufs);
    CHECK_EQ(n, 3u * 4096u);
    CHECK_EQ(against("label-lit.lz", bufs, n), 0);
    free(bufs);
}

TEST_CASE(the_header_names_props_dictionary_and_exact_length)
{
    uint8_t out[64];
    lzlit_begin(&s_z, out, sizeof out, 0x01020304u);
    CHECK_EQ(s_z.n, LZLIT_HEADER);
    static const uint8_t want[LZLIT_HEADER] = { 0x5D, 0x00, 0x20, 0x00, 0x00,
                                                0x04, 0x03, 0x02, 0x01,
                                                0x00, 0x00, 0x00, 0x00 };
    CHECK(memcmp(out, want, sizeof want) == 0);
}

TEST_CASE(one_byte_short_is_an_overflow_and_the_exact_size_is_not)
{
    uint8_t in[5000];
    lcg(in, sizeof in, 7u);
    uint8_t *out = malloc(LZLIT_BOUND(sizeof in));
    REQUIRE_ALLOC(out);
    const size_t m = encode(in, sizeof in, out, LZLIT_BOUND(sizeof in), 0u);
    CHECK(m > sizeof in);
    CHECK_EQ(encode(in, sizeof in, out, m, 0u), m);
    CHECK_EQ(encode(in, sizeof in, out, m - 1u, 0u), 0u);
    CHECK_EQ(encode(in, sizeof in, out, 0u, 0u), 0u);
    CHECK_EQ(encode(in, sizeof in, NULL, 1000u, 0u), 0u);
    CHECK_EQ(encode(in, 0u, out, LZLIT_HEADER + 4u, 0u), 0u);
    CHECK_EQ(encode(in, 0u, out, LZLIT_HEADER + 5u, 0u), LZLIT_HEADER + 5u);
    free(out);
}

TEST_CASE(input_that_grows_stays_inside_the_bound)
{
    static uint8_t in[65536];
    lcg(in, sizeof in, 99u);
    uint8_t *out = malloc(LZLIT_BOUND(sizeof in));
    REQUIRE_ALLOC(out);
    const size_t sizes[] = { 1u, 9u, 100u, 4096u, 20000u, 65536u };
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        const size_t m = encode(in, sizes[i], out, LZLIT_BOUND(sizes[i]), 0u);
        CHECK(m != 0u);
        CHECK(m <= LZLIT_BOUND(sizes[i]));
    }
    free(out);
}

TEST_CASE(the_coder_carries_into_bytes_it_held_back)
{
    /* Runs of 0x00 and 0xFF push low past 0xFF000000 and 2^32, the
     * cache's carry path.  The stream must match a second encoding fed in
     * pieces. */
    static uint8_t in[20000];
    for (size_t i = 0; i < sizeof in; ++i) {
        in[i] = (i % 4000u < 2000u) ? 0x00u : 0xFFu;
    }
    uint8_t *a = malloc(LZLIT_BOUND(sizeof in));
    uint8_t *b = malloc(LZLIT_BOUND(sizeof in));
    REQUIRE_ALLOC(a);
    REQUIRE_ALLOC(b);
    const size_t ma = encode(in, sizeof in, a, LZLIT_BOUND(sizeof in), 0u);
    const size_t mb = encode(in, sizeof in, b, LZLIT_BOUND(sizeof in), 1u);
    CHECK(ma != 0u);
    CHECK_EQ(ma, mb);
    CHECK(memcmp(a, b, ma) == 0);
    free(a);
    free(b);
}

int main(void)
{
    RUN(the_empty_input_and_one_byte_match_lzlit_py);
    RUN(zeros_and_a_pattern_match_lzlit_py);
    RUN(random_bytes_match_lzlit_py);
    RUN(the_print_buffers_of_the_test_label_match_lzlit_py);
    RUN(the_header_names_props_dictionary_and_exact_length);
    RUN(one_byte_short_is_an_overflow_and_the_exact_size_is_not);
    RUN(input_that_grows_stays_inside_the_bound);
    RUN(the_coder_carries_into_bytes_it_held_back);
    return test_summary("lzlit");
}
