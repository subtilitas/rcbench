/**
 * @file test_logview.c
 * @brief Host unit tests for the log viewer screen: opening a file, the states of
 * the card, and the import view.
 *
 * The screen is handed an I/O (input/output) vtable, so the whole browse ->
 * import -> plot path runs here against strings in memory: no card, no
 * filesystem, no board.  Tested is what the screen decides (which file,
 * which convention) and what it says when the card gives no list.
 *
 * Cases:
 * - every_view_draws_something
 * - an_open_file_is_not_read_from_the_card_swapped_behind_it
 * - a_file_that_vanished_between_listing_and_opening
 * - with_no_io_at_all_the_screen_still_works
 * - a_file_that_proves_both_conventions_is_flagged
 * - a_ragged_file_is_flagged_without_a_convention_conflict
 * - a_file_with_no_time_column_plots_against_the_row_number
 * - a_two_row_file_still_draws_a_trace
 * - no_card_says_so_and_stays_put
 * - an_empty_card_is_not_the_same_as_no_card
 * - a_card_that_does_not_mount_says_which
 * - a_second_tap_opens_the_file_and_analyses_it
 * - a_cancelled_press_opens_nothing
 * - a_german_file_is_read_as_german
 * - a_directory_row_cannot_be_opened_as_a_log
 * - a_file_of_2_gib_or_more_is_listed_and_refused_at_open
 * - a_size_is_written_in_its_unit_up_to_an_sdxc_card
 * - a_cut_last_row_is_left_out_and_said
 * - a_cut_last_row_is_said_beside_a_convention_conflict
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

static int pixels_of(gfx_color_t color)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (s_fb[i] == color) {
            ++n;
        }
    }
    return n;
}

/* ---------------------------------------------------------------- cases --- */

TEST_CASE(every_view_draws_something)
{
    fresh();
    draw();                                /* browse, nothing selected      */
    tap(400, BR_ROW_Y(0));
    draw();                                /* browse, a row highlighted     */

    /* Drag the list: with more files than rows it scrolls, and the sizes
     * exercise every unit the formatter has. */
    send(TOUCH_EVENT_DOWN, 400, BR_ROW_Y(4));
    send(TOUCH_EVENT_MOVE, 400, BR_ROW_Y(4) - 90);
    send(TOUCH_EVENT_UP, 400, BR_ROW_Y(4) - 90);
    draw();

    /* Back to the top, then all the way through to a plot. */
    send(TOUCH_EVENT_DOWN, 400, BR_ROW_Y(0));
    send(TOUCH_EVENT_MOVE, 400, BR_ROW_Y(0) + 400);
    send(TOUCH_EVENT_UP, 400, BR_ROW_Y(0) + 400);
    open_file(0);
    draw();
    tap(PLOT_X, IM_BTN_CY);
    draw();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

/*
 * A file open in IMPORT while LOGS is left and the card swapped for one of
 * the same label and names: the way back in reads the new card's list
 * behind the view, and PLOT does not read the new card's file of that
 * name as the open one.
 */
TEST_CASE(an_open_file_is_not_read_from_the_card_swapped_behind_it)
{
    fresh();
    log_viewer_refresh();
    open_file(0);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    const int opens = g_open_calls;
    g_swap_volume = "BENCH01";
    g_swap_runs = 0;
    screen()->enter();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(g_open_calls, opens);
    CHECK(log_viewer_view() != LOG_VIEW_PLOT);
    char want[96];
    snprintf(want, sizeof(want), ui_tr(TX_LOG_CANNOT_OPEN), k_card[0].name);
    CHECK_STR_EQ(log_viewer_message(), want);
    /* Opened again from the new list, it is the new card's file. */
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    open_file(0);
    CHECK_EQ(g_open_calls, opens + 1);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
}

TEST_CASE(a_file_that_vanished_between_listing_and_opening)
{
    /* The card is removable, so "it was there a moment ago" is a real state
     * and not an assertion failure. */
    fresh();
    open_file(6);
    CHECK(log_viewer_analysis() == NULL);
    draw();
    tap(PLOT_X, IM_BTN_CY);
    draw();
    CHECK(log_viewer_data() == NULL);
}

TEST_CASE(with_no_io_at_all_the_screen_still_works)
{
    fresh();
    log_viewer_set_io(NULL);
    log_viewer_refresh();
    draw();
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    draw();
    CHECK(log_viewer_analysis() == NULL);
}

TEST_CASE(a_file_that_proves_both_conventions_is_flagged)
{
    fresh();
    open_file(3);
    draw();
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    /* Two assertions, not a disjunction: "10.23" and "10,23" are both present
     * as whole cells, so the conflict must be reported on its own merits and
     * not because the file also happens to be ragged. */
    CHECK(a->convention_conflict);
    CHECK_EQ(a->ragged_rows, 0);

    /* And the banner that reports it is painted. */
    CHECK(pixels_of(UI_DANGER) > 0);
}

/* Raggedness is the other branch of the same banner, reached here without a
 * convention conflict so the two branches are tested apart. */
TEST_CASE(a_ragged_file_is_flagged_without_a_convention_conflict)
{
    fresh();
    /* Eight files, six visible rows: scroll to the bottom first, so the last
     * file lands on the last visible row.  Pins: a row reached by scrolling
     * accepts a touch. */
    send(TOUCH_EVENT_DOWN, 400, BR_ROW_Y(4));
    send(TOUCH_EVENT_MOVE, 400, BR_ROW_Y(4) - 400);
    send(TOUCH_EVENT_UP, 400, BR_ROW_Y(4) - 400);
    open_file(BR_ROWS_SEEN - 1);
    draw();
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK(a->ragged_rows > 0);
    CHECK(!a->convention_conflict);
}

TEST_CASE(a_file_with_no_time_column_plots_against_the_row_number)
{
    fresh();
    open_file(4);
    draw();
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->time_index, -1);

    tap(PLOT_X, IM_BTN_CY);
    draw();
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_STR_EQ(d->time_name, "");
    CHECK(d->unparsed_cells >= 1);
}

TEST_CASE(a_two_row_file_still_draws_a_trace)
{
    /* Fewer samples than pixel columns: the trace has to be joined up rather
     * than left as a row of isolated marks. */
    fresh();
    open_file(5);
    draw();
    tap(PLOT_X, IM_BTN_CY);
    draw();
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->count, 1);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

TEST_CASE(no_card_says_so_and_stays_put)
{
    fresh();
    g_no_card = true;
    log_viewer_refresh();

    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    /* And it still renders: an empty card is a normal state, not a fault. */
    screen()->render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

/*
 * A card that is there and carries nothing the viewer can open is not the same
 * state as no card, and the screen says so: one tells the operator to insert a
 * card and tap RESCAN, the other would send them looking for a card that is
 * already in.
 */
TEST_CASE(an_empty_card_is_not_the_same_as_no_card)
{
    /*
     * The two states must not draw the same panel.  Asserting that each one
     * renders something would pass with one message for both, which is the
     * bug: it would send an operator to look for a card that is already in.
     * So the two are rendered and compared.
     */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
        CHECK(s_fb_b != NULL);
    }

    fresh();
    g_no_card = true;
    log_viewer_refresh();
    screen()->render(&s_c, 0);
    gfx_color_t *no_card = calloc((size_t)W * H, sizeof(gfx_color_t));
    CHECK(no_card != NULL);
    memcpy(no_card, s_fb, (size_t)W * H * sizeof(gfx_color_t));

    fresh();
    g_empty_card = true;
    log_viewer_refresh();

    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    /* Nothing to open, the same as no card -- that much they do share. */
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    screen()->render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);

    /*
     * And what they SAY is different -- compared over the message line alone,
     * not the whole frame.  The whole frame differs anyway because the header
     * carries the volume name, which is empty with no card; asserting on that
     * would pass with one message for both, which is the defect.
     *
     * The panel is drawn at (120,160) 560x160 and the reason sits at
     * box.y + 52, so the band below covers that line and nothing else.
     */
    int differing = 0;
    for (int y = 205; y < 226; ++y) {
        for (int x = 140; x < 660; ++x) {
            if (no_card[y * W + x] != s_fb[y * W + x]) {
                ++differing;
            }
        }
    }
    CHECK(differing > 0);
    free(no_card);
}

/*
 * A card in the slot that does not mount says which: not formatted as FAT32
 * or exFAT, or unreadable, each in its own words, and neither as no card.  The slot is
 * asked after the list that found no volume, and only then.
 */
TEST_CASE(a_card_that_does_not_mount_says_which)
{
    static const card_state_t k_state[] = {
        CARD_STATE_NONE, CARD_STATE_NO_FAT, CARD_STATE_UNREADABLE,
    };
    enum { N = (int)(sizeof(k_state) / sizeof(k_state[0])) };
    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        gfx_color_t *band[N];
        for (int k = 0; k < N; ++k) {
            fresh();
            ui_text_set_language((ui_lang_t)lang);
            g_no_card = true;
            g_card_state = k_state[k];
            log_viewer_refresh();
            CHECK(g_card_asks >= 1u);
            CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
            screen()->render(&s_c, 0);
            band[k] = calloc((size_t)W * 21, sizeof(gfx_color_t));
            CHECK(band[k] != NULL);
            memcpy(band[k], &s_fb[205 * W], (size_t)W * 21 * sizeof(gfx_color_t));
        }
        /* Each pair differs over the reason's line. */
        for (int a = 0; a < N; ++a) {
            for (int b = a + 1; b < N; ++b) {
                CHECK(memcmp(band[a], band[b],
                             (size_t)W * 21 * sizeof(gfx_color_t)) != 0);
            }
        }
        for (int k = 0; k < N; ++k) {
            free(band[k]);
        }
        ui_text_set_language(UI_LANG_EN);
    }
    /* A card that lists is not asked about. */
    fresh();
    g_card_state = CARD_STATE_NO_FAT;
    log_viewer_refresh();
    CHECK_EQ(g_card_asks, 0u);
    /* An io without the question reads as no card. */
    log_viewer_io_t io = k_io;
    io.card = NULL;
    fresh();
    log_viewer_set_io(&io);
    g_no_card = true;
    log_viewer_refresh();
    screen()->render(&s_c, 0);
    log_viewer_set_io(&k_io);
}

/*
 * A cancel abandons the press in progress.  Every view here acts on the
 * release, and a press left standing after a lost event would let a later
 * contact's release open the file that was selected.
 */
TEST_CASE(a_cancelled_press_opens_nothing)
{
    fresh();
    tap(400, BR_ROW_Y(0));                      /* selects */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 400,
                                   .y = (int16_t)BR_ROW_Y(0), .strength = 40 } };
    log_viewer_screen()->event(&e);
    log_viewer_screen()->cancel();
    e.type = TOUCH_EVENT_UP;
    log_viewer_screen()->event(&e);              /* would have opened */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);

    /* And nothing is stuck: a fresh tap on the selected row opens. */
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
}

TEST_CASE(a_second_tap_opens_the_file_and_analyses_it)
{
    fresh();
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE); /* first tap only selects */
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "BENCH_01.CSV");

    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->convention, LOG_CONV_EN);
    CHECK_EQ(a->delimiter, ',');
    CHECK_EQ(a->n_columns, 5);
    CHECK_EQ(a->row_count, 4);
    CHECK(a->time_index >= 0);
    CHECK_STR_EQ(a->columns[a->time_index].name, "time");
}

TEST_CASE(a_german_file_is_read_as_german)
{
    fresh();
    open_file(1);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->convention, LOG_CONV_DE);
    CHECK_EQ(a->delimiter, ';');

    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    /* The rpm column is written 1.234 there, a grouped 1234, and must not
     * read as 1.234. */
    for (int k = 0; k < d->n_fields; ++k) {
        if (strcmp(d->field[k].name, "Drehzahl") == 0) {
            CHECK_NEAR(d->value[k][0], 1234.0, 1e-3);
        }
    }
}

TEST_CASE(a_directory_row_cannot_be_opened_as_a_log)
{
    fresh();
    g_dir_index = 2;
    log_viewer_refresh();
    open_file(2);

    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    /* A real file on the same card still opens. */
    open_file(0);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_analysis() != NULL);
}

/*
 * OPEN reads through stdio, whose seek takes a 32-bit long on the panel: a
 * file of 2 GiB and more is listed with its size and refused at OPEN with
 * a message, never opened, so no part of it passes for the whole run.
 */
TEST_CASE(a_file_of_2_gib_or_more_is_listed_and_refused_at_open)
{
    static const uint64_t k_refused[] = {
        LOG_VIEWER_OPEN_MAX + 1u,           /* 2^31 */
        0xFFFFFFFFull,                      /* 2^32 - 1, FAT32's largest */
        0x100000000ull,                     /* 2^32 */
        0x200000000ull,                     /* 2^33 */
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (size_t i = 0; i < sizeof(k_refused) / sizeof(k_refused[0]);
             ++i) {
            fresh();
            ui_text_set_language((ui_lang_t)lang);
            g_big_index = 0;
            g_big_size = k_refused[i];
            log_viewer_refresh();
            open_file(0);
            draw();
            CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
            CHECK(log_viewer_analysis() == NULL);
            CHECK_EQ(g_open_calls, 0);
            char want[96];
            snprintf(want, sizeof(want), ui_tr(TX_LOG_TOO_BIG),
                     k_card[0].name);
            CHECK_STR_EQ(log_viewer_message(), want);
        }
    }
    ui_text_set_language(UI_LANG_EN);

    /* One byte less opens. */
    fresh();
    g_big_index = 0;
    g_big_size = LOG_VIEWER_OPEN_MAX;
    log_viewer_refresh();
    open_file(0);
    CHECK_EQ(g_open_calls, 1);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_analysis() != NULL);
}

TEST_CASE(a_size_is_written_in_its_unit_up_to_an_sdxc_card)
{
    static const struct {
        uint64_t bytes;
        const char *text;
    } k[] = {
        { 0u,                    "0 B" },
        { 1023u,                 "1023 B" },
        { 1024u,                 "1 kB" },
        { 1024u * 1024u - 1u,    "1023 kB" },
        { 1024u * 1024u,         "1.0 MB" },
        { 0x3FFFFFFFull,         "1024.0 MB" },
        { 0x40000000ull,         "1.00 GB" },
        { 0x7FFFFFFFull,         "2.00 GB" },
        { 0xFFFFFFFFull,         "4.00 GB" },
        { 0x100000000ull,        "4.00 GB" },
        { 0x200000000ull,        "8.00 GB" },
        { 0x20000000000ull,      "2048.00 GB" },  /* 2 TiB, SDXC's top */
    };
    char out[16];
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        log_viewer_size_text(out, sizeof(out), k[i].bytes);
        CHECK_STR_EQ(out, k[i].text);
    }
    /* The largest size there is is cut at the buffer, and terminated. */
    memset(out, 'x', sizeof(out));
    log_viewer_size_text(out, sizeof(out), UINT64_MAX);
    CHECK(memchr(out, '\0', sizeof(out)) != NULL);
}

/* Open @p text as LONG.CSV on the import view and count the warning's
 * pixels there. */
static int import_warn_pixels(const char *text)
{
    fresh();
    snprintf(g_long, sizeof(g_long), "%s", text);
    g_removed[0] = g_removed[1] = g_removed[2] = true;
    log_viewer_refresh();
    open_file(5);
    draw();
    return pixels_of(UI_WARN);
}

/*
 * A file whose last row has no line feed -- a run cut off by a power loss --
 * imports without that row, and the import view says so: the row count in
 * the warning colour and a line of its own.
 */
TEST_CASE(a_cut_last_row_is_left_out_and_said)
{
    const int whole = import_warn_pixels(
        "t,v\n0.00,10.25\n0.05,20.50\n0.10,95.54\n");
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->row_count, 3);
    CHECK(!a->cut_row);
    /* Settled, so the warning line is free for the cut. */
    CHECK(a->convention_confident);

    const int cut = import_warn_pixels(
        "t,v\n0.00,10.25\n0.05,20.50\n0.10,95.5");
    a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->row_count, 2);
    CHECK(a->cut_row);
    CHECK_NEAR(a->columns[1].last, 20.5, 1e-9);
    CHECK(cut > whole);
    g_long[0] = '\0';
}

/*
 * A file the reader refuses opens on the import view with no analysis and
 * the reader's verdict as the message, in each language: a header cut off
 * before its line feed, a header with no row under it, and a header of 25
 * columns, one more than LOG_MAX_COLUMNS.
 */
TEST_CASE(a_file_the_reader_refuses_says_why)
{
    char wide[256];
    size_t at = 0;
    for (int i = 0; i <= LOG_MAX_COLUMNS; ++i) {
        at += (size_t)snprintf(wide + at, sizeof(wide) - at, "%sc%d",
                               (i > 0) ? "," : "", i);
    }
    snprintf(wide + at, sizeof(wide) - at, "\n");
    const struct {
        const char  *text;
        ui_text_id_t why;
    } k_refused[] = {
        { "t,v", TX_LOG_ERR_NO_HEADER },
        { "t,v\n", TX_LOG_ERR_NO_ROWS },
        { wide, TX_LOG_ERR_COLUMNS },
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (size_t i = 0; i < sizeof(k_refused) / sizeof(k_refused[0]);
             ++i) {
            fresh();
            ui_text_set_language((ui_lang_t)lang);
            snprintf(g_long, sizeof(g_long), "%s", k_refused[i].text);
            g_removed[0] = g_removed[1] = g_removed[2] = true;
            log_viewer_refresh();
            open_file(5);
            draw();
            CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
            CHECK(log_viewer_analysis() == NULL);
            CHECK_STR_EQ(log_viewer_message(), ui_tr(k_refused[i].why));
        }
    }
    ui_text_set_language(UI_LANG_EN);
    g_long[0] = '\0';
}

/* Pixels of one colour in the rows y0 <= y < y1 of the detection panel. */
static int panel_pixels(gfx_color_t color, int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = 16; x < 316; ++x) {
            if (s_fb[y * W + x] == color) {
                ++n;
            }
        }
    }
    return n;
}

/*
 * A cut last row is said even when another warning holds the first line: the
 * conflict banner (danger colour, y = 252) stays and the cut notice takes the
 * line below it (warning colour, y = 270).  Without the cut that line is empty.
 */
TEST_CASE(a_cut_last_row_is_said_beside_a_convention_conflict)
{
    const int plain = import_warn_pixels(
        "t;a;b\n0;10.23;1\n1;10,23;2\n2;11.5;3\n");
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK(a->convention_conflict);
    CHECK(!a->cut_row);
    CHECK(plain >= 0);
    CHECK_EQ(panel_pixels(UI_WARN, 268, 290), 0);

    (void)import_warn_pixels(
        "t;a;b\n0;10.23;1\n1;10,23;2\n2;11.5;3\n3;12.5");
    a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK(a->convention_conflict);
    CHECK(a->cut_row);
    CHECK(panel_pixels(UI_DANGER, 250, 268) > 0);
    CHECK(panel_pixels(UI_WARN, 268, 290) > 0);
    g_long[0] = '\0';
}

int main(void)
{
    RUN(every_view_draws_something);
    RUN(an_open_file_is_not_read_from_the_card_swapped_behind_it);
    RUN(a_file_that_vanished_between_listing_and_opening);
    RUN(with_no_io_at_all_the_screen_still_works);
    RUN(a_file_that_proves_both_conventions_is_flagged);
    RUN(a_ragged_file_is_flagged_without_a_convention_conflict);
    RUN(a_file_with_no_time_column_plots_against_the_row_number);
    RUN(a_two_row_file_still_draws_a_trace);
    RUN(no_card_says_so_and_stays_put);
    RUN(an_empty_card_is_not_the_same_as_no_card);
    RUN(a_card_that_does_not_mount_says_which);
    RUN(a_second_tap_opens_the_file_and_analyses_it);
    RUN(a_cancelled_press_opens_nothing);
    RUN(a_german_file_is_read_as_german);
    RUN(a_directory_row_cannot_be_opened_as_a_log);
    RUN(a_file_of_2_gib_or_more_is_listed_and_refused_at_open);
    RUN(a_size_is_written_in_its_unit_up_to_an_sdxc_card);
    RUN(a_cut_last_row_is_left_out_and_said);
    RUN(a_file_the_reader_refuses_says_why);
    RUN(a_cut_last_row_is_said_beside_a_convention_conflict);
    return test_summary("logview");
}
