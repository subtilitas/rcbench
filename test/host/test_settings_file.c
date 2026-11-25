/*
 * The settings as a text file (shared/settings/settings_file.c): every
 * setting and string out and back, the header and its versions, the lines
 * a load refuses with their codes and line numbers, and what a load puts in
 * force.  And the strings of the labelled servos: ID PREFIX and the label
 * printer's bind, with their keys, defaults and limits.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "settings_file.h"
#include "settings_rig.h"
#include "settings_sensors.h"

/** The text of a file, and room for one written with CR LF. */
static char g_text[2u * SF_FILE_MAX];
static sf_load_t g_l;
static float g_want[SETTING_COUNT];
static char g_want_text[SETTING_TEXT_COUNT][SETTINGS_TEXT_MAX];

/** The model on the memory store, with values and strings. */
static void fresh_store(void)
{
    fresh_model();
    s_has_text = false;
    settings_set_store(&s_text_store);
    settings_init();
}

/** Every setting moved off its default where the schema allows, every
 *  string set, all of it saved: the stored copy a file is written from. */
static void every_value_moved(void)
{
    fresh_store();
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (settings_derived(id)) {
            continue;
        }
        const float was = settings_get(id);
        settings_adjust(id, 1);
        if (settings_get(id) == was) {
            settings_adjust(id, -1);
        }
    }
    static const char *const texts[SETTING_TEXT_COUNT] = {
        "Wing \"L\" 2\\3", "MY/AUTO", "MY/USER",
        "00:0C:BF:08:19:29 P", "WT901BLE68",
        "D4:F9:8D:00:3A:10 R", "DIAL-3A10",
        "AA:BB:CC:DD:EE:FF P", "rcbench-pwr-3456",
        "11:22:33:44:55:66 R", "12345678901234567890123",
        "after 200 cycles", "KST X10 LEFT WING",
        "A4:C1:38:00:3A:10 P", "T50M Pro",
    };
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        settings_set_text((setting_text_id_t)i, texts[i]);
    }
    CHECK(settings_save());
    for (int i = 0; i < SETTING_COUNT; ++i) {
        g_want[i] = settings_get((setting_id_t)i);
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        memcpy(g_want_text[i], settings_text((setting_text_id_t)i),
               SETTINGS_TEXT_MAX);
    }
}

/** Write the stored copy; the bytes. */
static size_t write_file(void)
{
    const size_t n = sf_format(g_text, sizeof(g_text), "0.23.15",
                               "2026-11-21 14:05:33 UTC");
    CHECK(n > 0u);
    return n;
}

/** The text @p body after a format 1 header, and `end`. */
static size_t file_of(const char *body)
{
    const int n = snprintf(g_text, sizeof(g_text),
                           "rcbench settings, format 1\n%send\n", body);
    return (size_t)n;
}

/* --------------------------------------------------------------- the file */

TEST_CASE(every_setting_and_string_goes_out_and_comes_back)
{
    every_value_moved();
    int moved = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        moved += (g_want[i] != settings_def((setting_id_t)i)->def) ? 1 : 0;
    }
    /* Every setting has a second value; the derived SCL moves with SDA. */
    CHECK_EQ(moved, SETTING_COUNT);
    const size_t n = write_file();

    fresh_store();
    unsigned line = 99u;
    CHECK_EQ(sf_parse(g_text, n, &g_l, &line), SF_OK);
    CHECK_EQ(line, 0u);
    CHECK_EQ(g_l.refused_n, 0);
    CHECK_EQ(g_l.taken, SETTING_COUNT - 1 + SETTING_TEXT_COUNT);
    CHECK_EQ(g_l.head.format, 1u);
    CHECK_STR_EQ(g_l.head.version, "0.23.15");
    CHECK_STR_EQ(g_l.head.written, "2026-11-21 14:05:33 UTC");
    CHECK(sf_differs(&g_l));
    CHECK(sf_apply(&g_l) > 0);
    CHECK_EQ(g_l.refused_n, 0);
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (settings_get((setting_id_t)i) != g_want[i]) {
            T_FAIL("%s: got %g, want %g", settings_def((setting_id_t)i)->key,
                   (double)settings_get((setting_id_t)i), (double)g_want[i]);
        }
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        CHECK_STR_EQ(settings_text((setting_text_id_t)i), g_want_text[i]);
    }
    /* A load is an edit: the save is the caller's. */
    CHECK(settings_dirty());
    CHECK(settings_save());
    /* And the file the loaded panel writes is the same file. */
    static char again[2u * SF_FILE_MAX];
    const size_t m = sf_format(again, sizeof(again), "0.23.15",
                               "2026-11-21 14:05:33 UTC");
    CHECK_EQ(m, n);
    CHECK(memcmp(again, g_text, n) == 0);
}

TEST_CASE(every_option_of_every_choice_reads_back_as_itself)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        const setting_def_t *d = settings_def(id);
        if (d->type != SET_TYPE_ENUM && d->type != SET_TYPE_BOOL) {
            continue;
        }
        const int n = (d->type == SET_TYPE_BOOL)
                          ? 2 : settings_sensors_offered(d);
        for (int o = 0; o < n; ++o) {
            fresh_store();
            settings_set(id, (float)o);
            CHECK(settings_save());
            const size_t len = write_file();
            fresh_store();
            CHECK_EQ(sf_parse(g_text, len, &g_l, NULL), SF_OK);
            if (g_l.values[i] != (float)o || g_l.refused_n != 0) {
                T_FAIL("%s option %d read as %g, %u refused", d->key, o,
                       (double)g_l.values[i], (unsigned)g_l.refused_n);
            }
        }
    }
}

TEST_CASE(the_file_is_text_a_person_reads_and_it_fits_its_bounds)
{
    every_value_moved();
    const size_t n = write_file();
    CHECK(n < SF_FILE_MAX / 2u);
    /* Every line printable ASCII or UTF-8, under SF_LINE_MAX, LF ended. */
    size_t start = 0u;
    unsigned lines = 0u;
    for (size_t i = 0u; i < n; ++i) {
        const unsigned char c = (unsigned char)g_text[i];
        if (c == '\n') {
            if (i - start > SF_LINE_MAX) {
                T_FAIL("line %u has %zu characters", lines + 1u, i - start);
            }
            start = i + 1u;
            ++lines;
        } else if (c < 0x20u || c == 0x7Fu) {
            T_FAIL("byte 0x%02X at %zu", c, i);
        }
    }
    CHECK_EQ(g_text[n - 1u], '\n');
    static const char head[] = "rcbench settings, format 1\nPanel: 0.23.15\n"
                               "Written: 2026-11-21 14:05:33 UTC\n";
    CHECK(strncmp(g_text, head, sizeof(head) - 1u) == 0);
    CHECK(strstr(g_text, "\nend\n") != NULL);
    /* No Wi-Fi in it, and a line says so. */
    CHECK(strstr(g_text, "Not in this file: the Wi-Fi home network") != NULL);
    CHECK(strstr(g_text, "ssid") == NULL);
    CHECK(strstr(g_text, "pass =") == NULL);
    /* Sensor SCL follows SDA and has no line. */
    CHECK(strstr(g_text, "\nsns_scl") == NULL);
    CHECK(strstr(g_text, "\nsns_sda") != NULL);
    /* A line, its comment, as written. */
    CHECK(strstr(g_text, "\ntheme = ") != NULL);
    CHECK(strstr(g_text, "srv_dut = \"Wing \\\"L\\\" 2\\\\3\"") != NULL);
    /* With CR LF line ends it still fits, and reads. */
    static char crlf[2u * SF_FILE_MAX];
    size_t m = 0u;
    for (size_t i = 0u; i < n; ++i) {
        if (g_text[i] == '\n') {
            crlf[m++] = '\r';
        }
        crlf[m++] = g_text[i];
    }
    CHECK(m <= SF_FILE_MAX);
    fresh_store();
    CHECK_EQ(sf_parse(crlf, m, &g_l, NULL), SF_OK);
    CHECK_EQ(g_l.refused_n, 0);
    CHECK_STR_EQ(g_l.texts[SET_TEXT_DUT_NAME], "Wing \"L\" 2\\3");
}

TEST_CASE(a_buffer_too_small_writes_nothing)
{
    every_value_moved();
    const size_t n = write_file();
    static char small[2u * SF_FILE_MAX];
    CHECK_EQ(sf_format(small, n, "0.23.15", "2026-11-21 14:05:33 UTC"), 0u);
    CHECK_EQ(small[0], '\0');
    CHECK_EQ(sf_format(small, n + 1u, "0.23.15", "2026-11-21 14:05:33 UTC"),
             n);
    CHECK_EQ(sf_format(NULL, 100u, "v", "w"), 0u);
    /* No version, no clock: "not known", read back as "". */
    const size_t k = sf_format(small, sizeof(small), NULL, "");
    CHECK(strstr(small, "Panel: not known\nWritten: not known\n") != NULL);
    CHECK_EQ(sf_parse(small, k, &g_l, NULL), SF_OK);
    CHECK_STR_EQ(g_l.head.version, "");
    CHECK_STR_EQ(g_l.head.written, "");
}

/* ------------------------------------------------------------- versions */

TEST_CASE(the_header_decides_and_a_newer_format_is_refused_whole)
{
    fresh_store();
    unsigned line = 0u;
    const char *body = "pack_cells = 6\n";
    char t[256];
    snprintf(t, sizeof(t), "rcbench settings, format 2\n%send\n", body);
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, &line), SF_ERR_NEWER);
    CHECK_EQ(line, 1u);
    CHECK_EQ(g_l.head.format, 2u);
    /* Nothing of a refused file is taken. */
    CHECK_FEQ(g_l.values[SET_PACK_CELLS], settings_def(SET_PACK_CELLS)->def);
    snprintf(t, sizeof(t), "rcbench settings, format 999\nend\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, NULL), SF_ERR_NEWER);
    snprintf(t, sizeof(t), "rcbench settings, format 1000\nend\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, NULL), SF_ERR_VERSION);
    snprintf(t, sizeof(t), "rcbench settings, format 0\nend\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, NULL), SF_ERR_VERSION);
    snprintf(t, sizeof(t), "rcbench settings, format 1a\nend\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, NULL), SF_ERR_VERSION);
    snprintf(t, sizeof(t), "rcbench settings, format \nend\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, NULL), SF_ERR_VERSION);
    snprintf(t, sizeof(t), "KST servo configuration, format 2\nend\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, NULL), SF_ERR_NOT_SETTINGS);
    CHECK_EQ(sf_parse("", 0u, &g_l, NULL), SF_ERR_NOT_SETTINGS);
    CHECK_EQ(sf_parse(NULL, 0u, &g_l, NULL), SF_ERR_ARG);
    CHECK_EQ(sf_parse(t, 1u, NULL, NULL), SF_ERR_ARG);
    /* Case, spaces round it and a byte order mark are taken. */
    snprintf(t, sizeof(t), "\xEF\xBB\xBF  RCBench Settings, Format 1 \r\n"
                           "pack_cells = 6\r\nEND\r\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, &line), SF_OK);
    CHECK_FEQ(g_l.values[SET_PACK_CELLS], 6.0f);
    /* Over SF_FILE_MAX: refused before it is read. */
    static char big[SF_FILE_MAX + 2u];
    memset(big, ' ', sizeof(big));
    memcpy(big, "rcbench settings, format 1\n", 27u);
    CHECK_EQ(sf_parse(big, SF_FILE_MAX, &g_l, NULL), SF_ERR_TRUNCATED);
    CHECK_EQ(sf_parse(big, SF_FILE_MAX + 1u, &g_l, NULL), SF_ERR_OVERSIZED);
}

TEST_CASE(a_file_without_end_is_cut_and_loads_nothing)
{
    every_value_moved();
    const size_t n = write_file();
    /* Cut at every line end before `end`: refused each time, the line the
     * last one read. */
    unsigned cuts = 0u;
    for (size_t i = 0u; i + 5u < n; ++i) {
        if (g_text[i] != '\n') {
            continue;
        }
        unsigned line = 0u;
        if (sf_parse(g_text, i + 1u, &g_l, &line) != SF_ERR_TRUNCATED) {
            T_FAIL("cut at %zu loads", i);
        }
        ++cuts;
    }
    CHECK(cuts > (unsigned)SETTING_COUNT);
    /* A cut inside the last line. */
    CHECK_EQ(sf_parse(g_text, n - 2u, &g_l, NULL), SF_ERR_TRUNCATED);
    /* A line after `end` makes it not the last. */
    char t[128];
    snprintf(t, sizeof(t), "rcbench settings, format 1\nend\ntheme = DARK\n");
    unsigned line = 0u;
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, &line), SF_ERR_TRUNCATED);
    CHECK_EQ(line, 3u);
    /* A comment and blank lines after it do not. */
    snprintf(t, sizeof(t), "rcbench settings, format 1\nend # done\n\n# x\n");
    CHECK_EQ(sf_parse(t, strlen(t), &g_l, NULL), SF_OK);
}

TEST_CASE(the_head_reads_from_the_first_bytes)
{
    every_value_moved();
    write_file();
    sf_head_t h;
    CHECK_EQ(sf_head(g_text, 256u, &h), SF_OK);
    CHECK_EQ(h.format, 1u);
    CHECK_STR_EQ(h.version, "0.23.15");
    CHECK_STR_EQ(h.written, "2026-11-21 14:05:33 UTC");
    /* Cut inside the date: taken to the cut. */
    CHECK_EQ(sf_head(g_text, 27u + 15u + 14u, &h), SF_OK);
    CHECK_STR_EQ(h.written, "2026-");
    /* Without the two lines: not known. */
    const char *t = "rcbench settings, format 1\ntheme = DARK\n"
                    "Panel: 9.9.9\nend\n";
    CHECK_EQ(sf_head(t, strlen(t), &h), SF_OK);
    CHECK_STR_EQ(h.version, "");
    CHECK_EQ(sf_head("rcbench settings, format 3\n", 27u, &h), SF_ERR_NEWER);
    CHECK_EQ(sf_head("hello\n", 6u, &h), SF_ERR_NOT_SETTINGS);
    CHECK_EQ(sf_head(NULL, 6u, &h), SF_ERR_ARG);
    CHECK_EQ(sf_head(t, 6u, NULL), SF_ERR_ARG);
}

/* ------------------------------------------------------- refused lines */

TEST_CASE(each_refused_line_is_skipped_and_listed_with_its_code_and_line)
{
    fresh_store();
    const size_t n = file_of(
        "Panel: 0.23.15\n"                         /* 2 */
        "Written: not known\n"                     /* 3 */
        "# a comment\n"                            /* 4 */
        "pack_cells = 6\n"                         /* 5: taken */
        "pack_cells = 8\n"                         /* 6: TWICE */
        "no_such_key = 1\n"                        /* 7: KEY */
        "theme DARK\n"                             /* 8: MALFORMED */
        " = 4\n"                                   /* 9: MALFORMED */
        "theme = PURPLE\n"                         /* 10: VALUE */
        "pack_mah = 12x\n"                         /* 11: VALUE */
        "pack_mah = 30100\n"                       /* 12: RANGE */
        "brightness = 15\n"                        /* 13: RANGE, under 20 */
        "sns_scl = 5\n"                            /* 14: DERIVED */
        "srv_dut = SERVO\n"                        /* 15: VALUE */
        "srv_dut = \"123456789012345678901234\"\n" /* 16: LONG */
        "imu_name = \"bad\\\"\n"                   /* 17: VALUE */
        "Owner: me\n"                              /* 18: MALFORMED */
        "THEME = light   # case of key and value\n" /* 19: taken */
        "\n"                                       /* 20 */
        "\t log_auto_dir\t=\t\"A/B\"\t\n");        /* 21: taken */
    unsigned line = 0u;
    CHECK_EQ(sf_parse(g_text, n, &g_l, &line), SF_OK);
    CHECK_EQ(line, 0u);
    CHECK_EQ(g_l.taken, 3);
    static const struct { unsigned line; sf_line_err_t code; const char *key; }
        want[] = {
            { 6, SF_LINE_TWICE, "pack_cells" },
            { 7, SF_LINE_KEY, "no_such_key" },
            { 8, SF_LINE_MALFORMED, "theme DARK" },
            { 9, SF_LINE_MALFORMED, "" },
            { 10, SF_LINE_VALUE, "theme" },
            { 11, SF_LINE_VALUE, "pack_mah" },
            { 12, SF_LINE_RANGE, "pack_mah" },
            { 13, SF_LINE_RANGE, "brightness" },
            { 14, SF_LINE_DERIVED, "sns_scl" },
            { 15, SF_LINE_VALUE, "srv_dut" },
            { 16, SF_LINE_LONG, "srv_dut" },
            { 17, SF_LINE_VALUE, "imu_name" },
            { 18, SF_LINE_MALFORMED, "Owner: me" },
        };
    const size_t nw = sizeof(want) / sizeof(want[0]);
    CHECK_EQ(g_l.refused_n, (uint16_t)nw);
    for (size_t i = 0u; i < nw && i < SF_REFUSED_MAX; ++i) {
        CHECK_EQ(g_l.refused[i].line, want[i].line);
        CHECK_EQ(g_l.refused[i].code, want[i].code);
        CHECK_STR_EQ(g_l.refused[i].key, want[i].key);
    }
    CHECK_FEQ(g_l.values[SET_PACK_CELLS], 6.0f);
    CHECK_EQ(g_l.value_line[SET_PACK_CELLS], 5);
    CHECK_FEQ(g_l.values[SET_THEME], 1.0f);
    CHECK_STR_EQ(g_l.texts[SET_TEXT_LOG_AUTO_DIR], "A/B");
    /* Refused and missing ones read as the defaults. */
    CHECK_FEQ(g_l.values[SET_PACK_MAH], settings_def(SET_PACK_MAH)->def);
    CHECK_STR_EQ(g_l.texts[SET_TEXT_DUT_NAME], "SERVO");
    CHECK_EQ(g_l.value_line[SET_PACK_MAH], 0);
}

TEST_CASE(more_refused_lines_than_the_list_holds_are_counted)
{
    fresh_store();
    char body[4096] = "";
    for (int i = 0; i < 40; ++i) {
        char l[32];
        snprintf(l, sizeof(l), "k%d = 1\n", i);
        strcat(body, l);
    }
    const size_t n = file_of(body);
    CHECK_EQ(sf_parse(g_text, n, &g_l, NULL), SF_OK);
    CHECK_EQ(g_l.refused_n, 40);
    CHECK_EQ(g_l.refused[SF_REFUSED_MAX - 1u].line, (uint16_t)(1 + SF_REFUSED_MAX));
    CHECK_STR_EQ(g_l.refused[0].key, "k0");
    /* A key longer than the room is cut. */
    file_of("a_key_of_twenty_six_chars = 1\n");
    CHECK_EQ(sf_parse(g_text, strlen(g_text), &g_l, NULL), SF_OK);
    CHECK_STR_EQ(g_l.refused[0].key, "a_key_of_twenty");
}

/* ------------------------------------------------------- values at limits */

TEST_CASE(a_number_is_taken_at_its_limits_and_on_its_grid)
{
    fresh_store();
    const setting_def_t *d = settings_def(SET_BRIGHTNESS);   /* 20..100, 5 */
    CHECK(settings_takes(SET_BRIGHTNESS, d->min));
    CHECK(settings_takes(SET_BRIGHTNESS, d->max));
    CHECK(!settings_takes(SET_BRIGHTNESS, d->min - d->step));
    CHECK(!settings_takes(SET_BRIGHTNESS, d->max + d->step));
    /* Off the grid by less than half a step: snapped, taken. */
    CHECK(settings_takes(SET_BRIGHTNESS, 52.0f));
    CHECK(!settings_takes(SET_BRIGHTNESS, 102.0f + 0.6f));
    CHECK(!settings_takes(SET_BRIGHTNESS, NAN));
    CHECK(!settings_takes(SET_BRIGHTNESS, INFINITY));
    CHECK(!settings_takes((setting_id_t)-1, 1.0f));
    CHECK(!settings_takes(SETTING_COUNT, 1.0f));
    /* A switch is 0 or 1. */
    CHECK(settings_takes(SET_LOG_AUTO, 0.0f));
    CHECK(settings_takes(SET_LOG_AUTO, 1.0f));
    CHECK(!settings_takes(SET_LOG_AUTO, 2.0f));
    /* A choice one of its options. */
    const setting_def_t *t = settings_def(SET_THEME);
    CHECK(settings_takes(SET_THEME, (float)(t->option_count - 1)));
    CHECK(!settings_takes(SET_THEME, (float)t->option_count));
    CHECK(!settings_takes(SET_THEME, -1.0f));
    /* A pin the row does not take. */
    CHECK(settings_takes(SET_SENSE_SDA, -1.0f));
    CHECK(settings_takes(SET_SENSE_SDA, 4.0f));
    CHECK(!settings_takes(SET_SENSE_SDA, 5.0f));
    /* Through a file: the limits in, one step past them out. */
    char body[160];
    snprintf(body, sizeof(body), "brightness = %g\ncontrast = 10000\n",
             (double)d->max);
    CHECK_EQ(sf_parse(g_text, file_of(body), &g_l, NULL), SF_OK);
    CHECK_FEQ(g_l.values[SET_BRIGHTNESS], d->max);
    CHECK_EQ(g_l.refused_n, 1);
    CHECK_EQ(g_l.refused[0].code, SF_LINE_RANGE);
    /* A number written with a sign, a point, or neither digit. */
    CHECK_EQ(sf_parse(g_text, file_of("brightness = +60.\n"), &g_l, NULL),
             SF_OK);
    CHECK_FEQ(g_l.values[SET_BRIGHTNESS], 60.0f);
    CHECK_EQ(sf_parse(g_text, file_of("brightness = .\n"), &g_l, NULL),
             SF_OK);
    CHECK_EQ(g_l.refused[0].code, SF_LINE_VALUE);
    CHECK_EQ(sf_parse(g_text, file_of("brightness = 0x40\n"), &g_l, NULL),
             SF_OK);
    CHECK_EQ(g_l.refused[0].code, SF_LINE_VALUE);
    CHECK_EQ(sf_parse(g_text, file_of("brightness = 1e2\n"), &g_l, NULL),
             SF_OK);
    CHECK_EQ(g_l.refused[0].code, SF_LINE_VALUE);
}

/* ----------------------------------------------------------- putting it in */

static bool g_hold_brightness;

static bool hold_brightness(setting_id_t id)
{
    return g_hold_brightness && id == SET_BRIGHTNESS;
}

TEST_CASE(a_load_sets_the_file_and_the_defaults_and_leaves_a_held_one)
{
    fresh_store();
    settings_set(SET_PACK_MAH, 2200.0f);
    settings_set(SET_BRIGHTNESS, 40.0f);
    settings_set_text(SET_TEXT_DIAL_NAME, "DIAL-0001");
    CHECK(settings_save());
    /* The file names brightness and cells; pack_mah and the Dial's name
     * have no line and go back to their defaults. */
    const size_t n = file_of("pack_cells = 4\nbrightness = 80\n");
    CHECK_EQ(sf_parse(g_text, n, &g_l, NULL), SF_OK);
    CHECK(sf_differs(&g_l));
    g_hold_brightness = true;
    settings_set_hold(hold_brightness);
    const int n_changed = sf_apply(&g_l);
    settings_set_hold(NULL);
    g_hold_brightness = false;
    CHECK_EQ(n_changed, 3);
    CHECK_FEQ(settings_get(SET_PACK_CELLS), 4.0f);
    CHECK_FEQ(settings_get(SET_PACK_MAH), settings_def(SET_PACK_MAH)->def);
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_NAME), "");
    /* Held: left at 40 and listed with its line. */
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 40.0f);
    CHECK_EQ(g_l.refused_n, 1);
    CHECK_EQ(g_l.refused[0].code, SF_LINE_HELD);
    CHECK_EQ(g_l.refused[0].line, 3);
    CHECK_STR_EQ(g_l.refused[0].key, "brightness");
    CHECK_EQ(sf_apply(NULL), 0);
    CHECK(!sf_differs(NULL));
}

TEST_CASE(a_file_of_the_stored_settings_differs_in_nothing)
{
    every_value_moved();
    const size_t n = write_file();
    CHECK_EQ(sf_parse(g_text, n, &g_l, NULL), SF_OK);
    CHECK(!sf_differs(&g_l));
    /* One string apart is a difference. */
    snprintf(g_l.texts[SET_TEXT_BNODE_NAME], SETTINGS_TEXT_MAX, "other");
    CHECK(sf_differs(&g_l));
    /* A value under half a step apart is not. */
    CHECK_EQ(sf_parse(g_text, n, &g_l, NULL), SF_OK);
    g_l.values[SET_BRIGHTNESS] += 0.4f * settings_def(SET_BRIGHTNESS)->step;
    CHECK(!sf_differs(&g_l));
    g_l.values[SET_BRIGHTNESS] += 0.2f * settings_def(SET_BRIGHTNESS)->step;
    CHECK(sf_differs(&g_l));
}

TEST_CASE(a_fresh_panel_stores_the_defaults_only)
{
    fresh_store();
    CHECK(settings_stored_defaults());
    /* An edit not saved is not the stored copy. */
    settings_set(SET_BRIGHTNESS, 40.0f);
    CHECK(settings_stored_defaults());
    CHECK(settings_save());
    CHECK(!settings_stored_defaults());
    /* A string alone. */
    fresh_store();
    settings_set_text(SET_TEXT_IMU_NAME, "x");
    CHECK(settings_save());
    CHECK(!settings_stored_defaults());
    /* Defaults written back by an erase and a save: defaults. */
    settings_reset_all();
    CHECK(settings_save());
    CHECK(settings_stored_defaults());
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        CHECK_STR_EQ(settings_text_default((setting_text_id_t)i),
                     settings_text((setting_text_id_t)i));
    }
    CHECK_STR_EQ(settings_text_default((setting_text_id_t)-1), "");
    CHECK_STR_EQ(settings_text_default(SETTING_TEXT_COUNT), "");
}

/*
 * ID PREFIX and the label printer's bind: three strings set off SETUP, their
 * keys and defaults, a build that never held them loading the defaults, and
 * the prefix held to 17 characters where every other string takes 23.
 */
TEST_CASE(the_servo_prefix_and_printer_strings_are_kept_with_their_limits)
{
    fresh_model();
    CHECK_STR_EQ(settings_text_key(SET_TEXT_SERVO_PREFIX), "srv_pfx");
    CHECK_STR_EQ(settings_text_key(SET_TEXT_PRN_ADDR), "prn_ble");
    CHECK_STR_EQ(settings_text_key(SET_TEXT_PRN_NAME), "prn_name");
    CHECK_STR_EQ(settings_text_default(SET_TEXT_SERVO_PREFIX), "SERVO");
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "SERVO");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), "");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_NAME), "");
    CHECK_EQ(settings_text_category(SET_TEXT_SERVO_PREFIX), SET_CAT_COUNT);
    CHECK_EQ(settings_text_category(SET_TEXT_PRN_ADDR), SET_CAT_COUNT);
    CHECK_EQ(settings_text_category(SET_TEXT_PRN_NAME), SET_CAT_COUNT);

    /* The limits. */
    CHECK_EQ(settings_text_max(SET_TEXT_SERVO_PREFIX), 17);
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (i != SET_TEXT_SERVO_PREFIX) {
            CHECK_EQ(settings_text_max((setting_text_id_t)i),
                     SETTINGS_TEXT_MAX - 1);
        }
    }
    CHECK_EQ(settings_text_max(SETTING_TEXT_COUNT), 0);
    CHECK_EQ(settings_text_max((setting_text_id_t)-1), 0);
    settings_set_text(SET_TEXT_SERVO_PREFIX, "KST X10 LEFT WING");
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "KST X10 LEFT WING");
    settings_set_text(SET_TEXT_SERVO_PREFIX, "KST X10 LEFT WINGS");
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "KST X10 LEFT WING");
    settings_set_text(SET_TEXT_PRN_NAME, "T50M Pro-1234567890ABCDEF");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_NAME), "T50M Pro-1234567890ABCD");

    /* Kept and loaded back; a store of an older build gives the defaults. */
    s_has_text = false;
    settings_set_store(&s_text_store);
    settings_init();
    settings_set_text(SET_TEXT_SERVO_PREFIX, "KST_X10");
    settings_set_text(SET_TEXT_PRN_ADDR, "A4:C1:38:00:3A:10 P");
    settings_set_text(SET_TEXT_PRN_NAME, "T50M Pro");
    CHECK(settings_save());
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "KST_X10");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), "A4:C1:38:00:3A:10 P");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_NAME), "T50M Pro");
    /* A medium holding a longer prefix gives 17 characters of it. */
    snprintf(s_saved_text[SET_TEXT_SERVO_PREFIX], SETTINGS_TEXT_MAX, "%s",
             "ABCDEFGHIJKLMNOPQRSTUVW");
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "ABCDEFGHIJKLMNOPQ");
    settings_set_store(&s_mem_store);   /* keeps no string */
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "SERVO");
    CHECK_STR_EQ(settings_text(SET_TEXT_PRN_ADDR), "");
    settings_set_store(NULL);
}

/** One string line alone in a file, parsed: whether it was taken. */
static bool one_text_line(const char *key, const char *value)
{
    char body[96];
    (void)snprintf(body, sizeof(body), "%s = \"%s\"\n", key, value);
    const size_t n = file_of(body);
    CHECK_EQ(sf_parse(g_text, n, &g_l, NULL), SF_OK);
    return g_l.refused_n == 0 && g_l.taken == 1;
}

/*
 * A file holds a string to its own limit: ID PREFIX to 17 characters, the
 * printer's address and name to 23.  A longer prefix is refused with
 * SF_LINE_LONG, as any string over its limit, and the setting keeps its
 * value; an applied and saved file then differs in nothing.
 */
TEST_CASE(a_file_holds_each_new_string_to_its_own_limit)
{
    static const char k17[] = "KST X10 LEFT WING";
    static const char k18[] = "KST X10 LEFT WINGS";
    static const char k23[] = "KST X10 LEFT WING ABCDE";
    static const char k24[] = "KST X10 LEFT WING ABCDEF";

    fresh_store();
    CHECK(one_text_line("srv_pfx", k17));
    CHECK_STR_EQ(g_l.texts[SET_TEXT_SERVO_PREFIX], k17);
    CHECK(sf_apply(&g_l) > 0);
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), k17);
    CHECK(settings_save());
    CHECK(!sf_differs(&g_l));

    const char *const longer[] = { k18, k23, k24 };
    for (size_t i = 0u; i < sizeof(longer) / sizeof(longer[0]); ++i) {
        CHECK(!one_text_line("srv_pfx", longer[i]));
        CHECK_EQ(g_l.refused_n, 1);
        CHECK_EQ(g_l.refused[0].code, SF_LINE_LONG);
        CHECK_STR_EQ(g_l.refused[0].key, "srv_pfx");
        CHECK_EQ(g_l.refused[0].line, 2u);
        CHECK_STR_EQ(g_l.texts[SET_TEXT_SERVO_PREFIX], "SERVO");
        (void)sf_apply(&g_l);
        CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "SERVO");
        CHECK(settings_save());
        CHECK(!sf_differs(&g_l));
    }

    static const setting_text_id_t k_full[] = { SET_TEXT_PRN_ADDR,
                                                SET_TEXT_PRN_NAME };
    for (size_t i = 0u; i < sizeof(k_full) / sizeof(k_full[0]); ++i) {
        const char *key = settings_text_key(k_full[i]);
        CHECK(one_text_line(key, k17));
        CHECK(one_text_line(key, k18));
        CHECK(one_text_line(key, k23));
        CHECK_STR_EQ(g_l.texts[k_full[i]], k23);
        CHECK(!one_text_line(key, k24));
        CHECK_EQ(g_l.refused[0].code, SF_LINE_LONG);
    }
    settings_set_store(NULL);
}

int main(void)
{
    RUN(every_setting_and_string_goes_out_and_comes_back);
    RUN(every_option_of_every_choice_reads_back_as_itself);
    RUN(the_file_is_text_a_person_reads_and_it_fits_its_bounds);
    RUN(a_buffer_too_small_writes_nothing);
    RUN(the_header_decides_and_a_newer_format_is_refused_whole);
    RUN(a_file_without_end_is_cut_and_loads_nothing);
    RUN(the_head_reads_from_the_first_bytes);
    RUN(each_refused_line_is_skipped_and_listed_with_its_code_and_line);
    RUN(more_refused_lines_than_the_list_holds_are_counted);
    RUN(a_number_is_taken_at_its_limits_and_on_its_grid);
    RUN(a_load_sets_the_file_and_the_defaults_and_leaves_a_held_one);
    RUN(a_file_of_the_stored_settings_differs_in_nothing);
    RUN(a_fresh_panel_stores_the_defaults_only);
    RUN(the_servo_prefix_and_printer_strings_are_kept_with_their_limits);
    RUN(a_file_holds_each_new_string_to_its_own_limit);
    return test_summary("settings_file");
}
