/**
 * @file test_files_layout.c
 * @brief FILES' layout and its edges: nothing that acts under the alert
 *        band in any view, every label in its button in both languages,
 *        the sizes and times as the rows and INFO write them, the name
 *        keyboard's file-name rules, and the way in from LOGS and from
 *        SETUP with the home tag back to each.
 *
 * Cases:
 * - nothing_that_acts_lies_under_the_alert_band
 * - every_label_fits_its_button_in_both_languages
 * - sizes_read_as_the_rows_write_them
 * - times_read_as_info_writes_them
 * - the_name_keyboard_takes_a_name_by_the_cards_rules
 * - files_home_tag_goes_to_the_menu
 * - every_view_draws_in_both_languages_and_both_themes
 * - the_footer_runs_rescan_to_open_as_logs_does
 *
 * SPDX-License-Identifier: MIT
 */
#include "files_rig.h"
#include "logs_strip.h"

#include "files_int.h"
#include "settings_screen.h"
#include "ui_widgets.h"

/** @brief Whether a tap at every 4th px of the alert band's rows, every
 *         8th column, leaves the view, the requests and the card as they
 *         were. */
static bool band_is_dead(void)
{
    const files_view_t view = files_screen_view();
    const unsigned subs = g_h.submits;
    const int count = g_h.card.count;
    const int sel0 = files_screen_pane(0)->selected;
    const int sel1 = files_screen_pane(1)->selected;
    const int active = files_screen_active();
    for (int y = UI_BODY_H - UI_ALERT_H; y < UI_BODY_H; y += 4) {
        for (int x = 0; x < W; x += 8) {
            send(TOUCH_EVENT_DOWN, x, y);
            send(TOUCH_EVENT_UP, x, y);
        }
    }
    settle();
    return files_screen_view() == view && g_h.submits == subs
           && g_h.card.count == count
           && files_screen_pane(0)->selected == sel0
           && files_screen_pane(1)->selected == sel1
           && files_screen_active() == active && g_h.cancels == 0u;
}

TEST_CASE(nothing_that_acts_lies_under_the_alert_band)
{
    fresh();
    CHECK(band_is_dead());
    CHECK(pick(0, "NOTES.TXT"));
    CHECK(band_is_dead());
    tap_foot(FS_SPOT_NEW);
    CHECK(band_is_dead());
    CHECK(home());
    tap_foot(FS_SPOT_DELETE);
    CHECK(band_is_dead());
    CHECK(home());
    tap_foot(FS_SPOT_INFO);
    CHECK(band_is_dead());
    CHECK(home());
    tap_foot(FS_SPOT_RENAME);
    CHECK(band_is_dead());
    CHECK(home());
    g_h.defer = true;
    tap_foot(FS_SPOT_COPY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    CHECK(band_is_dead());
    while (fh_run(&g_h)) {
        settle();
    }
    /* And every control ends 4px above it. */
    CHECK(FS_SPOT_FOOT_BOT + 4 <= UI_BODY_H - UI_ALERT_H);
    CHECK(FS_SPOT_CLOSE_CY + 28 + 4 <= UI_BODY_H - UI_ALERT_H);
}

TEST_CASE(every_label_fits_its_button_in_both_languages)
{
    static const ui_text_id_t foot[] = {
        TX_LOG_OPEN, TX_FM_COPY, TX_FM_MOVE, TX_FM_RENAME, TX_LOG_DELETE,
        TX_FM_NEW, TX_FM_INFO,
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(foot) / sizeof(foot[0]); ++i) {
            /* 4px each side in the 10 x 20 face, the face of every key
             * in every language: VERSCHIEBEN is 110px of 118. */
            const gfx_font_t *f = ui_button_face(ui_tr(foot[i]), 118, 52,
                                                 FOOT_PAD, &gfx_font_10x20,
                                                 &gfx_font_8x16);
            CHECK(ui_text_fits(f, ui_tr(foot[i]), 118, 52, FOOT_PAD));
            CHECK(f == &gfx_font_10x20);
        }
        /* AKTUALISIEREN is 130px of 140. */
        const gfx_font_t *fr = ui_button_face(TR(LOG_RESCAN), 140, 52,
                                              FOOT_PAD, &gfx_font_10x20,
                                              &gfx_font_8x16);
        CHECK(ui_text_fits(fr, TR(LOG_RESCAN), 140, 52, FOOT_PAD));
        CHECK(fr == &gfx_font_10x20);
        CHECK(ui_text_fits(&gfx_font_10x20, TR(LOG_PREV), 88, 48, 4));
        CHECK(ui_text_fits(&gfx_font_10x20, TR(LOG_NEXT), 88, 48, 4));
        CHECK(ui_text_fits(&gfx_font_10x20, TR(CANCEL), 300, 56, 4));
        CHECK(ui_text_fits(&gfx_font_10x20, TR(FM_CLOSE), 300, 56, 4));
        /* A row's kind and its state word in its 8 x 16 line. */
        int widest = 0;
        for (int k = 0; k < FM_KIND_COUNT; ++k) {
            const int wk = gfx_text_cells(fs_kind_word(k));
            widest = (wk > widest) ? wk : widest;
        }
        CHECK((widest + 2 + gfx_text_cells(TR(FM_WRITING))) * 8 <= 420 - 40
                                                                     - 8);
        /* A pane's heading at its longest. */
        char head[96];
        int n = snprintf(head, sizeof(head), TR(FM_ENTRIES_OF), 9953, 10000,
                         10000);
        snprintf(head + n, sizeof(head) - (size_t)n, TR(FM_UNLISTED), 999u);
        CHECK(gfx_text_cells(head) * 8 + 26 <= 500 - 8);
    }
    ui_text_set_language(UI_LANG_EN);
}

/** @brief A size as the rows write it. */
static const char *size_of(uint64_t bytes)
{
    static char out[24];
    files_size_text(out, sizeof(out), bytes);
    return out;
}

TEST_CASE(sizes_read_as_the_rows_write_them)
{
    CHECK_STR_EQ(size_of(0u), "0B");
    CHECK_STR_EQ(size_of(1023u), "1023B");
    CHECK_STR_EQ(size_of(1024u), "1kB");
    CHECK_STR_EQ(size_of(1024u * 1024u - 1u), "1023kB");
    CHECK_STR_EQ(size_of(1024u * 1024u), "1.0MB");
    CHECK_STR_EQ(size_of(3u * 1024u * 1024u), "3.0MB");
    CHECK_STR_EQ(size_of(1024ull * 1024u * 1024u), "1.00GB");
    CHECK_STR_EQ(size_of(8ull * 1024u * 1024u * 1024u), "8.00GB");
    /* The row's box holds the widest, 9 cells. */
    CHECK(gfx_text_cells(size_of(1023ull * 1024u * 1024u * 1024u)) <= 9);
    files_size_text(NULL, 4u, 10u);
    char one[1] = { 'x' };
    files_size_text(one, 0u, 10u);
    CHECK_EQ(one[0], 'x');
}

/** @brief A time as INFO writes it. */
static const char *time_of(int64_t t)
{
    static char out[32];
    files_time_text(out, sizeof(out), t);
    return out;
}

TEST_CASE(times_read_as_info_writes_them)
{
    CHECK_STR_EQ(time_of(-1), "");
    CHECK_STR_EQ(time_of(0), "1970-01-01 00:00");
    /* The date FAT stamps on what the panel writes. */
    CHECK_STR_EQ(time_of(315532800), "1980-01-01 00:00");
    CHECK_STR_EQ(time_of(951782400), "2000-02-29 00:00");
    CHECK_STR_EQ(time_of(951868800), "2000-03-01 00:00");
    CHECK_STR_EQ(time_of(1709208000), "2024-02-29 12:00");
    CHECK_STR_EQ(time_of(1715949296), "2024-05-17 12:34");
    CHECK_STR_EQ(time_of(1735689599), "2024-12-31 23:59");
    /* FAT's last date, 2107-12-31 23:59:58. */
    CHECK_STR_EQ(time_of(4354819198LL), "2107-12-31 23:59");
    files_time_text(NULL, 4u, 0);
    char one[1] = { 'x' };
    files_time_text(one, 0u, 0);
    CHECK_EQ(one[0], 'x');
}

TEST_CASE(the_name_keyboard_takes_a_name_by_the_cards_rules)
{
    fresh();
    ui_textkey_t k;
    const gfx_rect_t area = gfx_rect_make(8, 4, 1008, 488);
    /* A name longer than 59 bytes is cut between two characters: the 59th
     * byte would split an ä. */
    char text[80];
    memset(text, 'a', 58);
    memcpy(text + 58, "\xc3\xa4" "bc", 5);
    ui_textkey_open_file(&k, area, "T", text);
    CHECK_EQ(k.len, 58);
    CHECK(k.file && k.wifi && !k.secret);
    CHECK_EQ(k.max_len, 59);
    ui_textkey_open_file(&k, area, "T", "a\xc3\xa4");
    CHECK_EQ(k.len, 3);
    ui_textkey_open_file(NULL, area, "T", "x");
    ui_textkey_open_file(&k, area, NULL, NULL);
    CHECK_EQ(k.len, 0);
    /* DEL takes the ä whole. */
    ui_textkey_open_file(&k, area, "T", "a\xc3\xa4");
    const gfx_rect_t del = ui_textkey_key_rect(&k, UI_TK_DEL);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(del.x + 4),
                                   .y = (int16_t)(del.y + 4) } };
    char out[64] = "";
    (void)ui_textkey_event(&k, &e, out, sizeof(out));
    e.type = TOUCH_EVENT_UP;
    (void)ui_textkey_event(&k, &e, out, sizeof(out));
    CHECK_EQ(k.len, 1);
    CHECK_STR_EQ(k.text, "a");
    /* Each refusal keeps it open with its reason, drawn in its line. */
    static const struct { const char *name; fm_name_t why; } bad[] = {
        { "", FM_NAME_EMPTY }, { "a/b", FM_NAME_CHAR },
        { "x ", FM_NAME_EDGE }, { "NUL.txt", FM_NAME_RESERVED },
    };
    const gfx_rect_t ok = ui_textkey_key_rect(&k, UI_TK_OK);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            ui_textkey_open_file(&k, area, "NEW NAME", bad[i].name);
            e.type = TOUCH_EVENT_DOWN;
            e.point.x = (int16_t)(ok.x + 4);
            e.point.y = (int16_t)(ok.y + 4);
            CHECK_EQ(ui_textkey_event(&k, &e, out, sizeof(out)),
                     UI_TEXTKEY_NONE);
            e.type = TOUCH_EVENT_UP;
            CHECK_EQ(ui_textkey_event(&k, &e, out, sizeof(out)),
                     UI_TEXTKEY_NONE);
            CHECK(k.open && k.refused);
            CHECK_EQ(k.why, (uint8_t)bad[i].why);
            ui_textkey_render(&k, &s_c);
        }
        /* 59 bytes with the 7 German letters counting 2 is refused as
         * long only when it is longer: a name the keyboard holds is one it
         * can take. */
        k.why = (uint8_t)FM_NAME_LONG;
        ui_textkey_render(&k, &s_c);
    }
    ui_text_set_language(UI_LANG_EN);
    /* A name it takes, as typed. */
    ui_textkey_open_file(&k, area, "T", "Run 7.txt");
    e.type = TOUCH_EVENT_DOWN;
    (void)ui_textkey_event(&k, &e, out, sizeof(out));
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(ui_textkey_event(&k, &e, out, sizeof(out)), UI_TEXTKEY_OK);
    CHECK_STR_EQ(out, "Run 7.txt");
    CHECK(!k.open);
}

/** @brief A tap of contact 1 through the router, panel coordinates. */
static void router_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/** @brief The home tag of the screen on top, through the router. */
static void router_home(void)
{
    const gfx_rect_t r =
        ui_home_tag_rect(ui_router_title(ui_router_current()));
    router_tap(r.x + r.w / 2, r.y + r.h / 2);
}

TEST_CASE(files_home_tag_goes_to_the_menu)
{
    fresh();
    CHECK_STR_EQ(ui_router_title(SCREEN_FILES), "FILES");
    CHECK_EQ(ui_router_parent(SCREEN_FILES), SCREEN_OVERVIEW);
    CHECK(!settings_screen_opens(SCREEN_FILES));
    /* LOGS' strip: the FILES tab, and the menu from there. */
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_LOGS);
    router_tap(LOGS_STRIP_X + 331 + 4 + 165,
               UI_BAND_H + LOGS_STRIP_Y + LOGS_STRIP_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    settle();
    router_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* A box closes before the screen is left. */
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_INFO);
    router_home();
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    router_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(every_view_draws_in_both_languages_and_both_themes)
{
    for (int theme = 0; theme < 2; ++theme) {
        for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
            fresh();
            ui_theme_set(theme ? UI_THEME_LIGHT : UI_THEME_DARK);
            ui_text_set_language((ui_lang_t)lang);
            fk_file(&g_h.card,
                    "A-name-of-sixty-three-characters-that-the-row-cuts-ab.txt",
                    NULL, 1u);
            tap_foot(FS_SPOT_RESCAN);
            files_screen_invalidate();
            draw();
            CHECK(pick(0, "NOTES.TXT"));
            files_screen_invalidate();
            draw();
            tap_foot(FS_SPOT_NEW);
            draw();
            CHECK(home());
            tap_foot(FS_SPOT_DELETE);
            draw();
            CHECK(home());
            tap_foot(FS_SPOT_INFO);
            draw();
            CHECK(home());
            CHECK(pick(1, "ATEST005.CSV"));
            tap_foot(FS_SPOT_DELETE);
            draw();
            CHECK(home());
            g_h.armed = true;
            settle();
            draw();
            g_h.armed = false;
            settle();
        }
    }
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_footer_runs_rescan_to_open_as_logs_does)
{
    fresh();
    /* RESCAN at the left, 140px; OPEN at the right margin, as on LOGS. */
    const gfx_rect_t first = FS_FOOT(FS_RESCAN);
    const gfx_rect_t last = FS_FOOT(FS_OPEN);
    CHECK_EQ(first.x, 8);
    CHECK_EQ(first.w, 140);
    CHECK_EQ(last.x + last.w, 1016);
    CHECK_EQ(last.w, 118);
    static const int order[FOOT_N] = {
        FS_RESCAN, FS_COPY, FS_MOVE, FS_RENAME, FS_DELETE, FS_NEW, FS_INFO,
        FS_OPEN,
    };
    for (int k = 0; k < FOOT_N; ++k) {
        const gfx_rect_t r = FS_FOOT(order[k]);
        CHECK_EQ(r.y, 440);
        CHECK_EQ(r.h, 52);
        if (k > 0) {
            const gfx_rect_t l = FS_FOOT(order[k - 1]);
            CHECK_EQ(l.x + l.w + 6, r.x);
        }
        /* The spot the suites tap is in the button drawn there, and a
         * press on it is that button's. */
        CHECK(gfx_rect_contains(r, FS_SPOT_FOOT_CX(order[k]),
                                FS_SPOT_FOOT_CY));
        send(TOUCH_EVENT_DOWN, FS_SPOT_FOOT_CX(order[k]), FS_SPOT_FOOT_CY);
        CHECK_EQ(fs_s.press, order[k]);
        screen()->cancel();
    }
    /* NEW's choice ends at NEW's right edge. */
    CHECK_EQ(FS_NEW_LIST.x + FS_NEW_LIST.w,
             FS_FOOT(FS_NEW).x + FS_FOOT(FS_NEW).w);
    /* A tap on the far left reads both panes; one on the far right with a
     * folder selected enters it. */
    const unsigned subs = g_h.submits;
    tap(12, FS_SPOT_FOOT_CY);
    CHECK_EQ(g_h.submits, subs + 2u);
    CHECK(pick(0, "BENCH"));
    tap(1012, FS_SPOT_FOOT_CY);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH");
}

int main(void)
{
    RUN(nothing_that_acts_lies_under_the_alert_band);
    RUN(every_label_fits_its_button_in_both_languages);
    RUN(sizes_read_as_the_rows_write_them);
    RUN(times_read_as_info_writes_them);
    RUN(the_name_keyboard_takes_a_name_by_the_cards_rules);
    RUN(files_home_tag_goes_to_the_menu);
    RUN(every_view_draws_in_both_languages_and_both_themes);
    RUN(the_footer_runs_rescan_to_open_as_logs_does);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_layout");
}
