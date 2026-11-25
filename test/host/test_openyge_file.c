/*
 * The settings file of programming for YGE ESCs: a table written as text
 * reads back as the same table; every refusal code of the parser comes from
 * the file that causes it; the size cap holds at its byte; a load never
 * gives a draft the guard refuses.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "openyge.h"
#include "openyge_file.h"
#include "openyge_guard.h"
#include "openyge_map.h"

static char g_txt[OY_FILE_MAX + 64u];
static char g_tmp[OY_FILE_MAX + 128u];
static uint16_t g_t[OPENYGE_MAX_PARAMS];

/* A table a writable ESC holds: count 32, type 5712, firmware V1.03555,
 * serial 0x1234. */
static void good_table(uint16_t *t)
{
    static const uint16_t good[32] = {
        32, 2, 74, 0, 0, 5, 5, 1, 1, 2, 1, 5712, 0x9483u, 0x0001u, 0x1234, 0,
        0, 1100, 800, 20, 5, 12, 120, 5, 20, 0, 0, 12000, 0, 0, 0, 0,
    };
    memset(t, 0, OPENYGE_MAX_PARAMS * sizeof(t[0]));
    memcpy(t, good, sizeof(good));
}

/* A guard that has read @p t. */
static void ready(oy_guard_t *g, const uint16_t *t, uint16_t count)
{
    memset(g, 0, sizeof(*g));
    oy_guard_init(g);
    oy_guard_table(g, t, count, false);
}

/* The text of @p t in g_txt, NUL-terminated; its length. */
static size_t text_of(const uint16_t *t, uint16_t count)
{
    memset(g_txt, 0, sizeof(g_txt));
    return oy_file_format(t, count, g_txt, OY_FILE_MAX);
}

/* Parse g_txt as a string. */
static oy_file_err_t parse(oy_file_t *f, unsigned *line)
{
    return oy_file_parse(g_txt, strlen(g_txt), f, line);
}

/* Replace the first @p from in g_txt by @p to. */
static void replace(const char *from, const char *to)
{
    const char *p = strstr(g_txt, from);
    if (p == NULL) {
        return;
    }
    snprintf(g_tmp, sizeof(g_tmp), "%.*s%s%s", (int)(p - g_txt), g_txt, to,
             p + strlen(from));
    memcpy(g_txt, g_tmp, sizeof(g_txt));
}

/* Put the checksum of the lines before it in g_txt's checksum line again,
 * LF line ends assumed. */
static void recrc(void)
{
    char *p = strstr(g_txt, "checksum ");
    if (p == NULL) {
        return;
    }
    const uint32_t crc = oy_file_crc32(g_txt, (size_t)(p - g_txt));
    char hex[9];
    snprintf(hex, sizeof(hex), "%08X", (unsigned)crc);
    memcpy(p + 9, hex, 8);
}

/* Every LF in g_txt becomes CR LF. */
static void to_crlf(void)
{
    size_t o = 0;
    for (size_t i = 0; g_txt[i] != '\0' && o + 2u < sizeof(g_tmp); ++i) {
        if (g_txt[i] == '\n') {
            g_tmp[o++] = '\r';
        }
        g_tmp[o++] = g_txt[i];
    }
    g_tmp[o] = '\0';
    memcpy(g_txt, g_tmp, sizeof(g_txt));
}

TEST_CASE(crc32_has_the_check_value_of_ieee_802_3)
{
    CHECK_EQ(oy_file_crc32("123456789", 9), 0xCBF43926u);
    CHECK_EQ(oy_file_crc32(NULL, 0), 0u);
}

TEST_CASE(a_table_reads_back_as_written)
{
    good_table(g_t);
    const size_t n = text_of(g_t, 32);
    CHECK(n > 0u);
    CHECK_EQ(strlen(g_txt), n);
    CHECK(strncmp(g_txt, "YGE ESC settings, format 1\n"
                         "Type: 5712 165 HVT\n"
                         "Firmware: 1.03555\n"
                         "Serial: 4660\n"
                         "Count: 32\n"
                         "00 PARAMETER COUNT = 32\n"
                         "01 ESC MODE = 2\n", 120) == 0);
    CHECK(strstr(g_txt, "\n27 CURRENT LIMIT = 12000\n") != NULL);
    CHECK(strstr(g_txt, "\n31 RPM SETPOINT = 0\nchecksum ") != NULL);
    CHECK_EQ(g_txt[n - 1u], '\n');

    oy_file_t f;
    unsigned line = 99;
    CHECK_EQ(oy_file_parse(g_txt, n, &f, &line), OY_FILE_OK);
    CHECK_EQ(line, 0u);
    CHECK_EQ(f.count, 32u);
    CHECK_EQ(f.type, 5712u);
    CHECK_EQ(f.firmware, 103555u);
    CHECK_EQ(f.serial, 0x1234u);
    CHECK(memcmp(f.value, g_t, sizeof(g_t)) == 0);

    /* The checksum line without its line end, CR LF, and a byte order
     * mark all read the same. */
    CHECK_EQ(oy_file_parse(g_txt, n - 1u, &f, NULL), OY_FILE_OK);
    to_crlf();
    CHECK_EQ(parse(&f, NULL), OY_FILE_OK);
    CHECK(memcmp(f.value, g_t, sizeof(g_t)) == 0);
    text_of(g_t, 32);
    snprintf(g_tmp, sizeof(g_tmp), "\xEF\xBB\xBF%s", g_txt);
    memcpy(g_txt, g_tmp, sizeof(g_txt));
    CHECK_EQ(parse(&f, NULL), OY_FILE_OK);

    /* Blank lines of spaces and tabs behind the checksum pass. */
    text_of(g_t, 32);
    strcat(g_txt, "\n  \t\n");
    CHECK_EQ(parse(&f, NULL), OY_FILE_OK);
}

TEST_CASE(an_unknown_type_is_written_as_its_number)
{
    good_table(g_t);
    g_t[11] = 7;
    text_of(g_t, 32);
    CHECK(strstr(g_txt, "\nType: 7\n") != NULL);
    oy_file_t f;
    CHECK_EQ(parse(&f, NULL), OY_FILE_OK);
    CHECK_EQ(f.type, 7u);
}

TEST_CASE(values_0_and_65535_and_the_widest_identity_read_back)
{
    good_table(g_t);
    g_t[11] = 65535u;
    g_t[12] = 0xFFFFu;
    g_t[13] = 0xFFFFu;
    g_t[14] = 0xFFFFu;
    g_t[15] = 0xFFFFu;
    g_t[30] = 65535u;
    g_t[31] = 0u;
    const size_t n = text_of(g_t, 32);
    CHECK(n > 0u);
    CHECK(strstr(g_txt, "\nFirmware: 42949.67295\n") != NULL);
    CHECK(strstr(g_txt, "\nSerial: 4294967295\n") != NULL);
    CHECK(strstr(g_txt, "\n30 SOFT BLEND = 65535\n") != NULL);
    oy_file_t f;
    CHECK_EQ(parse(&f, NULL), OY_FILE_OK);
    CHECK(memcmp(f.value, g_t, sizeof(g_t)) == 0);
    CHECK_EQ(f.firmware, 0xFFFFFFFFu);
    CHECK_EQ(f.serial, 0xFFFFFFFFu);

    /* Firmware 0 is written 0.00000. */
    good_table(g_t);
    g_t[12] = 0;
    g_t[13] = 0;
    text_of(g_t, 32);
    CHECK(strstr(g_txt, "\nFirmware: 0.00000\n") != NULL);
    CHECK_EQ(parse(&f, NULL), OY_FILE_OK);
    CHECK_EQ(f.firmware, 0u);

    /* 65536 is no value: the line is malformed even with its checksum. */
    good_table(g_t);
    text_of(g_t, 32);
    replace("\n31 RPM SETPOINT = 0\n", "\n31 RPM SETPOINT = 65536\n");
    recrc();
    unsigned line = 0;
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 37u);
    /* A leading zero is no value this format writes. */
    text_of(g_t, 32);
    replace("\n31 RPM SETPOINT = 0\n", "\n31 RPM SETPOINT = 00\n");
    recrc();
    CHECK_EQ(parse(&f, NULL), OY_FILE_ERR_MALFORMED);
}

TEST_CASE(counts_16_to_64_are_written_and_others_are_not)
{
    oy_file_t f;
    for (uint16_t c = 0; c <= 65u; ++c) {
        for (uint16_t i = 0; i < OPENYGE_MAX_PARAMS; ++i) {
            g_t[i] = (uint16_t)(i * 1000u + 7u);
        }
        g_t[0] = c;
        const size_t n = text_of(g_t, c);
        if (c < OY_FILE_COUNT_MIN || c > OPENYGE_MAX_PARAMS) {
            CHECK_EQ(n, 0u);
            continue;
        }
        CHECK(n > 0u);
        CHECK(n <= OY_FILE_MAX);
        CHECK_EQ(parse(&f, NULL), OY_FILE_OK);
        CHECK_EQ(f.count, c);
        CHECK(memcmp(f.value, g_t, (size_t)c * sizeof(g_t[0])) == 0);
        for (uint16_t i = c; i < OPENYGE_MAX_PARAMS; ++i) {
            CHECK_EQ(f.value[i], 0u);
        }
    }
    /* Index 32 and above have no field and are written RAW. */
    g_t[0] = 64;
    CHECK(text_of(g_t, 64) > 0u);
    CHECK(strstr(g_txt, "\n32 RAW = 32007\n") != NULL);
    CHECK(strstr(g_txt, "\n63 RAW = 63007\n") != NULL);

    /* Index 0 must hold the count. */
    good_table(g_t);
    g_t[0] = 31;
    CHECK_EQ(text_of(g_t, 32), 0u);
}

TEST_CASE(format_needs_room_for_every_byte)
{
    good_table(g_t);
    const size_t n = text_of(g_t, 32);
    CHECK_EQ(oy_file_format(g_t, 32, g_tmp, n), n);
    CHECK_EQ(oy_file_format(g_t, 32, g_tmp, n - 1u), 0u);
    CHECK_EQ(oy_file_format(NULL, 32, g_tmp, n), 0u);
    CHECK_EQ(oy_file_format(g_t, 32, NULL, n), 0u);

    /* The longest file there is fits OY_FILE_MAX in CR LF with a mark. */
    for (uint16_t i = 0; i < OPENYGE_MAX_PARAMS; ++i) {
        g_t[i] = 65535u;
    }
    g_t[0] = 64;
    const size_t m = text_of(g_t, 64);
    CHECK(m > 0u);
    to_crlf();
    CHECK(strlen(g_txt) + 3u <= OY_FILE_MAX);
}

TEST_CASE(the_size_cap_holds_at_its_byte)
{
    good_table(g_t);
    const size_t n = text_of(g_t, 32);
    memset(g_txt + n, '\n', OY_FILE_MAX + 1u - n);
    oy_file_t f;
    CHECK_EQ(oy_file_parse(g_txt, OY_FILE_MAX, &f, NULL), OY_FILE_OK);
    CHECK_EQ(oy_file_parse(g_txt, OY_FILE_MAX + 1u, &f, NULL),
             OY_FILE_ERR_OVERSIZED);
}

TEST_CASE(arguments_and_headers_are_refused_with_their_codes)
{
    oy_file_t f;
    memset(&f, 0x5A, sizeof(f));
    const oy_file_t keep = f;
    good_table(g_t);
    text_of(g_t, 32);
    CHECK_EQ(oy_file_parse(NULL, 10, &f, NULL), OY_FILE_ERR_ARG);
    CHECK_EQ(oy_file_parse(g_txt, strlen(g_txt), NULL, NULL), OY_FILE_ERR_ARG);
    CHECK_EQ(oy_file_parse(g_txt, 0, &f, NULL), OY_FILE_ERR_NOT_YGE);

    unsigned line = 0;
    replace("YGE ESC settings", "KST servo configuration");
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_NOT_YGE);
    CHECK_EQ(line, 1u);

    static const struct { const char *head; oy_file_err_t err; } heads[] = {
        { "format 0\n", OY_FILE_ERR_VERSION },
        { "format x\n", OY_FILE_ERR_VERSION },
        { "format 01\n", OY_FILE_ERR_VERSION },
        { "format 1000\n", OY_FILE_ERR_VERSION },
        { "format \n", OY_FILE_ERR_VERSION },
        { "format 2\n", OY_FILE_ERR_NEWER },
        { "format 999\n", OY_FILE_ERR_NEWER },
    };
    for (size_t i = 0; i < sizeof(heads) / sizeof(heads[0]); ++i) {
        text_of(g_t, 32);
        replace("format 1\n", heads[i].head);
        CHECK_EQ(parse(&f, &line), heads[i].err);
        CHECK_EQ(line, 1u);
    }
    /* A refusal leaves the file as it was. */
    CHECK(memcmp(&f, &keep, sizeof(f)) == 0);
}

TEST_CASE(a_cut_file_is_truncated_at_every_byte)
{
    good_table(g_t);
    const size_t n = text_of(g_t, 32);
    const char *ck = strstr(g_txt, "checksum ");
    const size_t at = (size_t)(ck - g_txt);
    oy_file_t f;
    for (size_t cut = 27; cut < n; ++cut) {
        const oy_file_err_t e = oy_file_parse(g_txt, cut, &f, NULL);
        if (cut == n - 1u) {
            CHECK_EQ(e, OY_FILE_OK);  /* only the last LF is gone */
        } else if (cut >= at + 8u) {
            CHECK_EQ(e, OY_FILE_ERR_CHECKSUM);  /* a cut checksum line */
        } else {
            CHECK_EQ(e, OY_FILE_ERR_TRUNCATED);
        }
    }
}

TEST_CASE(damage_and_edits_are_refused)
{
    good_table(g_t);
    oy_file_t f;
    unsigned line = 0;

    /* A changed value with the checksum kept: CHECKSUM. */
    text_of(g_t, 32);
    replace("\n21 PINION TEETH = 12\n", "\n21 PINION TEETH = 13\n");
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_CHECKSUM);
    CHECK_EQ(line, 38u);

    /* One bit of a hex digit of the checksum. */
    text_of(g_t, 32);
    char *ck = strstr(g_txt, "checksum ") + 9;
    ck[7] = (char)(ck[7] ^ 0x01);
    CHECK_EQ(parse(&f, NULL), OY_FILE_ERR_CHECKSUM);

    /* Lower-case digits and a short checksum. */
    text_of(g_t, 32);
    ck = strstr(g_txt, "checksum ") + 9;
    for (int i = 0; i < 8; ++i) {
        if (ck[i] >= 'A' && ck[i] <= 'F') {
            ck[i] = (char)(ck[i] + 32);
        }
    }
    if (strpbrk(ck, "abcdef") != NULL) {
        CHECK_EQ(parse(&f, NULL), OY_FILE_ERR_CHECKSUM);
    }
    text_of(g_t, 32);
    ck = strstr(g_txt, "checksum ") + 9;
    ck[7] = '\n';
    ck[8] = '\0';
    CHECK_EQ(parse(&f, NULL), OY_FILE_ERR_CHECKSUM);

    /* The checksum line made `edited`: a person's input, refused. */
    text_of(g_t, 32);
    ck = strstr(g_txt, "checksum ");
    strcpy(ck, "edited\n\n");
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_EDITED);
    CHECK_EQ(line, 0u);
    /* Neither line: truncated. */
    strcpy(ck, "edit\n");
    CHECK_EQ(parse(&f, NULL), OY_FILE_ERR_TRUNCATED);

    /* A line behind the checksum. */
    text_of(g_t, 32);
    strcat(g_txt, "21 PINION TEETH = 13\n");
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_TRAILING);
    CHECK_EQ(line, 39u);
}

TEST_CASE(line_ends_characters_and_lengths)
{
    good_table(g_t);
    oy_file_t f;
    unsigned line = 0;

    /* CR LF on line 1 and LF on the rest. */
    text_of(g_t, 32);
    replace("format 1\n", "format 1\r\n");
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_LINE_END);
    CHECK_EQ(line, 2u);

    /* A CR inside a line, a tab, a byte above 0x7E: malformed. */
    static const char *const bad[] = { "\r", "\t", "\x7F", "\xC2\xB5" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        text_of(g_t, 32);
        char with[40];
        snprintf(with, sizeof(with), "\n01 ESC%s MODE = 2\n", bad[i]);
        replace("\n01 ESC MODE = 2\n", with);
        recrc();
        CHECK_EQ(parse(&f, &line), OY_FILE_ERR_MALFORMED);
        CHECK_EQ(line, 7u);
    }

    /* A line of OY_FILE_LINE_MAX characters is taken as a line; one more
     * is refused before its meaning is read. */
    char longer[OY_FILE_LINE_MAX + 8u];
    memset(longer, 0, sizeof(longer));
    memcpy(longer, "Serial: ", 8);
    memset(longer + 8, '1', OY_FILE_LINE_MAX - 8u);
    text_of(g_t, 32);
    replace("Serial: 4660", longer);
    recrc();
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_MALFORMED);  /* 56 digits */
    CHECK_EQ(line, 4u);
    longer[OY_FILE_LINE_MAX] = '1';
    text_of(g_t, 32);
    replace("Serial: 4660", longer);
    recrc();
    CHECK_EQ(parse(&f, &line), OY_FILE_ERR_MALFORMED);
    CHECK_EQ(line, 4u);
}

TEST_CASE(header_lines_and_index_lines_must_agree)
{
    good_table(g_t);
    oy_file_t f;
    unsigned line;
    static const struct {
        const char *from;
        const char *to;
        oy_file_err_t err;
        unsigned line;
    } rows[] = {
        { "Type: 5712 165 HVT\n", "Type: 5459 Saphir 155 v2\n",
          OY_FILE_ERR_MISMATCH, 2 },
        { "Type: 5712 165 HVT\n", "Type: 5712 205 HVT\n",
          OY_FILE_ERR_MISMATCH, 2 },
        { "Type: 5712 165 HVT\n", "Type: 5712\n", OY_FILE_ERR_MISMATCH, 2 },
        { "Type: 5712 165 HVT\n", "Type 5712 165 HVT\n",
          OY_FILE_ERR_MALFORMED, 2 },
        { "Firmware: 1.03555\n", "Firmware: 1.03556\n",
          OY_FILE_ERR_MISMATCH, 3 },
        { "Firmware: 1.03555\n", "Firmware: 1.0355\n",
          OY_FILE_ERR_MALFORMED, 3 },
        { "Firmware: 1.03555\n", "Firmware: 42949.67296\n",
          OY_FILE_ERR_MALFORMED, 3 },
        { "Serial: 4660\n", "Serial: 4661\n", OY_FILE_ERR_MISMATCH, 4 },
        { "Serial: 4660\n", "Serial: 4294967296\n",
          OY_FILE_ERR_MALFORMED, 4 },
        { "Count: 32\n", "Count: 31\n", OY_FILE_ERR_COUNT, 37 },
        { "Count: 32\n", "Count: 33\n", OY_FILE_ERR_COUNT, 38 },
        { "Count: 32\n", "Count: 15\n", OY_FILE_ERR_COUNT, 5 },
        { "Count: 32\n", "Count: 65\n", OY_FILE_ERR_COUNT, 5 },
        { "\n00 PARAMETER COUNT = 32\n", "\n00 PARAMETER COUNT = 33\n",
          OY_FILE_ERR_MISMATCH, 6 },
        { "\n01 ESC MODE = 2\n", "\n01 ESC TYPE = 2\n",
          OY_FILE_ERR_MISMATCH, 7 },
        { "\n01 ESC MODE = 2\n", "\n02 ESC MODE = 2\n",
          OY_FILE_ERR_MALFORMED, 7 },
        { "\n01 ESC MODE = 2\n", "\n1 ESC MODE = 2\n",
          OY_FILE_ERR_MALFORMED, 7 },
        { "\n01 ESC MODE = 2\n", "\n01 ESC MODE=2\n",
          OY_FILE_ERR_MALFORMED, 7 },
        { "\n01 ESC MODE = 2\n", "\n01 ESC MODE = \n",
          OY_FILE_ERR_MALFORMED, 7 },
        { "\n31 RPM SETPOINT = 0\n", "\n31 RPM SETPOINT = 0\n32 RAW = 0\n",
          OY_FILE_ERR_COUNT, 38 },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        text_of(g_t, 32);
        replace(rows[i].from, rows[i].to);
        recrc();
        line = 99;
        CHECK_EQ(parse(&f, &line), rows[i].err);
        CHECK_EQ(line, rows[i].line);
    }

    /* A count of 33 whose 33 index lines are there reads; the key of 32 is
     * RAW, and nothing else. */
    text_of(g_t, 32);
    replace("Count: 32\n", "Count: 33\n");
    replace("= 32\n", "= 33\n");
    replace("\n31 RPM SETPOINT = 0\n", "\n31 RPM SETPOINT = 0\n32 RAW = 9\n");
    recrc();
    CHECK_EQ(parse(&f, NULL), OY_FILE_OK);
    CHECK_EQ(f.count, 33u);
    CHECK_EQ(f.value[32], 9u);
    replace("\n32 RAW = 9\n", "\n32 SOFT BLEND = 9\n");
    recrc();
    CHECK_EQ(parse(&f, NULL), OY_FILE_ERR_MISMATCH);
}

/* Parse the text of @p t into @p f. */
static void file_of(const uint16_t *t, uint16_t count, oy_file_t *f)
{
    text_of(t, count);
    memset(f, 0, sizeof(*f));
    (void)parse(f, NULL);
}

TEST_CASE(a_file_of_the_same_table_loads_with_no_change)
{
    uint16_t esc[OPENYGE_MAX_PARAMS];
    good_table(esc);
    oy_guard_t g;
    ready(&g, esc, 32);
    oy_file_t f;
    file_of(esc, 32, &f);
    oy_file_load_t out;
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_OK);
    CHECK_EQ(out.count, 32u);
    CHECK_EQ(out.changed, 0u);
    CHECK(!out.other_fw);
    CHECK(!out.other_serial);
    CHECK(memcmp(out.draft, esc, 32u * sizeof(esc[0])) == 0);
}

TEST_CASE(writable_indices_load_and_the_draft_passes_the_guard)
{
    uint16_t esc[OPENYGE_MAX_PARAMS];
    good_table(esc);
    oy_guard_t g;
    ready(&g, esc, 32);
    const oy_guard_t before = g;

    /* The file: other writable values, other raw and identity values, and
     * flags with bits beyond 0 and 1. */
    good_table(g_t);
    g_t[1] = 3;             /* mode, CHECK              */
    g_t[21] = 13;           /* pinion                   */
    g_t[27] = 65500;        /* current limit at its top */
    g_t[22] = 0;            /* main gear at its bottom  */
    g_t[4] = 1;             /* raw: kept                */
    g_t[14] = 0x9999u;      /* serial: kept             */
    g_t[12] = 0x9484u;      /* firmware: kept           */
    g_t[26] = 0x00F3u;      /* bits 0 and 1 taken only  */
    g_t[25] = 3;            /* telemetry: kept          */
    oy_file_t f;
    file_of(g_t, 32, &f);
    oy_file_load_t out;
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_OK);
    CHECK_EQ(out.changed, 5u);
    CHECK(out.other_fw);
    CHECK(out.other_serial);
    CHECK_EQ(out.draft[1], 3u);
    CHECK_EQ(out.draft[21], 13u);
    CHECK_EQ(out.draft[27], 65500u);
    CHECK_EQ(out.draft[22], 0u);
    CHECK_EQ(out.draft[4], esc[4]);
    CHECK_EQ(out.draft[14], esc[14]);
    CHECK_EQ(out.draft[12], esc[12]);
    CHECK_EQ(out.draft[25], esc[25]);
    CHECK_EQ(out.draft[26], 0x0003u);

    /* The load left the guard as it was: nothing approved. */
    CHECK(memcmp(&g, &before, sizeof(g)) == 0);
    CHECK(!g.approved);
    /* The draft passes the guard itself. */
    CHECK_EQ(oy_guard_approve(&g, out.draft, out.count, g.read_no, g.read_crc,
                              NULL),
             OY_REF_NONE);
}

TEST_CASE(a_load_refuses_another_type_and_another_count)
{
    uint16_t esc[OPENYGE_MAX_PARAMS];
    good_table(esc);
    oy_guard_t g;
    ready(&g, esc, 32);
    oy_file_t f;
    oy_file_load_t out;

    good_table(g_t);
    g_t[11] = 5459;
    file_of(g_t, 32, &f);
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_TYPE);
    CHECK(memcmp(out.draft, esc, 32u * sizeof(esc[0])) == 0);

    for (uint16_t c = 31; c <= 33u; ++c) {
        good_table(g_t);
        g_t[0] = c;
        file_of(g_t, c, &f);
        CHECK_EQ(f.count, c);
        CHECK_EQ(oy_file_load(&f, &g, &out),
                 c == 32u ? OY_LOAD_OK : OY_LOAD_COUNT);
    }
}

TEST_CASE(a_load_never_bypasses_the_guard)
{
    uint16_t esc[OPENYGE_MAX_PARAMS];
    good_table(esc);
    oy_guard_t g;
    ready(&g, esc, 32);
    oy_file_t f;
    oy_file_load_t out;
    static const struct {
        uint16_t index;
        uint16_t value;
    } rows[] = {
        { 1, 7 }, { 2, 54 }, { 2, 85 }, { 3, 16 }, { 5, 0 }, { 5, 10 },
        { 10, 2 }, { 17, 899 }, { 17, 1901 }, { 21, 0 }, { 22, 1801 },
        { 23, 27 }, { 24, 32 }, { 27, 0 }, { 27, 65501 }, { 27, 65535 },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        good_table(g_t);
        g_t[rows[i].index] = rows[i].value;
        file_of(g_t, 32, &f);
        memset(&out, 0, sizeof(out));
        CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_GUARD);
        CHECK_EQ(out.ref, OY_REF_VALUE);
        CHECK_EQ(out.bad, rows[i].index);
        CHECK(memcmp(out.draft, esc, 32u * sizeof(esc[0])) == 0);
    }

    /* BEC 120 with bit 3 in the file and not in the ESC: the bit is not
     * taken, so 120 is past 84. */
    good_table(g_t);
    g_t[2] = 120;
    g_t[26] = 0x0008u;
    file_of(g_t, 32, &f);
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_GUARD);
    CHECK_EQ(out.ref, OY_REF_VALUE);
    CHECK_EQ(out.bad, 2u);
    /* With bit 3 in the ESC it loads. */
    esc[26] = 0x0008u;
    ready(&g, esc, 32);
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_OK);
    CHECK_EQ(out.draft[2], 120u);

    /* A value outside the set that equals the ESC's is written back as
     * read. */
    good_table(esc);
    esc[10] = 2;
    ready(&g, esc, 32);
    good_table(g_t);
    g_t[10] = 2;
    file_of(g_t, 32, &f);
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_OK);
    CHECK_EQ(out.changed, 0u);
}

TEST_CASE(a_load_needs_a_writable_table)
{
    uint16_t esc[OPENYGE_MAX_PARAMS];
    good_table(esc);
    oy_file_t f;
    file_of(esc, 32, &f);
    oy_file_load_t out;
    oy_guard_t g;

    /* No table read. */
    memset(&g, 0, sizeof(g));
    oy_guard_init(&g);
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_GUARD);
    CHECK_EQ(out.ref, OY_REF_NO_TABLE);

    /* Firmware below V1.03547, and a legacy header. */
    esc[12] = (uint16_t)((103546u) & 0xFFFFu);
    esc[13] = (uint16_t)(103546u >> 16);
    ready(&g, esc, 32);
    file_of(esc, 32, &f);
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_GUARD);
    CHECK_EQ(out.ref, OY_REF_RO_FIRMWARE);
    good_table(esc);
    memset(&g, 0, sizeof(g));
    oy_guard_init(&g);
    oy_guard_table(&g, esc, 32, true);
    file_of(esc, 32, &f);
    CHECK_EQ(oy_file_load(&f, &g, &out), OY_LOAD_GUARD);
    CHECK_EQ(out.ref, OY_REF_RO_LEGACY);

    /* A NULL leaves the result untouched. */
    memset(&out, 0x5A, sizeof(out));
    const oy_file_load_t keep = out;
    CHECK_EQ(oy_file_load(NULL, &g, &out), OY_LOAD_ARG);
    CHECK_EQ(oy_file_load(&f, NULL, &out), OY_LOAD_ARG);
    CHECK_EQ(oy_file_load(&f, &g, NULL), OY_LOAD_ARG);
    CHECK(memcmp(&out, &keep, sizeof(out)) == 0);
}

TEST_CASE(backup_names_run_from_001_to_999)
{
    char name[OY_FILE_NAME_MAX];
    CHECK(oy_file_name(name, sizeof(name), 1, 5712));
    CHECK_STR_EQ(name, "YGE001-5712.TXT");
    CHECK(oy_file_name(name, sizeof(name), 999, 65535));
    CHECK_STR_EQ(name, "YGE999-65535.TXT");
    CHECK(oy_file_name(name, sizeof(name), 42, 0));
    CHECK_STR_EQ(name, "YGE042-0.TXT");
    CHECK(!oy_file_name(name, sizeof(name), 0, 5712));
    CHECK(!oy_file_name(name, sizeof(name), 1000, 5712));
    CHECK(!oy_file_name(name, OY_FILE_NAME_MAX - 1u, 999, 65535));
    CHECK(oy_file_name(name, 16, 1, 5712));
    CHECK(!oy_file_name(name, 15, 1, 5712));
    CHECK(!oy_file_name(NULL, sizeof(name), 1, 5712));
}

int main(void)
{
    RUN(crc32_has_the_check_value_of_ieee_802_3);
    RUN(a_table_reads_back_as_written);
    RUN(an_unknown_type_is_written_as_its_number);
    RUN(values_0_and_65535_and_the_widest_identity_read_back);
    RUN(counts_16_to_64_are_written_and_others_are_not);
    RUN(format_needs_room_for_every_byte);
    RUN(the_size_cap_holds_at_its_byte);
    RUN(arguments_and_headers_are_refused_with_their_codes);
    RUN(a_cut_file_is_truncated_at_every_byte);
    RUN(damage_and_edits_are_refused);
    RUN(line_ends_characters_and_lengths);
    RUN(header_lines_and_index_lines_must_agree);
    RUN(a_file_of_the_same_table_loads_with_no_change);
    RUN(writable_indices_load_and_the_draft_passes_the_guard);
    RUN(a_load_refuses_another_type_and_another_count);
    RUN(a_load_never_bypasses_the_guard);
    RUN(a_load_needs_a_writable_table);
    RUN(backup_names_run_from_001_to_999);
    return test_summary("openyge_file");
}
