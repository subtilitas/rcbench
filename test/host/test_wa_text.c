/*
 * The text the Wi-Fi page reads from a browser and writes back.
 *
 * The failure under test is a download path that leaves the card: "..",
 * written or percent-encoded, an absolute path, a backslash, an encoded
 * separator or NUL byte, a name longer than the card's file system allows.
 * Next to it: a name on the page that breaks the HTML (hypertext markup
 * language) or the link, a code or cookie read past its end, and a
 * download's file name that a browser would cut.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "wa_text.h"

static char g_out[WA_PATH_MAX + 1u];

static wa_path_err_t dec(const char *s)
{
    memset(g_out, 'Z', sizeof(g_out));
    const wa_path_err_t e = wa_path_decode(s, strlen(s), g_out,
                                           sizeof(g_out));
    if (e != WA_PATH_OK) {
        CHECK_EQ(g_out[0], '\0');     /* nothing half-decoded is left */
    }
    return e;
}

TEST_CASE(a_plain_path_decodes_to_itself)
{
    CHECK_EQ(dec("LOG00001.CSV"), WA_PATH_OK);
    CHECK_STR_EQ(g_out, "LOG00001.CSV");
    CHECK_EQ(dec("logs/2026/run 7.csv"), WA_PATH_OK);
    CHECK_STR_EQ(g_out, "logs/2026/run 7.csv");
    CHECK_EQ(dec("my%20runs/m%C3%BCller.csv"), WA_PATH_OK);
    CHECK_STR_EQ(g_out, "my runs/m\xc3\xbcller.csv");
    CHECK_EQ(dec("Gr\xc3\xbc\xc3\x9f" "e.txt"), WA_PATH_OK);
    /* Dots inside a name are a name. */
    CHECK_EQ(dec("...txt"), WA_PATH_OK);
    CHECK_EQ(dec("a..b/.hidden"), WA_PATH_OK);
    /* Decoded once: "%252e" is the name "%2e", not a dot. */
    CHECK_EQ(dec("%252e%252e"), WA_PATH_OK);
    CHECK_STR_EQ(g_out, "%2e%2e");
}

TEST_CASE(a_path_that_climbs_out_is_refused_however_written)
{
    CHECK_EQ(dec(".."), WA_PATH_DOTS);
    CHECK_EQ(dec("../secret"), WA_PATH_DOTS);
    CHECK_EQ(dec("logs/../../x"), WA_PATH_DOTS);
    CHECK_EQ(dec("logs/.."), WA_PATH_DOTS);
    CHECK_EQ(dec("./x"), WA_PATH_DOTS);
    CHECK_EQ(dec("%2e%2e"), WA_PATH_DOTS);
    CHECK_EQ(dec("%2E%2E/x"), WA_PATH_DOTS);
    CHECK_EQ(dec(".%2e/x"), WA_PATH_DOTS);
    CHECK_EQ(dec("logs/%2e"), WA_PATH_DOTS);
    CHECK_EQ(dec("%2e%2e%2fx"), WA_PATH_ENCODED_SLASH);
    CHECK_EQ(dec("..%2Fx"), WA_PATH_ENCODED_SLASH);
    CHECK_EQ(dec("logs%2fx"), WA_PATH_ENCODED_SLASH);
}

TEST_CASE(absolute_paths_and_backslashes_are_refused)
{
    CHECK_EQ(dec("/etc/passwd"), WA_PATH_ABSOLUTE);
    CHECK_EQ(dec("/"), WA_PATH_ABSOLUTE);
    CHECK_EQ(dec("%2fsdcard"), WA_PATH_ENCODED_SLASH);
    CHECK_EQ(dec("..\\x"), WA_PATH_BACKSLASH);
    CHECK_EQ(dec("logs\\run.csv"), WA_PATH_BACKSLASH);
    CHECK_EQ(dec("%5c..%5cx"), WA_PATH_BACKSLASH);
    CHECK_EQ(dec("C:%5Cx"), WA_PATH_BACKSLASH);
}

TEST_CASE(nul_control_bytes_and_bad_escapes_are_refused)
{
    CHECK_EQ(dec("run.csv%00.txt"), WA_PATH_NUL);
    CHECK_EQ(dec("%00"), WA_PATH_NUL);
    /* A NUL in the bytes themselves, not only encoded. */
    memset(g_out, 'Z', sizeof(g_out));
    CHECK_EQ(wa_path_decode("ab\0cd", 5u, g_out, sizeof(g_out)), WA_PATH_NUL);
    CHECK_EQ(g_out[0], '\0');
    CHECK_EQ(dec("a%0Ab"), WA_PATH_CONTROL);
    CHECK_EQ(dec("a\tb"), WA_PATH_CONTROL);
    CHECK_EQ(dec("a%7Fb"), WA_PATH_CONTROL);
    CHECK_EQ(dec("a%"), WA_PATH_ESCAPE);
    CHECK_EQ(dec("a%2"), WA_PATH_ESCAPE);
    CHECK_EQ(dec("a%zz"), WA_PATH_ESCAPE);
    CHECK_EQ(dec("a%g0"), WA_PATH_ESCAPE);
}

TEST_CASE(empty_names_and_bytes_that_are_not_utf8_are_refused)
{
    CHECK_EQ(dec(""), WA_PATH_EMPTY);
    CHECK_EQ(wa_path_decode(NULL, 0u, g_out, sizeof(g_out)), WA_PATH_EMPTY);
    CHECK_EQ(dec("logs/"), WA_PATH_EMPTY_NAME);
    CHECK_EQ(dec("logs//run.csv"), WA_PATH_EMPTY_NAME);
    CHECK_EQ(dec("%C3"), WA_PATH_UTF8);          /* cut short */
    CHECK_EQ(dec("%C0%AF"), WA_PATH_UTF8);       /* overlong "/" */
    CHECK_EQ(dec("%E0%80%AE"), WA_PATH_UTF8);    /* overlong "." */
    CHECK_EQ(dec("%ED%A0%80"), WA_PATH_UTF8);    /* a surrogate */
    CHECK_EQ(dec("%F4%90%80%80"), WA_PATH_UTF8); /* past U+10FFFF */
    CHECK_EQ(dec("%FF"), WA_PATH_UTF8);
}

static char g_big[2048];

TEST_CASE(a_name_is_at_most_255_utf16_units)
{
    memset(g_big, 'n', 256u);
    g_big[255] = '\0';
    CHECK_EQ(dec(g_big), WA_PATH_OK);
    g_big[255] = 'n';
    g_big[256] = '\0';
    CHECK_EQ(dec(g_big), WA_PATH_NAME_LONG);
    /* Units, not bytes: 127 two-byte letters are 254 bytes and fit;
     * a character above U+FFFF counts two units. */
    char *p = g_big;
    for (int i = 0; i < 127; ++i) {
        *p++ = '\xc3';
        *p++ = '\xa4';
    }
    *p = '\0';
    CHECK_EQ(wa_utf8_units(g_big, strlen(g_big)), 127);
    CHECK_EQ(dec(g_big), WA_PATH_OK);
    CHECK_EQ(wa_utf8_units("\xf0\x9f\x93\x81", 4u), 2);
    /* 63 four-byte characters (126 units) and 129 letters: 255 units. */
    p = g_big;
    for (int i = 0; i < 63; ++i) {
        memcpy(p, "\xf0\x9f\x93\x81", 4u);
        p += 4;
    }
    memset(p, 'x', 129u);
    p[129] = '\0';
    CHECK_EQ(wa_utf8_units(g_big, strlen(g_big)), 255);
    CHECK_EQ(dec(g_big), WA_PATH_OK);
    p[129] = 'x';
    p[130] = '\0';
    CHECK_EQ(dec(g_big), WA_PATH_NAME_LONG);
}

TEST_CASE(a_path_is_at_most_384_bytes)
{
    /* Two names of 191 and 192 bytes and the slash: 384. */
    memset(g_big, 'a', 384u);
    g_big[191] = '/';
    g_big[384] = '\0';
    CHECK_EQ(dec(g_big), WA_PATH_OK);
    CHECK_EQ(strlen(g_out), 384u);
    g_big[384] = 'a';
    g_big[385] = '\0';
    CHECK_EQ(dec(g_big), WA_PATH_LONG);
    /* Counted after decoding: 191 and 192 "%41" around a slash are 384. */
    char *p = g_big;
    for (int i = 0; i < 383; ++i) {
        if (i == 191) {
            *p++ = '/';
        }
        memcpy(p, "%41", 3u);
        p += 3;
    }
    *p = '\0';
    CHECK_EQ(dec(g_big), WA_PATH_OK);
    CHECK_EQ(strlen(g_out), 384u);
    memcpy(p, "%41", 4u);
    CHECK_EQ(dec(g_big), WA_PATH_LONG);
    /* A buffer smaller than the path. */
    char small[8];
    CHECK_EQ(wa_path_decode("abcdefgh", 8u, small, sizeof(small)),
             WA_PATH_LONG);
    CHECK_EQ(small[0], '\0');
    CHECK_EQ(wa_path_decode("abcdefg", 7u, small, sizeof(small)), WA_PATH_OK);
    CHECK_EQ(wa_path_decode("abc", 3u, NULL, 0u), WA_PATH_LONG);
}

/* ----------------------------------------------------------- output */

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
    int     calls;
    int     refuse_after;   /* -1: never */
} sink_t;

/* Far above any output here: a writer that never stops fails the case
 * rather than taking the machine's memory. */
#define SINK_CAP (64u * 1024u * 1024u)

static bool sink(void *ctx, const void *data, size_t n)
{
    sink_t *s = ctx;
    if (s->refuse_after >= 0 && s->calls >= s->refuse_after) {
        return false;
    }
    ++s->calls;
    CHECK(n <= WA_OUT_BUF);
    CHECK(s->len + n <= SINK_CAP);
    if (s->len + n > SINK_CAP) {
        return false;
    }
    if (s->len + n + 1u > s->cap) {
        s->cap = (s->len + n + 1u) * 2u;
        REQUIRE_REALLOC(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
    return true;
}

static wa_out_t g_o;
static sink_t g_s;

static void out_begin(int refuse_after)
{
    free(g_s.buf);
    memset(&g_s, 0, sizeof(g_s));
    g_s.refuse_after = refuse_after;
    wa_out_init(&g_o, sink, &g_s);
}

TEST_CASE(names_are_escaped_for_html_and_for_links)
{
    out_begin(-1);
    wa_out_html(&g_o, "<a href=\"x\">Tom's & Jerry's</a>");
    CHECK(wa_out_flush(&g_o));
    CHECK_STR_EQ(g_s.buf,
                 "&lt;a href=&quot;x&quot;&gt;Tom&#39;s &amp; Jerry&#39;s"
                 "&lt;/a&gt;");
    out_begin(-1);
    wa_out_url(&g_o, "logs/run 7 #1?&\"<\xc3\xa4>.csv");
    CHECK(wa_out_flush(&g_o));
    CHECK_STR_EQ(g_s.buf, "logs/run%207%20%231%3F%26%22%3C%C3%A4%3E.csv");
    /* What the link carries decodes to the name again. */
    char back[64];
    CHECK_EQ(wa_path_decode(g_s.buf, g_s.len, back, sizeof(back)),
             WA_PATH_OK);
    CHECK_STR_EQ(back, "logs/run 7 #1?&\"<\xc3\xa4>.csv");
    out_begin(-1);
    wa_out_u64(&g_o, 0u);
    wa_out_str(&g_o, " ");
    wa_out_u64(&g_o, 18446744073709551615u);
    CHECK(wa_out_flush(&g_o));
    CHECK_STR_EQ(g_s.buf, "0 18446744073709551615");
}

TEST_CASE(output_goes_in_pieces_and_stops_when_refused)
{
    out_begin(-1);
    for (int i = 0; i < 3000; ++i) {
        wa_out_str(&g_o, "x");
    }
    CHECK(wa_out_flush(&g_o));
    CHECK_EQ(g_s.len, 3000u);
    CHECK_EQ(g_o.total, 3000u);
    CHECK_EQ(g_s.calls, 3);             /* 1024 + 1024 + 952 */
    CHECK(wa_out_flush(&g_o));          /* nothing gathered: nothing sent */
    CHECK_EQ(g_s.calls, 3);

    out_begin(1);                       /* a browser gone after one piece */
    for (int i = 0; i < 3000; ++i) {
        wa_out_str(&g_o, "y");
    }
    CHECK(g_o.failed);
    CHECK(!wa_out_flush(&g_o));
    CHECK_EQ(g_s.len, WA_OUT_BUF);
    CHECK_EQ(g_o.total, (uint64_t)WA_OUT_BUF);
    free(g_s.buf);
    memset(&g_s, 0, sizeof(g_s));
}

/* ------------------------------------------------------ what comes in */

TEST_CASE(a_form_field_is_read_whole_or_not_at_all)
{
    const char *b = "x=1&code=ab%2Bc+d&codex=no";
    char v[16];
    CHECK(wa_form_get(b, strlen(b), "code", v, sizeof(v)));
    CHECK_STR_EQ(v, "ab+c d");
    CHECK(wa_form_get(b, strlen(b), "x", v, sizeof(v)));
    CHECK_STR_EQ(v, "1");
    CHECK(wa_form_get(b, strlen(b), "codex", v, sizeof(v)));
    CHECK_STR_EQ(v, "no");
    CHECK(!wa_form_get(b, strlen(b), "cod", v, sizeof(v)));
    CHECK(!wa_form_get(b, strlen(b), "y", v, sizeof(v)));
    /* The body is n bytes, not up to a terminator. */
    CHECK(!wa_form_get(b, 4u, "code", v, sizeof(v)));
    CHECK(!wa_form_get("code=%zz", 8u, "code", v, sizeof(v)));
    CHECK(!wa_form_get("code=%4", 7u, "code", v, sizeof(v)));
    /* 7 bytes fit 8, 8 do not. */
    char small[8];
    CHECK(wa_form_get("code=abcdefg", 12u, "code", small, sizeof(small)));
    CHECK(!wa_form_get("code=abcdefgh", 13u, "code", small, sizeof(small)));
    CHECK(wa_form_get("code=", 5u, "code", v, sizeof(v)));
    CHECK_STR_EQ(v, "");
}

TEST_CASE(a_cookie_is_read_by_its_whole_name)
{
    char v[16];
    CHECK(wa_cookie_get("a=1; rcbench_code=k3m9p2xq; b=2", "rcbench_code", v,
                        sizeof(v)));
    CHECK_STR_EQ(v, "k3m9p2xq");
    CHECK(wa_cookie_get("rcbench_code=abc", "rcbench_code", v, sizeof(v)));
    CHECK_STR_EQ(v, "abc");
    CHECK(!wa_cookie_get("xrcbench_code=abc", "rcbench_code", v, sizeof(v)));
    CHECK(!wa_cookie_get("rcbench_codes=abc", "rcbench_code", v, sizeof(v)));
    CHECK(!wa_cookie_get("", "rcbench_code", v, sizeof(v)));
    CHECK(!wa_cookie_get(NULL, "rcbench_code", v, sizeof(v)));
    char small[9];
    CHECK(wa_cookie_get("c=12345678", "c", small, sizeof(small)));
    CHECK(!wa_cookie_get("c=123456789", "c", small, sizeof(small)));
}

TEST_CASE(a_download_keeps_its_name_in_ascii_and_in_utf8)
{
    char d[256];
    size_t n = wa_disposition(d, sizeof(d), "LOG00001.CSV");
    CHECK_STR_EQ(d, "attachment; filename=\"LOG00001.CSV\"; "
                    "filename*=UTF-8''LOG00001.CSV");
    CHECK_EQ(n, strlen(d));
    wa_disposition(d, sizeof(d), "run \"7\"\\m\xc3\xbcller.csv");
    CHECK_STR_EQ(d, "attachment; filename=\"run _7__m__ller.csv\"; "
                    "filename*=UTF-8''run%20%227%22%5Cm%C3%BCller.csv");
    CHECK_EQ(wa_disposition(d, 20u, "LOG00001.CSV"), 0u);
    CHECK_EQ(d[0], '\0');
    CHECK_STR_EQ(wa_basename("logs/2026/a.csv"), "a.csv");
    CHECK_STR_EQ(wa_basename("a.csv"), "a.csv");
}

TEST_CASE(times_read_as_calendar_dates)
{
    wa_civil_t c = wa_civil(0);
    CHECK_EQ(c.year, 1970);
    CHECK_EQ(c.month, 1);
    CHECK_EQ(c.day, 1);
    c = wa_civil(951782400);            /* 2000-02-29 00:00:00 */
    CHECK_EQ(c.year, 2000);
    CHECK_EQ(c.month, 2);
    CHECK_EQ(c.day, 29);
    c = wa_civil(1791961199);           /* 2026-10-14 06:59:59 */
    CHECK_EQ(c.year, 2026);
    CHECK_EQ(c.month, 10);
    CHECK_EQ(c.day, 14);
    CHECK_EQ(c.hour, 6);
    CHECK_EQ(c.min, 59);
    CHECK_EQ(c.sec, 59);
    c = wa_civil(-1);                   /* 1969-12-31 23:59:59 */
    CHECK_EQ(c.year, 1969);
    CHECK_EQ(c.month, 12);
    CHECK_EQ(c.day, 31);
    CHECK_EQ(c.hour, 23);
    c = wa_civil(4354819199);           /* 2107-12-31 23:59:59 */
    CHECK_EQ(c.year, 2107);
    CHECK_EQ(c.month, 12);
    CHECK_EQ(c.day, 31);
    out_begin(-1);
    wa_out_time(&g_o, 1791961199);
    CHECK(wa_out_flush(&g_o));
    CHECK_STR_EQ(g_s.buf, "2026-10-14 06:59");
    free(g_s.buf);
    memset(&g_s, 0, sizeof(g_s));
}

int main(void)
{
    RUN(a_plain_path_decodes_to_itself);
    RUN(a_path_that_climbs_out_is_refused_however_written);
    RUN(absolute_paths_and_backslashes_are_refused);
    RUN(nul_control_bytes_and_bad_escapes_are_refused);
    RUN(empty_names_and_bytes_that_are_not_utf8_are_refused);
    RUN(a_name_is_at_most_255_utf16_units);
    RUN(a_path_is_at_most_384_bytes);
    RUN(names_are_escaped_for_html_and_for_links);
    RUN(output_goes_in_pieces_and_stops_when_refused);
    RUN(a_form_field_is_read_whole_or_not_at_all);
    RUN(a_cookie_is_read_by_its_whole_name);
    RUN(a_download_keeps_its_name_in_ascii_and_in_utf8);
    RUN(times_read_as_calendar_dates);
    return test_summary("wa_text");
}
