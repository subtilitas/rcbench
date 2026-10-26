/**
 * @file test_logview_row.c
 * @brief A row of the LOGS file list: the file's name, its date as FILES
 *        INFO writes it and its size.
 *
 * The date is the FAT (File Allocation Table) stamp in the zone in force,
 * "2026-10-26 09:41"; a stamp of 1980, which FatFs writes for a clock that
 * was not set, and no stamp at all leave the date out.  A name of more than
 * LOG_VIEWER_NAME_CELLS (60) cells is cut to 58 and "..".  On screen the
 * name ends before the date's box, the date stays in its 160px box and the
 * size in its 100px box at the row's end.
 *
 * Cases:
 * - a_row_shows_the_date_in_the_zone_in_force
 * - a_1980_stamp_and_no_stamp_show_no_date
 * - a_long_name_with_a_date_is_cut_to_60_cells
 * - a_folder_shows_its_date_and_folder
 * - the_three_columns_keep_to_their_boxes
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

#include <time.h>

#include "files_screen.h"
#include "wall_clock.h"

/** @brief 2026-10-26 09:41:00 UTC, seconds since 1970. */
#define T_0941 1793007660LL

/** @brief 1980-01-01 00:00:00 UTC: FatFs's stamp for a clock not set. */
#define T_1980 315532800LL

/** @brief The row's columns in the screen's coordinates: the name from x
 *         32, the date's box x 652 to 811, the size's box x 828 to 927; the
 *         row ends at x 944. */
#define NAME_X   32
#define DATE_X0  652
#define DATE_X1  812
#define SIZE_X0  828
#define SIZE_X1  928
#define ROW_X1   944

/** @brief The top pixel row of the list's row @p r. */
#define ROW_TOP(r) (LV_LIST_Y + 30 + (r) * LV_ROW_PITCH)

/** @brief Set TZ to zone @p index of SETUP's list (wall_zone_posix()).
 *  @param index  the zone */
static void zone(int index)
{
    (void)setenv("TZ", wall_zone_posix(index), 1);
    tzset();
}

/** @brief The entry @p name of @p size bytes stamped @p mtime.
 *  @return the entry */
static log_viewer_file_t entry(const char *name, uint64_t size, int64_t mtime)
{
    log_viewer_file_t f;
    memset(&f, 0, sizeof(f));
    snprintf(f.name, sizeof(f.name), "%s", name);
    f.size = size;
    f.mtime = mtime;
    return f;
}

/* ------------------------------------------------------ a card of rows -- */

static log_viewer_file_t g_rows[4];
static int g_n_rows;

/** @brief List hook: g_rows as one page, in the order given.
 *  @return the rows */
static int rows_list(log_viewer_file_t *out, int max_entries,
                     log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    const int n = (g_n_rows < max_entries) ? g_n_rows : max_entries;
    memcpy(out, g_rows, (size_t)n * sizeof(out[0]));
    if (page != NULL) {
        page->ahead = 0;
        page->held = n;
    }
    return n;
}

static const log_viewer_io_t k_rows_io = {
    .list = rows_list,
    .open = fake_open,
};

/** @brief The list of g_rows drawn, row 0 chosen. */
static void draw_rows(void)
{
    fresh();
    log_viewer_set_io(&k_rows_io);
    log_viewer_refresh();
    log_viewer_invalidate();
    draw();
}

/** @brief Pixels in row @p r from x @p x0 to @p x1 - 1 that are not the
 *         row's fill: the ink of what is drawn on it, the edges of the
 *         glyphs included.
 *  @return the count */
static int ink(int r, int x0, int x1)
{
    /* The fill, 10px into the row: past the chosen row's 4px accent and
     * before the name's first glyph at 16px. */
    const gfx_color_t fill = gfx_pixel_get(&s_c, 16 + 10, ROW_TOP(r) + 22);
    int n = 0;
    for (int y = ROW_TOP(r) + 2; y < ROW_TOP(r) + LV_ROW_PITCH - 6; ++y) {
        for (int x = x0; x < x1; ++x) {
            n += gfx_pixel_get(&s_c, x, y) != fill;
        }
    }
    return n;
}

/* ----------------------------------------------------------------- cases -- */

/*
 * The stamp as FILES INFO writes it: "YYYY-MM-DD HH:MM" in the zone SETUP
 * holds.  09:41 UTC is 10:41 in Berlin (CET from 2026-10-25), 18:41 in
 * Tokyo and 05:41 in New York (EDT to 2026-11-01).
 */
TEST_CASE(a_row_shows_the_date_in_the_zone_in_force)
{
    const log_viewer_file_t f = entry("MOTOR014.CSV", 41210u, T_0941);
    log_viewer_row_t t;
    zone(0);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.name, "MOTOR014.CSV");
    CHECK_STR_EQ(t.date, "2026-10-26 09:41");
    CHECK_STR_EQ(t.size, "40kB");
    /* The same text FILES INFO writes for the stamp. */
    char info[24];
    files_time_text(info, sizeof(info), wall_local_s(T_0941));
    CHECK_STR_EQ(t.date, info);
    zone(2);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.date, "2026-10-26 10:41");
    zone(8);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.date, "2026-10-26 18:41");
    zone(4);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.date, "2026-10-26 05:41");
    zone(0);
}

/*
 * No date for a stamp FatFs made from an unset clock, any time in 1980,
 * and none for no stamp (0 or -1): the column stays empty.  The first
 * stamp wall_stamp_known() takes has its date.
 */
TEST_CASE(a_1980_stamp_and_no_stamp_show_no_date)
{
    static const int64_t none[] = {
        T_1980, T_1980 + 365LL * 86400 - 60, 0, -1, WALL_STAMP_FLOOR_S - 1,
    };
    log_viewer_row_t t;
    for (int z = 0; z < WALL_ZONE_COUNT; ++z) {
        zone(z);
        for (size_t i = 0u; i < sizeof(none) / sizeof(none[0]); ++i) {
            const log_viewer_file_t f = entry("RUN.CSV", 300u, none[i]);
            log_viewer_row_text(&f, &t);
            CHECK_STR_EQ(t.date, "");
            CHECK_STR_EQ(t.name, "RUN.CSV");
            CHECK_STR_EQ(t.size, "300B");
        }
    }
    zone(0);
    const log_viewer_file_t first = entry("RUN.CSV", 300u,
                                          WALL_STAMP_FLOOR_S);
    log_viewer_row_text(&first, &t);
    CHECK_STR_EQ(t.date, "1981-01-01 12:00");
    /* Nothing to write from: three empty texts, and no write at all. */
    memset(&t, 'x', sizeof(t));
    log_viewer_row_text(NULL, &t);
    CHECK_STR_EQ(t.name, "");
    CHECK_STR_EQ(t.date, "");
    CHECK_STR_EQ(t.size, "");
    log_viewer_row_text(&first, NULL);
}

/*
 * 60 cells of name stand whole beside the date; 61 and more are cut to 58
 * and "..", the cut never inside a character.
 */
TEST_CASE(a_long_name_with_a_date_is_cut_to_60_cells)
{
    CHECK_EQ(LOG_VIEWER_NAME_CELLS, 60);
    char name[LOG_VIEWER_NAME_MAX];
    log_viewer_row_t t;
    zone(0);
    /* 60 cells: whole. */
    memset(name, 'A', 56);
    memcpy(name + 56, ".CSV", 5);
    log_viewer_file_t f = entry(name, 12u, T_0941);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.name, name);
    CHECK_STR_EQ(t.date, "2026-10-26 09:41");
    /* 63 cells, the longest name the card's listing keeps: 58 and "..". */
    memset(name, 'B', 59);
    memcpy(name + 59, ".CSV", 5);
    f = entry(name, 12u, T_0941);
    log_viewer_row_text(&f, &t);
    CHECK_EQ(gfx_text_cells(t.name), LOG_VIEWER_NAME_CELLS);
    CHECK_EQ(strncmp(t.name, name, 58), 0);
    CHECK_STR_EQ(t.name + 58, "..");
    CHECK_STR_EQ(t.date, "2026-10-26 09:41");
    /* Two-byte characters count one cell each.  58 of ASCII and two "Ä",
     * 62 bytes: 60 cells, whole. */
    memset(name, 'C', 58);
    memcpy(name + 58, "\xC3\x84\xC3\x84", 5);
    f = entry(name, 12u, T_0941);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.name, name);
    /* 56 of ASCII, two "Ä" and "DDD", 63 bytes and 61 cells: cut after
     * the second "Ä", the 58th cell, and not inside it. */
    memset(name, 'C', 56);
    memcpy(name + 56, "\xC3\x84\xC3\x84" "DDD", 8);
    f = entry(name, 12u, T_0941);
    log_viewer_row_text(&f, &t);
    CHECK_EQ(gfx_text_cells(t.name), LOG_VIEWER_NAME_CELLS);
    CHECK_EQ(strncmp(t.name, name, 60), 0);
    CHECK_STR_EQ(t.name + 60, "..");
}

/* A folder: its date as any entry's, and FOLDER where a size stands. */
TEST_CASE(a_folder_shows_its_date_and_folder)
{
    log_viewer_file_t f = entry("OLD", 0u, T_0941);
    f.is_dir = true;
    log_viewer_row_t t;
    zone(0);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.date, "2026-10-26 09:41");
    CHECK_STR_EQ(t.size, "FOLDER");
    ui_text_set_language(UI_LANG_DE);
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.date, "2026-10-26 09:41");
    CHECK_STR_EQ(t.size, "ORDNER");
    ui_text_set_language(UI_LANG_EN);
    f.mtime = T_1980;
    log_viewer_row_text(&f, &t);
    CHECK_STR_EQ(t.date, "");
}

/*
 * On screen, in English and in German: a row's name, cut at 60 cells,
 * ends 16px or more before the date's box; the date stays inside
 * its box and the size inside its own; a row with a 1980 stamp draws
 * nothing in the date's box.
 */
TEST_CASE(the_three_columns_keep_to_their_boxes)
{
    char name[LOG_VIEWER_NAME_MAX];
    memset(name, 'W', 59);
    memcpy(name + 59, ".CSV", 5);
    g_rows[0] = entry(name, 3u * 1024u * 1024u, T_0941);
    g_rows[1] = entry("MOTOR011.CSV", 39100u, T_1980);
    g_rows[2] = entry("WIDE.CSV", 2047ull * 1024u * 1024u * 1024u, T_0941);
    g_n_rows = 3;
    zone(0);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        draw_rows();
        /* Row 0: the name, cut at 60 cells, ends 16px or more before
         * the date's box; the date and the size stay in their boxes. */
        CHECK(ink(0, NAME_X, NAME_X + 600) > 0);
        CHECK_EQ(ink(0, NAME_X + 600, DATE_X0), 0);
        CHECK(ink(0, DATE_X0, DATE_X1) > 0);
        CHECK_EQ(ink(0, DATE_X1, SIZE_X0), 0);
        CHECK(ink(0, SIZE_X0, SIZE_X1) > 0);
        CHECK_EQ(ink(0, SIZE_X1, ROW_X1 - 8), 0);
        /* Row 1, 1980: no date, its size where it stands. */
        CHECK(ink(1, NAME_X, DATE_X0) > 0);
        CHECK_EQ(ink(1, DATE_X0 - 16, SIZE_X0), 0);
        CHECK(ink(1, SIZE_X0, SIZE_X1) > 0);
        /* Row 2, a size of 2047GB: "2047.00GB" in its box. */
        CHECK(ink(2, DATE_X0, DATE_X1) > 0);
        CHECK_EQ(ink(2, DATE_X1, SIZE_X0), 0);
        CHECK(ink(2, SIZE_X0, SIZE_X1) > 0);
        CHECK_EQ(ink(2, SIZE_X1, ROW_X1 - 8), 0);
    }
    ui_text_set_language(UI_LANG_EN);
    g_n_rows = 0;
}

int main(void)
{
    RUN(a_row_shows_the_date_in_the_zone_in_force);
    RUN(a_1980_stamp_and_no_stamp_show_no_date);
    RUN(a_long_name_with_a_date_is_cut_to_60_cells);
    RUN(a_folder_shows_its_date_and_folder);
    RUN(the_three_columns_keep_to_their_boxes);
    return test_summary("logview_row");
}
