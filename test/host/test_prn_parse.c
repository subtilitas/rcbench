/**
 * @file test_prn_parse.c
 * @brief The printer file reader: the T50M Pro file and the suites'
 *        fixtures read as written; each refusal E01 to E30 with its line,
 *        made by one change of the T50M Pro file; CRLF line ends; the
 *        limits of 8,192B, 120B a line and 256 lines on both sides; a
 *        refused file leaves the spec zeroed; prn_spec_check() on damaged
 *        specs; the advertisement match and its score.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "prn_fixture.h"
#include "prn_spec.h"

static char s_base[PRN_FILE_MAX + 1u];
static size_t s_base_n;
static char s_txt[2u * PRN_FILE_MAX];
static prn_spec_t s_spec;
static prn_spec_t s_zero;

/** @brief The T50M Pro file's text. */
static bool base(void)
{
    if (s_base_n == 0u) {
        s_base_n = prn_read(PRN_T50M_PATH, s_base, sizeof s_base);
    }
    return s_base_n > 0u;
}

/**
 * @brief The base text with one change.
 * @param from the text replaced, its first place
 * @param to   what replaces it
 * @return the length, 0 when @p from is not in the base
 */
static size_t edit(const char *from, const char *to)
{
    if (!base()) {
        return 0u;
    }
    memcpy(s_txt, s_base, s_base_n);
    s_txt[s_base_n] = '\0';
    char *at = strstr(s_txt, from);
    if (at == NULL) {
        return 0u;
    }
    const size_t head = (size_t)(at - s_txt);
    const size_t fl = strlen(from);
    const size_t tl = strlen(to);
    memmove(at + tl, at + fl, s_base_n - head - fl + 1u);
    memcpy(at, to, tl);
    return s_base_n - fl + tl;
}

/**
 * @brief The line of the first place of a text.
 * @param t      the file
 * @param needle the text
 * @return its line, from 1; 0 when it is not there
 */
static unsigned line_of(const char *t, const char *needle)
{
    const char *at = strstr(t, needle);
    if (at == NULL) {
        return 0u;
    }
    unsigned line = 1u;
    for (const char *c = t; c < at; ++c) {
        line += (*c == '\n') ? 1u : 0u;
    }
    return line;
}

/**
 * @brief Read a text and hold the refusal to a code and a line.
 * @param n    its length
 * @param name the file's name, or NULL
 * @param code the code wanted
 * @param line the line wanted
 * @return true when it is refused so and the spec is zeroed
 */
static bool refused(size_t n, const char *name, unsigned code, unsigned line)
{
    prn_perr_t e;
    memset(&s_spec, 0xA5, sizeof s_spec);
    const bool ok = prn_parse(s_txt, n, name, &s_spec, &e);
    if (ok || e.code != code || e.line != line
        || memcmp(&s_spec, &s_zero, sizeof s_spec) != 0) {
        printf("      want E%02u line %u, got %s E%02u line %u\n", code, line,
               ok ? "taken" : "refused", e.code, e.line);
        return false;
    }
    return true;
}

/** @brief One change and the refusal it gives. */
typedef struct {
    const char *from;  /**< the text replaced */
    const char *to;    /**< what replaces it */
    unsigned code;     /**< the code */
    const char *at;    /**< the text on the line reported; NULL: line 0 */
} change_t;

TEST_CASE(the_t50m_pro_file_reads_as_written)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    CHECK_STR_EQ(s_spec.id, "supvan-t50m-pro");
    CHECK_STR_EQ(s_spec.name, "T50M PRO");
    CHECK_STR_EQ(s_spec.licence, "MIT");
    CHECK_EQ(s_spec.version, 1);
    CHECK_EQ(s_spec.nmatch, 1);
    CHECK_EQ(s_spec.match[0].kind, PRN_M_OUI);
    CHECK_EQ(s_spec.service.len, 16);
    CHECK_EQ(s_spec.service.b[2], 0xE0);
    CHECK(s_spec.chr[PRN_CHR_CMD].used);
    CHECK_EQ(s_spec.chr[PRN_CHR_CMD].uuid.b[1], 0xE9);
    CHECK(!s_spec.chr[PRN_CHR_DATA].used);
    CHECK(s_spec.cccd_next);
    CHECK_EQ(s_spec.mtu_min, 131);
    CHECK_EQ(s_spec.conn[3], 400);
    CHECK_EQ(s_spec.head_dots, 384);
    CHECK_EQ(s_spec.line_bytes, 48);
    CHECK_EQ(s_spec.feed, PRN_EDGE_TOP);
    CHECK_EQ(s_spec.dot0, PRN_EDGE_RIGHT);
    CHECK(s_spec.align_center);
    CHECK_EQ(s_spec.roll_lim[3], 100);
    CHECK_EQ(s_spec.page_size, 4096);
    CHECK_EQ(s_spec.page_lines, 84);
    CHECK(s_spec.lzma);
    CHECK_EQ(s_spec.lzma_dict, 8192u);
    CHECK_EQ(s_spec.chunk_size, 500);
    CHECK_EQ(s_spec.chunk_max, 255);
    CHECK_EQ(s_spec.frag, 128);
    CHECK_EQ(s_spec.gap_ms, 10);
    CHECK_EQ(s_spec.nspeed, 7);
    CHECK_EQ(s_spec.speed_else, 60);
    CHECK_EQ(s_spec.ntmpl, 1);
    CHECK_EQ(s_spec.nanswer, 2);
    CHECK_EQ(s_spec.nfield, 17);
    CHECK_EQ(s_spec.nerror, 7);
    CHECK_EQ(s_spec.nstep, 12);
    CHECK_EQ(s_spec.nquery, 3);
    CHECK_EQ(s_spec.nprint, 12);
    CHECK(s_spec.has_abort);
    CHECK_EQ(s_spec.abort_from, 4);
    CHECK_EQ(s_spec.abort_to, 11);
    CHECK_EQ(s_spec.relink_ms, 5000);
    CHECK_EQ(s_spec.answer_ms, 1000);
    CHECK_EQ(s_spec.verdict, PRN_VD_COUNTER);
    CHECK_EQ(s_spec.vd_key, 0x30);
    CHECK_EQ(s_spec.step[4].relink, PRN_RELINK_VERDICT);
    CHECK_EQ(s_spec.step[10].relink, 11);
    CHECK_EQ(s_spec.step[11].delay_ms, 100);
    CHECK_EQ(prn_field_find(&s_spec, 0x30, PRN_F_LEFT), 16);
    CHECK_EQ(prn_field_find(&s_spec, 0x11, PRN_F_LEFT), -1);
    CHECK_EQ(prn_spec_check(&s_spec), 0);
}

TEST_CASE(the_suites_fixtures_read_and_check)
{
    static char buf[PRN_FILE_MAX + 1u];
    const char *const names[2] = { "rows-test.prn", "magic-test.prn" };
    for (unsigned i = 0; i < 2u; ++i) {
        char path[96];
        snprintf(path, sizeof path, "fixtures/printers/%s", names[i]);
        const size_t n = prn_read(path, buf, sizeof buf);
        prn_perr_t e;
        CHECK(prn_parse(buf, n, names[i], &s_spec, &e));
        CHECK_EQ(e.code, 0);
        CHECK_EQ(prn_spec_check(&s_spec), 0);
    }
    /* rows-test: a fixed roll list, without response, a notify joiner */
    const size_t n = prn_read("fixtures/printers/rows-test.prn", buf,
                              sizeof buf);
    prn_perr_t e;
    CHECK(prn_parse(buf, n, NULL, &s_spec, &e));
    CHECK(s_spec.roll_fixed);
    CHECK_EQ(s_spec.nroll, 2);
    CHECK(s_spec.cmd_nr);
    CHECK(s_spec.data_nr);
    CHECK_EQ(s_spec.join, PRN_J_NOTIFY);
    CHECK(s_spec.speed_stream);
    CHECK_EQ(s_spec.nmatch, 3);
    CHECK_EQ(s_spec.step[4].until[1].op, PRN_OP_NE_V);
}

TEST_CASE(crlf_line_ends_read_the_same)
{
    CHECK(base());
    size_t n = 0u;
    for (size_t i = 0; i < s_base_n; ++i) {
        if (s_base[i] == '\n') {
            s_txt[n++] = '\r';
        }
        s_txt[n++] = s_base[i];
    }
    prn_perr_t e;
    static prn_spec_t lf;
    CHECK(prn_parse(s_txt, n, NULL, &s_spec, &e));
    CHECK(prn_parse(s_base, s_base_n, NULL, &lf, &e));
    CHECK(memcmp(&s_spec, &lf, sizeof lf) == 0);
    /* a CR alone, and one at the end of the file */
    CHECK(refused(edit("version 1\n", "version 1\rx\n"), NULL, 2u,
                  line_of(s_txt, "version 1")));
    memcpy(s_txt, s_base, s_base_n);
    s_txt[s_base_n] = '\r';
    /* the CR is a line of its own after the last LF */
    CHECK(refused(s_base_n + 1u, NULL, 2u, line_of(s_base, "\nend\n") + 2u));
}

TEST_CASE(each_refusal_code_and_its_line)
{
    static const change_t k[] = {
        { "rcbench printer, format 1", "rcbench printer, format 2", 5u,
          "rcbench" },
        { "name T50M PRO", "name T50M\x01PRO", 2u, "name T50M" },
        { "name T50M PRO", "name T50M\x80PRO", 2u, "name T50M" },
        { "density 4", "dens 4", 6u, "dens 4" },
        { "density 4", "  density 4", 6u, "  density 4" },
        { "label align center", "label size center", 6u, "label size" },
        { "mtu_min 131", "mtu_min 248", 7u, "mtu_min" },
        { "mtu_min 131", "mtu_min 0x", 7u, "mtu_min" },
        { "mtu_min 131", "mtu_min 12345678901", 7u, "mtu_min" },
        { "version 1\n", "version 1\nversion 2\n", 8u, "version 2" },
        { "chr other FFEA notify", "chr rx FFEA notify", 8u, "chr rx FFEA" },
        { "head_dots 384\n", "", 9u, "line 48" },
        { "service 0000e0ff-3c17-d293-8e48-14fe2e4da212\n", "", 9u,
          "\nend\n" },
        { "tmpl cmd", "tmpl a 00\ntmpl b 00\ntmpl c 00\ntmpl d 00\n"
                      "tmpl e 00\ntmpl f 00\ntmpl g 00\ntmpl h 00\n"
                      "tmpl i 00\ntmpl j 00\ntmpl k 00\ntmpl l 00\n"
                      "tmpl m 00\ntmpl n 00\ntmpl o 00\ntmpl p 00\n"
                      "tmpl cmd", 10u,
          "tmpl cmd" },
        { "page head {ck:svpage:le16} {pflags:u8} 50 {lines:le16} "
          "{line_bytes:u8} 00 {mlead:le16} {mtrail:le16} {density:u8} 00",
          "page head 00 00 00 00 00 00 00 00 "
          "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
          "00 00 00 #", 10u, "page head 00" },
        { "{a:u8}", "{a:u9}", 11u, "tmpl cmd" },
        { "{a:u8}", "{idx:u8}", 11u, "tmpl cmd" },
        { "{a:u8}", "{data}", 22u, "tmpl cmd" },
        { "{ck:sum16le:10:15}", "{ck:sum16le:10:16}", 12u, "tmpl cmd" },
        { "field 30 left 43 le32", "field 30 left 44 le32", 13u,
          "field 30 left" },
        { "  send cmd a=0x12", "  send cmdx a=0x12", 14u, "  send cmdx" },
        { "  send cmd a=0x12", "  send cmd a=nope", 14u, "a=nope" },
        { "  save c0 left\n  save uid0", "  save c0 busy\n  save uid0", 14u,
          "save c0 busy" },
        { "job query check material ready", "job query check nosuch", 15u,
          "job query" },
        { "job query check material ready", "job query check data", 15u,
          "job query" },
        { "\nend\n", "\n", 16u, "verdict counter" },
        { "\nend\n", "\nend\nid x\n", 16u, "id x" },
        { "mtu_min 131", "mtu_min 130", 18u, "write data" },
        { "2800:15", "3000:15", 19u, "table speed" },
        { "match oui A4:93:40\n", "", 20u, "\nend\n" },
        { "label dot0 right", "label dot0 bottom", 21u, "label dot0" },
        { "{total:u8} {data}", "{total:u8}", 22u, "chunk frame" },
        { "compress lzma_lit 8192", "compress lzma_lit 8192\nrows {data}",
          23u, "rows {data}" },
        { "step check\n  send cmd a=0x12\n  expect 12\n  within 1000\n",
          "step check\n  send cmd a=0x12\n  expect 12\n", 24u,
          "step check" },
        { "step pause\n  wait 20\n", "step pause\n  wait 20\n  within 100\n",
          24u, "step pause" },
        { "  relink done\nstep done", "  relink nosuch\nstep done", 25u,
          "relink nosuch" },
        { "from start to done", "from done to start", 26u, "abort cmd" },
        { "roll source record", "roll source fixed", 27u, "roll source" },
        { "answer 30 47", "answer 30 47\nanswer 01 9\nanswer 02 9\n"
                          "answer 03 9\nanswer 04 9\nanswer 05 9\n"
                          "answer 06 9\nanswer 07 9", 28u, "answer 07" },
        { "id supvan-t50m-pro", "id Supvan", 29u, "id Supvan" },
        { "step check\n", "step Check\n", 29u, "step Check" },
        { "source https://github.com/heeen/supvan-cups\n",
          "source a\nsource b\nsource c\nsource d\n", 30u, "source d" },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        const size_t n = edit(k[i].from, k[i].to);
        CHECK(n != 0u);
        unsigned line = (k[i].at != NULL) ? line_of(s_txt, k[i].at) : 0u;
        if (k[i].at != NULL && k[i].at[0] == '\n') {
            line++; /* the text starts with the line end before it */
        }
        if (!refused(n, NULL, k[i].code, line)) {
            printf("      change %u: %s\n", (unsigned)i, k[i].to);
            t_case_failed = 1;
        }
    }
}

TEST_CASE(the_file_name_must_be_the_id)
{
    CHECK(base());
    memcpy(s_txt, s_base, s_base_n);
    CHECK(refused(s_base_n, "supvan.prn", 17u,
                  line_of(s_base, "id supvan")));
    CHECK(refused(s_base_n, "supvan-t50m-pro.txt", 17u,
                  line_of(s_base, "id supvan")));
    prn_perr_t e;
    CHECK(prn_parse(s_base, s_base_n, "supvan-t50m-pro.prn", &s_spec, &e));
    CHECK(prn_parse(s_base, s_base_n, NULL, &s_spec, &e));
}

TEST_CASE(the_size_line_and_line_count_limits_on_both_sides)
{
    CHECK(base());
    prn_perr_t e;
    CHECK(!prn_parse(s_base, 0u, NULL, &s_spec, &e));
    CHECK_EQ(e.code, 1);
    CHECK_EQ(e.line, 0);
    CHECK(!prn_parse(NULL, 10u, NULL, &s_spec, &e));
    CHECK_EQ(e.code, 1);
    /* comments up to 8,192 and 8,193 bytes */
    for (size_t total = PRN_FILE_MAX; total <= PRN_FILE_MAX + 1u; ++total) {
        memcpy(s_txt, s_base, s_base_n);
        size_t n = s_base_n;
        while (n < total) {
            const size_t room = total - n;
            const size_t len = (room > 100u) ? 100u : room;
            memset(s_txt + n, '#', len);
            if (len > 1u) {
                s_txt[n + len - 1u] = '\n';
            }
            n += len;
        }
        const bool ok = prn_parse(s_txt, n, NULL, &s_spec, &e);
        CHECK_EQ(ok, total == PRN_FILE_MAX);
        CHECK_EQ(e.code, (total == PRN_FILE_MAX) ? 0 : 1);
    }
    /* a comment line of 120 and 121 bytes */
    for (size_t w = 120u; w <= 121u; ++w) {
        char line[130];
        memset(line, 'x', sizeof line);
        line[0] = '#';
        line[w] = '\n';
        line[w + 1u] = '\0';
        const size_t n = edit("density 4\n", line);
        const char *at = strstr(s_txt, "#xxx");
        CHECK(at != NULL);
        memcpy(s_txt + n, "", 1u);
        const bool ok = prn_parse(s_txt, n, NULL, &s_spec, &e);
        CHECK_EQ(ok, w == 120u);
        if (w == 121u) {
            CHECK_EQ(e.code, 3);
            CHECK_EQ(e.line, line_of(s_txt, "#xxx"));
        }
    }
    /* 256 and 257 lines: comments before the header */
    for (unsigned lines = 256u; lines <= 257u; ++lines) {
        size_t n = 0u;
        const unsigned have = line_of(s_base, "\nend") + 1u;
        for (unsigned i = have; i < lines; ++i) {
            s_txt[n++] = '#';
            s_txt[n++] = '\n';
        }
        memcpy(s_txt + n, s_base, s_base_n);
        n += s_base_n;
        const bool ok = prn_parse(s_txt, n, NULL, &s_spec, &e);
        CHECK_EQ(ok, lines == 256u);
        if (lines == 257u) {
            CHECK_EQ(e.code, 4);
            CHECK_EQ(e.line, 257);
        }
    }
    /* a file of comments only has no header */
    memcpy(s_txt, "# a\n\n# b\n", 9u);
    CHECK(refused(9u, NULL, 5u, 0u));
}

TEST_CASE(a_damaged_spec_fails_its_check)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    static prn_spec_t d;
    const struct {
        unsigned code;
        unsigned what;
    } k[] = {
        { 30u, 0u }, { 29u, 1u }, { 20u, 2u }, { 7u, 3u }, { 18u, 4u },
        { 21u, 5u }, { 19u, 6u }, { 11u, 7u }, { 9u, 8u },  { 7u, 9u },
        { 23u, 10u }, { 27u, 11u }, { 7u, 12u }, { 13u, 13u }, { 14u, 14u },
        { 14u, 15u }, { 24u, 16u }, { 25u, 17u }, { 15u, 18u }, { 26u, 19u },
        { 14u, 20u }, { 7u, 21u }, { 30u, 22u }, { 15u, 23u },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        d = s_spec;
        switch (k[i].what) {
        case 0u: d.nstep = PRN_STEPS + 1u; break;
        case 1u: memset(d.id, 'a', sizeof d.id); break;
        case 2u: d.nmatch = 0u; break;
        case 3u: d.head_dots = 2000u; break;
        case 4u: d.frag = 129u; break;
        case 5u: d.dot0 = PRN_EDGE_BOTTOM; break;
        case 6u: d.speed_above[1] = 4000u; break;
        case 7u: d.tok[d.tmpl[0].seq.first].type = 9u; break;
        case 8u: d.frame.n = 0u; break;
        case 9u: d.chunk_max = 0u; break;
        case 10u: d.chunk_size = 0u; break;
        case 11u: d.roll_fixed = true; break;
        case 12u: d.key_off = 8u; break;
        case 13u: d.field[16].off = 44u; break;
        case 14u: d.field[0].key = 0x99u; break;
        case 15u: d.step[0].send.tmpl = 3u; break;
        case 16u: d.step[0].kind = 7u; break;
        case 17u: d.step[4].relink = 40u; break;
        case 18u: d.print[0] = 30u; break;
        case 19u: d.abort_to = 20u; break;
        case 20u: d.vd_send.tmpl = 5u; break;
        case 21u: d.verdict = 9u; break;
        case 22u: d.tmpl[0].seq.n = 41u; break;
        default: d.step[d.print[8]].kind = PRN_S_SEND; break;
        }
        const unsigned got = prn_spec_check(&d);
        if (got != k[i].code) {
            printf("      damage %u: got %u, want %u\n", k[i].what, got,
                   k[i].code);
            t_case_failed = 1;
        }
    }
}

TEST_CASE(a_device_matches_by_kind_and_scores)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    prn_adv_t a;
    memset(&a, 0, sizeof a);
    const uint8_t t50[6] = { 0xA4u, 0x93u, 0x40u, 0x30u, 0xFAu, 0x71u };
    memcpy(a.addr, t50, 6u);
    a.addr_public = true;
    CHECK_EQ(prn_match(&s_spec, &a), 4u);
    a.addr_public = false; /* a random address carries no OUI */
    CHECK_EQ(prn_match(&s_spec, &a), 0u);
    a.addr_public = true;
    a.addr[2] = 0x41u;
    CHECK_EQ(prn_match(&s_spec, &a), 0u);
    /* rows-test: name ROW, iname rows, svc FF00 */
    static char buf[PRN_FILE_MAX + 1u];
    const size_t n = prn_read("fixtures/printers/rows-test.prn", buf,
                              sizeof buf);
    prn_perr_t e;
    CHECK(prn_parse(buf, n, NULL, &s_spec, &e));
    const uint8_t u16[4] = { 0x18u, 0x0Fu, 0xFFu, 0x00u };
    memset(&a, 0, sizeof a);
    a.name = "ROWster";
    a.uuid16 = u16;
    a.n16 = 2u;
    CHECK_EQ(prn_match(&s_spec, &a), 3u);
    a.name = "rOwS 1";
    CHECK_EQ(prn_match(&s_spec, &a), 3u);
    a.name = "RO";
    CHECK_EQ(prn_match(&s_spec, &a), 0u);
    a.name = NULL;
    CHECK_EQ(prn_match(&s_spec, &a), 0u);
    a.name = "ROW";
    a.n16 = 1u; /* FF00 not in the list: svc misses, the AND fails */
    CHECK_EQ(prn_match(&s_spec, &a), 0u);
    /* magic-test: oui 11:22:33, name MAGIC, a 128-bit service */
    const size_t m = prn_read("fixtures/printers/magic-test.prn", buf,
                              sizeof buf);
    CHECK(prn_parse(buf, m, NULL, &s_spec, &e));
    memset(&a, 0, sizeof a);
    a.addr[0] = 0x11u;
    a.addr[1] = 0x22u;
    a.addr[2] = 0x33u;
    a.addr_public = true;
    a.name = "MAGIC-7";
    CHECK_EQ(prn_match(&s_spec, &a), 6u);
    /* a spec with no match lines matches nothing */
    s_spec.nmatch = 0u;
    CHECK_EQ(prn_match(&s_spec, &a), 0u);
    /* a 128-bit match */
    s_spec.nmatch = 1u;
    s_spec.match[0].kind = PRN_M_SVC;
    s_spec.match[0].len = 16u;
    memset(s_spec.match[0].b, 0x5Au, 16u);
    uint8_t u128[32];
    memset(u128, 0x11u, 16u);
    memset(u128 + 16, 0x5Au, 16u);
    a.uuid128 = u128;
    a.n128 = 2u;
    CHECK_EQ(prn_match(&s_spec, &a), 1u);
    a.n128 = 1u;
    CHECK_EQ(prn_match(&s_spec, &a), 0u);
}

int main(void)
{
    memset(&s_zero, 0, sizeof s_zero);
    RUN(the_t50m_pro_file_reads_as_written);
    RUN(the_suites_fixtures_read_and_check);
    RUN(crlf_line_ends_read_the_same);
    RUN(each_refusal_code_and_its_line);
    RUN(the_file_name_must_be_the_id);
    RUN(the_size_line_and_line_count_limits_on_both_sides);
    RUN(a_damaged_spec_fails_its_check);
    RUN(a_device_matches_by_kind_and_scores);
    return test_summary("prn_parse");
}
