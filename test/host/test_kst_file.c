/*
 * The KST servo configuration file: the text of an image, reading it back,
 * reading a text a person wrote, and holding either against a servo.
 *
 * Every text is parsed from a heap block of its exact length, so a read
 * past its end stops the sanitizer build.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "kst_file.h"
#include "kst_limits.h"
#include "kst_reg.h"
#include "kst_sim.h"

#define HEAD "KST servo configuration, format 2\n"
#define HEAD1 "KST servo configuration, format 1\n"
#define BIT(id) KST_FIELD_BIT(id)
#define RULE(r) KST_RULE_BIT(r)

static char g_text[KST_FILE_MAX + 64];

/* --- helpers ------------------------------------------------------------------ */

static uint32_t g_seed;

static uint32_t rnd(void)
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

static kst_image_t random_image(void)
{
    kst_image_t img;

    for (unsigned i = 0; i < KST_REG_COUNT; ++i) {
        img.r[i] = (uint8_t)rnd();
    }
    return img;
}

static kst_file_err_t parse(const char *text, size_t len, kst_file_t *out,
                            unsigned *line)
{
    char *exact = malloc(len + (len == 0u ? 1u : 0u));
    REQUIRE_ALLOC(exact);
    kst_file_err_t err;

    memcpy(exact, text, len);
    err = kst_file_parse(exact, len, out, line);
    free(exact);
    return err;
}

/* The text of @p img in g_text, terminated. */
static size_t written(const kst_image_t *img)
{
    const size_t n = kst_file_format(img, kst_type(img), g_text, KST_FILE_MAX);

    g_text[n] = '\0';
    return n;
}

/* CRC-32 one bit a step: not the table the module uses. */
static uint32_t crc32_bits(const char *s, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < n; ++i) {
        crc ^= (unsigned char)s[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* @p body with a fitting checksum line, in g_text.  Returns the length. */
static size_t sealed(const char *body)
{
    static const char hex[] = "0123456789ABCDEF";
    const size_t n = strlen(body);
    const uint32_t crc = crc32_bits(body, n);

    memcpy(g_text, body, n);
    memcpy(g_text + n, "checksum ", 9);
    for (unsigned i = 0; i < 8u; ++i) {
        g_text[n + 9u + i] = hex[(crc >> (28u - 4u * i)) & 0x0Fu];
    }
    g_text[n + 17u] = '\n';
    g_text[n + 18u] = '\0';
    return n + 18u;
}

/* The lines of the bench image's file before the checksum line, with
 * @p from replaced by @p to once. */
static const char *bench_body(const char *from, const char *to)
{
    static char body[KST_FILE_MAX];
    const kst_image_t img = sim_bench_image();
    const char *at;
    char *end;

    written(&img);
    end = strstr(g_text, "checksum");
    *end = '\0';
    at = strstr(g_text, from);
    if (at == NULL) {
        body[0] = '\0';
        return body;
    }
    memcpy(body, g_text, (size_t)(at - g_text));
    strcpy(body + (at - g_text), to);
    strcat(body, at + strlen(from));
    return body;
}

static kst_file_err_t parse_sealed(const char *body, unsigned *line)
{
    kst_file_t f;
    const size_t n = sealed(body);

    return parse(g_text, n, &f, line);
}

/* A file without a checksum: the header and @p body. */
static kst_file_err_t edited(const char *body, kst_file_t *out, unsigned *line)
{
    static char text[KST_FILE_MAX + 1];

    strcpy(text, HEAD);
    strcat(text, body);
    return parse(text, strlen(text), out, line);
}

static kst_file_verdict_t load(const char *body, kst_file_load_t *out)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_t f;

    memset(out, 0xA5, sizeof(*out));
    if (edited(body, &f, NULL) != KST_FILE_OK) {
        return KST_FILE_LOAD_ARG;
    }
    return kst_file_load(&f, &servo, &servo, kst_type(&servo), out);
}

static bool same_image(const kst_image_t *a, const kst_image_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static kst_image_t bench_with(kst_field_id_t id, unsigned raw)
{
    kst_image_t img = sim_bench_image();

    (void)kst_field_edit(&img, id, (uint16_t)raw);
    return img;
}

/* --- the text of an image ----------------------------------------------------- */

TEST_CASE(the_bench_image_is_written_as_the_documented_file)
{
    const kst_image_t img = sim_bench_image();

    CHECK_EQ(written(&img), 808);
    CHECK_STR_EQ(g_text,
        "KST servo configuration, format 2\n"
        "Type: V6\n"
        "Max. Duty = 96 %\n"
        "Boost = 300.0 us\n"
        "Stretch = x1\n"
        "Soft_Start = 1\n"
        "20k_sel = 1 [not loaded]\n"
        "spd_sel = 0\n"
        "Dead Band = 2.0 us\n"
        "Lock = 1 [not loaded]\n"
        "Allow Uncont = 0\n"
        "Pulse Lower = 900.0 us\n"
        "Pulse Upper = 2100.0 us\n"
        "Neutral = 1500.2 us\n"
        "Reversion = 0\n"
        "Uncontrolled Pos = 1500.0 us\n"
        "Uncontrolled Time = 2.0 s\n"
        "Left Angle = 150\n"
        "Right Angle = 150\n"
        "prot1_cancle = 0 [not loaded]\n"
        "prot1en = 0 [not loaded]\n"
        "prot1_time = 0.1 s [not loaded]\n"
        "prot1_pwm = 0 % [not loaded]\n"
        "prot_pot_err = 0 of 12 [not loaded]\n"
        "pot_sam_times = 0 [not loaded]\n"
        "prot_pwm_err = 0 of 8 [not loaded]\n"
        "SPD = 1.0 %\n"
        "node_addr = 16 [not loaded]\n"
        "Servo / Gyro Servo = 0 [not loaded]\n"
        "registers 32\n"
        "00: 00 F5 F5 14 48 77 04 65\n"
        "08: 41 A4 0D 80 53 07 14 00\n"
        "10: 96 96 00 01 01 00 00 00\n"
        "18: 00 00 00 0A 00 10 00 00\n"
        "checksum 77F4E82D\n");
}

TEST_CASE(the_checksum_is_crc_32_over_the_lines_before_it)
{
    g_seed = 0x1234567u;
    for (int i = 0; i < 200; ++i) {
        const kst_image_t img = random_image();
        const size_t n = written(&img);
        char want[9];
        const uint32_t crc = crc32_bits(g_text, n - 18u);

        for (unsigned d = 0; d < 8u; ++d) {
            want[d] = "0123456789ABCDEF"[(crc >> (28u - 4u * d)) & 0x0Fu];
        }
        want[8] = '\0';
        g_text[n - 1u] = '\0';
        CHECK_STR_EQ(g_text + n - 9u, want);
    }
}

TEST_CASE(a_type_8_image_states_type_v8_and_stretch_6_its_raw_value)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_set(&img, KST_F_20K_SEL, 0));
    CHECK(kst_field_set(&img, KST_F_STRETCH, 6));
    written(&img);
    CHECK(strstr(g_text, "\nType: V8\n") != NULL);
    CHECK(strstr(g_text, "\nStretch = raw 6, no displayed value\n") != NULL);
}

TEST_CASE(the_formatter_writes_nothing_past_its_buffer_and_refuses_a_short_one)
{
    const kst_image_t img = sim_bench_image();
    const size_t full = written(&img);

    CHECK_EQ(kst_file_format(NULL, KST_TYPE_V6, g_text, KST_FILE_MAX), 0);
    CHECK_EQ(kst_file_format(&img, KST_TYPE_V6, NULL, KST_FILE_MAX), 0);
    for (size_t size = 0; size < full; ++size) {
        char *exact = malloc(size + 1u);
        REQUIRE_ALLOC(exact);

        CHECK_EQ(kst_file_format(&img, KST_TYPE_V6, exact, size), 0);
        free(exact);
    }
    {
        char *exact = malloc(full > 0u ? full : 1u);
        REQUIRE_ALLOC(exact);

        CHECK_EQ(kst_file_format(&img, KST_TYPE_V6, exact, full), full);
        free(exact);
    }
}

/* Lines of at most KST_FILE_LINE_MAX characters, ASCII, LF at the end. */
static bool well_formed(size_t n)
{
    size_t line = 0;

    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)g_text[i];

        if (c == '\n') {
            line = 0;
            continue;
        }
        line++;
        if (c < 0x20u || c > 0x7Eu || line > KST_FILE_LINE_MAX) {
            return false;
        }
    }
    return n > 0u && g_text[n - 1u] == '\n';
}

TEST_CASE(every_written_file_is_ascii_in_short_lines_and_under_1024_bytes)
{
    kst_image_t longest;

    memset(&longest, 0, sizeof(longest));
    /* The fields share no bit, so the longest line of each adds up. */
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        size_t best = 0;
        unsigned best_raw = 0;

        if (kst_field(id)->name[0] == '\0' || id == KST_F_20K_SEL) {
            continue;
        }
        for (unsigned raw = 0; raw <= kst_field_raw_max(id); ++raw) {
            kst_image_t img;
            size_t n;

            memset(&img, 0, sizeof(img));
            CHECK(kst_field_set(&img, id, (uint16_t)raw));
            n = written(&img);
            CHECK(well_formed(n));
            if (n > best) {
                best = n;
                best_raw = raw;
            }
        }
        CHECK(kst_field_set(&longest, id, (uint16_t)best_raw));
    }
    CHECK(written(&longest) < 1024u);
    CHECK(written(&longest) > 808u);
    CHECK(KST_FILE_MAX == 3432u);
}

/* --- reading a written file back ---------------------------------------------- */

static bool round_trips(const kst_image_t *img)
{
    kst_file_t f;
    unsigned line = 99;
    const size_t n = written(img);

    memset(&f, 0xA5, sizeof(f));
    return parse(g_text, n, &f, &line) == KST_FILE_OK && line == 0u &&
           f.kind == KST_FILE_VERIFIED && same_image(&f.image, img) &&
           f.type == kst_type(img) && f.named == 0u && f.as_raw == 0u;
}

TEST_CASE(an_image_of_zeros_and_one_of_ff_read_back)
{
    kst_image_t img;

    memset(&img, 0, sizeof(img));
    CHECK(round_trips(&img));
    memset(&img, 0xFF, sizeof(img));
    CHECK(round_trips(&img));
}

TEST_CASE(every_field_at_both_ends_of_its_range_reads_back)
{
    for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        kst_image_t img = sim_bench_image();

        CHECK(kst_field_set(&img, id, 0));
        CHECK(round_trips(&img));
        CHECK(kst_field_set(&img, id, kst_field_raw_max(id)));
        CHECK(round_trips(&img));
    }
}

TEST_CASE(two_thousand_seeded_random_images_read_back)
{
    g_seed = 0xC0FFEEu;
    for (int i = 0; i < 2000; ++i) {
        const kst_image_t img = random_image();

        CHECK(round_trips(&img));
    }
}

/* g_text with every LF as CR LF.  Returns the length. */
static size_t to_crlf(size_t n)
{
    static char lf[KST_FILE_MAX];
    size_t out = 0;

    memcpy(lf, g_text, n);
    for (size_t i = 0; i < n; ++i) {
        if (lf[i] == '\n') {
            g_text[out++] = '\r';
        }
        g_text[out++] = lf[i];
    }
    return out;
}

TEST_CASE(a_file_with_cr_lf_on_every_line_reads_back_with_the_same_checksum)
{
    const kst_image_t img = sim_bench_image();
    kst_file_t f;
    const size_t n = to_crlf(written(&img));

    CHECK_EQ(n, 808 + 35);
    CHECK_EQ(parse(g_text, n, &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    CHECK(same_image(&f.image, &img));
}

TEST_CASE(lf_and_cr_lf_in_one_checked_file_are_refused_at_the_line)
{
    const kst_image_t img = sim_bench_image();
    kst_file_t f;
    unsigned line = 0;
    size_t n = written(&img);
    char *third = strstr(g_text, "Max. Duty = 96 %\n") + 16;

    /* One CR LF among LF. */
    memmove(third + 1, third, n - (size_t)(third - g_text));
    *third = '\r';
    CHECK_EQ(parse(g_text, n + 1u, &f, &line), KST_FILE_ERR_LINE_END);
    CHECK_EQ(line, 3);

    /* One LF among CR LF. */
    n = to_crlf(written(&img));
    third = strstr(g_text, "Max. Duty = 96 %\r\n") + 16;
    memmove(third, third + 1, n - (size_t)(third + 1 - g_text));
    CHECK_EQ(parse(g_text, n - 1u, &f, &line), KST_FILE_ERR_LINE_END);
    CHECK_EQ(line, 3);

    /* A CR inside a line. */
    n = written(&img);
    *strstr(g_text, "Duty") = '\r';
    CHECK_EQ(parse(g_text, n, &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 3);
}

/* --- a damaged file ----------------------------------------------------------- */

static unsigned lines_in(const char *text, size_t n)
{
    unsigned lines = 0;

    for (size_t i = 0; i < n; ++i) {
        if (text[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

static unsigned bits(kst_field_set_t set)
{
    unsigned n = 0;

    for (; set != 0u; set >>= 1) {
        n += (unsigned)(set & 1u);
    }
    return n;
}

TEST_CASE(a_file_cut_at_any_byte_never_reads_as_a_checked_file)
{
    static char full[KST_FILE_MAX];
    const kst_image_t img = sim_bench_image();
    const size_t n = written(&img);
    const size_t block = (size_t)(strstr(g_text, "registers") - g_text);
    kst_file_t lines_only;
    unsigned inside = 0;

    memcpy(full, g_text, n);
    /* The header, the type and the 27 field lines: what each line gives. */
    CHECK_EQ(parse(full, block, &lines_only, NULL), KST_FILE_OK);
    for (size_t cut = 0; cut + 1u < n; ++cut) {
        kst_file_t f;
        unsigned line = 0;
        const unsigned lines = lines_in(full, cut);
        const bool at_line_end = cut > 0u && full[cut - 1u] == '\n';
        const kst_file_err_t err = parse(full, cut, &f, &line);

        if (at_line_end && (lines <= 29u || lines == 34u)) {
            /* Header, type and whole field lines, or all of it but the
             * checksum line: a person's file with fewer fields. */
            const unsigned fields = lines == 34u ? 27u : lines - 2u;

            CHECK_EQ(err, KST_FILE_OK);
            CHECK_EQ(bits(f.named), lines < 2u ? 0u : fields);
        } else if (err == KST_FILE_OK) {
            /* Inside a line that shows its own end: the unit, the "[not
             * loaded]" or the whole type line is there.  The line counts. */
            CHECK(!at_line_end);
            CHECK(lines >= 1u && (lines <= 28u || lines == 33u));
            CHECK_EQ(bits(f.named), lines == 33u ? 27u : lines - 1u);
            inside++;
        } else {
            /* Inside a line, and inside the register block. */
            CHECK_EQ(err, KST_FILE_ERR_TRUNCATED);
            CHECK_EQ(line, lines + 1u);
        }
        if (err != KST_FILE_OK) {
            continue;
        }
        CHECK_EQ(f.kind, lines == 1u && at_line_end ? KST_FILE_EDITED_UNTYPED
                                                    : KST_FILE_EDITED_TYPED);
        /* No cut gives a number the whole line does not give. */
        for (unsigned i = 0; i < (unsigned)KST_F_COUNT; ++i) {
            if ((f.named & BIT(i)) != 0u && lines != 33u && lines != 34u) {
                CHECK_EQ(f.value[i], lines_only.value[i]);
                CHECK_EQ(f.line[i], lines_only.line[i]);
            }
        }
    }
    /* Every one of the 29 lines behind the header is taken without its
     * line end at least once: the type, 27 fields, the last register row. */
    CHECK(inside >= 29u);
}

TEST_CASE(a_cut_bench_file_states_how_many_of_its_27_values_it_holds)
{
    static char full[KST_FILE_MAX];
    const kst_image_t img = sim_bench_image();
    const size_t n = written(&img);
    unsigned seen = 0;

    memcpy(full, g_text, n);
    for (size_t cut = 1; cut <= n; ++cut) {
        const unsigned lines = lines_in(full, cut);
        kst_file_t f;
        kst_file_load_t r;

        if (full[cut - 1u] != '\n' || parse(full, cut, &f, NULL) != KST_FILE_OK) {
            continue;
        }
        memset(&r, 0xA5, sizeof(r));
        CHECK_EQ(kst_file_load(&f, &img, &img, kst_type(&img), &r), KST_FILE_LOAD_OK);
        CHECK_EQ(r.fields, 27);
        if (lines == 35u) {
            CHECK_EQ(r.kind, KST_FILE_VERIFIED);
            CHECK_EQ(r.named, 27);
        } else {
            CHECK(r.kind != KST_FILE_VERIFIED);
            CHECK_EQ(r.named, lines == 34u ? 27u : lines < 2u ? 0u : lines - 2u);
        }
        seen++;
    }
    /* Lines 1 to 29, line 34 and the whole file. */
    CHECK_EQ(seen, 31);
}

TEST_CASE(a_file_of_a_few_lines_states_the_values_it_names)
{
    const kst_image_t servo = sim_bench_image();
    kst_image_t other = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;

    CHECK_EQ(load("", &r), KST_FILE_LOAD_OK);
    CHECK_EQ(r.named, 0);
    CHECK_EQ(r.fields, 27);
    CHECK_EQ(load("Type: V6\nBoost = 300\nNeutral = 1500\n", &r),
             KST_FILE_LOAD_OK);
    CHECK_EQ(r.named, 2);
    CHECK_EQ(r.fields, 27);
    /* The counts are there for a file that is not loaded, too. */
    CHECK(kst_field_set(&other, KST_F_20K_SEL, 0));
    written(&other);
    CHECK_EQ(parse(g_text, strlen(g_text), &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r),
             KST_FILE_LOAD_TYPE_MISMATCH);
    CHECK_EQ(r.named, 27);
    CHECK_EQ(r.fields, 27);
}

TEST_CASE(a_changed_byte_never_reads_as_a_checked_file)
{
    static char full[KST_FILE_MAX];
    const kst_image_t img = sim_bench_image();
    const size_t n = written(&img);
    unsigned as_edited = 0;

    memcpy(full, g_text, n);
    for (size_t at = 0; at < n; ++at) {
        const char was = full[at];

        for (unsigned v = 0; v < 256u; ++v) {
            kst_file_t f;
            kst_file_load_t r;

            if ((char)v == was) {
                continue;
            }
            full[at] = (char)v;
            if (parse(full, n, &f, NULL) != KST_FILE_OK) {
                continue;
            }
            /* The checksum line became a comment: a person's file with
             * the values as written. */
            as_edited++;
            CHECK_EQ(v, '#');
            CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
            CHECK_EQ(kst_file_load(&f, &img, &img, kst_type(&img), &r), KST_FILE_LOAD_OK);
            CHECK(r.editable == 0u);
            CHECK(r.skipped == 0u);
        }
        full[at] = was;
    }
    /* The first byte of the checksum line and the line end before it. */
    CHECK_EQ(as_edited, 2);
}

TEST_CASE(a_wrong_checksum_is_refused_and_a_removed_one_reads_as_edited)
{
    const kst_image_t img = sim_bench_image();
    kst_file_t f;
    unsigned line = 0;
    size_t n = written(&img);

    /* A value changed under a standing checksum line. */
    *strstr(g_text, "96 %") = '8';
    CHECK_EQ(parse(g_text, n, &f, &line), KST_FILE_ERR_CHECKSUM);
    CHECK_EQ(line, 35);
    /* The same text without the line. */
    n -= 18u;
    CHECK_EQ(parse(g_text, n, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
    CHECK_EQ(line, 0);

    /* The checksum line itself: lower case, short, another word case. */
    n = written(&img);
    g_text[n - 2u] = (char)(g_text[n - 2u] == '2' ? 'a' : 'b');
    CHECK_EQ(parse(g_text, n, &f, &line), KST_FILE_ERR_CHECKSUM);
    n = written(&img);
    g_text[n - 2u] = '\n';
    CHECK_EQ(parse(g_text, n - 1u, &f, &line), KST_FILE_ERR_CHECKSUM);
    n = written(&img);
    *strstr(g_text, "checksum") = 'C';
    CHECK_EQ(parse(g_text, n, &f, &line), KST_FILE_ERR_CHECKSUM);
    CHECK_EQ(line, 35);
}

TEST_CASE(bytes_after_the_checksum_line_are_refused)
{
    const kst_image_t img = sim_bench_image();
    kst_file_t f;
    unsigned line = 0;
    const size_t n = written(&img);

    strcpy(g_text + n, "x");
    CHECK_EQ(parse(g_text, n + 1u, &f, &line), KST_FILE_ERR_TRAILING);
    CHECK_EQ(line, 36);
    strcpy(g_text + n, "x\n");
    CHECK_EQ(parse(g_text, n + 2u, &f, &line), KST_FILE_ERR_TRAILING);
    CHECK_EQ(line, 36);
    strcpy(g_text + n, "\n\n# end\n");
    CHECK_EQ(parse(g_text, n + 8u, &f, &line), KST_FILE_ERR_TRAILING);
    CHECK_EQ(line, 38);
    strcpy(g_text + n, "\r");
    CHECK_EQ(parse(g_text, n + 1u, &f, &line), KST_FILE_ERR_TRAILING);
    strcpy(g_text + n, "Max. Duty = 50 %\n");
    CHECK_EQ(parse(g_text, n + 17u, &f, &line), KST_FILE_ERR_TRAILING);
}

TEST_CASE(blank_lines_after_the_checksum_line_are_taken)
{
    static const char *const tail[] = {
        "\n", "\n\n\n", " \n", "\t \n\n", "\r\n", "\r\n\r\n", "  ", "\n\t",
    };
    const kst_image_t img = sim_bench_image();

    for (unsigned crlf = 0; crlf < 2u; ++crlf) {
        for (size_t i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) {
            kst_file_t f;
            unsigned line = 9;
            size_t n = written(&img);

            if (crlf != 0u) {
                n = to_crlf(n);
            }
            strcpy(g_text + n, tail[i]);
            CHECK_EQ(parse(g_text, n + strlen(tail[i]), &f, &line),
                     KST_FILE_OK);
            CHECK_EQ(line, 0);
            CHECK_EQ(f.kind, KST_FILE_VERIFIED);
            CHECK(same_image(&f.image, &img));
        }
    }
}

TEST_CASE(a_checked_file_is_taken_without_its_last_line_end)
{
    const kst_image_t img = sim_bench_image();
    kst_file_t f;
    unsigned line = 9;
    size_t n = written(&img);

    CHECK_EQ(g_text[n - 1u], '\n');
    CHECK_EQ(parse(g_text, n - 1u, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    CHECK(same_image(&f.image, &img));
    /* One digit short it is a cut file, and with a wrong digit a damaged
     * one: the checksum is held as strictly as with the line end. */
    CHECK_EQ(parse(g_text, n - 2u, &f, &line), KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 35);
    g_text[n - 2u] = (char)(g_text[n - 2u] == '2' ? '3' : '2');
    CHECK_EQ(parse(g_text, n - 1u, &f, &line), KST_FILE_ERR_CHECKSUM);
    CHECK_EQ(line, 35);
    /* CR LF on every line, the last one without either. */
    n = to_crlf(written(&img));
    CHECK_EQ(parse(g_text, n - 2u, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    /* A line before the checksum line never lacks its end. */
    n = written(&img);
    *strstr(g_text, "\nchecksum") = ' ';
    CHECK(parse(g_text, n - 1u, &f, &line) != KST_FILE_OK);
}

#define BOM "\xEF\xBB\xBF"

TEST_CASE(a_byte_order_mark_in_front_of_the_header_is_skipped)
{
    static char text[KST_FILE_MAX + 8];
    const kst_image_t img = sim_bench_image();
    kst_file_t f;
    unsigned line = 9;
    size_t n = written(&img);

    /* A checked file stays one: the mark is no part of the checksum. */
    strcpy(text, BOM);
    memcpy(text + 3, g_text, n);
    CHECK_EQ(parse(text, n + 3u, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    CHECK(same_image(&f.image, &img));
    n = to_crlf(n);
    memcpy(text + 3, g_text, n);
    CHECK_EQ(parse(text, n + 3u, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    /* An edited one, with the line numbers it has without the mark. */
    strcpy(text, BOM HEAD "Boost = 300\nNeutral = 99999\n");
    CHECK_EQ(parse(text, strlen(text), &f, &line), KST_FILE_ERR_VALUE);
    CHECK_EQ(line, 3);
    strcpy(text, BOM HEAD "Boost = 300\n");
    CHECK_EQ(parse(text, strlen(text), &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_UNTYPED);
    CHECK(f.named == BIT(KST_F_BOOST));
    /* A file of the mark only, and an empty one, hold no header. */
    CHECK_EQ(parse(BOM, 3, &f, &line), KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 1);
    CHECK_EQ(parse("", 0, &f, &line), KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 1);
    CHECK_EQ(parse(BOM "\n", 4, &f, &line), KST_FILE_ERR_NOT_KST);
    CHECK_EQ(line, 1);
    /* Part of a mark, a second one and one at another place are bytes
     * outside the format. */
    strcpy(text, "\xEF\xBB" HEAD "Boost = 300\n");
    CHECK_EQ(parse(text, strlen(text), &f, &line), KST_FILE_ERR_NOT_KST);
    strcpy(text, BOM BOM HEAD "Boost = 300\n");
    CHECK_EQ(parse(text, strlen(text), &f, &line), KST_FILE_ERR_NOT_KST);
    strcpy(text, HEAD BOM "Boost = 300\n");
    CHECK_EQ(parse(text, strlen(text), &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 2);
    /* The size limit counts the mark. */
    memset(text, ' ', sizeof(text));
    memcpy(text, BOM, 3);
    CHECK_EQ(parse(text, KST_FILE_MAX + 1u, &f, &line),
             KST_FILE_ERR_OVERSIZED);
}

TEST_CASE(a_text_that_is_no_kst_file_is_refused_at_line_1)
{
    kst_file_t f;
    unsigned line = 0;

    CHECK_EQ(parse("hello\n", 6, &f, &line), KST_FILE_ERR_NOT_KST);
    CHECK_EQ(line, 1);
    CHECK_EQ(parse("KST servo\n", 10, &f, &line), KST_FILE_ERR_NOT_KST);
    CHECK_EQ(parse("kst servo configuration, format 1\n", 34, &f, &line),
             KST_FILE_ERR_NOT_KST);
    /* The start of the header and no line end: a cut file. */
    CHECK_EQ(parse("KST servo", 9, &f, &line), KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 1);
    CHECK_EQ(parse("", 0, &f, &line), KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 1);
}

TEST_CASE(a_version_that_is_no_number_and_a_newer_one_are_told_apart)
{
    kst_file_t f;
    unsigned line = 0;
#define V(text) parse(text, strlen(text), &f, &line)

    CHECK_EQ(V("KST servo configuration, format \n"), KST_FILE_ERR_VERSION);
    CHECK_EQ(line, 1);
    CHECK_EQ(V("KST servo configuration, format x\n"), KST_FILE_ERR_VERSION);
    CHECK_EQ(V("KST servo configuration, format 1 \n"), KST_FILE_ERR_VERSION);
    CHECK_EQ(V("KST servo configuration, format 0\n"), KST_FILE_ERR_VERSION);
    CHECK_EQ(V("KST servo configuration, format -1\n"), KST_FILE_ERR_VERSION);
    CHECK_EQ(V("KST servo configuration, format 3\n"), KST_FILE_ERR_NEWER);
    CHECK_EQ(line, 1);
    CHECK_EQ(V("KST servo configuration, format 99999999999999999999\n"),
             KST_FILE_ERR_NEWER);
    CHECK_EQ(V("KST servo configuration, format 1\n"), KST_FILE_OK);
    CHECK_EQ(f.version, 1u);
    CHECK_EQ(V("KST servo configuration, format 1\r\n"), KST_FILE_OK);
    CHECK_EQ(V("KST servo configuration, format 01\n"), KST_FILE_OK);
    CHECK_EQ(V("KST servo configuration, format 2\n"), KST_FILE_OK);
    CHECK_EQ(f.version, 2u);
    CHECK_EQ(V("KST servo configuration, format 02\r\n"), KST_FILE_OK);
#undef V
}

TEST_CASE(null_and_a_text_above_the_size_limit_are_refused)
{
    static char big[KST_FILE_MAX + 1];
    kst_file_t f;
    unsigned line = 7;

    memset(&f, 0x5A, sizeof(f));
    CHECK_EQ(kst_file_parse(NULL, 0, &f, &line), KST_FILE_ERR_ARG);
    CHECK_EQ(line, 0);
    CHECK_EQ(kst_file_parse(HEAD, 34, NULL, NULL), KST_FILE_ERR_ARG);

    memset(big, '#', sizeof(big));
    memcpy(big, HEAD, 34);
    big[KST_FILE_MAX - 1u] = '\n';
    big[KST_FILE_MAX] = '\n';
    line = 7;
    CHECK_EQ(parse(big, KST_FILE_MAX + 1u, &f, &line), KST_FILE_ERR_OVERSIZED);
    CHECK_EQ(line, 0);
    /* Refusals leave the result as it was. */
    CHECK_EQ((unsigned char)((const char *)&f)[0], 0x5A);
    CHECK_EQ(parse(big, KST_FILE_MAX, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_UNTYPED);
}

TEST_CASE(a_checked_file_is_held_to_the_written_form_line_by_line)
{
    unsigned line = 0;

    /* The body as written passes with the checksum this suite computes. */
    CHECK_EQ(parse_sealed(bench_body("Type", "Type"), &line), KST_FILE_OK);

    CHECK_EQ(parse_sealed(bench_body("Type: V6", "Type: V7"), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 2);
    CHECK_EQ(parse_sealed(bench_body("Type: V6", "type: V6"), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(parse_sealed(bench_body("Type: V6", "Type: V6 "), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(parse_sealed(bench_body("Type: V6", "TYPE V6"), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 2);
    /* Format 2 leaves the line out where the type is not stated; format 1
     * has it. */
    CHECK_EQ(parse_sealed(bench_body("Type: V6\n", ""), &line), KST_FILE_OK);
    CHECK_EQ(parse_sealed(bench_body("format 2\nType: V6\n", "format 1\n"),
                          &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 2);

    /* A field line is not interpreted, but it is ASCII and short. */
    CHECK_EQ(parse_sealed(bench_body("Boost = 300.0 us", "anything"), &line),
             KST_FILE_OK);
    CHECK_EQ(parse_sealed(bench_body("Boost", "Bo\x01st"), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 4);
    CHECK_EQ(parse_sealed(bench_body("Boost", "Bo\x7Fst"), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(parse_sealed(bench_body("Boost = 300.0 us",
        "Boost = 300.0 us                                                 "),
        &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 4);
    CHECK_EQ(parse_sealed(bench_body("Boost = 300.0 us",
        "Boost = 300.0 us                                                "),
        &line), KST_FILE_OK);

    /* A second checksum line among the fields. */
    CHECK_EQ(parse_sealed(bench_body("Boost = 300.0 us", " Checksum 0"),
                          &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 4);
}

TEST_CASE(a_register_count_other_than_32_is_refused)
{
    unsigned line = 0;

    CHECK_EQ(parse_sealed(bench_body("registers 32", "registers 31"), &line),
             KST_FILE_ERR_REG_COUNT);
    CHECK_EQ(line, 30);
    CHECK_EQ(parse_sealed(bench_body("registers 32", "registers 33"), &line),
             KST_FILE_ERR_REG_COUNT);
    CHECK_EQ(parse_sealed(bench_body("registers 32", "registers x"), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(parse_sealed(bench_body("registers 32", "registers 32 "), &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 30);
}

TEST_CASE(a_register_line_of_a_checked_file_is_held_to_the_written_form)
{
    unsigned line = 0;
#define ROW(to) parse_sealed(bench_body("08: 41 A4 0D 80 53 07 14 00", to), &line)

    CHECK_EQ(ROW("08: 41 a4 0D 80 53 07 14 00"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 32);
    CHECK_EQ(ROW("08: 41  A4 0D 80 53 07 14 00"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("08:41 A4 0D 80 53 07 14 00"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("09: 41 A4 0D 80 53 07 14 00"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("08: 41 A4 0D 80 53 07 14"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("08: 41 A4 0D 80 53 07 14 00 00"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("08: 41 A4 0D 80 53 07 14 0"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("08: 41 A4 0D 80 53 07 14 0G"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("08 41 A4 0D 80 53 07 14 00"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("08"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("0"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(ROW("Boost = 1"), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 32);
    /* Other registers under a fitting checksum: the file as it is. */
    CHECK_EQ(ROW("08: 41 A4 0D 80 53 07 15 00"), KST_FILE_OK);
#undef ROW
}

/* @p body with @p from replaced by @p to once, in place. */
static char *swap_once(char *body, const char *from, const char *to)
{
    static char out[KST_FILE_MAX];
    const char *at = strstr(body, from);

    if (at == NULL) {
        return body;
    }
    memcpy(out, body, (size_t)(at - body));
    strcpy(out + (at - body), to);
    strcat(out, at + strlen(from));
    strcpy(body, out);
    return body;
}

TEST_CASE(a_format_1_checked_file_whose_type_line_contradicts_its_registers_is_refused)
{
    static char body[KST_FILE_MAX];
    kst_file_t f;
    unsigned line = 0;

    CHECK_EQ(parse_sealed(bench_body("format 2\nType: V6", "format 1\nType: V8"),
                          &line), KST_FILE_ERR_TYPE);
    CHECK_EQ(line, 2);
    /* 20k_sel cleared in register 04 under "Type: V6". */
    strcpy(body, bench_body("format 2", "format 1"));
    CHECK_EQ(parse_sealed(swap_once(body, "00: 00 F5 F5 14 48",
                                    "00: 00 F5 F5 14 08"), &line),
             KST_FILE_ERR_TYPE);
    CHECK_EQ(line, 2);
    /* As written then, it reads, and its type is no statement: none. */
    CHECK_EQ(parse(g_text, sealed(bench_body("format 2", "format 1")), &f,
                   &line), KST_FILE_OK);
    CHECK_EQ(f.version, 1u);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    CHECK_EQ(f.type, KST_TYPE_NONE);
}

TEST_CASE(a_format_2_checked_file_states_its_type_whatever_its_registers_hold)
{
    static char body[KST_FILE_MAX];
    kst_file_t f;
    unsigned line = 0;

    /* A V8 servo with 20k_sel set, as the bench found V8 wing servos. */
    CHECK_EQ(parse(g_text, sealed(bench_body("Type: V6", "Type: V8")), &f,
                   &line), KST_FILE_OK);
    CHECK_EQ(f.version, 2u);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    CHECK_EQ(f.type, KST_TYPE_V8);
    CHECK_EQ(kst_type(&f.image), KST_TYPE_V6);
    /* And V6 over 20k_sel clear. */
    strcpy(body, bench_body("Type: V6", "Type: V6"));
    CHECK_EQ(parse(g_text, sealed(swap_once(body, "00: 00 F5 F5 14 48",
                                            "00: 00 F5 F5 14 08")),
                   &f, &line), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_V6);
    CHECK_EQ(kst_type(&f.image), KST_TYPE_V8);
    /* No type line: none stated. */
    CHECK_EQ(parse(g_text, sealed(bench_body("Type: V6\n", "")), &f, &line),
             KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    CHECK_EQ(f.type, KST_TYPE_NONE);
}

TEST_CASE(the_formatter_writes_the_type_the_caller_states_and_none_for_none)
{
    kst_image_t img = sim_bench_image();
    kst_file_t f;

    /* 20k_sel set, the caller says V8. */
    CHECK(kst_file_format(&img, KST_TYPE_V8, g_text, KST_FILE_MAX) > 0u);
    CHECK(strstr(g_text, "format 2\nType: V8\n") != NULL);
    CHECK_EQ(kst_file_parse(g_text, strlen(g_text), &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_V8);
    memset(g_text, 0, sizeof(g_text));
    CHECK_EQ(kst_file_format(&img, KST_TYPE_NONE, g_text, KST_FILE_MAX), 799u);
    CHECK(strstr(g_text, "Type") == NULL);
    CHECK(strstr(g_text, "format 2\nMax. Duty = 96 %\n") != NULL);
    CHECK_EQ(kst_file_parse(g_text, strlen(g_text), &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_VERIFIED);
    CHECK_EQ(f.type, KST_TYPE_NONE);
    CHECK(same_image(&f.image, &img));
    /* A value that is neither type writes none. */
    memset(g_text, 0, sizeof(g_text));
    CHECK_EQ(kst_file_format(&img, (kst_type_t)7, g_text, KST_FILE_MAX), 799u);
    CHECK(strstr(g_text, "Type") == NULL);
}

/* --- a file a person wrote ---------------------------------------------------- */

TEST_CASE(a_file_of_a_few_lines_gives_the_fields_it_names)
{
    kst_file_t f;
    unsigned line = 9;

    CHECK_EQ(edited("Type: V6\n"
                    "Neutral = 1520 us\n"
                    "Dead Band = 3.0 us\n"
                    "Reversion = 1\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(line, 0);
    CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
    CHECK_EQ(f.type, KST_TYPE_V6);
    CHECK(f.named == (BIT(KST_F_NEUTRAL) | BIT(KST_F_DEAD_BAND) |
                      BIT(KST_F_REVERSION)));
    CHECK_EQ(f.value[KST_F_NEUTRAL], 3555);
    CHECK_EQ(f.value[KST_F_DEAD_BAND], 50);
    CHECK_EQ(f.value[KST_F_REVERSION], 1);
    CHECK_EQ(f.line[KST_F_NEUTRAL], 3);
    CHECK_EQ(f.line[KST_F_DEAD_BAND], 4);
    CHECK_EQ(f.line[KST_F_REVERSION], 5);
    CHECK(f.as_raw == 0u);
}

TEST_CASE(names_values_units_comments_and_blank_lines_are_read_leniently)
{
    kst_file_t f;

    CHECK_EQ(edited("# a servo for the left wing\r\n"
                    "\n"
                    "   \t \n"
                    "  max.duty=90\n"
                    "BOOST\t=\t300,0US   # as before\n"
                    "stretch = X 2\n"
                    "Soft_Start : 0\n"
                    "DeadBand = 2 us\n"
                    "pulse lower = 900.0\n"
                    "Pulse  Upper = 2100 u s\n"
                    "Uncontrolled Time = 2,5 S\n"
                    "SPD = 50.0 %\n"
                    "Left Angle = raw 120\n"
                    "Right Angle = RAW  130, as measured\n"
                    "Neutral = 1500.0 us [a note]\n"
                    "# caf\xC3\xA9 \x01\xFF\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_UNTYPED);
    CHECK_EQ(f.type, KST_TYPE_NONE);
    CHECK_EQ(bits(f.named), 12);
    CHECK_EQ(f.value[KST_F_DUTY], 229);
    CHECK_EQ(f.line[KST_F_DUTY], 5);
    CHECK_EQ(f.value[KST_F_BOOST], 20);
    CHECK_EQ(f.value[KST_F_STRETCH], 1);
    CHECK_EQ(f.value[KST_F_SOFT_START], 0);
    CHECK_EQ(f.value[KST_F_DEAD_BAND], 55);
    CHECK_EQ(f.value[KST_F_PULSE_LOWER], 1125);
    CHECK_EQ(f.value[KST_F_PULSE_UPPER], 2625);
    CHECK_EQ(f.value[KST_F_UNCONT_TIME], 25);
    CHECK_EQ(f.value[KST_F_SPD], 500);
    CHECK_EQ(f.value[KST_F_LEFT_ANGLE], 120);
    CHECK_EQ(f.value[KST_F_RIGHT_ANGLE], 130);
    CHECK_EQ(f.line[KST_F_NEUTRAL], 16);
    CHECK(f.as_raw == (BIT(KST_F_LEFT_ANGLE) | BIT(KST_F_RIGHT_ANGLE)));
}

TEST_CASE(a_written_file_without_its_checksum_line_changes_nothing_in_its_servo)
{
    g_seed = 0xBEEFu;
    for (int i = 0; i < 2000; ++i) {
        const kst_image_t img = i == 0 ? sim_bench_image() : random_image();
        kst_file_t f;
        kst_file_load_t r;
        const size_t n = written(&img) - 18u;

        CHECK_EQ(parse(g_text, n, &f, NULL), KST_FILE_OK);
        CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
        CHECK_EQ(f.type, kst_type(&img));
        CHECK(same_image(&f.image, &img));
        CHECK_EQ(bits(f.named), 27);
        (void)kst_file_load(&f, &img, &img, kst_type(&img), &r);
        CHECK(r.editable == 0u);
        CHECK(r.skipped == 0u);
        CHECK(same_image(&r.target, &img));
    }
}

TEST_CASE(an_unknown_name_and_a_name_given_twice_are_refused_with_the_line)
{
    kst_file_t f;
    unsigned line = 0;

    CHECK_EQ(edited("Neutral = 1500\nNeutrol = 1500\n", &f, &line),
             KST_FILE_ERR_NAME);
    CHECK_EQ(line, 3);
    CHECK_EQ(edited(" = 5\n", &f, &line), KST_FILE_ERR_NAME);
    CHECK_EQ(line, 2);
    /* The unnamed bits and the duty copy have no name to give. */
    CHECK_EQ(edited("= 5\n", &f, &line), KST_FILE_ERR_NAME);
    CHECK_EQ(edited("00: 00 F5 F5 14 48 77 04 65\n", &f, &line),
             KST_FILE_ERR_NAME);

    CHECK_EQ(edited("Neutral = 1500\n\n# x\nNEUTRAL = 1501\n", &f, &line),
             KST_FILE_ERR_DUPLICATE);
    CHECK_EQ(line, 5);
    CHECK_EQ(edited("Type: V6\nBoost = 300\ntype = v6\n", &f, &line),
             KST_FILE_ERR_DUPLICATE);
    CHECK_EQ(line, 4);
}

TEST_CASE(a_value_the_field_does_not_take_is_refused_with_the_line)
{
    static const char *const bad[] = {
        "Neutral = \n",
        "Neutral = us\n",
        "Neutral = -1500\n",
        "Neutral = 1500.\n",
        "Neutral = 1500.x\n",
        "Neutral = 1500.25\n",
        "Neutral = 1500 ms\n",
        "Neutral = 1500 us us\n",
        "Neutral = 100\n",              /* below what the field encodes */
        "Neutral = 99999999999999\n",
        "Neutral = 1500 note]\n",
        "Neutral = 1500 ]\n",
        "Neutral = raw\n",
        "Neutral = raw x\n",
        "Neutral = raw 8192\n",         /* 13 bits hold 8191 */
        "Neutral = raw 10 us\n",
        "Neutral = x1500\n",
        "Max. Duty = 50.0 %\n",         /* whole per cent */
        "Max. Duty = 101 %\n",
        "Stretch = 3\n",                /* no power of two */
        "Stretch = x\n",
        "Reversion = 2\n",
        "Reversion = yes\n",
        "prot_pot_err = 3 of 11 [not loaded]\n",
        "node_addr = raw 256\n",
        "Type: V7\n",
        "Type:\n",
    };
    kst_file_t f;
    unsigned line = 0;

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char body[80] = "# line 2\n";

        strcat(body, bad[i]);
        CHECK_EQ(edited(body, &f, &line), KST_FILE_ERR_VALUE);
        CHECK_EQ(line, 3);
    }
    CHECK_EQ(edited("Neutral = raw 8191\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(edited("Stretch = 32\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(edited("prot_pot_err = 3 of 12\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(edited("node_addr = raw 255\n", &f, &line), KST_FILE_OK);
}

TEST_CASE(a_line_that_is_no_field_and_a_byte_outside_ascii_are_refused)
{
    kst_file_t f;
    unsigned line = 0;

    CHECK_EQ(edited("Neutral 1500\n", &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 2);
    CHECK_EQ(edited("registers\n", &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(edited("registersx32\n", &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(edited("registrars 32\n", &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(edited("\n\nNeutr\xE4l = 1500\n", &f, &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 4);
    CHECK_EQ(edited("Neutral = 1500\x01\n", &f, &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(edited("Neutral = 1500\x7F\n", &f, &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(edited("Neutral = 15\r00\n", &f, &line), KST_FILE_ERR_MALFORMED);
}

#define BLOCK_V6 "registers 32\n"                \
                 "00: 00 F5 F5 14 48 77 04 65\n" \
                 "08: 41 A4 0D 80 53 07 14 00\n" \
                 "10: 96 96 00 01 01 00 00 00\n" \
                 "18: 00 00 00 0A 00 10 00 00\n"

TEST_CASE(a_last_line_that_ends_in_a_number_or_a_name_is_refused_as_truncated)
{
    static const char *const cut[] = {
        "Boost = 300\nNeutral = 15",
        "Boost = 300\nNeutral = 1500",
        "Boost = 300\nNeutral = 1500 ",
        "Boost = 300\nNeutral = 1500 u",
        "Boost = 300\nNeutral = raw 15",
        "Boost = 300\nNeutral = ",
        "Boost = 300\nNeutral",
        "Boost = 300\nNeut",
        "Boost = 300\nNeutral = 1500 [not loa",
        "Boost = 300\nType: V",
        "Boost = 300\nregisters 3",
        "Boost = 300\nNeutral = 99999 us",
        "Boost = 300\nNeutral = 1500\x01",
    };
    kst_file_t f;
    unsigned line = 0;

    for (size_t i = 0; i < sizeof(cut) / sizeof(cut[0]); ++i) {
        line = 0;
        CHECK_EQ(edited(cut[i], &f, &line), KST_FILE_ERR_TRUNCATED);
        CHECK_EQ(line, 3);
    }
    /* The register block is whole or the file is cut. */
    CHECK_EQ(edited("registers 32", &f, &line), KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 2);
    CHECK_EQ(edited("registers 32\n"
                    "00: 00 F5 F5 14 48 77 04 65", &f, &line),
             KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 3);
    CHECK_EQ(edited("registers 32\n"
                    "00: 00 F5 F5 14 48 77 04 6", &f, &line),
             KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 3);
    CHECK_EQ(edited("Boost = 300\nNeutral = 1500\n", &f, &line), KST_FILE_OK);
}

TEST_CASE(a_last_line_that_shows_its_end_is_taken_without_a_line_end)
{
    static const char *const whole[] = {
        "Boost = 300\nNeutral = 1500 us",
        "Boost = 300\nNeutral = 1500us",
        "Boost = 300\nNeutral = 1500 us  ",
        "Boost = 300\nNeutral = 1500 [x]",
        "Boost = 300\nNeutral = 1500 us [not loaded]",
        "Boost = 300\nNeutral = 1500 # mid",
        "Boost = 300\nNeutral = 1500#",
        "Boost = 300\nNeutral = raw 1500, as read",
    };
    kst_file_t f, ref;
    unsigned line = 9;

    CHECK_EQ(edited("Boost = 300\nNeutral = 1500\n", &ref, NULL), KST_FILE_OK);
    for (size_t i = 0; i < sizeof(whole) / sizeof(whole[0]); ++i) {
        CHECK_EQ(edited(whole[i], &f, &line), KST_FILE_OK);
        CHECK_EQ(line, 0);
        CHECK(f.named == ref.named);
        CHECK_EQ(f.line[KST_F_NEUTRAL], 3);
    }
    CHECK_EQ(edited("Boost = 300\nNeutral = 1500 us", &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.value[KST_F_NEUTRAL], ref.value[KST_F_NEUTRAL]);
    /* A comment, a type line, blanks and a whole register block. */
    CHECK_EQ(edited("Boost = 300\n# end", &f, &line), KST_FILE_OK);
    CHECK(f.named == BIT(KST_F_BOOST));
    CHECK_EQ(edited("Boost = 300\n  \t", &f, &line), KST_FILE_OK);
    CHECK_EQ(edited("Boost = 300\nType: V6", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
    CHECK_EQ(f.type, KST_TYPE_V6);
    CHECK_EQ(edited("registers 32\n"
                    "00: 00 F5 F5 14 48 77 04 65\n"
                    "08: 41 A4 0D 80 53 07 14 00\n"
                    "10: 96 96 00 01 01 00 00 00\n"
                    "18: 00 00 00 0A 00 10 00 00", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_NONE);
    /* CR LF on the lines before it. */
    CHECK_EQ(edited("Boost = 300\r\nNeutral = 1500 us", &f, &line),
             KST_FILE_OK);
    CHECK_EQ(f.value[KST_F_NEUTRAL], ref.value[KST_F_NEUTRAL]);
    /* An error in a whole line keeps its name with the line end. */
    CHECK_EQ(edited("Boost = 300\nNeutral = 99999 us\n", &f, &line),
             KST_FILE_ERR_VALUE);
    CHECK_EQ(line, 3);
}

TEST_CASE(a_person_s_file_with_cr_lf_reads_as_the_one_with_lf)
{
    kst_file_t lf, crlf;

    CHECK_EQ(edited("Type: V6\nBoost = 300\n\nNeutral = 1500 us\n", &lf, NULL),
             KST_FILE_OK);
    CHECK_EQ(edited("Type: V6\r\nBoost = 300\r\n\r\nNeutral = 1500 us\r\n",
                    &crlf, NULL), KST_FILE_OK);
    CHECK_EQ(memcmp(&lf, &crlf, sizeof(lf)), 0);
    CHECK_EQ(edited("Type: V6\r\nBoost = 300\nNeutral = 1500 us", &crlf, NULL),
             KST_FILE_OK);
    CHECK_EQ(memcmp(&lf, &crlf, sizeof(lf)) == 0, false);
    CHECK(crlf.named == lf.named);
}

TEST_CASE(every_raw_max_duty_loads_as_written_once_the_checksum_line_is_gone)
{
    static const char *const tail[] = { "", "\n", "\r\n" };
    unsigned shown_as[256];
    unsigned distinct = 0;

    for (unsigned raw = 0; raw < 256u; ++raw) {
        const kst_image_t img = bench_with(KST_F_DUTY, raw);
        const kst_image_t servo = bench_with(KST_F_DUTY, raw ^ 0x80u);
        int32_t shown = 0;
        kst_file_t f;
        kst_file_load_t r;

        CHECK_EQ(kst_field_get(&img, KST_F_DUTY), raw);
        CHECK(kst_field_display(KST_F_DUTY, (uint16_t)raw, &shown));
        shown_as[raw] = (unsigned)shown;
        distinct += raw == 0u || shown_as[raw] != shown_as[raw - 1u] ? 1u : 0u;
        /* The checksum line deleted, and its line end with it or not. */
        for (size_t t = 0; t < sizeof(tail) / sizeof(tail[0]); ++t) {
            size_t n = written(&img) - 18u;
            if (t == 0u) {
                n--;
            } else {
                strcpy(g_text + n, tail[t]);
                n += strlen(tail[t]);
            }
            CHECK_EQ(parse(g_text, n, &f, NULL), KST_FILE_OK);
            CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
            CHECK_EQ(f.value[KST_F_DUTY], (int32_t)raw);
            CHECK((f.as_raw & BIT(KST_F_DUTY)) != 0u);
            if (kst_file_load(&f, &servo, &servo, kst_type(&servo), &r) == KST_FILE_LOAD_OK) {
                CHECK(r.editable == BIT(KST_F_DUTY));
                CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY), raw);
                CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY_COPY), raw);
            } else {
                CHECK_EQ(r.verdict, KST_FILE_LOAD_HARD_LIMIT);
            }
            /* Into the servo it was read from it is no change.  A duty
             * below the hard limit is refused for the servo's own value. */
            (void)kst_file_load(&f, &img, &img, kst_type(&img), &r);
            CHECK(r.editable == 0u);
            CHECK(same_image(&r.target, &img));
        }
    }
    /* Fewer numbers than raw values: the line alone does not hold them. */
    CHECK(distinct < 256u);
}

TEST_CASE(a_changed_line_wins_over_the_register_block)
{
    const kst_image_t img = bench_with(KST_F_DUTY, 244);
    const kst_image_t servo = sim_bench_image();
    char *at;
    kst_file_t f;
    kst_file_load_t r;
    size_t n;

    /* 244 and 245 both show as 96%.  The line unchanged: the block's 244. */
    n = written(&img) - 18u;
    CHECK_EQ(parse(g_text, n, &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.value[KST_F_DUTY], 244);
    /* The line changed to 95%: its number, converted as a typed one. */
    at = strstr(g_text, "Max. Duty = 96 %");
    CHECK(at != NULL);
    at[13] = '5';
    CHECK_EQ(parse(g_text, n, &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.value[KST_F_DUTY], 242);
    CHECK((f.as_raw & BIT(KST_F_DUTY)) == 0u);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == BIT(KST_F_DUTY));
    CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY), 242);
    /* "raw" on the line is the line's. */
    n = written(&img) - 18u;
    at = strstr(g_text, "Max. Duty = 96 %");
    memcpy(at, "Max. Duty=raw 99", 16);
    CHECK_EQ(parse(g_text, n, &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.value[KST_F_DUTY], 99);
    /* Without the line the block gives nothing. */
    n = written(&img) - 18u;
    at = strstr(g_text, "Max. Duty = 96 %");
    memcpy(at, "#", 1);
    CHECK_EQ(parse(g_text, n, &f, NULL), KST_FILE_OK);
    CHECK((f.named & BIT(KST_F_DUTY)) == 0u);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == 0u);
    /* Without the block the line converts: 96% is raw 245. */
    CHECK_EQ(edited("Type: V6\nMax. Duty = 96 %\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.value[KST_F_DUTY], 245);
    CHECK((f.as_raw & BIT(KST_F_DUTY)) == 0u);
}

/* A file without a checksum in format 1: HEAD1 and @p body. */
static kst_file_err_t edited1(const char *body, kst_file_t *out,
                              unsigned *line)
{
    static char text[KST_FILE_MAX + 1];

    strcpy(text, HEAD1);
    strcat(text, body);
    return parse(text, strlen(text), out, line);
}

TEST_CASE(the_type_of_an_edited_file_comes_from_its_type_line_alone)
{
    const kst_image_t bench = sim_bench_image();
    kst_file_t f;
    unsigned line = 0;

    /* Neither. */
    CHECK_EQ(edited("Boost = 300\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_UNTYPED);
    CHECK_EQ(f.type, KST_TYPE_NONE);

    /* The line alone, in each spelling. */
    CHECK_EQ(edited("Type: V8\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
    CHECK_EQ(f.type, KST_TYPE_V8);
    CHECK_EQ(edited("  type = v6  # the 20 kHz one\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_V6);
    CHECK_EQ(edited("TYPE:V 8\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_V8);

    /* The block alone, as written and as a person retyped it: 20k_sel is a
     * register, and the block states no type. */
    CHECK_EQ(edited(BLOCK_V6, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_UNTYPED);
    CHECK_EQ(f.type, KST_TYPE_NONE);
    CHECK(same_image(&f.image, &bench));
    CHECK(f.named == 0u);
    CHECK_EQ(edited("Registers   32   # all of them\n"
                    "\n"
                    "00:00f5F514 48 77\t04 65\n"
                    "# half way\n"
                    "08: 41 a4 0d 80 53 07 14 00\r\n"
                    "10: 96 96 00 01 01 00 00 00\n"
                    "  18: 00 00 00 0A 00 10 00 00  \n"
                    "Boost = 300\n", &f, &line), KST_FILE_OK);
    CHECK(same_image(&f.image, &bench));
    CHECK_EQ(f.type, KST_TYPE_NONE);
    CHECK(f.named == BIT(KST_F_BOOST));

    /* Both, in either order: the line states the type, whatever 20k_sel
     * in the block holds. */
    CHECK_EQ(edited("Type: V6\n" BLOCK_V6, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_V6);
    CHECK_EQ(edited(BLOCK_V6 "Type: V6\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(edited("# x\nType: V8\n" BLOCK_V6, &f, &line), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
    CHECK_EQ(f.type, KST_TYPE_V8);
    CHECK_EQ(edited(BLOCK_V6 "Type: V8\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_V8);

    /* Format 1: the type it names is the one 20k_sel gave, so a line that
     * contradicts the block is refused at the line, and the file states
     * no type. */
    CHECK_EQ(edited1("# x\nType: V8\n" BLOCK_V6, &f, &line), KST_FILE_ERR_TYPE);
    CHECK_EQ(line, 3);
    CHECK_EQ(edited1(BLOCK_V6 "Type: V8\n", &f, &line), KST_FILE_ERR_TYPE);
    CHECK_EQ(line, 7);
    CHECK_EQ(edited1("Type: V6\n" BLOCK_V6 "Boost = 300\n", &f, &line),
             KST_FILE_OK);
    CHECK_EQ(f.version, 1u);
    CHECK_EQ(f.kind, KST_FILE_EDITED_UNTYPED);
    CHECK_EQ(f.type, KST_TYPE_NONE);
    CHECK(f.named == BIT(KST_F_BOOST));
    CHECK_EQ(edited1("Type: V8\nBoost = 300\n", &f, &line), KST_FILE_OK);
    CHECK_EQ(f.type, KST_TYPE_NONE);
}

TEST_CASE(a_register_block_that_is_cut_doubled_or_miscounted_is_refused)
{
    kst_file_t f;
    unsigned line = 0;

    CHECK_EQ(edited("registers 32\n"
                    "00: 00 F5 F5 14 48 77 04 65\n"
                    "08: 41 A4 0D 80 53 07 14 00\n", &f, &line),
             KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(line, 5);
    CHECK_EQ(edited("registers 32\n", &f, &line), KST_FILE_ERR_TRUNCATED);
    CHECK_EQ(edited(BLOCK_V6 BLOCK_V6, &f, &line), KST_FILE_ERR_DUPLICATE);
    CHECK_EQ(line, 7);
    CHECK_EQ(edited("registers 31\n", &f, &line), KST_FILE_ERR_REG_COUNT);
    CHECK_EQ(line, 2);
    CHECK_EQ(edited("registers 32 bytes\n", &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(edited("registers 32\n"
                    "00: 00 F5 F5 14 48 77 04 65\n"
                    "Boost = 300\n", &f, &line), KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 4);
    CHECK_EQ(edited("registers 32\n"
                    "08: 41 A4 0D 80 53 07 14 00\n", &f, &line),
             KST_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 3);
}

/* --- a file against a servo --------------------------------------------------- */

TEST_CASE(a_file_of_another_type_is_denied_whatever_its_kind)
{
    const kst_image_t v6 = sim_bench_image();
    kst_image_t v8 = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;

    CHECK(kst_field_set(&v8, KST_F_20K_SEL, 0));

    /* Checked. */
    CHECK_EQ(parse(g_text, written(&v8), &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, kst_type(&v6), &r), KST_FILE_LOAD_TYPE_MISMATCH);
    CHECK_EQ(r.verdict, KST_FILE_LOAD_TYPE_MISMATCH);
    CHECK_EQ(r.kind, KST_FILE_VERIFIED);
    CHECK_EQ(r.file_type, KST_TYPE_V8);
    CHECK_EQ(r.servo_type, KST_TYPE_V6);
    CHECK(r.editable == 0u);
    CHECK(r.skipped == 0u);
    CHECK(r.rules == 0u);
    CHECK(same_image(&r.target, &v6));
    CHECK_EQ(kst_file_load(&f, &v8, &v8, kst_type(&v8), &r), KST_FILE_LOAD_OK);
    CHECK_EQ(r.servo_type, KST_TYPE_V8);

    /* Edited with a register block and no type line: no type, into
     * either. */
    CHECK_EQ(edited(BLOCK_V6 "Boost = 500\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v8, &v8, KST_TYPE_V8, &r), KST_FILE_LOAD_OK);
    CHECK_EQ(r.kind, KST_FILE_EDITED_UNTYPED);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_V6, &r), KST_FILE_LOAD_OK);

    /* Edited, type from the line. */
    CHECK_EQ(edited("Type: V8\nBoost = 500\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, kst_type(&v6), &r), KST_FILE_LOAD_TYPE_MISMATCH);
    CHECK_EQ(kst_file_load(&f, &v8, &v8, kst_type(&v8), &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == BIT(KST_F_BOOST));

    /* Edited, no type: the values apply to either servo. */
    CHECK_EQ(edited("Boost = 500\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, kst_type(&v6), &r), KST_FILE_LOAD_OK);
    CHECK_EQ(r.kind, KST_FILE_EDITED_UNTYPED);
    CHECK_EQ(r.file_type, KST_TYPE_NONE);
    CHECK_EQ(r.servo_type, KST_TYPE_V6);
    CHECK_EQ(kst_file_load(&f, &v8, &v8, kst_type(&v8), &r), KST_FILE_LOAD_OK);
    CHECK_EQ(kst_field_get(&r.target, KST_F_BOOST), 36);
}

TEST_CASE(a_load_holds_the_file_to_the_type_the_caller_states_not_to_20k_sel)
{
    const kst_image_t v6 = sim_bench_image();   /* 20k_sel set */
    kst_file_t f;
    kst_file_load_t r;

    /* A V8 file into a servo whose 20k_sel is set, stated V8: taken. */
    CHECK_EQ(edited("Type: V8\nBoost = 500\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_V8, &r), KST_FILE_LOAD_OK);
    CHECK_EQ(r.servo_type, KST_TYPE_V8);
    CHECK(r.editable == BIT(KST_F_BOOST));
    /* Stated V6: refused, whatever the registers say. */
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_V6, &r),
             KST_FILE_LOAD_TYPE_MISMATCH);
    CHECK_EQ(r.file_type, KST_TYPE_V8);
    CHECK_EQ(r.servo_type, KST_TYPE_V6);
    /* Not stated: no file that states a type is taken, at either type. */
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_NONE, &r),
             KST_FILE_LOAD_TYPE_MISMATCH);
    CHECK_EQ(r.servo_type, KST_TYPE_NONE);
    CHECK(same_image(&r.target, &v6));
    CHECK_EQ(edited("Type: V6\nBoost = 500\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_NONE, &r),
             KST_FILE_LOAD_TYPE_MISMATCH);
    /* One that states none is. */
    CHECK_EQ(edited("Boost = 500\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_NONE, &r), KST_FILE_LOAD_OK);
    /* A file of format 1 states none: it loads at either type. */
    CHECK_EQ(edited1("Type: V8\nBoost = 500\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_V6, &r), KST_FILE_LOAD_OK);
    CHECK_EQ(kst_file_load(&f, &v6, &v6, KST_TYPE_V8, &r), KST_FILE_LOAD_OK);
}

TEST_CASE(a_load_without_an_argument_is_refused_and_writes_nothing)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;

    CHECK_EQ(edited("Boost = 500\n", &f, NULL), KST_FILE_OK);
    memset(&r, 0x5A, sizeof(r));
    CHECK_EQ(kst_file_load(NULL, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_ARG);
    CHECK_EQ(kst_file_load(&f, NULL, &servo, KST_TYPE_V6, &r), KST_FILE_LOAD_ARG);
    CHECK_EQ(kst_file_load(&f, &servo, NULL, kst_type(&servo), &r), KST_FILE_LOAD_ARG);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), NULL), KST_FILE_LOAD_ARG);
    CHECK_EQ((unsigned char)((const char *)&r)[0], 0x5A);
}

TEST_CASE(a_checked_file_loads_its_editable_fields_and_reports_the_others)
{
    const kst_image_t servo = sim_bench_image();
    kst_image_t other = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;

    /* Editable. */
    CHECK(kst_field_edit(&other, KST_F_DUTY, 200));
    CHECK(kst_field_edit(&other, KST_F_NEUTRAL, 3500));
    CHECK(kst_field_edit(&other, KST_F_REVERSION, 1));
    /* The protection block, the mode, Lock, the pairing value, unnamed
     * bits. */
    CHECK(kst_field_set(&other, KST_F_PROT1_EN, 1));
    CHECK(kst_field_set(&other, KST_F_PROT1_TIME, 50));
    CHECK(kst_field_set(&other, KST_F_PROT_POT_ERR, 7));
    CHECK(kst_field_set(&other, KST_F_GYRO, 1));
    CHECK(kst_field_set(&other, KST_F_LOCK, 0));
    CHECK(kst_field_set(&other, KST_F_NODE_ADDR, 99));
    CHECK(kst_field_set(&other, KST_F_R06, 0x55));

    CHECK_EQ(parse(g_text, written(&other), &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == (BIT(KST_F_DUTY) | BIT(KST_F_NEUTRAL) |
                         BIT(KST_F_REVERSION)));
    CHECK(r.skipped == (BIT(KST_F_PROT1_EN) | BIT(KST_F_PROT1_TIME) |
                        BIT(KST_F_PROT_POT_ERR) | BIT(KST_F_GYRO) |
                        BIT(KST_F_LOCK) | BIT(KST_F_NODE_ADDR) |
                        BIT(KST_F_R06)));
    CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY), 200);
    CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY_COPY), 200);
    CHECK_EQ(kst_field_get(&r.target, KST_F_NEUTRAL), 3500);
    CHECK_EQ(kst_field_get(&r.target, KST_F_REVERSION), 1);
    /* Every bit outside the 3 fields and the duty copy is the servo's. */
    {
        kst_image_t back = r.target;

        CHECK(kst_field_edit(&back, KST_F_DUTY, 245));
        CHECK(kst_field_edit(&back, KST_F_NEUTRAL,
                             kst_field_get(&servo, KST_F_NEUTRAL)));
        CHECK(kst_field_edit(&back, KST_F_REVERSION, 0));
        CHECK(same_image(&back, &servo));
    }
    /* The target passes the check an edit made by hand goes through. */
    CHECK((kst_limits_edit(&servo, &r.target, 0, NULL) & KST_RULES_HARD) == 0u);
    CHECK(r.rules == kst_limits_edit(&servo, &r.target, 0, NULL));
    CHECK((r.rules & RULE(KST_S_REVERSION_CHANGED)) != 0u);
    CHECK_EQ(r.hard_field, KST_F_COUNT);
    CHECK_EQ(r.hard_line, 0);
}

TEST_CASE(a_file_equal_to_the_servo_changes_and_asks_nothing)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;

    CHECK_EQ(parse(g_text, written(&servo), &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == 0u);
    CHECK(r.skipped == 0u);
    CHECK(r.rules == 0u);
    CHECK(same_image(&r.target, &servo));
}

TEST_CASE(a_locked_field_in_an_edited_file_is_reported_and_never_taken)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_load_t r;

    CHECK_EQ(load("prot1en = 1\n"
                  "20k_sel = 1\n"                 /* as the servo */
                  "Lock = 0\n"
                  "prot1_time = 5.0 s\n"
                  "prot_pot_err = 0 of 12\n"      /* as the servo */
                  "node_addr = raw 17\n"
                  "pot_sam_times = raw 0\n"       /* as the servo */
                  "Servo / Gyro Servo = 1\n"
                  "Boost = 500\n", &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == BIT(KST_F_BOOST));
    CHECK(r.skipped == (BIT(KST_F_PROT1_EN) | BIT(KST_F_LOCK) |
                        BIT(KST_F_PROT1_TIME) | BIT(KST_F_NODE_ADDR) |
                        BIT(KST_F_GYRO)));
    {
        kst_image_t back = r.target;

        CHECK(kst_field_edit(&back, KST_F_BOOST, 20));
        CHECK(same_image(&back, &servo));
    }
    CHECK((r.rules & KST_RULES_HARD) == 0u);
}

TEST_CASE(a_locked_value_no_raw_value_shows_is_refused_with_the_line)
{
    kst_file_t f;
    unsigned line = 0;

    CHECK_EQ(edited("Boost = 500\n20k_sel = 2\n", &f, &line),
             KST_FILE_ERR_VALUE);
    CHECK_EQ(line, 3u);
    CHECK_EQ(edited("node_addr = 256\n", &f, &line), KST_FILE_ERR_VALUE);
    CHECK_EQ(line, 2u);
    CHECK_EQ(edited("prot1_pwm = 999 %\n", &f, NULL), KST_FILE_ERR_VALUE);
    CHECK_EQ(edited("prot1_pwm = 98 %\n", &f, NULL), KST_FILE_ERR_VALUE);
    CHECK_EQ(edited("prot_pot_err = 13 of 12\n", &f, NULL), KST_FILE_ERR_VALUE);
    CHECK_EQ(edited("prot_pwm_err = 9 of 8\n", &f, NULL), KST_FILE_ERR_VALUE);
    CHECK_EQ(edited("Lock = 2\n", &f, NULL), KST_FILE_ERR_VALUE);

    /* The largest value each of them shows is taken. */
    CHECK_EQ(edited("20k_sel = 1\nnode_addr = 255\nprot1_pwm = 97 %\n"
                    "prot_pot_err = 12 of 12\nprot_pwm_err = 8 of 8\n"
                    "Lock = 1\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(f.value[KST_F_NODE_ADDR], 255);
    CHECK_EQ(f.value[KST_F_PROT1_PWM], 97);
    CHECK_EQ(f.value[KST_F_PROT_POT_ERR], 12);
}

TEST_CASE(a_locked_value_is_compared_as_displayed_and_not_as_raw)
{
    /* Raw 7 of prot_pot_err shows as 3 of 12: a file that says 7 of 12
     * differs from that servo, one that says 3 of 12 does not. */
    kst_image_t servo = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;

    CHECK(kst_field_set(&servo, KST_F_PROT_POT_ERR, 7));
    CHECK_EQ(edited("prot_pot_err = 7 of 12\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.skipped == BIT(KST_F_PROT_POT_ERR));
    CHECK(r.editable == 0u);
    CHECK(same_image(&r.target, &servo));

    CHECK_EQ(edited("prot_pot_err = 3 of 12\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.skipped == 0u);

    CHECK_EQ(edited("prot_pot_err = raw 7\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.skipped == 0u);

    CHECK_EQ(edited("prot_pot_err = raw 3\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.skipped == BIT(KST_F_PROT_POT_ERR));
}

TEST_CASE(a_locked_field_whose_servo_value_has_no_display_counts_as_differing)
{
    /* No field of that kind is locked in this table; the editable Stretch
     * at index 6 on the servo stands for the comparison. */
    const kst_image_t servo = bench_with(KST_F_STRETCH, 6);
    kst_file_t f;
    kst_file_load_t r;

    CHECK_EQ(edited("Stretch = x2\n", &f, NULL), KST_FILE_OK);
    (void)kst_file_load(&f, &servo, &servo, kst_type(&servo), &r);
    CHECK(r.editable == BIT(KST_F_STRETCH));
    CHECK_EQ(edited("Stretch = raw 6\n", &f, NULL), KST_FILE_OK);
    (void)kst_file_load(&f, &servo, &servo, kst_type(&servo), &r);
    CHECK(r.editable == 0u);
}

TEST_CASE(a_number_the_servo_already_shows_changes_nothing)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_load_t r;

    /* Raw 245 shows as 96%, and 96% converts to raw 245. */
    CHECK_EQ(load("Max. Duty = 96 %\n", &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == 0u);
    /* Raw 244 shows as 96% too: typed as a number it is no change, as a
     * raw value it is one. */
    {
        const kst_image_t at_244 = bench_with(KST_F_DUTY, 244);
        kst_file_t f;

        CHECK_EQ(edited("Max. Duty = 96\n", &f, NULL), KST_FILE_OK);
        CHECK_EQ(f.value[KST_F_DUTY], 245);
        CHECK_EQ(kst_file_load(&f, &at_244, &at_244, kst_type(&at_244), &r), KST_FILE_LOAD_OK);
        CHECK(r.editable == 0u);
        CHECK(same_image(&r.target, &at_244));
        CHECK_EQ(edited("Max. Duty = raw 245\n", &f, NULL), KST_FILE_OK);
        CHECK_EQ(kst_file_load(&f, &at_244, &at_244, kst_type(&at_244), &r), KST_FILE_LOAD_OK);
        CHECK(r.editable == BIT(KST_F_DUTY));
        CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY_COPY), 245);
    }
    CHECK_EQ(load("Max. Duty = 95 %\n", &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == BIT(KST_F_DUTY));
    CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY), 242);
    CHECK(!same_image(&r.target, &servo));
}

TEST_CASE(a_value_outside_16_bits_in_a_file_built_by_hand_is_not_applied)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;

    CHECK_EQ(edited("Boost = 500\nLeft Angle = 100\nSPD = 2.0\n", &f, NULL),
             KST_FILE_OK);
    f.value[KST_F_BOOST] = -1;
    f.value[KST_F_LEFT_ANGLE] = 0x10000;
    f.value[KST_F_SPD] = 0xFFFF;     /* in 16 bits, above the field's 12 */
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK(r.editable == 0u);
    CHECK(same_image(&r.target, &servo));
}

TEST_CASE(soft_rules_come_back_as_the_set_an_edit_by_hand_gets)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_load_t r;

    CHECK_EQ(load("Max. Duty = 98 %\n"
                  "Soft_Start = 0\n"
                  "Dead Band = 0.8 us\n"
                  "Reversion = 1\n"
                  "Neutral = 1530 us\n", &r), KST_FILE_LOAD_OK);
    CHECK(r.rules == (RULE(KST_S_DUTY_RAISED) | RULE(KST_S_SOFT_START_OFF) |
                      RULE(KST_S_DEAD_BAND_NARROWED) |
                      RULE(KST_S_DEAD_BAND_BELOW_1US) |
                      RULE(KST_S_REVERSION_CHANGED) |
                      RULE(KST_S_NEUTRAL_MOVED)));
    CHECK(r.rules == kst_limits_edit(&servo, &r.target, 0, NULL));
    CHECK_EQ(r.hard_field, KST_F_COUNT);
    CHECK_EQ(kst_field_get(&r.target, KST_F_DUTY), 250);

    /* One step inside each soft limit asks nothing. */
    CHECK_EQ(load("Dead Band = 2.2 us\nNeutral = 1519.9 us\n", &r),
             KST_FILE_LOAD_OK);
    CHECK(r.editable == (BIT(KST_F_DEAD_BAND) | BIT(KST_F_NEUTRAL)));
    CHECK(r.rules == 0u);
}

/* Load "name = raw <raw>" on line 3 into the bench servo. */
static kst_file_verdict_t load_raw(kst_field_id_t id, unsigned raw,
                                   kst_file_load_t *out)
{
    char body[96];
    char digits[8];
    unsigned n = 0;

    strcpy(body, "# line 2\n");
    strcat(body, kst_field(id)->name);
    strcat(body, " = raw ");
    do {
        digits[n++] = (char)('0' + raw % 10u);
        raw /= 10u;
    } while (raw != 0u);
    while (n > 0u) {
        const size_t at = strlen(body);

        body[at] = digits[--n];
        body[at + 1u] = '\0';
    }
    strcat(body, "\n");
    return load(body, out);
}

/* Walk one field away from the bench value until the core refuses the
 * image; hold the file to the last value it takes and the first it does
 * not.  Returns the hard rules met. */
static kst_rules_t walk_to_the_limit(kst_field_id_t id, int step)
{
    const kst_image_t servo = sim_bench_image();
    int raw = kst_field_get(&servo, id);

    for (;;) {
        const int next = raw + step;
        kst_image_t img;
        kst_rules_t hard;
        kst_file_load_t r;

        if (next < 0 || next > (int)kst_field_raw_max(id)) {
            return 0;
        }
        img = bench_with(id, (unsigned)next);
        hard = kst_limits_edit(&servo, &img, 0, NULL) & KST_RULES_HARD;
        if (hard == 0u) {
            raw = next;
            continue;
        }
        /* At the limit. */
        if (raw != (int)kst_field_get(&servo, id)) {
            CHECK_EQ(load_raw(id, (unsigned)raw, &r), KST_FILE_LOAD_OK);
            CHECK(r.editable == BIT(id));
            CHECK_EQ(kst_field_get(&r.target, id), raw);
            CHECK((r.rules & KST_RULES_HARD) == 0u);
        }
        /* One step beyond. */
        CHECK_EQ(load_raw(id, (unsigned)next, &r), KST_FILE_LOAD_HARD_LIMIT);
        CHECK_EQ(r.verdict, KST_FILE_LOAD_HARD_LIMIT);
        CHECK((r.rules & KST_RULES_HARD) == hard);
        CHECK_EQ(r.hard_field, id);
        CHECK_EQ(r.hard_line, 3);
        CHECK(r.editable == BIT(id));
        CHECK(same_image(&r.target, &servo));
        return hard;
    }
}

TEST_CASE(every_hard_limit_takes_its_value_and_refuses_one_step_beyond)
{
    static const kst_field_id_t fields[] = {
        KST_F_DUTY, KST_F_BOOST, KST_F_STRETCH, KST_F_SPD, KST_F_UNCONT_POS,
        KST_F_UNCONT_TIME, KST_F_PULSE_LOWER, KST_F_PULSE_UPPER,
        KST_F_NEUTRAL, KST_F_LEFT_ANGLE, KST_F_RIGHT_ANGLE,
    };
    kst_rules_t met = 0;

    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        met |= walk_to_the_limit(fields[i], 1);
        met |= walk_to_the_limit(fields[i], -1);
    }
    /* Every hard rule one editable field reaches from the bench image. */
    CHECK(met == (RULE(KST_H_DUTY_HIGH) | RULE(KST_H_DUTY_LOW) |
                  RULE(KST_H_BOOST_HIGH) | RULE(KST_H_STRETCH_INDEX) |
                  RULE(KST_H_SPD_HIGH) | RULE(KST_H_UNCONT_POS_SPAN) |
                  RULE(KST_H_UNCONT_TIME_LOW) | RULE(KST_H_UNCONT_TIME_HIGH) |
                  RULE(KST_H_LOWER_LOW) | RULE(KST_H_LOWER_MARGIN) |
                  RULE(KST_H_UPPER_HIGH) | RULE(KST_H_UPPER_MARGIN) |
                  RULE(KST_H_NEUTRAL_LOW) | RULE(KST_H_NEUTRAL_HIGH) |
                  RULE(KST_H_LEFT_ZERO) | RULE(KST_H_RIGHT_ZERO)));
}

static bool refused_for(const char *body, kst_rule_t rule,
                        kst_field_id_t field, unsigned line)
{
    kst_file_load_t r;

    return load(body, &r) == KST_FILE_LOAD_HARD_LIMIT &&
           (r.rules & RULE(rule)) != 0u && r.hard_field == field &&
           r.hard_line == line;
}

static bool taken(const char *body)
{
    kst_file_load_t r;

    return load(body, &r) == KST_FILE_LOAD_OK &&
           (r.rules & KST_RULES_HARD) == 0u;
}

TEST_CASE(the_hard_limits_between_fields_hold_for_values_in_their_unit)
{
    /* 500us and 2500us for the end points, with Uncontrolled Pos and
     * Neutral where the other rules leave them. */
    CHECK(taken("Pulse Lower = 500 us\n"));
    CHECK(refused_for("Pulse Lower = 499.2 us\n", KST_H_LOWER_LOW,
                      KST_F_PULSE_LOWER, 2));
    CHECK(taken("Pulse Upper = 2500 us\n"));
    CHECK(refused_for("Pulse Upper = 2500.8 us\n", KST_H_UPPER_HIGH,
                      KST_F_PULSE_UPPER, 2));
    /* Uncontrolled Pos follows the end points to 500 and 2500us. */
    CHECK(taken("Pulse Lower = 500 us\nUncontrolled Pos = 500 us\n"));
    CHECK(refused_for("Pulse Lower = 500 us\nUncontrolled Pos = 499.2 us\n",
                      KST_H_UNCONT_POS_LOW, KST_F_UNCONT_POS, 3));
    CHECK(taken("Pulse Upper = 2500 us\nUncontrolled Pos = 2500 us\n"));
    CHECK(refused_for("Pulse Upper = 2500 us\nUncontrolled Pos = 2500.8 us\n",
                      KST_H_UNCONT_POS_HIGH, KST_F_UNCONT_POS, 3));
    /* 400us between the end points; the margins to Neutral break first
     * unless Neutral sits in the middle. */
    CHECK(taken("Pulse Lower = 1300 us\nPulse Upper = 1700 us\n"));
    CHECK(refused_for("Pulse Lower = 1300.8 us\nPulse Upper = 1700 us\n"
                      "Uncontrolled Pos = 1500 us\n",
                      KST_H_SPAN, KST_F_PULSE_LOWER, 2));
    /* spd_sel on needs SPD at 5% or more. */
    CHECK(taken("spd_sel = 1\nSPD = 5.0 %\n"));
    /* The servo's SPD is 1.0%: taking the SPD line back leaves the rule
     * broken, taking the spd_sel line back clears it. */
    CHECK(refused_for("SPD = 4.9 %\nspd_sel = 1\n", KST_H_SPD_SEL_LOW_SPD,
                      KST_F_SPD_SEL, 3));
    CHECK(refused_for("spd_sel = 1\n", KST_H_SPD_SEL_LOW_SPD,
                      KST_F_SPD_SEL, 2));
    /* The percentages of Max. Duty. */
    CHECK(taken("Max. Duty = 98 %\n"));
    CHECK(refused_for("# 1\n# 2\nMax. Duty = 99 %\n", KST_H_DUTY_HIGH,
                      KST_F_DUTY, 4));
    /* 10% converts to raw 25, one below the limit of 26. */
    CHECK(taken("Max. Duty = 11 %\n"));
    CHECK(refused_for("Max. Duty = 10 %\n", KST_H_DUTY_LOW, KST_F_DUTY, 2));
}

TEST_CASE(the_refusal_names_the_first_line_at_fault_among_several)
{
    kst_file_load_t r;

    /* Harmless lines before and after the one at fault. */
    CHECK_EQ(load("Reversion = 1\n"
                  "Dead Band = 3.0 us\n"
                  "Boost = raw 237\n"
                  "Left Angle = 140\n", &r), KST_FILE_LOAD_HARD_LIMIT);
    CHECK_EQ(r.hard_field, KST_F_BOOST);
    CHECK_EQ(r.hard_line, 4);
    CHECK(r.editable == (BIT(KST_F_REVERSION) | BIT(KST_F_DEAD_BAND) |
                         BIT(KST_F_BOOST) | BIT(KST_F_LEFT_ANGLE)));

    /* Two at fault: the one on the earlier line, whatever the table
     * order. */
    CHECK_EQ(load("Left Angle = 0\nMax. Duty = 99 %\n", &r),
             KST_FILE_LOAD_HARD_LIMIT);
    CHECK_EQ(r.hard_field, KST_F_LEFT_ANGLE);
    CHECK_EQ(r.hard_line, 2);
    CHECK((r.rules & RULE(KST_H_LEFT_ZERO)) != 0u);
    CHECK((r.rules & RULE(KST_H_DUTY_HIGH)) != 0u);
    CHECK_EQ(load("Max. Duty = 99 %\nLeft Angle = 0\n", &r),
             KST_FILE_LOAD_HARD_LIMIT);
    CHECK_EQ(r.hard_field, KST_F_DUTY);
    CHECK_EQ(r.hard_line, 2);
}

TEST_CASE(a_checked_file_beyond_a_hard_limit_is_refused_whole)
{
    const kst_image_t servo = sim_bench_image();
    kst_image_t other = bench_with(KST_F_BOOST, 237);
    kst_file_t f;
    kst_file_load_t r;

    CHECK(kst_field_edit(&other, KST_F_REVERSION, 1));
    CHECK_EQ(parse(g_text, written(&other), &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_HARD_LIMIT);
    CHECK((r.rules & RULE(KST_H_BOOST_HIGH)) != 0u);
    CHECK_EQ(r.hard_field, KST_F_BOOST);
    CHECK_EQ(r.hard_line, 0);
    CHECK(same_image(&r.target, &servo));
}

TEST_CASE(a_servo_that_breaks_a_hard_rule_itself_has_no_line_at_fault)
{
    const kst_image_t servo = bench_with(KST_F_LEFT_ANGLE, 0);
    kst_file_t f;
    kst_file_load_t r;

    CHECK_EQ(edited("Reversion = 1\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_HARD_LIMIT);
    CHECK((r.rules & RULE(KST_H_LEFT_ZERO)) != 0u);
    CHECK_EQ(r.hard_field, KST_F_COUNT);
    CHECK_EQ(r.hard_line, 0);
    CHECK(same_image(&r.target, &servo));
}

TEST_CASE(the_limits_compare_with_the_backup_the_caller_passes)
{
    const kst_image_t servo = sim_bench_image();
    const kst_image_t backup = bench_with(KST_F_BOOST, 40);
    kst_file_t f;
    kst_file_load_t r;

    /* 500us is above the servo's 300us and at the backup's 550us less
     * one step: raised against the servo, not against the backup. */
    CHECK_EQ(edited("Boost = 537.5 us\n", &f, NULL), KST_FILE_OK);
    CHECK_EQ(kst_file_load(&f, &servo, &servo, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK((r.rules & RULE(KST_S_BOOST_RAISED)) != 0u);
    CHECK_EQ(kst_file_load(&f, &servo, &backup, kst_type(&servo), &r), KST_FILE_LOAD_OK);
    CHECK((r.rules & RULE(KST_S_BOOST_RAISED)) == 0u);
    CHECK_EQ(kst_field_get(&r.target, KST_F_BOOST), 39);
}

/* --- arbitrary text ----------------------------------------------------------- */

static void parse_and_load(const char *text, size_t n)
{
    const kst_image_t servo = sim_bench_image();
    kst_file_t f;
    kst_file_load_t r;
    unsigned line = 0;

    if (parse(text, n, &f, &line) == KST_FILE_OK) {
        (void)kst_file_load(&f, &servo, &servo, kst_type(&servo), &r);
        /* A loaded target never breaks a hard rule and never changes a
         * bit outside the editable fields and the duty copy. */
        if (r.verdict == KST_FILE_LOAD_OK) {
            CHECK((kst_limits_edit(&servo, &r.target, 0, NULL) &
                   KST_RULES_HARD) == 0u);
        } else {
            CHECK(same_image(&r.target, &servo));
        }
    } else {
        CHECK(line <= lines_in(text, n) + 1u);
    }
}

/*
 * kst_file_checksum_line() is the test the reader makes, exported for the
 * text editor: the first word "checksum" in any case, after spaces and
 * tabs, ended by a space, a tab, '=', ':' or the line's end.
 */
TEST_CASE(a_checksum_line_is_named_as_the_reader_names_it)
{
    static const char *const yes[] = {
        "checksum 1234ABCD", "CHECKSUM 1234ABCD", "ChEcKsUm 1234ABCD",
        "checksum=1234ABCD", "checksum:1234ABCD", "checksum\t1234ABCD",
        "  \tchecksum 1234ABCD", "checksum", "  checksum", "checksum =",
    };
    static const char *const no[] = {
        "", "  ", "checksums 1234ABCD", "check sum 1234ABCD", "# checksum",
        "checksum1234ABCD", "xchecksum", "checksu", "=checksum",
    };
    for (size_t i = 0u; i < sizeof(yes) / sizeof(yes[0]); ++i) {
        CHECK(kst_file_checksum_line(yes[i], strlen(yes[i])));
    }
    for (size_t i = 0u; i < sizeof(no) / sizeof(no[0]); ++i) {
        CHECK(!kst_file_checksum_line(no[i], strlen(no[i])));
    }
    CHECK(!kst_file_checksum_line(NULL, 0u));
    /* The length bounds the line: "checksum" cut short is none. */
    CHECK(!kst_file_checksum_line("checksum", 7u));
    CHECK(kst_file_checksum_line("checksum 1", 8u));
}

TEST_CASE(forty_thousand_random_texts_are_read_without_a_fault)
{
    static const char *const words[] = {
        "Max. Duty", "Boost", "Stretch", "Neutral", "Pulse Lower", "SPD",
        "Type", "registers 32", "checksum", "raw", " = ", ": ", "\n", "\r\n",
        "#", "[", "]", "x", "us", "%", "of 12", "V6", "V8", ",", ".", " ",
        "\t", "00:", "08:", "10:", "18:", " F5", " 00", "1500", "99999999999",
        "prot1en", "node_addr", "0", "1",
    };
    static char text[KST_FILE_MAX + 1];

    g_seed = 0xF00Du;
    /* Bytes of any value, with and without the header. */
    for (int i = 0; i < 10000; ++i) {
        const size_t n = rnd() % 300u;
        size_t at = 0;

        if ((i & 1) != 0) {
            memcpy(text, HEAD, 34);
            at = 34;
        }
        for (; at < n; ++at) {
            text[at] = (char)rnd();
        }
        parse_and_load(text, at);
    }
    /* Words of the format in a random order. */
    for (int i = 0; i < 20000; ++i) {
        size_t at = 34;
        const unsigned count = rnd() % 60u;

        memcpy(text, HEAD, 34);
        for (unsigned k = 0; k < count; ++k) {
            const char *w = words[rnd() % (sizeof(words) / sizeof(words[0]))];

            memcpy(text + at, w, strlen(w));
            at += strlen(w);
        }
        parse_and_load(text, at);
    }
    /* A written file with up to 4 bytes changed, with and without its
     * checksum line. */
    for (int i = 0; i < 10000; ++i) {
        const kst_image_t img = random_image();
        size_t n = written(&img);
        const unsigned changes = 1u + rnd() % 4u;

        if ((i & 1) != 0) {
            n -= 18u;
        }
        for (unsigned k = 0; k < changes; ++k) {
            g_text[rnd() % n] = (char)rnd();
        }
        parse_and_load(g_text, n);
    }
}

int main(void)
{
    RUN(the_bench_image_is_written_as_the_documented_file);
    RUN(the_checksum_is_crc_32_over_the_lines_before_it);
    RUN(a_type_8_image_states_type_v8_and_stretch_6_its_raw_value);
    RUN(the_formatter_writes_nothing_past_its_buffer_and_refuses_a_short_one);
    RUN(every_written_file_is_ascii_in_short_lines_and_under_1024_bytes);
    RUN(an_image_of_zeros_and_one_of_ff_read_back);
    RUN(every_field_at_both_ends_of_its_range_reads_back);
    RUN(two_thousand_seeded_random_images_read_back);
    RUN(a_file_with_cr_lf_on_every_line_reads_back_with_the_same_checksum);
    RUN(lf_and_cr_lf_in_one_checked_file_are_refused_at_the_line);
    RUN(a_file_cut_at_any_byte_never_reads_as_a_checked_file);
    RUN(a_cut_bench_file_states_how_many_of_its_27_values_it_holds);
    RUN(a_file_of_a_few_lines_states_the_values_it_names);
    RUN(a_changed_byte_never_reads_as_a_checked_file);
    RUN(a_wrong_checksum_is_refused_and_a_removed_one_reads_as_edited);
    RUN(bytes_after_the_checksum_line_are_refused);
    RUN(blank_lines_after_the_checksum_line_are_taken);
    RUN(a_checked_file_is_taken_without_its_last_line_end);
    RUN(a_byte_order_mark_in_front_of_the_header_is_skipped);
    RUN(a_text_that_is_no_kst_file_is_refused_at_line_1);
    RUN(a_version_that_is_no_number_and_a_newer_one_are_told_apart);
    RUN(null_and_a_text_above_the_size_limit_are_refused);
    RUN(a_checked_file_is_held_to_the_written_form_line_by_line);
    RUN(a_register_count_other_than_32_is_refused);
    RUN(a_register_line_of_a_checked_file_is_held_to_the_written_form);
    RUN(a_format_1_checked_file_whose_type_line_contradicts_its_registers_is_refused);
    RUN(a_format_2_checked_file_states_its_type_whatever_its_registers_hold);
    RUN(the_formatter_writes_the_type_the_caller_states_and_none_for_none);
    RUN(a_file_of_a_few_lines_gives_the_fields_it_names);
    RUN(names_values_units_comments_and_blank_lines_are_read_leniently);
    RUN(a_written_file_without_its_checksum_line_changes_nothing_in_its_servo);
    RUN(an_unknown_name_and_a_name_given_twice_are_refused_with_the_line);
    RUN(a_value_the_field_does_not_take_is_refused_with_the_line);
    RUN(a_line_that_is_no_field_and_a_byte_outside_ascii_are_refused);
    RUN(a_last_line_that_ends_in_a_number_or_a_name_is_refused_as_truncated);
    RUN(a_last_line_that_shows_its_end_is_taken_without_a_line_end);
    RUN(a_person_s_file_with_cr_lf_reads_as_the_one_with_lf);
    RUN(every_raw_max_duty_loads_as_written_once_the_checksum_line_is_gone);
    RUN(a_changed_line_wins_over_the_register_block);
    RUN(the_type_of_an_edited_file_comes_from_its_type_line_alone);
    RUN(a_register_block_that_is_cut_doubled_or_miscounted_is_refused);
    RUN(a_file_of_another_type_is_denied_whatever_its_kind);
    RUN(a_load_holds_the_file_to_the_type_the_caller_states_not_to_20k_sel);
    RUN(a_load_without_an_argument_is_refused_and_writes_nothing);
    RUN(a_checked_file_loads_its_editable_fields_and_reports_the_others);
    RUN(a_file_equal_to_the_servo_changes_and_asks_nothing);
    RUN(a_locked_field_in_an_edited_file_is_reported_and_never_taken);
    RUN(a_locked_value_no_raw_value_shows_is_refused_with_the_line);
    RUN(a_locked_value_is_compared_as_displayed_and_not_as_raw);
    RUN(a_locked_field_whose_servo_value_has_no_display_counts_as_differing);
    RUN(a_number_the_servo_already_shows_changes_nothing);
    RUN(a_value_outside_16_bits_in_a_file_built_by_hand_is_not_applied);
    RUN(soft_rules_come_back_as_the_set_an_edit_by_hand_gets);
    RUN(every_hard_limit_takes_its_value_and_refuses_one_step_beyond);
    RUN(the_hard_limits_between_fields_hold_for_values_in_their_unit);
    RUN(the_refusal_names_the_first_line_at_fault_among_several);
    RUN(a_checked_file_beyond_a_hard_limit_is_refused_whole);
    RUN(a_servo_that_breaks_a_hard_rule_itself_has_no_line_at_fault);
    RUN(the_limits_compare_with_the_backup_the_caller_passes);
    RUN(a_checksum_line_is_named_as_the_reader_names_it);
    RUN(forty_thousand_random_texts_are_read_without_a_fault);
    return test_summary("kst_file");
}
