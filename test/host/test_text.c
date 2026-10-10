/*
 * The string tables: every language complete, every format converting what
 * the English converts, every string inside the field it is drawn in, and
 * the language following SET_LANGUAGE.
 *
 * The widths of the strings a screenshot shows are measured on the screen
 * by tools/render_ui.py --fit; the ones no screenshot reaches carry their
 * field's width in ui_text.def, and this suite holds them to it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "busfault_screen.h"
#include "esc_profile.h"
#include "esc_stick.h"
#include "gfx.h"
#include "programmer_screen.h"
#include "servo_test.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"

/*
 * A format's conversions, as a string: what each one takes from the
 * argument list, in order.  Flags, widths and precisions written as digits
 * change how a value looks and not what is read, so they are left out; a
 * '*' reads an int, so it is kept.
 */
static void conversions(const char *fmt, char *out, size_t n)
{
    size_t k = 0;
    out[0] = '\0';
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            continue;
        }
        ++p;
        if (*p == '%') {
            continue;
        }
        while (*p != '\0' && strchr("-+ #0", *p) != NULL) {
            ++p;
        }
        while (*p == '*' || (*p >= '0' && *p <= '9')) {
            if (*p == '*' && k + 1 < n) {
                out[k++] = '*';
            }
            ++p;
        }
        if (*p == '.') {
            ++p;
            while (*p == '*' || (*p >= '0' && *p <= '9')) {
                if (*p == '*' && k + 1 < n) {
                    out[k++] = '*';
                }
                ++p;
            }
        }
        while (*p != '\0' && strchr("hlLzjt", *p) != NULL) {
            if (k + 1 < n) {
                out[k++] = *p;
            }
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (k + 2 < n) {
            out[k++] = *p;
            out[k++] = ',';
        }
    }
    out[k] = '\0';
}

static void same_conversions(const char *what, const char *en,
                             const char *tr)
{
    char a[96], b[96];
    conversions(en, a, sizeof(a));
    conversions(tr, b, sizeof(b));
    if (strcmp(a, b) != 0) {
        T_FAIL("%s: \"%s\" converts [%s], its translation \"%s\" [%s]",
               what, en, a, tr, b);
    }
}

TEST_CASE(the_format_check_sees_what_is_read)
{
    char a[32];
    conversions("%d of %d", a, sizeof(a));
    CHECK_STR_EQ(a, "d,d,");
    conversions("100 %% at %-6s %5.2f %lu %+d %.*f %02X", a, sizeof(a));
    CHECK_STR_EQ(a, "s,f,lu,d,*f,X,");
    conversions("none", a, sizeof(a));
    CHECK_STR_EQ(a, "");
}

TEST_CASE(german_has_every_string)
{
    for (int i = 0; i < TX_COUNT; ++i) {
        if (!ui_text_has(UI_LANG_DE, (ui_text_id_t)i)) {
            T_FAIL("no German for %s", ui_text_name((ui_text_id_t)i));
        }
    }
    const ui_language_t *de = ui_text_table(UI_LANG_DE);
    CHECK(de != NULL);
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (de->setting_label[i] == NULL || de->setting_help[i] == NULL) {
            T_FAIL("no German label or help for setting %s",
                   settings_def((setting_id_t)i)->key);
        }
    }
    for (int i = 0; i < SET_CAT_COUNT; ++i) {
        CHECK(de->category[i] != NULL);
    }
    /* The servo table: every entry the English has words in. */
    for (int i = 0; i < SERVO_STR_COUNT; ++i) {
        const char *en = servo_str((servo_str_t)i);
        if (en[0] != '\0' && de->servo[i] == NULL) {
            T_FAIL("no German for servo string %d \"%s\"", i, en);
        }
    }
}

TEST_CASE(every_translation_converts_what_the_english_does)
{
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (int i = 0; i < TX_COUNT; ++i) {
            same_conversions(ui_text_name((ui_text_id_t)i),
                             ui_tr_in(UI_LANG_EN, (ui_text_id_t)i),
                             ui_tr_in((ui_lang_t)l, (ui_text_id_t)i));
        }
        const ui_language_t *t = ui_text_table((ui_lang_t)l);
        for (int i = 0; i < SERVO_STR_COUNT; ++i) {
            same_conversions("servo string", servo_str((servo_str_t)i),
                             servo_str_in(t != NULL ? t->servo : NULL,
                                          (servo_str_t)i));
        }
    }
}

/* Every language, English included: a string no screenshot shows is held
 * to the width ui_text.def gives its field. */
TEST_CASE(every_string_fits_its_declared_field)
{
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (int i = 0; i < TX_COUNT; ++i) {
            const int cells = ui_text_cells((ui_text_id_t)i);
            const char *s = ui_tr_in((ui_lang_t)l, (ui_text_id_t)i);
            if (cells > 0 && gfx_text_cells(s) > cells) {
                T_FAIL("%s in language %d is %d cells, the field %d: \"%s\"",
                       ui_text_name((ui_text_id_t)i), l, gfx_text_cells(s),
                       cells, s);
            }
        }
    }
}

/*
 * Buffers are bytes and a German letter is two of them: an alert has to fit
 * the router's and the snapshot's UI_ALERT_MAX, terminator included, and
 * everything main.c hands the splash as a detail its 32.
 */
TEST_CASE(alerts_and_details_fit_their_buffers)
{
    static const ui_text_id_t k_details[] = {
        TX_SPLASH_NO_PANEL, TX_SPLASH_NO_ANSWER, TX_SPLASH_NO_CARD,
        TX_SPLASH_NVS_MISSING, TX_SPLASH_NOT_OPENED, TX_SPLASH_REFUSED,
        TX_SPLASH_WRONG_REPLY, TX_SPLASH_NO_LINK,
        /* The self-test's verdict, through busfault_verdict_text(). */
        TX_CAN_OK, TX_CAN_SILENT, TX_CAN_CORRUPT, TX_CAN_LOSSY,
        TX_CAN_DROPPED, TX_CAN_RUNNING,
    };
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (int i = 0; i < TX_COUNT; ++i) {
            const char *name = ui_text_name((ui_text_id_t)i);
            const size_t n = strlen(ui_tr_in((ui_lang_t)l, (ui_text_id_t)i));
            if (strncmp(name, "ALERT_", 6) == 0 && n >= UI_ALERT_MAX) {
                T_FAIL("%s is %zu bytes", name, n);
            }
        }
        for (size_t k = 0; k < sizeof(k_details) / sizeof(k_details[0]);
             ++k) {
            const size_t n = strlen(ui_tr_in((ui_lang_t)l, k_details[k]));
            if (n >= 32u) {
                T_FAIL("%s is %zu bytes", ui_text_name(k_details[k]), n);
            }
        }
    }
}

/*
 * SETUP draws a label in 18 cells of the heading face and its help in 36
 * cells, a category name in 22 of the label face (the heading face's 12
 * where it fits) and an option in 13;
 * ESC STICK's TIMING page a label in 47 and its help in 96; the SERVO
 * screen an option in a button of 12.  Translations only: English help
 * longer than 36 cells is cut on SETUP, and that is the layout's to fix.
 */
TEST_CASE(translated_settings_fit_where_they_are_drawn)
{
    for (int l = UI_LANG_EN + 1; l < UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        for (int i = 0; i < SETTING_COUNT; ++i) {
            const setting_def_t *d = settings_def((setting_id_t)i);
            const int label = gfx_text_cells(ui_setting_label((setting_id_t)i));
            const int help = gfx_text_cells(ui_setting_help((setting_id_t)i));
            int label_max = 99, help_max = 99, option_max = 99;
            if (d->cat < SET_CAT_SETUP_COUNT) {
                label_max = 18;
                help_max = 36;
                option_max = 13;
            } else if (d->cat == SET_CAT_STICK) {
                label_max = 47;
                help_max = 96;
            } else if (d->cat == SET_CAT_SERVO) {
                option_max = 12;
            }
            if (label > label_max || help > help_max) {
                T_FAIL("%s: label %d of %d cells, help %d of %d", d->key,
                       label, label_max, help, help_max);
            }
            for (int k = 0; k < d->option_count; ++k) {
                const char *o = ui_setting_option((setting_id_t)i, k);
                if (gfx_text_cells(o) > option_max) {
                    T_FAIL("%s option %d \"%s\" over %d cells", d->key, k, o,
                           option_max);
                }
            }
        }
        for (int c = 0; c < SET_CAT_SETUP_COUNT; ++c) {
            CHECK(gfx_text_cells(ui_setting_category((setting_cat_t)c)) <= 22);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_missing_entry_shows_the_english)
{
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(TR(CANCEL), "ABBRECHEN");
    /* No German options for the INA228's addresses: the English ones. */
    settings_init();
    char buf[24];
    CHECK_STR_EQ(ui_setting_value(SET_INA228_ADDR, buf, sizeof(buf)), "0x45");
    CHECK_STR_EQ(ui_setting_option(SET_INA228_ADDR, 15), "0x4F");
    CHECK_STR_EQ(ui_setting_option(SET_INA228_ADDR, 16), "?");
    CHECK_STR_EQ(ui_setting_value(SET_INA3221_CH, buf, sizeof(buf)), "CH1");
    CHECK_STR_EQ(ui_setting_option(SET_THEME, 1), "HELL");
    CHECK_STR_EQ(ui_setting_value(SET_THEME, buf, sizeof(buf)), "DUNKEL");
    CHECK_STR_EQ(ui_setting_value(SET_BACKLIGHT, buf, sizeof(buf)), "EIN");
    CHECK_STR_EQ(ui_setting_value(SET_PACK_CELLS, buf, sizeof(buf)), "6");
    CHECK_STR_EQ(ui_setting_label(SETTING_COUNT), "");
    CHECK_STR_EQ(ui_setting_help(SETTING_COUNT), "");
    CHECK_STR_EQ(ui_setting_category(SET_CAT_COUNT), "");
    CHECK_STR_EQ(ui_on_off(0), "AUS");
    /* Out of range is nothing, and an unknown language is English. */
    CHECK_STR_EQ(ui_tr(TX_COUNT), "");
    CHECK_STR_EQ(ui_text_name(TX_COUNT), "");
    CHECK_EQ(ui_text_cells(TX_COUNT), 0);
    CHECK(!ui_text_has(UI_LANG_DE, TX_COUNT));
    CHECK(ui_text_has(UI_LANG_EN, TX_BAND_SAFE));
    ui_text_set_language((ui_lang_t)7);
    CHECK_EQ(ui_text_language(), UI_LANG_EN);
    CHECK_STR_EQ(TR(BAND_SAFE), "SAFE");
    CHECK_STR_EQ(ui_tr_in((ui_lang_t)7, TX_BAND_SAFE), "SAFE");
    CHECK(ui_text_table((ui_lang_t)7) == NULL);
    CHECK(ui_servo_table() == NULL);
    CHECK_STR_EQ(ui_servo_str(SERVO_STR_PASS), "PASS");
}

/* SETUP's LANGUAGE is applied with the theme, and the screens draw the
 * next frame in it: nothing waits for a restart. */
TEST_CASE(the_language_follows_the_setting)
{
    settings_init();
    ui_router_init();
    settings_set(SET_LANGUAGE, 1.0f);
    settings_apply_ui();
    CHECK_EQ(ui_text_language(), UI_LANG_DE);
    CHECK_STR_EQ(TR(CANCEL), "ABBRECHEN");
    CHECK_STR_EQ(ui_setting_label(SET_PACK_CELLS), "Zellen");
    CHECK_STR_EQ(ui_setting_category(SET_CAT_IFACE), "ANSCHLÜSSE");
    CHECK_STR_EQ(ui_servo_str(SERVO_STR_PASS), "BESTANDEN");
    settings_set(SET_LANGUAGE, 0.0f);
    settings_apply_ui();
    CHECK_EQ(ui_text_language(), UI_LANG_EN);
    CHECK_STR_EQ(TR(CANCEL), "CANCEL");
    CHECK_STR_EQ(ui_setting_label(SET_PACK_CELLS), "Cells");
}

/* Labels that replace one another in place are padded to the longest of
 * their set, in cells and not in bytes. */
TEST_CASE(a_padded_label_is_as_wide_as_the_longest)
{
    static const ui_text_id_t k_set[] = { TX_SUP_TAG_LIVE, TX_SUP_TAG_HELD,
                                          TX_SUP_TAG_IDLE };
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        char a[48], b[48], c[48];
        ui_tr_pad(k_set, 3, 0, a, sizeof(a));
        ui_tr_pad(k_set, 3, 1, b, sizeof(b));
        ui_tr_pad(k_set, 3, 2, c, sizeof(c));
        CHECK_EQ(gfx_text_cells(a), gfx_text_cells(b));
        CHECK_EQ(gfx_text_cells(b), gfx_text_cells(c));
        CHECK(strncmp(a, ui_tr(TX_SUP_TAG_LIVE), strlen(ui_tr(TX_SUP_TAG_LIVE)))
              == 0);
    }
    char d[8];
    CHECK_STR_EQ(ui_tr_pad(NULL, 3, 0, d, sizeof(d)), "");
    CHECK_STR_EQ(ui_tr_pad(k_set, 3, 0, NULL, 0), "");
    ui_text_set_language(UI_LANG_EN);
    ui_tr_pad(k_set, 3, 9, d, sizeof(d));            /* clamped to entry 0 */
    CHECK(strncmp(d, "LIVE OU", 7) == 0);
}

/* The CSV is for tools and stays English; the report is the language the
 * run started in. */
TEST_CASE(the_report_is_translated_and_the_csv_is_not)
{
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(servo_str(SERVO_STR_PHASE_MOVE), "MOVE");
    CHECK_STR_EQ(servo_test_phase_name(SERVO_TEST_PH_MOVE), "MOVE");
    CHECK(strncmp(servo_test_csv_header(), "time (s);test;step;phase;", 25)
          == 0);
    static servo_test_t t;
    memset(&t, 0, sizeof(t));
    t.cfg.text = ui_servo_table();
    char line[SERVO_TEST_LINE_MAX];
    CHECK(servo_report_line(&t, 0, line, sizeof(line)));
    CHECK_STR_EQ(line, "RCBENCH SERVOTEST-BERICHT");
    t.cfg.text = NULL;
    CHECK(servo_report_line(&t, 0, line, sizeof(line)));
    CHECK_STR_EQ(line, "RCBENCH SERVO TEST REPORT");
    CHECK_STR_EQ(servo_str_in(ui_servo_table(), SERVO_STR_COUNT), "");
    /* A line is SERVO_TEST_LINE_MAX bytes with its numbers in: each
     * template leaves 40 for them. */
    for (int i = 0; i < SERVO_STR_COUNT; ++i) {
        const size_t n = strlen(servo_str_in(ui_servo_table(),
                                             (servo_str_t)i));
        if (n + 40u >= SERVO_TEST_LINE_MAX) {
            T_FAIL("servo string %d is %zu bytes", i, n);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* How many cells @p s takes, as the 8 px face draws it. */
static int cells_of(const char *s)
{
    int n = 0;
    for (uint32_t c = gfx_utf8_next(&s); c != 0u; c = gfx_utf8_next(&s)) {
        ++n;
    }
    return n;
}

/* A run's reason is a line of the SERVO screen's result box, 250 px for
 * 31 cells, and the two lines that say STALL AT cannot be reached go
 * across the settings card under START TEST, 468 px for 58 cells with
 * their numbers in: in every language. */
TEST_CASE(a_runs_reasons_fit_where_the_screen_draws_them)
{
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        const ui_language_t *t = ui_text_table((ui_lang_t)l);
        const char *const *table = (t != NULL) ? t->servo : NULL;
        for (int why = SERVO_TEST_AB_NONE + 1; why < SERVO_TEST_AB_COUNT;
             ++why) {
            const char *s = servo_str_in(
                table, servo_test_abort_str((servo_test_abort_t)why));
            if (s[0] == '\0' || cells_of(s) > 31) {
                T_FAIL("language %d: reason %d \"%s\" is %d cells", l, why,
                       s, cells_of(s));
            }
        }
        char line[SERVO_TEST_LINE_MAX];
        snprintf(line, sizeof(line),
                 servo_str_in(table, SERVO_STR_R_LIM_STALL_LIMIT), 5.0, 3.0);
        CHECK(cells_of(line) <= 58);
        snprintf(line, sizeof(line),
                 servo_str_in(table, SERVO_STR_R_LIM_STALL_RANGE), 5.0,
                 32.76);
        CHECK(cells_of(line) <= 58);
    }
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_CC),
                 "constant current for 1 s");
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_INA_METER),
                 "INA3221 no longer the meter");
}

/* The step table's heading names each column where the step lines' format
 * puts it, in every language. */
TEST_CASE(the_report_columns_line_up)
{
    static const int k_cols[] = { 0, 7, 15, 22, 29, 36, 43, 50, 58, 66, 73, 81,
                                  87, 92 };
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        const ui_language_t *t = ui_text_table((ui_lang_t)l);
        const char *head = servo_str_in(t != NULL ? t->servo : NULL,
                                        SERVO_STR_R_COLUMNS);
        /* Decoded into cells, as a text editor shows the file. */
        uint32_t cp[128];
        int n = 0;
        const char *p = head;
        for (uint32_t c = gfx_utf8_next(&p); c != 0u && n < 128;
             c = gfx_utf8_next(&p)) {
            cp[n++] = c;
        }
        for (size_t k = 0; k < sizeof(k_cols) / sizeof(k_cols[0]); ++k) {
            const int at = k_cols[k];
            if (at >= n || cp[at] == ' ' || (at > 0 && cp[at - 1] != ' ')) {
                T_FAIL("language %d: no heading starts at column %d of "
                       "\"%s\"", l, at, head);
            }
        }
    }
}

/* The report's labelled lines start their values in one column. */
TEST_CASE(the_report_labels_line_up)
{
    static const servo_str_t k_labelled[] = {
        SERVO_STR_R_RESULT, SERVO_STR_R_RESULT_WHY, SERVO_STR_R_RESULT_UNSEEN,
        SERVO_STR_R_RESULT_BO_UNSEEN,
        SERVO_STR_R_DEVICE,
        SERVO_STR_R_FIRMWARE, SERVO_STR_R_LOG, SERVO_STR_R_SUPPLY,
        SERVO_STR_R_READINGS, SERVO_STR_R_READINGS_FEW, SERVO_STR_R_SKIPPED,
        SERVO_STR_R_RESOLUTION, SERVO_STR_R_RESOLUTION_UNKNOWN,
        SERVO_STR_R_LAG, SERVO_STR_R_REPEATS, SERVO_STR_R_UPPER_BOUND,
        SERVO_STR_R_DURATION, SERVO_STR_R_ROWS, SERVO_STR_R_TYPE,
        SERVO_STR_R_RATE, SERVO_STR_R_DANGER, SERVO_STR_R_HV,
        SERVO_STR_R_ENDS, SERVO_STR_R_BROWNOUT,
        SERVO_STR_R_BROWNOUT_NOT_RUN, SERVO_STR_R_I_LIMIT,
        SERVO_STR_R_TIMING, SERVO_STR_R_LEN_MOVES, SERVO_STR_R_LEN_TIME,
        SERVO_STR_R_LIMITS,
    };
    static const servo_str_t k_limits[] = {
        SERVO_STR_R_LIM_IDLE, SERVO_STR_R_LIM_HOLD, SERVO_STR_R_LIM_TRAVEL,
        SERVO_STR_R_LIM_TRAVEL_OFF, SERVO_STR_R_LIM_TRAVEL_BOUND,
        SERVO_STR_R_LIM_TRAVEL_NONE,
        SERVO_STR_R_LIM_STALL,
        SERVO_STR_R_LIM_LATE, SERVO_STR_R_LIM_UNSEEN,
        SERVO_STR_R_LIM_BO_SEEN, SERVO_STR_R_LIM_BO_UNSEEN,
    };
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        const ui_language_t *t = ui_text_table((ui_lang_t)l);
        const char *const *tbl = (t != NULL) ? t->servo : NULL;
        /* The value starts after the colon and its padding. */
        int column = -1;
        for (size_t k = 0; k < sizeof(k_labelled) / sizeof(k_labelled[0]);
             ++k) {
            const char *s = servo_str_in(tbl, k_labelled[k]);
            const char *colon = strchr(s, ':');
            CHECK(colon != NULL);
            if (colon == NULL) {
                continue;
            }
            const char *v = colon + 1;
            while (*v == ' ') {
                ++v;
            }
            char label[64];
            snprintf(label, sizeof(label), "%.*s", (int)(v - s), s);
            const int at = gfx_text_cells(label);
            if (column < 0) {
                column = at;
            } else if (at != column) {
                T_FAIL("language %d: \"%s\" starts its value at %d, the "
                       "others at %d", l, s, at, column);
            }
        }
        /* STEPS carries its values with a space of their own in front. */
        CHECK_EQ(gfx_text_cells(servo_str_in(tbl, SERVO_STR_R_STEPS)) + 1,
                 column);
        /* The limit lines after a label padded with spaces: the column
         * from the first, whose label is the shortest, and every line with
         * a space before it and its value at it. */
        const char *first = servo_str_in(tbl, k_limits[0]);
        const char *gap = strstr(first, "  ");
        CHECK(gap != NULL);
        if (gap == NULL) {
            continue;
        }
        while (*gap == ' ') {
            ++gap;
        }
        char label[64];
        snprintf(label, sizeof(label), "%.*s", (int)(gap - first), first);
        const int lim = gfx_text_cells(label);
        for (size_t k = 0; k < sizeof(k_limits) / sizeof(k_limits[0]); ++k) {
            const char *s = servo_str_in(tbl, k_limits[k]);
            const size_t at = gfx_text_prefix(s, lim);
            if (at == 0u || s[at - 1] != ' ' || s[at] == ' ' || s[at] == '\0') {
                T_FAIL("language %d: \"%s\" does not start at %d", l, s,
                       lim);
            }
        }
    }
}

/* The stick engine's refusals are matched by their English: every one the
 * profiles produce has a translation. */
TEST_CASE(every_stick_refusal_is_translated)
{
    esc_profiles_clear_overrides();
    ui_text_set_language(UI_LANG_DE);
    const size_t n = esc_profiles_count();
    int refused = 0;
    for (size_t i = 0; i < n; ++i) {
        const char *why = NULL;
        if (esc_stick_kind(esc_profiles_at(i), &why) == ESC_STICK_KIND_NONE) {
            ++refused;
            if (strcmp(programmer_screen_why_text(why), why) == 0) {
                T_FAIL("\"%s\" has no German", why);
            }
        }
    }
    CHECK(refused > 0);
    /* And the run's own refusals. */
    const char *why = NULL;
    CHECK(!esc_stick_check(NULL, NULL, 0, NULL, &why));
    CHECK(strcmp(programmer_screen_why_text(why), why) != 0);
    esc_stick_timing_t tm;
    esc_stick_timing_defaults(&tm);
    const esc_profile_t *p = NULL;
    for (size_t i = 0; i < n && p == NULL; ++i) {
        if (esc_stick_kind(esc_profiles_at(i), NULL) != ESC_STICK_KIND_NONE) {
            p = esc_profiles_at(i);
        }
    }
    CHECK(p != NULL);
    CHECK(!esc_stick_check(p, NULL, 0, NULL, &why));
    CHECK(strcmp(programmer_screen_why_text(why), why) != 0);
    CHECK(!esc_stick_check(p, NULL, 0, &tm, &why));
    CHECK(strcmp(programmer_screen_why_text(why), why) != 0);
    CHECK_STR_EQ(programmer_screen_why_text(NULL), "abgelehnt");
    CHECK_STR_EQ(programmer_screen_why_text("not a refusal"),
                 "not a refusal");
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(programmer_screen_why_text("no profile"), "no profile");
}

/* The bus-fault verdicts, as the screen heads them. */
TEST_CASE(every_bus_verdict_has_a_heading)
{
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        for (int v = CAN_SELFTEST_RUNNING; v <= CAN_SELFTEST_DROPPED; ++v) {
            CHECK(busfault_verdict_text((can_selftest_verdict_t)v)[0]
                  != '\0');
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_format_check_sees_what_is_read);
    RUN(german_has_every_string);
    RUN(every_translation_converts_what_the_english_does);
    RUN(every_string_fits_its_declared_field);
    RUN(alerts_and_details_fit_their_buffers);
    RUN(translated_settings_fit_where_they_are_drawn);
    RUN(a_missing_entry_shows_the_english);
    RUN(the_language_follows_the_setting);
    RUN(a_padded_label_is_as_wide_as_the_longest);
    RUN(the_report_is_translated_and_the_csv_is_not);
    RUN(the_report_columns_line_up);
    RUN(a_runs_reasons_fit_where_the_screen_draws_them);
    RUN(the_report_labels_line_up);
    RUN(every_stick_refusal_is_translated);
    RUN(every_bus_verdict_has_a_heading);
    return test_summary("text");
}
