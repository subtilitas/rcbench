/**
 * @file
 * @brief The QR scanner's line framer (scan_line.h).
 *
 * The failures this is about: a code cut by a slow byte taken as a whole
 * code, a byte of line noise taken into a servo id, a line of 65 bytes
 * taken as a 64-byte id, and an answer frame mixed into a code.  Frames are
 * built here byte by byte with a CRC computed in this file, not with the
 * module's own code.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include <string.h>

#include "scan_line.h"

/** @brief The manual's CRC routine (10.1), as printed. */
static uint16_t ref_crc(const uint8_t *p, size_t n)
{
    unsigned crc = 0u;
    while (n-- != 0u) {
        for (unsigned i = 0x80u; i != 0u; i /= 2u) {
            crc *= 2u;
            if ((crc & 0x10000u) != 0u) {
                crc ^= 0x11021u;
            }
            if ((*p & i) != 0u) {
                crc ^= 0x1021u;
            }
        }
        ++p;
    }
    return (uint16_t)crc;
}

/** @brief An answer frame 02 00 00 len data CRC; returns its length. */
static size_t make_answer(uint8_t *out, uint8_t head, const uint8_t *data,
                          uint8_t n)
{
    out[0] = head;
    out[1] = 0x00u;
    out[2] = 0x00u;
    out[3] = n;
    memcpy(&out[4], data, n);
    const uint16_t crc = ref_crc(&out[2], (size_t)n + 2u);
    out[4u + n] = (uint8_t)(crc >> 8);
    out[5u + n] = (uint8_t)(crc & 0xFFu);
    return (size_t)n + 6u;
}

/** @brief Results of one feed run. */
typedef struct {
    unsigned lines, frames, longs;
    char     last[SCAN_LINE_MAX + 1u];
} got_t;

/** @brief Feed @p n bytes at 1ms each from @p t; count what comes out. */
static uint32_t feed(scan_line_t *s, const void *bytes, size_t n, uint32_t t,
                     got_t *g)
{
    const uint8_t *b = bytes;
    for (size_t i = 0; i < n; ++i, ++t) {
        switch (scan_line_feed(s, b[i], t)) {
        case SCAN_OUT_LINE:
            ++g->lines;
            strcpy(g->last, scan_line_text(s));
            break;
        case SCAN_OUT_FRAME:
            ++g->frames;
            break;
        case SCAN_OUT_LONG:
            ++g->longs;
            strcpy(g->last, scan_line_text(s));
            break;
        default:
            break;
        }
    }
    return t;
}

TEST_CASE(a_line_byte_by_byte_comes_out_at_its_cr)
{
    scan_line_t s;
    got_t g = { 0 };
    const char *id = "KST_X10-0000A";
    scan_line_init(&s);
    uint32_t t = 0u;
    for (size_t i = 0; i < strlen(id); ++i) {
        CHECK_EQ(scan_line_feed(&s, (uint8_t)id[i], t), SCAN_OUT_NONE);
        t += 10u;
    }
    CHECK_EQ(scan_line_feed(&s, '\r', t), SCAN_OUT_LINE);
    CHECK_STR_EQ(scan_line_text(&s), id);
    CHECK_EQ(scan_line_feed(&s, '\n', t + 1u), SCAN_OUT_NONE);
    CHECK_EQ(scan_line_counts(&s).lines, 1u);
    (void)g;
}

TEST_CASE(cr_crlf_and_lf_alone_each_end_one_line)
{
    scan_line_t s;
    got_t g = { 0 };
    scan_line_init(&s);
    uint32_t t = feed(&s, "AB\r", 3u, 0u, &g);
    CHECK_EQ(g.lines, 1u);
    t = feed(&s, "CD\r\n", 4u, t, &g);
    CHECK_EQ(g.lines, 2u);
    CHECK_STR_EQ(g.last, "CD");
    t = feed(&s, "EF\n", 3u, t, &g);
    CHECK_EQ(g.lines, 3u);
    CHECK_STR_EQ(g.last, "EF");
    /* LF LF and CR CR: the second end is an empty line, dropped uncounted. */
    t = feed(&s, "GH\n\n\r\r", 6u, t, &g);
    CHECK_EQ(g.lines, 4u);
    (void)feed(&s, "\r\n\r\n", 4u, t, &g);
    const scan_counts_t c = scan_line_counts(&s);
    CHECK_EQ(c.lines, 4u);
    CHECK_EQ(c.noise, 0u);
    CHECK_EQ(c.partial, 0u);
}

TEST_CASE(two_lines_in_one_read_both_come_out)
{
    scan_line_t s;
    got_t g = { 0 };
    char seen[2][SCAN_LINE_MAX + 1u];
    const char *two = "A-00001\r\nB-00002\r\n";
    unsigned k = 0u;
    scan_line_init(&s);
    for (size_t i = 0; i < strlen(two); ++i) {
        if (scan_line_feed(&s, (uint8_t)two[i], 5u) == SCAN_OUT_LINE) {
            strcpy(seen[k++], scan_line_text(&s));
        }
    }
    CHECK_EQ(k, 2u);
    CHECK_STR_EQ(seen[0], "A-00001");
    CHECK_STR_EQ(seen[1], "B-00002");
    (void)g;
}

TEST_CASE(a_gap_of_100ms_drops_the_open_line_and_99ms_keeps_it)
{
    scan_line_t s;
    scan_line_init(&s);
    (void)scan_line_feed(&s, 'A', 1000u);
    (void)scan_line_feed(&s, 'B', 1099u);      /* 99ms: same line */
    CHECK_EQ(scan_line_feed(&s, '\r', 1100u), SCAN_OUT_LINE);
    CHECK_STR_EQ(scan_line_text(&s), "AB");
    CHECK_EQ(scan_line_counts(&s).partial, 0u);

    (void)scan_line_feed(&s, 'C', 2000u);
    (void)scan_line_feed(&s, 'D', 2100u);      /* 100ms: C is dropped */
    CHECK_EQ(scan_line_counts(&s).partial, 1u);
    CHECK_EQ(scan_line_feed(&s, '\r', 2101u), SCAN_OUT_LINE);
    CHECK_STR_EQ(scan_line_text(&s), "D");

    /* scan_line_idle() drops at 100ms and not at 99ms. */
    (void)scan_line_feed(&s, 'E', 3000u);
    CHECK_EQ(scan_line_idle(&s, 3099u), SCAN_OUT_NONE);
    CHECK_EQ(scan_line_counts(&s).partial, 1u);
    (void)scan_line_idle(&s, 3100u);
    CHECK_EQ(scan_line_counts(&s).partial, 2u);
    CHECK_EQ(scan_line_feed(&s, '\r', 3101u), SCAN_OUT_NONE);

    /* Silence between lines is no partial line. */
    (void)scan_line_idle(&s, 9000u);
    CHECK_EQ(scan_line_counts(&s).partial, 2u);
}

TEST_CASE(the_gap_holds_across_the_clock_wrap)
{
    scan_line_t s;
    scan_line_init(&s);
    (void)scan_line_feed(&s, 'A', 0xFFFFFFC0u);
    (void)scan_line_feed(&s, 'B', 0x00000022u);   /* 98ms later */
    CHECK_EQ(scan_line_feed(&s, '\r', 0x00000023u), SCAN_OUT_LINE);
    CHECK_STR_EQ(scan_line_text(&s), "AB");
    (void)scan_line_feed(&s, 'C', 0xFFFFFFF0u);
    (void)scan_line_feed(&s, 'D', 0x00000054u);   /* 100ms later */
    CHECK_EQ(scan_line_counts(&s).partial, 1u);
}

TEST_CASE(every_byte_value_inside_a_line)
{
    for (unsigned v = 0u; v <= 0xFFu; ++v) {
        scan_line_t s;
        got_t g = { 0 };
        uint8_t line[6] = { 'A', 'B', (uint8_t)v, 'C', 'D', '\r' };
        scan_line_init(&s);
        (void)feed(&s, line, sizeof(line), 0u, &g);
        const scan_counts_t c = scan_line_counts(&s);
        if (v == '\r' || v == '\n') {
            CHECK_EQ(g.lines, 2u);
            CHECK_STR_EQ(g.last, "CD");
        } else if (v >= 0x20u && v <= 0x7Eu) {
            CHECK_EQ(g.lines, 1u);
            CHECK_EQ((unsigned char)g.last[2], v);
            CHECK_EQ(strlen(g.last), 5u);
        } else {
            CHECK_EQ(g.lines, 0u);
            CHECK_EQ(c.noise, 1u);
        }
        CHECK_EQ(g.frames, 0u);
    }
}

TEST_CASE(a_byte_outside_the_printable_range_at_the_start_poisons_the_line)
{
    for (unsigned v = 0u; v <= 0xFFu; ++v) {
        if ((v >= 0x20u && v <= 0x7Eu) || v == '\r' || v == '\n'
            || v == 0x02u || v == 0x03u) {
            continue;
        }
        scan_line_t s;
        got_t g = { 0 };
        uint8_t line[4] = { (uint8_t)v, 'A', 'B', '\r' };
        scan_line_init(&s);
        (void)feed(&s, line, sizeof(line), 0u, &g);
        CHECK_EQ(g.lines, 0u);
        CHECK_EQ(scan_line_counts(&s).noise, 1u);
    }
}

TEST_CASE(sixty_four_bytes_are_a_line_and_sixty_five_are_too_long)
{
    char buf[SCAN_LINE_MAX + 3u];
    scan_line_t s;
    got_t g = { 0 };
    memset(buf, 'X', sizeof(buf));
    buf[SCAN_LINE_MAX] = '\r';
    scan_line_init(&s);
    uint32_t t = feed(&s, buf, SCAN_LINE_MAX + 1u, 0u, &g);
    CHECK_EQ(g.lines, 1u);
    CHECK_EQ(strlen(g.last), SCAN_LINE_MAX);

    buf[SCAN_LINE_MAX] = 'Y';
    buf[SCAN_LINE_MAX + 1u] = '\r';
    t = feed(&s, buf, SCAN_LINE_MAX + 2u, t, &g);
    CHECK_EQ(g.lines, 1u);
    CHECK_EQ(g.longs, 1u);
    CHECK_EQ(strlen(g.last), SCAN_LINE_MAX);   /* the first 64 bytes */
    CHECK(strchr(g.last, 'Y') == NULL);
    CHECK_EQ(scan_line_counts(&s).overlong, 1u);

    /* A long line with noise in it is noise. */
    buf[3] = 0x01;
    (void)feed(&s, buf, SCAN_LINE_MAX + 2u, t, &g);
    CHECK_EQ(g.longs, 1u);
    CHECK_EQ(scan_line_counts(&s).noise, 1u);
}

TEST_CASE(a_frame_in_two_halves_and_right_after_a_line)
{
    const uint8_t zone = 0x3Eu;
    uint8_t f[16];
    scan_line_t s;
    got_t g = { 0 };
    const size_t n = make_answer(f, 0x02u, &zone, 1u);
    scan_line_init(&s);

    uint32_t t = feed(&s, f, 3u, 0u, &g);
    t = feed(&s, &f[3], n - 3u, t + 60u, &g);    /* 60ms between the halves */
    CHECK_EQ(g.frames, 1u);
    size_t len = 0u;
    const uint8_t *p = scan_line_frame(&s, &len);
    CHECK_EQ(len, n);
    CHECK(memcmp(p, f, n) == 0);
    /* The manual's example answer, byte for byte. */
    const uint8_t manual[7] = { 0x02, 0x00, 0x00, 0x01, 0x3E, 0xE4, 0xAC };
    CHECK(memcmp(p, manual, sizeof(manual)) == 0);

    t = feed(&s, "A-00001\r\n", 9u, t, &g);
    t = feed(&s, f, n, t, &g);
    CHECK_EQ(g.lines, 1u);
    CHECK_EQ(g.frames, 2u);
    CHECK_STR_EQ(g.last, "A-00001");
    /* A frame, then a line with no gap. */
    t = feed(&s, f, n, t, &g);
    (void)feed(&s, "B-00002\r", 8u, t, &g);
    CHECK_EQ(g.frames, 3u);
    CHECK_EQ(g.lines, 2u);
    CHECK_STR_EQ(g.last, "B-00002");
    CHECK_STR_EQ(scan_line_text(&s), "B-00002");
}

TEST_CASE(a_heartbeat_answer_with_head_03_and_the_longest_frame_pass)
{
    uint8_t data[SCAN_DATA_MAX];
    uint8_t f[SCAN_FRAME_MAX];
    scan_line_t s;
    got_t g = { 0 };
    const uint8_t zero = 0u;
    scan_line_init(&s);
    size_t n = make_answer(f, 0x03u, &zero, 1u);
    const uint8_t manual[7] = { 0x03, 0x00, 0x00, 0x01, 0x00, 0x33, 0x31 };
    CHECK(memcmp(f, manual, sizeof(manual)) == 0);
    uint32_t t = feed(&s, f, n, 0u, &g);
    CHECK_EQ(g.frames, 1u);

    for (unsigned i = 0; i < SCAN_DATA_MAX; ++i) {
        data[i] = (uint8_t)(i * 7u);           /* holds 0x0D and 0x0A */
    }
    n = make_answer(f, 0x02u, data, (uint8_t)SCAN_DATA_MAX);
    CHECK_EQ(n, SCAN_FRAME_MAX);
    (void)feed(&s, f, n, t, &g);
    CHECK_EQ(g.frames, 2u);
    CHECK_EQ(g.lines, 0u);
    size_t len = 0u;
    CHECK(memcmp(scan_line_frame(&s, &len), f, n) == 0);
    CHECK_EQ(len, n);
}

TEST_CASE(a_frame_with_a_wrong_crc_or_length_is_dropped_and_counted)
{
    const uint8_t zone = 0x3Eu;
    uint8_t f[16];
    scan_line_t s;
    got_t g = { 0 };
    scan_line_init(&s);
    size_t n = make_answer(f, 0x02u, &zone, 1u);
    f[n - 1u] ^= 0x01u;
    uint32_t t = feed(&s, f, n, 0u, &g);
    CHECK_EQ(g.frames, 0u);
    CHECK_EQ(scan_line_counts(&s).bad_crc, 1u);
    t = feed(&s, "A-00001\r", 8u, t, &g);
    CHECK_EQ(g.lines, 1u);

    /* Length 0 and 98: past what a frame can hold. */
    const uint8_t zero_len[] = { 0x02, 0x00, 0x00, 0x00, 'Q', '\r' };
    const uint8_t big_len[] = { 0x02, 0x00, 0x00, 98u, 'Q', 'R', '\n' };
    t = feed(&s, zero_len, sizeof(zero_len), t, &g);
    t = feed(&s, big_len, sizeof(big_len), t, &g);
    CHECK_EQ(scan_line_counts(&s).bad_crc, 3u);
    CHECK_EQ(g.lines, 1u);                       /* the rest was skipped */

    /* A length one short: the CRC lands on the wrong bytes. */
    n = make_answer(f, 0x02u, &zone, 1u);
    f[3] = 2u;
    t = feed(&s, f, n, t, &g);
    t = feed(&s, "Z", 1u, t, &g);
    CHECK_EQ(scan_line_counts(&s).bad_crc, 4u);
    CHECK_EQ(g.frames, 0u);

    /* A wrong head byte or type byte. */
    const uint8_t bad_head[] = { 0x02, 0x01, 0x00, 0x01, 0x00, '\r' };
    const uint8_t bad_type[] = { 0x02, 0x00, 0x05, 0x01, 0x00, '\r' };
    t = feed(&s, bad_head, sizeof(bad_head), t, &g);
    t = feed(&s, bad_type, sizeof(bad_type), t, &g);
    CHECK_EQ(scan_line_counts(&s).bad_crc, 6u);
    (void)feed(&s, "A-00002\r", 8u, t, &g);
    CHECK_EQ(g.lines, 2u);
    CHECK_STR_EQ(g.last, "A-00002");
}

TEST_CASE(a_frame_cut_after_each_of_its_bytes_is_dropped_as_partial)
{
    const uint8_t zone = 0x3Eu;
    uint8_t f[16];
    const size_t n = make_answer(f, 0x02u, &zone, 1u);
    for (size_t cut = 1u; cut < n; ++cut) {
        scan_line_t s;
        got_t g = { 0 };
        scan_line_init(&s);
        uint32_t t = feed(&s, f, cut, 0u, &g);
        t += SCAN_GAP_MS;
        (void)feed(&s, "A-00001\r", 8u, t, &g);
        CHECK_EQ(g.frames, 0u);
        CHECK_EQ(g.lines, 1u);
        CHECK_STR_EQ(g.last, "A-00001");
        CHECK_EQ(scan_line_counts(&s).partial, 1u);
        CHECK_EQ(scan_line_counts(&s).bad_crc, 0u);
    }
}

TEST_CASE(a_frame_byte_inside_a_line_is_noise)
{
    scan_line_t s;
    got_t g = { 0 };
    scan_line_init(&s);
    (void)feed(&s, "AB\x02\x00\x00\r", 6u, 0u, &g);
    CHECK_EQ(g.lines, 0u);
    CHECK_EQ(g.frames, 0u);
    CHECK_EQ(scan_line_counts(&s).noise, 1u);
}

/**
 * @brief Stand-in for servo_id_parse() (PLAN-A 2.1-2.2): prefix of 1 to 17
 *        characters of A-Z, 0-9, space, '-', '_', '.', then '-' and 5
 *        characters 0-9A-Z; lower case folded.
 *
 * SWAP: servo_id_parse() comes with shared/logfile/servo_id.h (slice A1).
 * Once it is on main, this function and its own test case go, and the
 * fuzz case calls servo_id_parse(text, NULL).
 */
static bool id_like(const char *text)
{
    char id[24];                                /* SERVO_ID_MAX + 1 */
    const size_t n = strlen(text);
    if (n < 7u || n >= sizeof(id)) {
        return false;
    }
    memcpy(id, text, n + 1u);
    const size_t dash = n - 6u;
    if (id[dash] != '-' || id[0] == ' ' || id[0] == '.'
        || id[dash - 1u] == ' ') {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        char c = id[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (i > dash && !alnum) {
            return false;                       /* the serial */
        }
        if (i < dash && !alnum && c != ' ' && c != '-' && c != '_'
            && c != '.') {
            return false;                       /* the prefix */
        }
    }
    return true;
}

TEST_CASE(the_id_stand_in_takes_ids_and_refuses_the_rest)
{
    CHECK(id_like("KST_X10-0000A"));
    CHECK(id_like("A-ZZZZZ"));
    CHECK(id_like("kst x10-0000a"));
    CHECK(id_like("ABCDEFGHIJKLMNOPQ-00000"));
    CHECK(!id_like("ABCDEFGHIJKLMNOPQR-00000"));
    CHECK(!id_like("-00000"));
    CHECK(!id_like("A-0000"));
    CHECK(!id_like("A-000000"));
    CHECK(!id_like("A;B-00000"));
    CHECK(!id_like("A-0000_"));
    CHECK(!id_like("A_00000"));
    CHECK(!id_like(" A-00000"));
    CHECK(!id_like(".A-00000"));
    CHECK(!id_like("A -00000"));
    CHECK(id_like("A.B-C_D-00000"));
}

/** @brief The suite's fixed pseudo-random sequence (32-bit LCG). */
static uint32_t lcg(uint32_t *x)
{
    *x = *x * 1664525u + 1013904223u;
    return *x >> 24;
}

TEST_CASE(ten_thousand_random_bytes_give_no_line_that_is_a_servo_id)
{
    scan_line_t s;
    uint32_t seed = 0x5CA9u;
    unsigned lines = 0u;
    scan_line_init(&s);
    for (uint32_t t = 0u; t < 10000u; ++t) {
        const scan_out_t o = scan_line_feed(&s, (uint8_t)lcg(&seed), t);
        if (o == SCAN_OUT_LINE) {
            ++lines;
            CHECK(!id_like(scan_line_text(&s)));
        }
        CHECK(strlen(scan_line_text(&s)) <= SCAN_LINE_MAX);
    }
    const scan_counts_t c = scan_line_counts(&s);
    CHECK(c.noise > 0u);
    CHECK_EQ(c.frames, 0u);
    CHECK_EQ(lines, c.lines);
}

TEST_CASE(a_null_framer_does_nothing)
{
    size_t len = 7u;
    scan_line_init(NULL);
    CHECK_EQ(scan_line_feed(NULL, 'A', 0u), SCAN_OUT_NONE);
    CHECK_EQ(scan_line_idle(NULL, 0u), SCAN_OUT_NONE);
    CHECK_STR_EQ(scan_line_text(NULL), "");
    CHECK(scan_line_frame(NULL, &len) == NULL);
    CHECK_EQ(len, 0u);
    CHECK(scan_line_frame(NULL, NULL) == NULL);
}

int main(void)
{
    RUN(a_line_byte_by_byte_comes_out_at_its_cr);
    RUN(cr_crlf_and_lf_alone_each_end_one_line);
    RUN(two_lines_in_one_read_both_come_out);
    RUN(a_gap_of_100ms_drops_the_open_line_and_99ms_keeps_it);
    RUN(the_gap_holds_across_the_clock_wrap);
    RUN(every_byte_value_inside_a_line);
    RUN(a_byte_outside_the_printable_range_at_the_start_poisons_the_line);
    RUN(sixty_four_bytes_are_a_line_and_sixty_five_are_too_long);
    RUN(a_frame_in_two_halves_and_right_after_a_line);
    RUN(a_heartbeat_answer_with_head_03_and_the_longest_frame_pass);
    RUN(a_frame_with_a_wrong_crc_or_length_is_dropped_and_counted);
    RUN(a_frame_cut_after_each_of_its_bytes_is_dropped_as_partial);
    RUN(a_frame_byte_inside_a_line_is_noise);
    RUN(the_id_stand_in_takes_ids_and_refuses_the_rest);
    RUN(ten_thousand_random_bytes_give_no_line_that_is_a_servo_id);
    RUN(a_null_framer_does_nothing);
    return test_summary("scan_line");
}
