/*
 * The text interface's lines and replies (protocols/escwire/ew_text.c), and
 * ESCape32's keys (ew_map_e32.c).
 *
 * Under test: the lines of the specification byte for byte; a line that does
 * not fit; numbers at their limits and in forms the wire does not write; the
 * last line of a reply, a cut, a bad stop bit, a byte no text has; `info` with
 * and without a second sensor, a target name over its size, revisions at and
 * past their limits; `show` of 47 keys, a key missing, repeated, unknown or
 * bad; `music` at 255 and 256 characters; the longest `show` against the
 * buffer; the reply of `set`; every key at both ends of its values and one past.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "ew_text.h"

static uint8_t g_buf[EW_RX_TEXT_MAX + 512u];
static size_t g_n;

static void say(const char *s)
{
    const size_t n = strlen(s);

    memcpy(g_buf + g_n, s, n);
    g_n += n;
}

static ew_capture_t cap(size_t n)
{
    ew_capture_t c;

    memset(&c, 0, sizeof(c));
    c.n = (uint16_t)n;
    return c;
}

static const ew_key_t *key(const char *name)
{
    return ew_key_find(name, strlen(name), NULL);
}

/* A `show` body: each key holds its place in the table. */
static void say_show(const char *music)
{
    char line[40];

    g_n = 0u;
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        if (i == 42u) {
            say("music: ");
            say(music);
            say("\n");
        }
        (void)snprintf(line, sizeof(line), "%s: %d\n", ew_map_e32[i].key,
                       (int)i);
        say(line);
    }
}

TEST_CASE(the_lines_byte_for_byte)
{
    uint8_t out[32];

    CHECK_EQ(ew_text_word(out, sizeof(out), "info"), 5u);
    CHECK(memcmp(out, "info\n", strlen("info\n")) == 0);
    CHECK_EQ(ew_text_word(out, 5u, "info"), 5u);
    CHECK_EQ(ew_text_word(out, 4u, "info"), 0u);
    CHECK_EQ(ew_text_set(out, sizeof(out), "timing", 20), 14u);
    CHECK(memcmp(out, "set timing 20\n", strlen("set timing 20\n")) == 0);
    CHECK_EQ(ew_text_set(out, sizeof(out), "telem_volt", -80), 19u);
    CHECK(memcmp(out, "set telem_volt -80\n", strlen("set telem_volt -80\n")) == 0);
    CHECK_EQ(ew_text_set(out, sizeof(out), "arm", 0), 10u);
    CHECK(memcmp(out, "set arm 0\n", strlen("set arm 0\n")) == 0);
}

TEST_CASE(a_line_that_does_not_fit)
{
    uint8_t out[32];

    CHECK_EQ(ew_text_set(out, 14u, "timing", 20), 14u);
    CHECK_EQ(ew_text_set(out, 13u, "timing", 20), 0u);
}

static bool num(const char *s, int16_t *v)
{
    return ew_text_num((const uint8_t *)s, strlen(s), v);
}

TEST_CASE(numbers_at_their_limits_and_in_other_forms)
{
    int16_t v = 0;

    CHECK(num("0", &v));
    CHECK_EQ(v, 0);
    CHECK(num("32767", &v));
    CHECK_EQ(v, 32767);
    CHECK(num("-32767", &v));
    CHECK_EQ(v, -32767);
    CHECK(!num("32768", &v));
    /* The one value that means no value is no number. */
    CHECK(!num("-32768", &v));
    CHECK(!num("100000", &v));
    CHECK(!num("", &v));
    CHECK(!num("-", &v));
    CHECK(!num("-0", &v));
    CHECK(!num("007", &v));
    CHECK(!num("+5", &v));
    CHECK(!num("5 ", &v));
    CHECK(!num("1a", &v));
}

static ew_ans_t reply_of(const char *s, size_t *body)
{
    const size_t n = strlen(s);
    const ew_capture_t c = cap(n);

    memcpy(g_buf, s, n);
    return ew_text_reply(&c, g_buf, body);
}

TEST_CASE(the_last_line_of_a_reply)
{
    size_t body = 99u;

    CHECK_EQ(reply_of("OK\n", &body), EW_ANS_OK);
    CHECK_EQ(body, 0u);
    CHECK_EQ(reply_of("timing: 20\nOK\n", &body), EW_ANS_OK);
    CHECK_EQ(body, 11u);
    CHECK_EQ(reply_of("ERROR\n", &body), EW_ANS_TEXT_ERROR);
    CHECK_EQ(body, 0u);
    CHECK_EQ(reply_of("x\nERROR\n", &body), EW_ANS_TEXT_ERROR);
    /* No last line: a reply cut anywhere. */
    CHECK_EQ(reply_of("OK", &body), EW_ANS_LENGTH);
    CHECK_EQ(reply_of("timing: 20\nO", &body), EW_ANS_LENGTH);
    CHECK_EQ(reply_of("timing: 20\n", &body), EW_ANS_LENGTH);
    CHECK_EQ(reply_of("OK\nx", &body), EW_ANS_LENGTH);
    /* A line that only ends with the word. */
    CHECK_EQ(reply_of("NOTOK\n", &body), EW_ANS_LENGTH);
    CHECK_EQ(reply_of("music: OK\n", &body), EW_ANS_LENGTH);
    CHECK_EQ(reply_of("\n", &body), EW_ANS_LENGTH);
    /* A CR is no line end here, and no text. */
    CHECK_EQ(reply_of("OK\r\n", &body), EW_ANS_OTHER);
    CHECK_EQ(reply_of("\x7FOK\n", &body), EW_ANS_OTHER);
    CHECK_EQ(reply_of("\x01OK\n", &body), EW_ANS_OTHER);
}

TEST_CASE(a_capture_that_is_not_whole)
{
    size_t body = 0u;
    ew_capture_t c = cap(0u);

    memcpy(g_buf, "OK\n", 3u);
    CHECK_EQ(ew_text_reply(&c, g_buf, &body), EW_ANS_SILENT);
    c = cap(3u);
    c.bad_stop = 1u;
    CHECK_EQ(ew_text_reply(&c, g_buf, &body), EW_ANS_FRAMING);
    c = cap(3u);
    c.overflow = true;
    CHECK_EQ(ew_text_reply(&c, g_buf, &body), EW_ANS_ABORTED);
    c = cap(3u);
    c.aborted = true;
    CHECK_EQ(ew_text_reply(&c, g_buf, &body), EW_ANS_ABORTED);
}

static bool info_of(const char *s, ew_text_info_t *out)
{
    return ew_text_info((const uint8_t *)s, strlen(s), out);
}

TEST_CASE(info_with_its_live_values)
{
    ew_text_info_t i;

    CHECK(info_of("ESCape32 rev17.3 [BOARD_A]\nTemp: 25C\nVolt: 16.1V\n"
                  "Curr: 0.25A\nCsum: 12mAh\nERPM: 900\n", &i));
    CHECK(i.rev_read);
    CHECK_EQ(i.rev, 17u);
    CHECK_EQ(i.sub, 3u);
    CHECK(ew_text_rev_known(&i));
    CHECK_STR_EQ(i.target, "BOARD_A");
    CHECK_STR_EQ(i.temp, "25C");
    CHECK_STR_EQ(i.temp_ext, "");
    CHECK_STR_EQ(i.volt, "16.1V");
    CHECK_STR_EQ(i.curr, "0.25A");
    CHECK_STR_EQ(i.csum, "12mAh");
    CHECK_STR_EQ(i.erpm, "900");
    /* A second sensor, a negative temperature, lines in another order. */
    CHECK(info_of("ESCape32 rev17.0 [B]\nERPM: 1\nOther: 2\nTemp: -3C, ext 31C\n",
                  &i));
    CHECK_STR_EQ(i.temp, "-3C");
    CHECK_STR_EQ(i.temp_ext, "31C");
    CHECK_STR_EQ(i.erpm, "1");
    CHECK_STR_EQ(i.volt, "");
    /* A value over its size is cut. */
    CHECK(info_of("ESCape32 rev17.0\nVolt: 123456789012345V\n", &i));
    CHECK_STR_EQ(i.volt, "12345678901");
    /* A short line that starts like no value. */
    CHECK(info_of("ESCape32 rev17.0\nT\n", &i));
}

TEST_CASE(a_target_name_at_its_size_and_past_it)
{
    ew_text_info_t i;

    CHECK(info_of("ESCape32 rev17.3 [12345678901234567890123456789]\n", &i));
    CHECK_EQ(strlen(i.target), EW_ID_NAME_MAX);
    CHECK(info_of("ESCape32 rev17.3 [123456789012345678901234567890123]\n", &i));
    CHECK_EQ(strlen(i.target), EW_ID_NAME_MAX);
    CHECK(i.rev_read);
    /* No name at all reads, as the check names only the start and the number. */
    CHECK(info_of("ESCape32 rev17.3\n", &i));
    CHECK(i.rev_read);
    CHECK_STR_EQ(i.target, "");
}

TEST_CASE(revisions_at_and_past_their_limits)
{
    ew_text_info_t i;

    /* Not a revision this module writes, with the firmware known. */
    CHECK(info_of("ESCape32 rev16.9 [B]\n", &i));
    CHECK(i.rev_read);
    CHECK_EQ(i.rev, 16u);
    CHECK(!ew_text_rev_known(&i));
    CHECK(info_of("ESCape32 rev18.0 [B]\n", &i));
    CHECK(!ew_text_rev_known(&i));
    CHECK(info_of("ESCape32 rev255.0 [B]\n", &i));
    CHECK_EQ(i.rev, 255u);
    /* 256 + 17 must not read as 17. */
    CHECK(info_of("ESCape32 rev273.0 [B]\n", &i));
    CHECK(i.rev_read);
    CHECK_EQ(i.rev, 0u);
    CHECK(!ew_text_rev_known(&i));
    CHECK(info_of("ESCape32 rev65553.0 [B]\n", &i));
    CHECK(!i.rev_read);
    CHECK(!ew_text_rev_known(&i));
    /* Sub revisions all count as the revision. */
    CHECK(info_of("ESCape32 rev17.65535 [B]\n", &i));
    CHECK(ew_text_rev_known(&i));
    /* Another form: the firmware known, the revision not read. */
    static const char *const k_forms[] = {
        "ESCape32 rev\n", "ESCape32 rev17\n", "ESCape32 rev.3\n",
        "ESCape32 rev17.\n", "ESCape32 rev17x.3\n", "ESCape32 rev17.3x\n",
        "ESCape32 rev17.65536 [B]\n", "ESCape32 rev17.3 B\n",
        "ESCape32 rev17.3 [B\n", "ESCape32 rev17.3 [\n",
        "ESCape32 rev-17.3\n",
    };

    for (size_t k = 0; k < sizeof(k_forms) / sizeof(k_forms[0]); ++k) {
        CHECK(info_of(k_forms[k], &i));
        CHECK(!i.rev_read);
        CHECK_EQ(i.rev, 0u);
        CHECK(!ew_text_rev_known(&i));
    }
    /* Not this firmware. */
    CHECK(!info_of("", &i));
    CHECK(!info_of("Escape32 rev17.3\n", &i));
    CHECK(!info_of("ESCape32\n", &i));
    CHECK(!info_of("Temp: 25C\nESCape32 rev17.3\n", &i));
}

static uint8_t g_page[EW_MAP_LEN_E32];
static char g_music[EW_E32_MUSIC_MAX + 1u];

TEST_CASE(show_of_47_keys)
{
    say_show("dfa#");
    CHECK(ew_text_show(g_buf, g_n, g_page, g_music));
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        CHECK_EQ(ew_e32_get(g_page, i), (int16_t)i);
    }
    CHECK_STR_EQ(g_music, "dfa#");
    /* Any order: by key, not by place.  With no place for `music`. */
    CHECK(ew_text_show(g_buf, g_n, g_page, NULL));
    /* An empty tune, with and without the blank. */
    say_show("");
    CHECK(ew_text_show(g_buf, g_n, g_page, g_music));
    CHECK_STR_EQ(g_music, "");
    /* A tune that holds what a line of a key holds. */
    say_show("a: 1 OK");
    CHECK(ew_text_show(g_buf, g_n, g_page, g_music));
    CHECK_STR_EQ(g_music, "a: 1 OK");
}

TEST_CASE(show_in_another_order_and_empty_music)
{
    char line[40];

    g_n = 0u;
    for (size_t i = EW_E32_KEYS; i > 0u; --i) {
        (void)snprintf(line, sizeof(line), "%s: %d\n", ew_map_e32[i - 1u].key,
                       (int)i);
        say(line);
    }
    say("music:\n");
    CHECK(ew_text_show(g_buf, g_n, g_page, g_music));
    CHECK_EQ(ew_e32_get(g_page, 0u), 1);
    CHECK_EQ(ew_e32_get(g_page, 45u), 46);
    CHECK_STR_EQ(g_music, "");
}

TEST_CASE(music_at_255_characters_and_256)
{
    char tune[EW_E32_MUSIC_MAX + 2u];

    memset(tune, 'a', sizeof(tune));
    tune[EW_E32_MUSIC_MAX] = '\0';
    say_show(tune);
    CHECK(ew_text_show(g_buf, g_n, g_page, g_music));
    CHECK_EQ(strlen(g_music), EW_E32_MUSIC_MAX);
    tune[EW_E32_MUSIC_MAX] = 'a';
    tune[EW_E32_MUSIC_MAX + 1u] = '\0';
    say_show(tune);
    CHECK(!ew_text_show(g_buf, g_n, g_page, g_music));
}

TEST_CASE(the_longest_show_fits_the_buffer)
{
    char tune[EW_E32_MUSIC_MAX + 1u];
    char line[40];
    size_t total = 0u;

    memset(tune, 'a', sizeof(tune));
    tune[EW_E32_MUSIC_MAX] = '\0';
    total += strlen("music: ") + EW_E32_MUSIC_MAX + 1u;
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        const ew_key_t *k = &ew_map_e32[i];
        const int v = k->lo < 0 ? k->lo : k->hi;

        total += (size_t)snprintf(line, sizeof(line), "%s: %d\n", k->key, v);
    }
    total += 3u; /* OK */
    CHECK(total <= 1024u);
    CHECK(total <= EW_RX_TEXT_MAX);
}

/* A whole show with one line changed: the line's text replaced. */
static void say_show_with(size_t skip, const char *instead)
{
    char line[40];

    g_n = 0u;
    say("music: dfa#\n");
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        if (i == skip) {
            say(instead);
            continue;
        }
        (void)snprintf(line, sizeof(line), "%s: 1\n", ew_map_e32[i].key);
        say(line);
    }
}

TEST_CASE(show_with_a_key_missing_repeated_unknown_or_bad)
{
    /* Whole. */
    say_show_with(99u, "");
    CHECK(ew_text_show(g_buf, g_n, g_page, g_music));
    /* The first and the last key missing: they read as no value. */
    say_show_with(0u, "");
    CHECK(!ew_text_show(g_buf, g_n, g_page, g_music));
    CHECK_EQ(ew_e32_get(g_page, 0u), EW_KEY_NONE);
    CHECK_EQ(ew_e32_get(g_page, 1u), 1);
    say_show_with(EW_E32_KEYS - 1u, "");
    CHECK(!ew_text_show(g_buf, g_n, g_page, g_music));
    CHECK_EQ(ew_e32_get(g_page, EW_E32_KEYS - 1u), EW_KEY_NONE);
    /* `music` missing. */
    say_show_with(99u, "");
    CHECK(!ew_text_show(g_buf + 12u, g_n - 12u, g_page, g_music));
    CHECK_STR_EQ(g_music, "");
    /* Repeated, a numeric key and `music`. */
    say_show_with(1u, "arm: 1\n");
    CHECK(!ew_text_show(g_buf, g_n, g_page, g_music));
    say_show_with(99u, "");
    say("music: dfa#\n");
    CHECK(!ew_text_show(g_buf, g_n, g_page, g_music));
    /* One more line of no key, of no form. */
    static const char *const k_extra[] = {
        "newkey: 1\n", "nocolon\n", ": 1\n", "arm:1\n", "\n", "damp: \n",
        "damp: x\n", "damp: 40000\n", "damp: -32768\n", "damp 1\n",
    };

    for (size_t i = 0; i < sizeof(k_extra) / sizeof(k_extra[0]); ++i) {
        say_show_with(1u, k_extra[i]);
        CHECK(!ew_text_show(g_buf, g_n, g_page, g_music));
    }
    /* A bad value leaves the key without one. */
    say_show_with(1u, "damp: x\n");
    (void)ew_text_show(g_buf, g_n, g_page, g_music);
    CHECK_EQ(ew_e32_get(g_page, 1u), EW_KEY_NONE);
    /* An empty body. */
    CHECK(!ew_text_show(g_buf, 0u, g_page, g_music));
    /* A last line with no LF is still read. */
    say_show_with(99u, "");
    CHECK(ew_text_show(g_buf, g_n - 1u, g_page, g_music));
}

static bool set_reply(const char *s, const char *k, int16_t *v)
{
    return ew_text_set_reply((const uint8_t *)s, strlen(s), k, v);
}

TEST_CASE(the_reply_of_set)
{
    int16_t v = 0;

    CHECK(set_reply("timing: 20\n", "timing", &v));
    CHECK_EQ(v, 20);
    CHECK(set_reply("telem_volt: -80\n", "telem_volt", &v));
    CHECK_EQ(v, -80);
    CHECK(!set_reply("", "timing", &v));
    CHECK(!set_reply("\n", "timing", &v));
    CHECK(!set_reply("timing 20\n", "timing", &v));
    CHECK(!set_reply("damp: 1\n", "timing", &v));
    CHECK(!set_reply("timin: 20\n", "timing", &v));
    CHECK(!set_reply("timingg: 20\n", "timing", &v));
    CHECK(!set_reply("timing: x\n", "timing", &v));
    CHECK(!set_reply("timing:\n", "timing", &v));
    CHECK(!set_reply("timing: 20\ndamp: 1\n", "timing", &v));
}

TEST_CASE(the_keys_as_the_wire_has_them)
{
    static const char *const k_order[EW_E32_KEYS] = {
        "arm", "damp", "revdir", "brushed", "timing", "sine_range",
        "sine_power", "freq_min", "freq_max", "duty_min", "duty_max",
        "duty_spup", "duty_ramp", "duty_rate", "duty_drag", "duty_lock",
        "throt_mode", "throt_rev", "throt_brk", "throt_set", "throt_ztc",
        "throt_cal", "throt_min", "throt_mid", "throt_max", "analog_min",
        "analog_max", "input_mode", "input_ch1", "input_ch2", "telem_mode",
        "telem_phid", "telem_poles", "telem_volt", "telem_curr", "prot_stall",
        "prot_temp", "prot_sens", "prot_volt", "prot_cells", "prot_curr",
        "prot_park", "volume", "beacon", "bec", "led",
    };

    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        size_t slot = 99u;

        CHECK_STR_EQ(ew_map_e32[i].key, k_order[i]);
        CHECK(key(k_order[i]) == &ew_map_e32[i]);
        (void)ew_key_find(k_order[i], strlen(k_order[i]), &slot);
        CHECK_EQ(slot, i);
    }
    CHECK(key("music") == NULL);
    CHECK(key("ar") == NULL);
    CHECK(key("armm") == NULL);
    CHECK(key("") == NULL);
}

TEST_CASE(every_key_at_both_ends_and_one_past)
{
    static const struct {
        const char *k;
        int lo, hi, step;
    } k_ends[] = {
        { "arm", 0, 1, 1 }, { "damp", 0, 1, 1 }, { "revdir", 0, 1, 1 },
        { "brushed", 0, 1, 1 }, { "timing", 1, 31, 1 },
        { "sine_power", 1, 15, 1 }, { "freq_min", 16, 48, 1 },
        { "freq_max", 16, 96, 1 }, { "duty_min", 1, 100, 1 },
        { "duty_max", 1, 100, 1 }, { "duty_spup", 1, 100, 1 },
        { "duty_ramp", 0, 100, 1 }, { "duty_rate", 1, 100, 1 },
        { "duty_drag", 0, 100, 1 }, { "duty_lock", 0, 2, 1 },
        { "bec", 0, 4, 1 }, { "throt_mode", 0, 3, 1 }, { "throt_rev", 0, 3, 1 },
        { "throt_brk", 0, 100, 1 }, { "throt_set", 0, 100, 1 },
        { "throt_ztc", 0, 1, 1 }, { "throt_cal", 0, 1, 1 },
        { "throt_min", 900, 1900, 10 }, { "throt_mid", 1000, 2000, 10 },
        { "throt_max", 1100, 2100, 10 }, { "analog_min", 0, 3200, 10 },
        { "analog_max", 200, 3400, 10 }, { "input_mode", 0, 7, 1 },
        { "input_ch1", 0, 32, 1 }, { "input_ch2", 0, 32, 1 },
        { "telem_mode", 0, 6, 1 }, { "telem_phid", 0, 28, 1 },
        { "telem_poles", 2, 100, 1 }, { "telem_volt", -80, 160, 1 },
        { "telem_curr", -100, 200, 1 }, { "prot_sens", 0, 2, 1 },
        { "prot_park", 0, 4, 1 }, { "volume", 0, 100, 1 },
        { "beacon", 0, 100, 1 }, { "led", 0, 15, 1 },
        /* With 0 and a gap above it. */
        { "sine_range", 5, 25, 1 }, { "prot_stall", 10, 3500, 10 },
        { "prot_temp", 5, 140, 5 }, { "prot_volt", 28, 38, 1 },
        { "prot_cells", 1, 24, 1 }, { "prot_curr", 1, 999, 1 },
    };

    CHECK_EQ(sizeof(k_ends) / sizeof(k_ends[0]), EW_E32_KEYS);
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        const ew_key_t *k = key(k_ends[i].k);

        CHECK(k != NULL);
        CHECK(ew_key_ok(k, (int16_t)k_ends[i].lo));
        CHECK(ew_key_ok(k, (int16_t)k_ends[i].hi));
        CHECK_EQ(ew_key_ok(k, (int16_t)(k_ends[i].lo - 1)), k->has_alt && k->alt == k_ends[i].lo - 1);
        CHECK(!ew_key_ok(k, (int16_t)(k_ends[i].hi + 1)));
        if (k_ends[i].step > 1) {
            CHECK(!ew_key_ok(k, (int16_t)(k_ends[i].lo + 1)));
            CHECK(ew_key_ok(k, (int16_t)(k_ends[i].lo + k_ends[i].step)));
        }
        CHECK(!ew_key_ok(k, EW_KEY_NONE));
    }
    /* The gap: 0, and nothing up to the first value. */
    CHECK(ew_key_ok(key("sine_range"), 0));
    CHECK(!ew_key_ok(key("sine_range"), 1));
    CHECK(ew_key_ok(key("prot_volt"), 0));
    CHECK(!ew_key_ok(key("prot_volt"), 27));
    CHECK(ew_key_ok(key("prot_stall"), 0));
    CHECK(!ew_key_ok(key("prot_stall"), 5));
    CHECK(!ew_key_ok(NULL, 0));
}

static const char *text_of(const char *k, int v)
{
    static char out[32];

    (void)ew_key_text(key(k), (int16_t)v, out, sizeof(out));
    return out;
}

TEST_CASE(the_text_of_a_value)
{
    CHECK_STR_EQ(text_of("arm", 1), "ON");
    CHECK_STR_EQ(text_of("arm", 2), "raw 2");
    CHECK_STR_EQ(text_of("timing", 16), "15deg");
    CHECK_STR_EQ(text_of("timing", 1), "0.9375deg");
    CHECK_STR_EQ(text_of("duty_rate", 30), "3%/ms");
    CHECK_STR_EQ(text_of("duty_rate", 31), "3.1%/ms");
    CHECK_STR_EQ(text_of("prot_volt", 0), "OFF");
    CHECK_STR_EQ(text_of("prot_volt", 28), "2.8V/cell");
    CHECK_STR_EQ(text_of("prot_cells", 0), "AUTO");
    CHECK_STR_EQ(text_of("bec", 4), "12V");
    CHECK_STR_EQ(text_of("throt_rev", 3), "25%");
    CHECK_STR_EQ(text_of("telem_volt", -80), "-80");
    CHECK_STR_EQ(text_of("throt_min", 1000), "1000us");
    CHECK_STR_EQ(text_of("throt_min", 1005), "raw 1005");
    CHECK_STR_EQ(text_of("telem_volt", -81), "raw -81");
    /* A key a read did not bring. */
    CHECK_STR_EQ(text_of("arm", EW_KEY_NONE), "");
    /* A buffer of no bytes and of one. */
    char one[1] = { 'x' };

    CHECK_EQ(ew_key_text(key("arm"), 1, one, 0u), 0u);
    CHECK_EQ(one[0], 'x');
    CHECK_EQ(ew_key_text(key("arm"), 1, one, 1u), 0u);
    CHECK_EQ(one[0], '\0');
}

TEST_CASE(the_warning_marks)
{
    /* Any change names these; a protection only when it goes to 0. */
    static const char *const k_any[] = { "brushed", "bec", "input_mode" };
    static const char *const k_zero[] = {
        "prot_stall", "prot_temp", "prot_sens", "prot_volt", "prot_cells",
        "prot_curr", "prot_park",
    };
    int marked = 0;

    for (size_t i = 0; i < 3u; ++i) {
        CHECK_EQ(key(k_any[i])->flags, EW_FF_WARN);
    }
    for (size_t i = 0; i < 7u; ++i) {
        CHECK_EQ(key(k_zero[i])->flags, EW_FF_WARN | EW_FF_WARN_ZERO);
    }
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        marked += ew_map_e32[i].flags != 0u;
    }
    CHECK_EQ(marked, 10);
}

TEST_CASE(the_map_of_the_firmware)
{
    size_t count = 9u;

    CHECK(ew_map_table(EW_FW_E32, &count) == NULL);
    CHECK_EQ(count, 0u);
    CHECK_EQ(ew_map_len(EW_FW_E32), 92u);
    for (unsigned l = 0u; l < 256u; ++l) {
        CHECK_EQ(ew_map_writable(EW_FW_E32, (uint8_t)l), l == 17u);
    }
    uint8_t page[EW_MAP_LEN_E32];

    ew_e32_put(page, 45u, -80);
    CHECK_EQ(page[90], 0xB0u);
    CHECK_EQ(page[91], 0xFFu);
    CHECK_EQ(ew_e32_get(page, 45u), -80);
}

TEST_CASE(a_draft_of_keys)
{
    uint8_t orig[EW_MAP_LEN_E32];
    uint8_t draft[EW_MAP_LEN_E32];
    uint16_t off = 999u;
    size_t slot = 0u;
    const size_t freq_min = (ew_key_find("freq_min", 8u, &slot), slot);
    const size_t freq_max = (ew_key_find("freq_max", 8u, &slot), slot);
    const size_t timing = (ew_key_find("timing", 6u, &slot), slot);

    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        ew_e32_put(orig, i, ew_map_e32[i].lo);
    }
    ew_e32_put(orig, freq_min, 24);
    ew_e32_put(orig, freq_max, 48);
    memcpy(draft, orig, sizeof(orig));
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_OK);
    ew_e32_put(draft, timing, 31);
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_OK);
    ew_e32_put(draft, timing, 32);
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_VALUE);
    CHECK_EQ(off, timing * 2u);
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, NULL), EW_IMG_VALUE);
    /* A value out of range the ESC already held is not this write's. */
    memcpy(orig, draft, sizeof(orig));
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_OK);
    ew_e32_put(orig, timing, 16);
    ew_e32_put(draft, timing, 16);
    /* A maximum not below its minimum: equal is fine, one under is not. */
    ew_e32_put(draft, freq_max, 24);
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_OK);
    ew_e32_put(draft, freq_max, 23);
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_VALUE);
    CHECK_EQ(off, freq_max * 2u);
    /* The minimum moved over a maximum left alone. */
    ew_e32_put(draft, freq_max, 48);
    ew_e32_put(orig, freq_max, 30);
    ew_e32_put(draft, freq_max, 30);
    ew_e32_put(draft, freq_min, 31);
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_VALUE);
    CHECK_EQ(off, freq_max * 2u);
    /* Both already that way and neither changed: not this write's. */
    ew_e32_put(orig, freq_min, 40);
    ew_e32_put(draft, freq_min, 40);
    CHECK_EQ(ew_image_check(EW_FW_E32, 17u, orig, draft, 92u, &off), EW_IMG_OK);
}

int main(void)
{
    RUN(the_lines_byte_for_byte);
    RUN(a_line_that_does_not_fit);
    RUN(numbers_at_their_limits_and_in_other_forms);
    RUN(the_last_line_of_a_reply);
    RUN(a_capture_that_is_not_whole);
    RUN(info_with_its_live_values);
    RUN(a_target_name_at_its_size_and_past_it);
    RUN(revisions_at_and_past_their_limits);
    RUN(show_of_47_keys);
    RUN(show_in_another_order_and_empty_music);
    RUN(music_at_255_characters_and_256);
    RUN(the_longest_show_fits_the_buffer);
    RUN(show_with_a_key_missing_repeated_unknown_or_bad);
    RUN(the_reply_of_set);
    RUN(the_keys_as_the_wire_has_them);
    RUN(every_key_at_both_ends_and_one_past);
    RUN(the_text_of_a_value);
    RUN(the_warning_marks);
    RUN(the_map_of_the_firmware);
    RUN(a_draft_of_keys);
    return test_summary("ew_text");
}
