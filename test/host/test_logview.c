/*
 * Host unit tests for the log viewer screen.
 *
 * The screen is handed an I/O (input/output) vtable, so the whole browse ->
 * import -> plot path runs here against strings in memory: no card, no
 * filesystem, no board.  Tested is what the screen decides (which file, which
 * columns, which convention, where the cursor lands) and one property about
 * pixels: a redraw over an older frame leaves nothing of it behind.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <math.h>
#include <stdlib.h>

#include "card_tree.h"
#include "log_name.h"
#include "log_select.h"
#include "log_take.h"
#include "log_viewer_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "touch_feed.h"

#define W 800
#define H 480

/* --------------------------------------------------------- the fake card -- */

static const char k_en[] =
    "time,voltage,current,rpm,temp\n"
    "0.000,22.34V,10.23A,1234,25\n"
    "0.020,22.31V,11.05A,1567,25\n"
    "0.040,22.28V,12.40A,1890,26\n"
    "0.060,22.20V,15.02A,2345,26\n";

static const char k_de[] =
    "Zeit;Spannung;Strom;Drehzahl;Temperatur\n"
    "0,000;22,34V;10,23A;1.234;25\n"
    "0,020;22,31V;11,05A;1.567;25\n"
    "0,040;22,28V;12,40A;1.890;26\n"
    "0,060;22,20V;15,02A;2.345;26\n";

static const char k_prose[] = "note\nnothing numeric here\nnor here\n";

/*
 * Both conventions proven in one file: the screen reports the conflict rather
 * than picking a winner.
 *
 * Semicolon-delimited, so that "10,23" is one cell.  With a comma delimiter it
 * splits into two fields and the fixture holds no comma-decimal cell at all.
 */
static const char k_mixed[] = "t;a;b\n0;10.23;1\n1;10,23;2\n2;11.5;3\n";

/* Ragged for its own sake, so the two conditions can be told apart. */
static const char k_ragged[] = "t,a,b\n0,1,2\n1,2\n2,3,4\n";

/* No time column, and a column that is mostly but not entirely numeric. */
static const char k_plain[] =
    "left,right\n5,1.0\n3,2.0\n9,n/a\n4,4.0\n8,5.0\n7,6.0\n";

/* 400 rows of a ramp, written by long_file() before a zoom case opens it. */
static char g_long[16384];


static const struct {
    const char *name;
    const char *text;
} k_card[] = {
    { "BENCH_01.CSV", k_en },
    { "PRUEFUNG.CSV", k_de },
    { "README.CSV", k_prose },
    { "MIXED.CSV", k_mixed },
    { "PLAIN.CSV", k_plain },
    { "TINY.CSV", "a,b\n1,2\n" },
    { "GONE.CSV", NULL },
    /* Appended, not inserted: the cases above address the card by index. */
    { "RAGGED.CSV", k_ragged },
    { "LONG.CSV", g_long },
};

static bool g_no_card;
static bool g_empty_card;
/* The card's name in the header; another name is another card. */
static const char *g_volume = "BENCH01";
/* A card the next listing finds in the slot and mounts, as card_list()'s
 * look does on the panel: its name, its runs; NULL for none. */
static const char *g_swap_volume;
static int g_swap_runs;
/* The mount number the io reports; a swap counts it up. */
static unsigned g_mounts;

/*
 * A card holding more runs than the list has room for, or 0 for the fixture
 * card above.  The lister writes the newest that fit, in the order the browse
 * list draws them, and returns what the card holds -- what card_list() does
 * on the panel with storage_walk()'s count.
 */
static int g_card_runs;
/* Beside those runs, this many files the bench did not write, OTHERnnn.CSV,
 * and the first run number, so a card can hold runs 61 to 120 only. */
static int g_card_others;
static int g_card_run0 = 1;
/* Runs deleted from that card, by number. */
static bool g_run_gone[LOG_RUN_LAST + 1];
/* What the last listing asked for, and how many listings there were. */
static log_viewer_page_t g_last_page;

/* Which listed entry is a subdirectory, or -1 for none.  A card full of
 * directories is not the normal case, and every other case here wants to open
 * the file it selected. */
static int g_dir_index = -1;
static log_mem_ctx_t g_ctx;
/* A size the fixture card lists for one entry in place of its own, and the
 * entry, or -1 for none: an exFAT card holds files of 4 GiB and more. */
static int g_big_index = -1;
static uint64_t g_big_size;
static int g_open_calls;        /* how often open() was asked */

/* What DELETE has done to the fixture card: which entries are gone, how many
 * times remove() was asked, the last name it was asked for, and whether it
 * refuses -- the panel's refuses the run the logger still has open. */
static bool g_removed[16];
static int g_remove_calls;
static char g_removed_name[LOG_VIEWER_NAME_MAX];
static bool g_remove_refuses;
static int g_list_calls;        /* how often the card was listed */

/*
 * The runs card as the panel reads it: every entry offered to the page
 * selection in directory order, which is creation order on FAT -- the others
 * first, then the runs oldest first -- as card_list() does.
 */
static int runs_list(log_viewer_file_t *out, int max_entries,
                     log_viewer_page_t *page)
{
    log_select_page_t sel;
    log_select_page_begin(&sel, out, max_entries, page);
    int n = 0;
    log_viewer_file_t f;
    memset(&f, 0, sizeof(f));
    for (int i = 0; i < g_card_others; ++i, ++n) {
        snprintf(f.name, sizeof(f.name), "OTHER%03d.CSV", i);
        f.size = 512u;
        log_select_page_offer(&sel, &f);
    }
    for (int r = g_card_run0; r < g_card_run0 + g_card_runs; ++r) {
        if (r > LOG_RUN_LAST || g_run_gone[r]) {
            continue;
        }
        log_run_name(f.name, sizeof(f.name), r);
        f.size = 1024u;
        log_select_page_offer(&sel, &f);
        ++n;
    }
    (void)log_select_page_end(&sel, page);
    return n;
}

static int fake_list(log_viewer_file_t *out, int max_entries,
                     log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    ++g_list_calls;
    if (g_swap_volume != NULL) {
        ++g_mounts;
        g_volume = g_swap_volume;
        g_card_runs = g_swap_runs;
        g_swap_volume = NULL;
    }
    if (page != NULL) {
        g_last_page = *page;
    }
    if (g_no_card) {
        return -1;
    }
    if (g_empty_card) {
        if (page != NULL) {
            page->ahead = 0;
            page->held = 0;
        }
        return 0;       /* mounted, and nothing on it the viewer can open */
    }
    if (g_card_runs > 0 || g_card_others > 0) {
        return runs_list(out, max_entries, page);
    }
    static const uint32_t sizes[] = { 900u, 4096u, 300u, 200u, 120u,
                                      3u * 1024u * 1024u, 12u };
    int n = 0;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]) && n < max_entries;
         ++i) {
        if (g_removed[i]) {
            continue;
        }
        /* LONG.CSV is on the card only for the zoom cases that write it. */
        if (k_card[i].text == g_long && g_long[0] == '\0') {
            continue;
        }
        snprintf(out[n].name, sizeof(out[n].name), "%s", k_card[i].name);
        out[n].size = ((int)i == g_big_index)
                          ? g_big_size
                          : sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
        out[n].is_dir = ((int)i == g_dir_index);
        ++n;
    }
    /* The fixture card is one page, in the order the cases address it. */
    if (page != NULL) {
        page->ahead = 0;
        page->held = n;
    }
    return n;
}

static bool fake_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    ++g_open_calls;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]); ++i) {
        if (strcmp(name, k_card[i].name) == 0 && !g_removed[i]) {
            if (k_card[i].text == NULL) {
                return false; /* listed, but gone by the time it was opened */
            }
            log_source_memory(src, &g_ctx, k_card[i].text,
                              strlen(k_card[i].text));
            return true;
        }
    }
    return false;
}

static const char *fake_volume(void *ctx)
{
    (void)ctx;
    return g_no_card ? "" : g_volume;
}

static bool fake_remove(const char *name, void *ctx)
{
    (void)ctx;
    ++g_remove_calls;
    snprintf(g_removed_name, sizeof(g_removed_name), "%s", name);
    if (g_remove_refuses) {
        return false;
    }
    const int run = log_run_number(name);
    if (g_card_runs > 0 && run >= g_card_run0
        && run < g_card_run0 + g_card_runs && !g_run_gone[run]) {
        g_run_gone[run] = true;
        return true;
    }
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]); ++i) {
        if (strcmp(name, k_card[i].name) == 0 && !g_removed[i]) {
            g_removed[i] = true;
            return true;
        }
    }
    return false;
}

/* What the slot holds while g_no_card: asked after a list that failed. */
static card_state_t g_card_state;
static unsigned g_card_asks;

static card_state_t fake_card(void *ctx)
{
    (void)ctx;
    ++g_card_asks;
    return g_card_state;
}

static unsigned fake_mounts(void *ctx)
{
    (void)ctx;
    return g_mounts;
}

static const log_viewer_io_t k_io = {
    .list = fake_list,
    .open = fake_open,
    .close = NULL,
    .volume = fake_volume,
    .remove = fake_remove,
    .card = fake_card,
    .mounts = fake_mounts,
    .ctx = NULL,
};

/* ------------------------------------------------------------- harness --- */

static gfx_color_t *s_fb;
static gfx_color_t *s_fb_b;
static gfx_canvas_t s_c;


static const ui_screen_t *screen(void)
{
    return log_viewer_screen();
}

/* Put the screen back to its start state without touching any framebuffer --
 * the stale-pixel case needs to replay the same interaction into a second
 * buffer, and wiping the first one would compare a render against a blank. */
static void reset_screen(void)
{
    settings_init();
    ui_theme_set(UI_THEME_DARK);
    g_no_card = false;
    g_card_state = CARD_STATE_NONE;
    g_card_asks = 0u;
    g_empty_card = false;
    g_card_runs = 0;
    g_card_others = 0;
    g_card_run0 = 1;
    memset(g_run_gone, 0, sizeof(g_run_gone));
    memset(&g_last_page, 0, sizeof(g_last_page));
    g_volume = "BENCH01";
    g_swap_volume = NULL;
    g_mounts = 0u;
    memset(g_removed, 0, sizeof(g_removed));
    g_remove_calls = 0;
    g_removed_name[0] = '\0';
    g_remove_refuses = false;
    screen()->reset();
    log_viewer_set_io(&k_io);
    screen()->enter();
}

static void fresh(void)
{
    g_dir_index = -1;
    g_big_index = -1;
    g_big_size = 0u;
    g_open_calls = 0;
    g_long[0] = '\0';
    if (s_fb == NULL) {
        s_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(s_fb);
    }
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&s_c, s_fb, W, H, W);
    reset_screen();
}

static void send_id(int type, int id, int x, int y)
{
    touch_event_t e = {
        .type = (touch_event_type_t)type,
        .point = { .id = (uint8_t)id, .x = (int16_t)x, .y = (int16_t)y },
    };
    screen()->event(&e);
}

static void send(int type, int x, int y)
{
    send_id(type, 1, x, y);
}

static void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
}

/* Draw the current state.  Called after every interaction in the cases below,
 * because a screen that decides correctly and then draws nothing is still
 * broken -- and because a crash in a rarely-reached panel should surface
 * here rather than on the bench. */
static void draw(void)
{
    screen()->render(&s_c, 0);
}

/* Geometry mirrored from the screen; if it moves, these move with it.  Panel
 * coordinates: the band is UI_BAND_H rows tall and the screen starts below. */
#define BR_ROW_Y(i) (44 + 30 + (i) * 44 + 18)
/* BR_ROWS: the file rows the list shows at once. */
#define BR_ROWS_SEEN 6
#define IM_ROW_Y(i) (36 + 30 + (i) * 36 + 14)
#define IM_BTN_CY   (352 + 21)
#define SEP_X       110
#define NUM_X       308
#define BACK_X      500
#define PLOT_X      690
#define FOOT_CY     373
#define OPEN_X      724
#define DELETE_X    596
#define RESCAN_X    76
/* The plot view: a point inside the plot, and the > button's centre. */
#define PV_TAP_X    400
#define NEXT_X      324
#define FOOT_PLOT_CY (352 + 21)
/* The plot area: left edge, width in columns, the row a constant trace runs
 * along (the middle grid line) and the bottom row. */
#define PV_X0       24
#define PV_COLS     752
#define PV_TOP_Y    76
#define PV_BOTTOM_Y (76 + 241)
/* The DELETE question's two buttons. */
#define DQ_CY       282
#define DQ_CANCEL_X 264
#define DQ_DELETE_X 536

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

static void open_file(int index)
{
    tap(400, BR_ROW_Y(index)); /* select */
    tap(400, BR_ROW_Y(index)); /* open   */
}

static int column_row(const log_analysis_t *a, const char *name)
{
    for (int i = 0; i < a->n_columns; ++i) {
        if (strcmp(a->columns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
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

TEST_CASE(plotting_loads_exactly_the_picked_columns)
{
    fresh();
    open_file(0);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    /* Four numeric non-time columns, so all four slots are preselected. */
    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);

    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->n_fields, 4);
    CHECK_EQ(d->count, 4);
    CHECK_NEAR(d->duration_s, 0.06, 1e-4);
    for (int k = 0; k < d->n_fields; ++k) {
        if (strcmp(d->field[k].name, "voltage") == 0) {
            CHECK_NEAR(d->value[k][0], 22.34, 1e-3);
            CHECK_STR_EQ(d->field[k].unit, "V");
        }
    }
}

TEST_CASE(tapping_a_column_toggles_it_and_the_time_axis_is_not_offered)
{
    fresh();
    open_file(0);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }

    int temp = column_row(a, "temp");
    CHECK(temp >= 0);
    tap(600, IM_ROW_Y(temp)); /* off */
    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->n_fields, 3);
    for (int k = 0; k < d->n_fields; ++k) {
        CHECK(strcmp(d->field[k].name, "temp") != 0);
    }

    /* Back to the picker: the time column must refuse to become a trace,
     * because plotting it against itself just draws a diagonal. */
    tap(100, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tap(600, IM_ROW_Y(a->time_index));
    tap(PLOT_X, IM_BTN_CY);
    d = log_viewer_data();
    if (d != NULL) {
        for (int k = 0; k < d->n_fields; ++k) {
            CHECK(strcmp(d->field[k].name, "time") != 0);
        }
    }
}

/* storage_list applies the suffix filter only to files, so subdirectories are
 * listed.  There is no directory navigation; opening a directory is reported
 * as such rather than analysed as a log. */
/* Both override buttons re-run the analysis.  Pins: re-running the analysis
 * keeps the operator's column selection. */
TEST_CASE(an_override_does_not_discard_the_column_selection)
{
    fresh();
    open_file(0);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }

    /* Turn one column off, so the selection is something the operator chose
     * rather than the default. */
    int row = column_row(a, "current");
    if (row < 0) {
        T_FAIL("no current column");
        return;
    }
    tap(600, IM_ROW_Y(row));

    /* Cycle the decimal-convention override all the way round (AUTO, DE, EN,
     * AUTO) so the file ends up analysed as it started.  Each press re-runs
     * the analysis, which must keep the selection made above.  Going round
     * the whole loop matters: stopping on DE makes voltage and current
     * unparseable, and then a preserved selection and a rebuilt one agree by
     * accident. */
    tap(NUM_X, IM_BTN_CY);
    tap(NUM_X, IM_BTN_CY);
    tap(NUM_X, IM_BTN_CY);
    if (log_viewer_analysis() == NULL) {
        T_FAIL("no analysis after the override");
        return;
    }

    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("nothing plotted");
        return;
    }
    for (int i = 0; i < d->n_fields; ++i) {
        if (strcmp(d->field[i].name, "current") == 0) {
            T_FAIL("the override put 'current' back");
        }
    }
    CHECK(d->n_fields > 0);
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

TEST_CASE(a_column_of_prose_is_not_plottable)
{
    fresh();
    open_file(2);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK(!a->columns[0].numeric);

    tap(PLOT_X, IM_BTN_CY);
    /* Nothing to draw, so the screen stays where the user can fix it. */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_data() == NULL);
}

TEST_CASE(forcing_the_separator_and_the_convention_re_runs_the_analysis)
{
    fresh();
    open_file(0);

    /* AUTO -> ';' on a comma file: the columns collapse to one. */
    tap(SEP_X, IM_BTN_CY);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis after forcing");
        return;
    }
    CHECK_EQ(a->delimiter, ';');
    CHECK_EQ(a->n_columns, 1);

    /* Back round the cycle to AUTO and the file reads correctly again. */
    for (int i = 0; i < 4; ++i) {
        tap(SEP_X, IM_BTN_CY);
    }
    a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis after cycling back");
        return;
    }
    CHECK_EQ(a->delimiter, ',');
    CHECK_EQ(a->n_columns, 5);

    /* Forcing German on an English file must reject values, not corrupt them. */
    tap(NUM_X, IM_BTN_CY);
    a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis after forcing german");
        return;
    }
    CHECK(a->convention_forced);
    CHECK_EQ(a->convention, LOG_CONV_DE);
    int v = column_row(a, "voltage");
    CHECK(v >= 0);
    if (v >= 0) {
        CHECK_EQ(a->columns[v].parsed, 0);
    }
}

TEST_CASE(the_cursor_follows_the_touch_and_stays_in_range)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }

    /* Dragging off the left edge clamps to the first sample, not to -1. */
    send(TOUCH_EVENT_DOWN, 400, 250);
    send(TOUCH_EVENT_MOVE, -50, 250);
    send(TOUCH_EVENT_UP, -50, 250);
    screen()->render(&s_c, 0);

    send(TOUCH_EVENT_DOWN, 400, 250);
    send(TOUCH_EVENT_MOVE, 2000, 250);
    send(TOUCH_EVENT_UP, 2000, 250);
    screen()->render(&s_c, 0);

    /* The step buttons walk it one sample at a time and stop at the ends. */
    for (int i = 0; i < 10; ++i) {
        tap(236, FOOT_CY);
    }
    screen()->render(&s_c, 0);
    for (int i = 0; i < 10; ++i) {
        tap(324, FOOT_CY);
    }
    screen()->render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

TEST_CASE(fields_returns_to_the_picker_and_keeps_the_selection)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);

    tap(100, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);

    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    const log_data_t *d = log_viewer_data();
    if (d != NULL) {
        CHECK_EQ(d->n_fields, 4);
    }
}

TEST_CASE(back_from_the_picker_returns_to_the_file_list)
{
    fresh();
    open_file(0);
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
}

static int first_diff(const gfx_color_t *a, const gfx_color_t *b, int *count)
{
    int at = -1;
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (a[i] != b[i]) {
            if (at < 0) {
                at = i;
            }
            ++n;
        }
    }
    *count = n;
    return at;
}

TEST_CASE(redraw_leaves_no_stale_pixels)
{
    /* With two framebuffers, anything drawn outside the region a screen clears
     * shows up as flicker rather than as an obviously stale pixel.  Rendering
     * state B over state A must equal rendering B onto a fresh buffer. */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_t cb;

    static const int cursor_x[] = { 30, 700, 400, 24, 775 };

    for (size_t i = 1; i < sizeof(cursor_x) / sizeof(cursor_x[0]); ++i) {
        fresh();
        open_file(0);
        tap(PLOT_X, IM_BTN_CY);
        tap(cursor_x[i - 1], 250);
        screen()->render(&s_c, 0);
        tap(cursor_x[i], 250);
        screen()->render(&s_c, 0);

        memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
        gfx_canvas_init(&cb, s_fb_b, W, H, W);
        reset_screen();
        open_file(0);
        tap(PLOT_X, IM_BTN_CY);
        tap(cursor_x[i], 250);
        screen()->render(&cb, 0);

        int count = 0;
        int at = first_diff(s_fb, s_fb_b, &count);
        if (at >= 0) {
            T_FAIL("cursor %d -> %d: %d stale pixel(s), first at (%d,%d)",
                   cursor_x[i - 1], cursor_x[i], count, at % W, at / W);
        }
    }
}

TEST_CASE(both_framebuffers_follow_an_interaction)
{
    /* The screen caches its frame per framebuffer, and the panel alternates
     * between two.  An interaction that invalidates only the buffer being
     * drawn leaves the other one a frame behind, which reads as flicker rather
     * than as an obviously wrong pixel -- so assert the two agree. */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(s_fb_b);
    }
    gfx_canvas_t cb;
    memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cb, s_fb_b, W, H, W);

    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);

    screen()->render(&s_c, 0);
    screen()->render(&cb, 1);

    tap(700, 250); /* move the cursor */
    screen()->render(&s_c, 0);
    screen()->render(&cb, 1);

    int count = 0;
    int at = first_diff(s_fb, s_fb_b, &count);
    if (at >= 0) {
        T_FAIL("%d pixel(s) differ between framebuffers, first at (%d,%d)",
               count, at % W, at / W);
    }

    /* And with nothing happening, a repeat render is a no-op, which is what
     * makes the cache worth having. */
    screen()->render(&s_c, 0);
    at = first_diff(s_fb, s_fb_b, &count);
    CHECK_EQ(at, -1);
}

/* ------------------------------------------------- what a full card shows -- */

/*
 * The accent tab on the browse panel's top edge is the title's width plus
 * 26 px, and its top row is the one row the chamfer does not cut, so the run
 * of accent pixels along it measures the title.  Cheaper than reading pixels
 * back as letters, and it fails when the title stops saying what it should.
 */
#define BR_TAB_X 16
#define BR_TAB_Y 44

static int browse_tab_width(void)
{
    int w = 0;
    while (BR_TAB_X + w < W &&
           gfx_pixel_get(&s_c, BR_TAB_X + w, BR_TAB_Y) == UI_ACCENT) {
        ++w;
    }
    return w;
}

static int tab_width_for(const char *title)
{
    return gfx_text_width(UI_FONT_LABEL, title, 1) + 26;
}

TEST_CASE(a_run_name_and_its_number_are_one_rule)
{
    /*
     * The logger writes the name and the viewer reads the number back out of
     * it, and that number is the only order the card carries: the board has no
     * clock that survives a power cycle, so every file on it is dated
     * 1980-01-01.  Every run in range has to survive the round trip, because
     * one that does not stops looking like a run and sorts with the files
     * nobody can date.
     */
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        char name[LOG_RUN_NAME_MAX];
        log_run_name(name, sizeof(name), i);
        if (log_run_number(name) != i) {
            T_FAIL("run %d wrote \"%s\", read back %d", i, name,
                   log_run_number(name));
            break;
        }
    }
    char first[LOG_RUN_NAME_MAX];
    log_run_name(first, sizeof(first), LOG_RUN_FIRST);
    CHECK_STR_EQ(first, "BENCH001.CSV");
    log_run_name(first, sizeof(first), LOG_RUN_LAST);
    CHECK_STR_EQ(first, "BENCH999.CSV");

    /* A name off a computer, in lower case, is the same run. */
    CHECK_EQ(log_run_number("bench042.csv"), 42);

    /* A run's report carries its number under its own suffix, and is not a
     * run itself. */
    log_report_name(first, sizeof(first), 7);
    CHECK_STR_EQ(first, "BENCH007.TXT");
    CHECK_EQ(log_report_number(first), 7);
    CHECK_EQ(log_report_number("bench999.txt"), 999);
    CHECK_EQ(log_report_number("BENCH007.CSV"), -1);
    CHECK_EQ(log_report_number("BENCH000.TXT"), -1);
    CHECK_EQ(log_run_number("BENCH007.TXT"), -1);
    log_report_name(first, sizeof(first), LOG_RUN_LAST + 1);
    CHECK_STR_EQ(first, "");

    /* And what is not a run of this bench's: a short number, a long one, one
     * below where the numbering starts, another suffix, and a name that only
     * begins like one. */
    CHECK_EQ(log_run_number("BENCH12.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH0001.CSV"), -1);
    CHECK_EQ(log_run_number("BENCHABC.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH000.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH001.TXT"), -1);
    CHECK_EQ(log_run_number("BENCH001.CSV.BAK"), -1);
    CHECK_EQ(log_run_number("BENCH"), -1);
    CHECK_EQ(log_run_number("SWEEP_920KV.CSV"), -1);
    CHECK_EQ(log_run_number(""), -1);
    CHECK_EQ(log_run_number(NULL), -1);

    /* A number it cannot write is not written as a near miss. */
    char name[LOG_RUN_NAME_MAX];
    log_run_name(name, sizeof(name), LOG_RUN_FIRST - 1);
    CHECK_STR_EQ(name, "");
    log_run_name(name, sizeof(name), LOG_RUN_LAST + 1);
    CHECK_STR_EQ(name, "");
    char small[LOG_RUN_NAME_MAX - 1];
    log_run_name(small, sizeof(small), LOG_RUN_FIRST);
    CHECK_STR_EQ(small, "");
    log_run_name(NULL, sizeof(name), LOG_RUN_FIRST); /* nowhere to write it */
}

TEST_CASE(the_newest_run_outranks_the_rest_of_the_card)
{
    /* Which entry a full list keeps.  A run is the only entry whose age is
     * known, so runs come first and the newest of them first of all;
     * everything else follows in the order it is drawn. */
    CHECK(log_name_rank("BENCH200.CSV", "BENCH199.CSV") < 0);
    CHECK(log_name_rank("BENCH049.CSV", "BENCH050.CSV") > 0);
    CHECK(log_name_rank("BENCH100.CSV", "BENCH099.CSV") < 0);
    CHECK_EQ(log_name_rank("BENCH007.CSV", "BENCH007.CSV"), 0);

    /* A run against a file nobody can date, both ways round. */
    CHECK(log_name_rank("BENCH001.CSV", "SWEEP_920KV.CSV") < 0);
    CHECK(log_name_rank("SWEEP_920KV.CSV", "BENCH999.CSV") > 0);

    /* And two of those: by name, with case folded, as the list draws them. */
    CHECK(log_name_rank("aaa.csv", "BBB.CSV") < 0);
    CHECK(log_name_rank("SWEEP.CSV", "PRUEFUNG.CSV") > 0);
    CHECK_EQ(log_name_rank("SWEEP.CSV", "sweep.csv"), 0);
    CHECK(log_name_rank(NULL, "SWEEP.CSV") < 0);
    CHECK(log_name_rank("SWEEP.CSV", NULL) > 0);
}

TEST_CASE(a_card_of_999_runs_offers_the_newest_that_fit)
{
    /*
     * The runs arrive oldest first, which is the order a FAT (File Allocation
     * Table) directory hands its entries back until a file is deleted and its
     * slot filled again.  Keeping the first LOG_VIEWER_MAX_FILES to arrive
     * would put the oldest runs on the first page and the newest on the
     * last.
     */
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    int held = 0;
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        log_run_name(f.name, sizeof(f.name), i);
        f.size = ((uint64_t)i << 32) | (uint64_t)i;   /* past 4 GiB */
        held = log_select_keep(set, held, LOG_VIEWER_MAX_FILES, &f);
    }
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);

    /* Drawn newest at the top, the order the pages run in. */
    log_select_sort(set, held);
    for (int r = 0; r < held; ++r) {
        char want[LOG_RUN_NAME_MAX];
        log_run_name(want, sizeof(want), LOG_RUN_LAST - r);
        if (strcmp(set[r].name, want) != 0) {
            T_FAIL("row %d: got \"%s\", want \"%s\"", r, set[r].name, want);
            break;
        }
    }
    /* The whole entry travels with the name it was kept by. */
    CHECK(set[0].size
          == (((uint64_t)LOG_RUN_LAST << 32) | (uint64_t)LOG_RUN_LAST));
}

/*
 * Directories sort above files whichever side of the comparison they arrive
 * on. qsort decides that, so a set whose only directory is already first
 * exercises one answer and never the other, and a comparator that returned
 * the same sign both ways would order correctly here and wrongly on a card
 * that happened to be read in another order.
 */
TEST_CASE(a_directory_sorts_first_from_either_side_of_the_comparison)
{
    static const char *const names[] = { "A.CSV", "DIR", "B.CSV", "ZDIR" };
    static const bool dirs[]         = { false,   true,  false,   true };

    log_viewer_file_t set[4];
    for (int i = 0; i < 4; ++i) {
        memset(&set[i], 0, sizeof(set[i]));
        snprintf(set[i].name, sizeof(set[i].name), "%s", names[i]);
        set[i].is_dir = dirs[i];
    }
    log_select_sort(set, 4);
    CHECK_STR_EQ(set[0].name, "DIR");
    CHECK_STR_EQ(set[1].name, "ZDIR");
    CHECK_STR_EQ(set[2].name, "A.CSV");
    CHECK_STR_EQ(set[3].name, "B.CSV");

    /* And again with the input reversed, so the comparator meets the same
     * pairs the other way round. */
    for (int i = 0; i < 4; ++i) {
        memset(&set[i], 0, sizeof(set[i]));
        snprintf(set[i].name, sizeof(set[i].name), "%s", names[3 - i]);
        set[i].is_dir = dirs[3 - i];
    }
    log_select_sort(set, 4);
    CHECK_STR_EQ(set[0].name, "DIR");
    CHECK_STR_EQ(set[1].name, "ZDIR");
    CHECK_STR_EQ(set[2].name, "A.CSV");
    CHECK_STR_EQ(set[3].name, "B.CSV");
}

TEST_CASE(a_full_list_keeps_runs_over_what_it_cannot_date)
{
    /* Four slots and five entries, offered in the order a card holds them.
     * Runs take the slots; of what is left the name decides, so the file that
     * sorts last is the one that goes. */
    static const struct {
        const char *name;
        bool is_dir;
    } offered[] = {
        { "SWEEP.CSV", false },
        { "BENCH001.CSV", false },
        { "OLD", true },
        { "BENCH002.CSV", false },
        { "BENCH003.CSV", false },
    };
    log_viewer_file_t set[4];
    int held = 0;
    for (size_t i = 0; i < sizeof(offered) / sizeof(offered[0]); ++i) {
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", offered[i].name);
        f.is_dir = offered[i].is_dir;
        held = log_select_keep(set, held, (int)(sizeof(set) / sizeof(set[0])),
                               &f);
    }
    CHECK_EQ(held, 4);

    /* A candidate that ranks below a full set is dropped where it stands, and
     * the set does not move: the four already held are the four to keep. */
    log_viewer_file_t late;
    memset(&late, 0, sizeof(late));
    snprintf(late.name, sizeof(late.name), "%s", "ZZZ_IMPORT.CSV");
    CHECK_EQ(log_select_keep(set, held, 4, &late), 4);

    log_select_sort(set, held);
    /* Directories first, then the newest run: the order the browse list
     * draws. */
    CHECK_STR_EQ(set[0].name, "OLD");
    CHECK_STR_EQ(set[1].name, "BENCH003.CSV");
    CHECK_STR_EQ(set[2].name, "BENCH002.CSV");
    CHECK_STR_EQ(set[3].name, "BENCH001.CSV");

    /* A set with no room takes nothing and says so. */
    log_viewer_file_t one;
    memset(&one, 0, sizeof(one));
    snprintf(one.name, sizeof(one.name), "%s", "BENCH999.CSV");
    CHECK_EQ(log_select_keep(set, 0, 0, &one), 0);
    CHECK_EQ(log_select_keep(NULL, 0, 4, &one), 0);
    CHECK_EQ(log_select_keep(set, held, 4, NULL), held);
    log_select_sort(NULL, 4);
    log_select_sort(set, 1);
    CHECK_STR_EQ(set[0].name, "OLD");
}

TEST_CASE(a_list_that_was_cut_says_so)
{
    /*
     * A card takes LOG_RUN_LAST runs and the list holds LOG_VIEWER_MAX_FILES,
     * so most of a full card is not on the screen.  The browse panel's tab
     * carries both numbers when that happens: a list that quietly shows a
     * subset reads as the whole card, and the run that is missing from it
     * reads as a run that was never written.
     */
    fresh();
    g_card_runs = LOG_VIEWER_MAX_FILES;  /* exactly fits: nothing to say */
    log_viewer_refresh();
    draw();
    CHECK_EQ(browse_tab_width(), tab_width_for("FILES"));

    fresh();
    g_card_runs = 200;
    log_viewer_refresh();
    draw();
    char want[32];
    snprintf(want, sizeof(want), "FILES 1 TO %d OF %d", LOG_VIEWER_MAX_FILES,
             200);
    CHECK_EQ(browse_tab_width(), tab_width_for(want));
}

TEST_CASE(a_count_above_the_list_does_not_reach_past_it)
{
    /*
     * The count is what the card holds, which is more than the lister wrote
     * into the array.  Rows come out of the array, so the list has to stop at
     * what is in it: scrolling to the bottom of a 200-run card lands on the
     * oldest run written, not on whatever follows the array.
     */
    fresh();
    g_card_runs = 200;
    log_viewer_refresh();

    /* Two drags: one press scrolls by its own travel and the bottom of a
     * 48-row list is further down than that. */
    for (int i = 0; i < 2; ++i) {
        send(TOUCH_EVENT_DOWN, 400, 300);
        send(TOUCH_EVENT_MOVE, 400, -700);
        send(TOUCH_EVENT_UP, 400, -700);
    }
    draw();

    /* The top row is now six from the end of what was written, and the last
     * run written is the oldest one on the page, the newest at the top. */
    char want[LOG_RUN_NAME_MAX];
    log_run_name(want, sizeof(want),
                 g_card_runs - LOG_VIEWER_MAX_FILES + BR_ROWS_SEEN);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_STR_EQ(log_viewer_open_name(), want);
}

/* ------------------------------------------------------------- deleting -- */

/* Select a row and ask to delete it: the question, not the deletion. */
static void ask_to_delete(int row)
{
    tap(400, BR_ROW_Y(row));
    tap(DELETE_X, FOOT_CY);
}

/* -------------------------------------------------------------- pages --- */

/*
 * A card of @p runs runs and @p others other files, offered in an order that
 * is neither the rank nor its reverse, and its full rank order in @p all.
 */
#define PG_MAX (LOG_RUN_LAST + 8)
static log_viewer_file_t g_pg_all[PG_MAX];
static int g_pg_n;
static int g_pg_order[PG_MAX];

static int by_rank(const void *a, const void *b)
{
    return log_name_rank(((const log_viewer_file_t *)a)->name,
                         ((const log_viewer_file_t *)b)->name);
}

/* One entry on the card, @p size telling it from any other. */
static void pg_put(const char *name, uint32_t size)
{
    if (g_pg_n < PG_MAX) {
        memset(&g_pg_all[g_pg_n], 0, sizeof(g_pg_all[0]));
        snprintf(g_pg_all[g_pg_n].name, sizeof(g_pg_all[0].name), "%s", name);
        g_pg_all[g_pg_n].size = size;
        ++g_pg_n;
    }
}

/* The offer order: a stride coprime with any count up to PG_MAX scatters
 * it. */
static void pg_scatter(void)
{
    for (int i = 0; i < g_pg_n; ++i) {
        g_pg_order[i] = (int)(((long)i * 7919L) % g_pg_n);
    }
}

/* @p runs runs written by writer @p prefix, or with @p mixed by BENCH, ESC
 * and SERVO in turn, and @p others other files. */
static void pg_card_by(int runs, int others, bool mixed, log_prefix_t prefix)
{
    g_pg_n = 0;
    for (int r = 1; r <= runs; ++r) {
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name),
                       mixed ? (log_prefix_t)(r % (int)LOG_PREFIX_COUNT)
                             : prefix,
                       r, false);
        pg_put(name, (uint32_t)r);
    }
    for (int i = 0; i < others; ++i) {
        char name[LOG_VIEWER_NAME_MAX];
        snprintf(name, sizeof(name), "OTHER%03d.CSV", i);
        pg_put(name, 0u);
    }
    pg_scatter();
}

static void pg_card(int runs, int others)
{
    pg_card_by(runs, others, false, LOG_PREFIX_BENCH);
}

/* One walk: the page @p q asks for, into @p set of @p cap. */
static int pg_walk(log_viewer_file_t *set, int cap, log_viewer_page_t *q)
{
    log_select_page_t sel;
    log_select_page_begin(&sel, set, cap, q);
    for (int i = 0; i < g_pg_n; ++i) {
        log_select_page_offer(&sel, &g_pg_all[g_pg_order[i]]);
    }
    return log_select_page_end(&sel, q);
}

/* The edge of a page in rank order: first (@p sign 1) or last (-1). */
static const char *pg_edge(const log_viewer_file_t *set, int n, int sign)
{
    int best = 0;
    for (int i = 1; i < n; ++i) {
        if (sign * log_name_rank(set[i].name, set[best].name) < 0) {
            best = i;
        }
    }
    return set[best].name;
}

/* Whether @p set holds ranks [first, first + n) of the card, drawn in
 * display order. */
static bool pg_is_slice(const log_viewer_file_t *set, int n, int first,
                        const log_viewer_file_t *ranked)
{
    static log_viewer_file_t want[LOG_VIEWER_MAX_FILES];
    memcpy(want, ranked + first, (size_t)n * sizeof(want[0]));
    log_select_sort(want, n);
    for (int i = 0; i < n; ++i) {
        if (strcmp(set[i].name, want[i].name) != 0
            || set[i].size != want[i].size) {
            return false;
        }
    }
    return true;
}

/* Index of @p name in g_pg_all, or -1. */
static int pg_index(const char *name)
{
    for (int i = 0; i < g_pg_n; ++i) {
        if (strcmp(g_pg_all[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * One page as it is drawn: in strict rank order, so its runs stand newest
 * first whichever writer made them, and each entry counted in @p hits.
 */
static void pg_count(const log_viewer_file_t *set, int n, int *hits)
{
    for (int i = 0; i < n; ++i) {
        const int k = pg_index(set[i].name);
        CHECK(k >= 0);
        if (k >= 0) {
            ++hits[k];
        }
        if (i > 0 && log_name_rank(set[i - 1].name, set[i].name) >= 0) {
            T_FAIL("\"%s\" is drawn above \"%s\"", set[i - 1].name,
                   set[i].name);
        }
        const int a = (i > 0) ? log_run_number(set[i - 1].name) : -1;
        const int b = log_run_number(set[i].name);
        if (a >= 0 && b >= 0 && b > a) {
            T_FAIL("run %d is drawn under the older run %d", b, a);
        }
    }
}

/*
 * The card in g_pg_all, paged: NEXT's chain from the first page reaches every
 * entry once, in rank order, and PREV's chain from the last page walks the
 * same pages back.  Every entry is on exactly one page each way.
 */
static void pg_check(void)
{
    static log_viewer_file_t ranked[PG_MAX];
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    static int starts[PG_MAX / LOG_VIEWER_MAX_FILES + 2];
    static int fwd[PG_MAX];
    static int back[PG_MAX];
    memset(fwd, 0, sizeof(fwd));
    memset(back, 0, sizeof(back));
    memcpy(ranked, g_pg_all, (size_t)g_pg_n * sizeof(ranked[0]));
    qsort(ranked, (size_t)g_pg_n, sizeof(ranked[0]), by_rank);

    const int pages = (g_pg_n + LOG_VIEWER_MAX_FILES - 1)
                      / LOG_VIEWER_MAX_FILES;
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    int seen = 0;
    int p = 0;
    for (;;) {
        const int held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
        CHECK_EQ(q.held, held);
        if (held == 0 || p > pages) {
            break;
        }
        CHECK_EQ(q.ahead, seen);
        CHECK(pg_is_slice(set, held, seen, ranked));
        CHECK_EQ(held, (g_pg_n - seen < LOG_VIEWER_MAX_FILES)
                           ? g_pg_n - seen : LOG_VIEWER_MAX_FILES);
        pg_count(set, held, fwd);
        starts[p++] = seen;
        seen += held;
        q.dir = LOG_PAGE_AFTER;
        snprintf(q.key, sizeof(q.key), "%s", pg_edge(set, held, -1));
    }
    CHECK_EQ(seen, g_pg_n);
    CHECK_EQ(p, pages);

    /* Back from the last page: BEFORE with no key is the last of all,
     * cut to what whole pages leave. */
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_BEFORE;
    if (pages == 0) {
        CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), 0);
        CHECK_EQ(q.ahead, 0);
        return;
    }
    int held = pg_walk(set, g_pg_n - starts[pages - 1], &q);
    CHECK_EQ(q.ahead, starts[pages - 1]);
    CHECK(pg_is_slice(set, held, starts[pages - 1], ranked));
    pg_count(set, held, back);
    for (int k = pages - 2; k >= 0; --k) {
        snprintf(q.key, sizeof(q.key), "%s", pg_edge(set, held, 1));
        held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
        CHECK_EQ(held, LOG_VIEWER_MAX_FILES);
        CHECK_EQ(q.ahead, starts[k]);
        CHECK(pg_is_slice(set, held, starts[k], ranked));
        pg_count(set, held, back);
    }
    /* And before the first page there is nothing. */
    snprintf(q.key, sizeof(q.key), "%s", pg_edge(set, held, 1));
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), 0);
    CHECK_EQ(q.ahead, 0);

    for (int i = 0; i < g_pg_n; ++i) {
        if (fwd[i] != 1 || back[i] != 1) {
            T_FAIL("\"%s\" is on %d pages forward and %d back",
                   g_pg_all[i].name, fwd[i], back[i]);
        }
    }
}

TEST_CASE(pages_of_a_card_cover_it_once_in_rank_order_both_ways)
{
    /*
     * At the page boundaries: none, one page exactly, one over, two exactly,
     * two and one over, and a full card of 999 runs with files the bench did
     * not write behind them.
     */
    static const struct { int runs, others; } cards[] = {
        { 0, 0 }, { 48, 0 }, { 49, 0 }, { 96, 0 }, { 97, 0 },
        { 999, 0 }, { 999, 3 }, { 40, 9 },
    };
    for (size_t c = 0; c < sizeof(cards) / sizeof(cards[0]); ++c) {
        pg_card(cards[c].runs, cards[c].others);
        pg_check();
    }
}

TEST_CASE(pages_of_mixed_writers_run_newest_first_across_the_names)
{
    /*
     * The 7 writers in turn: 999 runs with and without other files
     * behind them, and the page boundaries.  Each page holds all seven
     * names and draws them by number, newest at the top; the pages run on
     * by number across the names.
     */
    static const int runs[] = { 47, 48, 49, 96, 97, 999 };
    for (size_t c = 0; c < sizeof(runs) / sizeof(runs[0]); ++c) {
        pg_card_by(runs[c], 0, true, LOG_PREFIX_BENCH);
        pg_check();
    }
    pg_card_by(LOG_RUN_LAST, 3, true, LOG_PREFIX_BENCH);
    pg_check();
    /* Each writer alone pages the same way. */
    pg_card_by(LOG_RUN_LAST, 0, false, LOG_PREFIX_ESC);
    pg_check();
    pg_card_by(LOG_RUN_LAST, 0, false, LOG_PREFIX_SERVO);
    pg_check();

    /* The first page of the mixed full card: 999 down to 952, all seven
     * names in it. */
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    pg_card_by(LOG_RUN_LAST, 0, true, LOG_PREFIX_BENCH);
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(set[0].name, "BENCH999.CSV");       /* 999 % 7 == 5 */
    CHECK_STR_EQ(set[1].name, "SUPPL998.CSV");
    CHECK_STR_EQ(set[2].name, "MOTOR997.CSV");
    CHECK_STR_EQ(set[3].name, "SERVO996.CSV");
    CHECK_STR_EQ(set[4].name, "LIMIT995.CSV");
    CHECK_STR_EQ(set[5].name, "ATEST994.CSV");
    CHECK_STR_EQ(set[6].name, "ESC993.CSV");
    CHECK_STR_EQ(set[LOG_VIEWER_MAX_FILES - 1].name, "ATEST952.CSV");
}

/*
 * A card edited on a computer can hold one number under several names.  The
 * rank breaks that tie by name, BENCH before ESC before SERVO, case folded,
 * so the order stays strict and a page edge between them drops none.
 */
static void pg_tie_card(int above, const char *const *tied, int n_tied)
{
    g_pg_n = 0;
    for (int r = 42 + above; r > 42; --r) {
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name),
                       (log_prefix_t)(r % (int)LOG_PREFIX_COUNT), r, false);
        pg_put(name, (uint32_t)r);
    }
    for (int i = 0; i < n_tied; ++i) {
        pg_put(tied[i], 4200u + (uint32_t)i);
    }
    for (int r = 1; r < 42; ++r) {
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name),
                       (log_prefix_t)(r % (int)LOG_PREFIX_COUNT), r, false);
        pg_put(name, (uint32_t)r);
    }
    pg_scatter();
}

TEST_CASE(runs_that_share_a_number_split_by_a_page_edge_are_each_on_one_page)
{
    static const char *const upper[] = {
        "SERVO042.CSV", "BENCH042.CSV", "ESC042.CSV",
    };
    static const char *const lower[] = {
        "servo042.csv", "Bench042.CSV", "esc042.csv",
    };
    CHECK(log_name_rank("BENCH042.CSV", "ESC042.CSV") < 0);
    CHECK(log_name_rank("ESC042.CSV", "SERVO042.CSV") < 0);
    CHECK(log_name_rank("SERVO042.CSV", "BENCH042.CSV") > 0);
    CHECK(log_name_rank("servo042.csv", "Bench042.CSV") > 0);
    CHECK_EQ(log_name_rank("ESC042.CSV", "esc042.csv"), 0);
    CHECK(log_name_rank("BENCH043.CSV", "SERVO042.CSV") < 0);

    /* 47, 46 and 45 runs above: the edge after the first, after the
     * second, and after all three of the tied names; then no edge at 48. */
    static const int above[] = { 47, 46, 45, 48, 44 };
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    for (size_t a = 0; a < sizeof(above) / sizeof(above[0]); ++a) {
        for (int c = 0; c < 2; ++c) {
            pg_tie_card(above[a], c ? lower : upper, 3);
            pg_check();
        }
    }

    /* 47 above: page 1 ends on BENCH042, page 2 opens on ESC042, and PREV
     * from page 2 is page 1 again with BENCH042 at its foot. */
    pg_tie_card(47, upper, 3);
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    int held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(set[held - 1].name, "BENCH042.CSV");
    q.dir = LOG_PAGE_AFTER;
    snprintf(q.key, sizeof(q.key), "%s", set[held - 1].name);
    held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
    CHECK_EQ(held, 2 + 41);         /* the other two 042s and 041 to 001 */
    CHECK_EQ(q.ahead, LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(set[0].name, "ESC042.CSV");
    CHECK_STR_EQ(set[1].name, "SERVO042.CSV");
    q.dir = LOG_PAGE_BEFORE;
    snprintf(q.key, sizeof(q.key), "%s", set[0].name);
    held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);
    CHECK_EQ(q.ahead, 0);
    CHECK_STR_EQ(set[held - 1].name, "BENCH042.CSV");
}

TEST_CASE(a_page_key_whose_file_has_gone_still_names_its_place)
{
    /* FROM a name the card no longer holds starts with the entry ranking
     * behind it: a deleted page's first entry still finds the page. */
    pg_card(100, 0);
    /* BENCH060 is gone. */
    for (int i = 0; i < g_pg_n; ++i) {
        if (strcmp(g_pg_all[i].name, "BENCH060.CSV") == 0) {
            g_pg_all[i] = g_pg_all[g_pg_n - 1];
            --g_pg_n;
            break;
        }
    }
    for (int i = 0; i < g_pg_n; ++i) {
        g_pg_order[i] = i;
    }
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    snprintf(q.key, sizeof(q.key), "%s", "BENCH060.CSV");
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), LOG_VIEWER_MAX_FILES);
    CHECK_EQ(q.ahead, 40);                     /* runs 100 to 61 */
    CHECK_STR_EQ(pg_edge(set, q.held, 1), "BENCH059.CSV");
    CHECK_STR_EQ(pg_edge(set, q.held, -1), "BENCH012.CSV");

    /* Case is folded: the key is the same file in lower case. */
    snprintf(q.key, sizeof(q.key), "%s", "bench059.csv");
    q.dir = LOG_PAGE_AFTER;
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(pg_edge(set, q.held, 1), "BENCH058.CSV");

    /* No set, no room, no candidate: nothing kept, nothing past its end. */
    log_select_page_t sel;
    log_select_page_begin(&sel, NULL, 4, &q);
    log_select_page_offer(&sel, &g_pg_all[0]);
    log_select_page_offer(&sel, NULL);
    CHECK_EQ(log_select_page_end(&sel, NULL), 0);
    log_select_page_begin(NULL, set, 4, NULL);
    log_select_page_offer(NULL, &g_pg_all[0]);
    CHECK_EQ(log_select_page_end(NULL, &q), 0);
    /* A page of NULL is the first. */
    log_select_page_begin(&sel, set, 4, NULL);
    for (int i = 0; i < g_pg_n; ++i) {
        log_select_page_offer(&sel, &g_pg_all[i]);
    }
    CHECK_EQ(log_select_page_end(&sel, NULL), 4);
    CHECK_STR_EQ(pg_edge(set, 4, 1), "BENCH100.CSV");
}

/* The header row's PREV and NEXT, in screen coordinates. */
#define PREV_X  596
#define NEXT_X_PG 724
#define PG_CY   21

static void next_page(void)
{
    tap(NEXT_X_PG, PG_CY);
}

static void prev_page(void)
{
    tap(PREV_X, PG_CY);
}

/* The tab reads "FILES a TO b OF n", or FILES with @p n 0: the text the
 * list draws, compared as text, and its width on the drawn frame. */
static void check_tab(int a, int b, int n)
{
    draw();
    char want[48];
    if (n == 0) {
        snprintf(want, sizeof(want), "%s", "FILES");
    } else {
        snprintf(want, sizeof(want), "FILES %d TO %d OF %d", a, b, n);
    }
    char got[64];
    log_viewer_browse_title(got, sizeof(got));
    CHECK_STR_EQ(got, want);
    if (browse_tab_width() != tab_width_for(want)) {
        T_FAIL("the tab is %d px wide, \"%s\" is %d", browse_tab_width(),
               want, tab_width_for(want));
    }
}

/* The run OPEN opens, which is the newest run on the page: the page's
 * identity, read without a getter.  Back to the list afterwards. */
static void check_newest(int run)
{
    tap(OPEN_X, FOOT_CY);
    char want[LOG_RUN_NAME_MAX];
    log_run_name(want, sizeof(want), run);
    CHECK_STR_EQ(log_viewer_open_name(), want);
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
}

/* The bar: x of its middle, and the track's top and height. */
#define BAR_X     755
#define BAR_TOP   (44 + 30)
#define BAR_H     (6 * 44 - 4)

/* Open the top row; its run number says where the list is scrolled. */
static int top_row_run(void)
{
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    const int run = log_run_number(log_viewer_open_name());
    tap(BACK_X, IM_BTN_CY);
    return run;
}

/* The thumb on the drawn frame: the first accent row down the bar's middle,
 * and how many accent rows follow. */
static int thumb_top(void)
{
    for (int y = BAR_TOP; y < BAR_TOP + BAR_H; ++y) {
        if (gfx_pixel_get(&s_c, BAR_X, y) == UI_ACCENT
            || gfx_pixel_get(&s_c, BAR_X, y) == UI_PANEL_HI) {
            return y;
        }
    }
    return -1;
}

static int thumb_len(void)
{
    const int y0 = thumb_top();
    if (y0 < 0) {
        return 0;
    }
    const gfx_color_t col = gfx_pixel_get(&s_c, BAR_X, y0);
    int y = y0;
    while (y < BAR_TOP + BAR_H && gfx_pixel_get(&s_c, BAR_X, y) == col) {
        ++y;
    }
    return y - y0;
}

/* The page is drawn newest at the top: row 0 is the page's newest run, the
 * one OPEN opens. */
static void check_top(int run)
{
    check_newest(run);
    CHECK_EQ(top_row_run(), run);
}

/* Nothing selected: OPEN opens nothing, and DELETE asks nothing, so the
 * confirm under it removes nothing. */
static void check_nothing_selected(void)
{
    const int removes = g_remove_calls;
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, removes);
}

static void runs_card(int runs)
{
    fresh();
    g_card_runs = runs;
    log_viewer_refresh();
}

TEST_CASE(a_card_of_one_page_or_none_shows_no_page_buttons)
{
    /* 0 files and exactly 48: the tab reads FILES, and where PREV and NEXT
     * would be a tap reads nothing from the card. */
    fresh();
    g_empty_card = true;
    log_viewer_refresh();
    draw();
    int calls = g_list_calls;
    next_page();
    prev_page();
    CHECK_EQ(g_list_calls, calls);

    runs_card(LOG_VIEWER_MAX_FILES);
    check_tab(0, 0, 0);
    calls = g_list_calls;
    next_page();
    prev_page();
    CHECK_EQ(g_list_calls, calls);
    check_newest(LOG_VIEWER_MAX_FILES);
}

TEST_CASE(forty_nine_runs_are_two_pages_and_the_second_holds_the_oldest)
{
    runs_card(49);
    check_tab(1, 48, 49);
    check_newest(49);

    /* PREV is grey on the first page: a tap reads nothing. */
    int calls = g_list_calls;
    prev_page();
    CHECK_EQ(g_list_calls, calls);

    next_page();
    CHECK_EQ(g_list_calls, calls + 1);       /* one walk for the page */
    check_tab(49, 49, 49);
    check_newest(1);

    /* NEXT is grey on the last. */
    calls = g_list_calls;
    next_page();
    CHECK_EQ(g_list_calls, calls);
    CHECK_EQ(g_last_page.dir, LOG_PAGE_AFTER);

    prev_page();
    CHECK_EQ(g_list_calls, calls + 1);
    CHECK_EQ(g_last_page.dir, LOG_PAGE_BEFORE);
    check_tab(1, 48, 49);
    check_newest(49);
}

TEST_CASE(ninety_six_and_ninety_seven_runs_page_at_48)
{
    runs_card(96);
    check_tab(1, 48, 96);
    next_page();
    check_tab(49, 96, 96);
    check_newest(48);
    int calls = g_list_calls;
    next_page();
    CHECK_EQ(g_list_calls, calls);

    runs_card(97);
    next_page();
    check_tab(49, 96, 97);
    check_newest(49);
    next_page();
    check_tab(97, 97, 97);
    check_newest(1);
    /* 48 ahead of the last page is a whole page: PREV shows page 2, not
     * the first. */
    prev_page();
    check_tab(49, 96, 97);
    check_newest(49);
    prev_page();
    check_tab(1, 48, 97);
    check_newest(97);
}

TEST_CASE(a_full_card_is_reached_to_its_oldest_run_a_walk_a_page)
{
    /* 999 runs: 21 pages, the last holding 39.  Each page is one listing. */
    runs_card(LOG_RUN_LAST);
    const int pages = (LOG_RUN_LAST + LOG_VIEWER_MAX_FILES - 1)
                      / LOG_VIEWER_MAX_FILES;
    for (int p = 1; p < pages; ++p) {
        const int calls = g_list_calls;
        next_page();
        CHECK_EQ(g_list_calls, calls + 1);
        const int a = p * LOG_VIEWER_MAX_FILES + 1;
        const int b = (a + LOG_VIEWER_MAX_FILES - 1 < LOG_RUN_LAST)
                      ? a + LOG_VIEWER_MAX_FILES - 1 : LOG_RUN_LAST;
        check_tab(a, b, LOG_RUN_LAST);
    }
    const int first = (pages - 1) * LOG_VIEWER_MAX_FILES + 1;
    check_tab(first, LOG_RUN_LAST, LOG_RUN_LAST);
    check_newest(LOG_RUN_LAST - first + 1);
    /* Back one page at a time, each page where NEXT found it. */
    for (int p = pages - 2; p >= 0; --p) {
        const int calls = g_list_calls;
        prev_page();
        CHECK_EQ(g_list_calls, calls + 1);
        const int a = p * LOG_VIEWER_MAX_FILES + 1;
        check_tab(a, a + LOG_VIEWER_MAX_FILES - 1, LOG_RUN_LAST);
        check_newest(LOG_RUN_LAST - a + 1);
    }
    check_tab(1, 48, LOG_RUN_LAST);
    check_newest(LOG_RUN_LAST);
}

TEST_CASE(files_the_bench_did_not_write_follow_the_runs_and_are_counted)
{
    /* 48 runs and 5 other .csv files: the runs fill the first page, the
     * others are the second, and the count holds all 53. */
    fresh();
    g_card_runs = 48;
    g_card_others = 5;
    log_viewer_refresh();
    check_tab(1, 48, 53);
    check_newest(48);
    next_page();
    check_tab(49, 53, 53);
    /* No run on that page: nothing selected, OPEN opens nothing. */
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_STR_EQ(log_viewer_open_name(), "OTHER000.CSV");
}

TEST_CASE(deleting_the_last_pages_only_file_shows_the_page_before)
{
    runs_card(49);
    next_page();
    check_tab(49, 49, 49);
    /* The page selects its newest run, BENCH001: DELETE asks about it. */
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[1]);
    /* 48 left, one page: FILES, nothing selected, the newest run on top. */
    check_tab(0, 0, 0);
    check_nothing_selected();
    CHECK_EQ(top_row_run(), 49);

    /* Two on the last page: one deleted, the page stays. */
    runs_card(98);
    next_page();
    next_page();
    check_tab(97, 98, 98);
    tap(DELETE_X, FOOT_CY);                     /* BENCH002, selected */
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[2]);
    check_tab(97, 97, 97);
    check_nothing_selected();
    CHECK_EQ(top_row_run(), 1);
}

TEST_CASE(after_delete_nothing_is_selected_and_the_scroll_stays)
{
    /*
     * Page 2 of 150 runs, scrolled to its bottom: rows 60 to 55 show.  The
     * page's newest run, BENCH102, is off screen above them.  DELETE of
     * BENCH055 leaves nothing selected, so DELETE and OPEN act on nothing
     * and the next deletion needs a row chosen in sight.
     */
    runs_card(150);
    next_page();
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    tap(400, BR_ROW_Y(BR_ROWS_SEEN - 1));
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "BENCH055.CSV");
    check_nothing_selected();
    CHECK_EQ(g_remove_calls, 1);
    CHECK(!g_run_gone[102]);
    /* The page is read again from BENCH102: 102 to 56, then 54.  The
     * scroll is the one before, 42, so the top row is the 43rd: 60. */
    check_tab(49, 96, 149);
    CHECK_EQ(top_row_run(), 60);

    /* The first page: the same, the scroll at its top. */
    runs_card(60);
    tap(400, BR_ROW_Y(2));
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[58]);
    check_nothing_selected();
    CHECK_EQ(g_remove_calls, 1);
    check_tab(1, 48, 59);
    CHECK_EQ(top_row_run(), 60);
}

TEST_CASE(rescan_on_page_three_reads_that_page_and_follows_a_shrunk_card)
{
    runs_card(150);
    next_page();
    next_page();
    check_tab(97, 144, 150);
    check_top(54);

    /* The same card read again: the same page, its first entry included. */
    tap(RESCAN_X, FOOT_CY);
    check_tab(97, 144, 150);
    check_top(54);
    tap(RESCAN_X, FOOT_CY);
    check_tab(97, 144, 150);
    check_top(54);

    /* Runs 1 to 60 gone behind the screen's back: the page starts where it
     * did, at run 54's place, and that is past the card's end now.  The
     * last page is shown, cut as the pages from the first cut it. */
    g_card_run0 = 61;
    g_card_runs = 90;
    tap(RESCAN_X, FOOT_CY);
    check_tab(49, 90, 90);
    check_top(102);

    /* A run recorded while page 1 shows is on it after RESCAN. */
    runs_card(60);
    tap(RESCAN_X, FOOT_CY);
    check_top(60);
    g_card_runs = 61;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 61);
    check_top(61);
}

TEST_CASE(prev_reads_the_first_page_from_the_top_whatever_changed_ahead)
{
    /* Page 2 of 100, and a run recorded meanwhile: 49 entries stand ahead
     * of the page now.  PREV shows the first page as the pages cut it,
     * the new run at its top, not the 48 just ahead of the page. */
    runs_card(100);
    next_page();
    check_tab(49, 96, 100);
    g_card_runs = 101;
    prev_page();
    check_tab(1, 48, 101);
    check_top(101);
    /* And the page after it is cut from the top as well. */
    next_page();
    check_tab(49, 96, 101);
    check_top(53);

    /* Everything ahead of page 2 gone behind the screen's back: PREV has
     * nothing before the key and shows the first page, not the last. */
    runs_card(100);
    next_page();
    for (int r = 53; r <= 100; ++r) {
        g_run_gone[r] = true;
    }
    prev_page();
    check_tab(1, 48, 52);
    check_top(52);

    /* Some of it gone: fewer than a page ahead, read from the top. */
    runs_card(100);
    next_page();
    for (int r = 60; r <= 100; ++r) {
        g_run_gone[r] = true;
    }
    prev_page();
    check_tab(1, 48, 59);
    check_top(59);
}

TEST_CASE(a_card_swapped_mid_page_opens_on_its_first_page)
{
    runs_card(150);
    next_page();
    check_tab(49, 96, 150);

    /* Another card's name in the header: its first page. */
    g_volume = "UNITEST";
    g_card_runs = 120;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 120);
    check_newest(120);

    /* Pulled, then put back: the read that finds no volume forgets the
     * page, and the next one starts at the first. */
    next_page();
    g_no_card = true;
    tap(RESCAN_X, FOOT_CY);
    g_no_card = false;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 120);

    /* Same name, other runs: the page's place in the new card's order,
     * with the tab's numbers the new card's. */
    next_page();
    g_card_run0 = 500;
    g_card_runs = 200;
    tap(RESCAN_X, FOOT_CY);
    /* The page started at run 72; every run of 500 to 699 ranks ahead of
     * it, so it is past the end and the last page shows. */
    check_tab(193, 200, 200);

    /* Swapped with no tap between, found by the RESCAN's own look: the
     * header named the old card until the list mounted the new one, and
     * the new one opens on its first page. */
    runs_card(150);
    next_page();
    check_tab(49, 96, 150);
    g_swap_volume = "SK64G";
    g_swap_runs = 120;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 120);
    check_newest(120);
    /* And PREV and NEXT go on from there. */
    next_page();
    check_tab(49, 96, 120);

    /* Swapped for a card of the same label: the mount number tells them
     * apart, at the RESCAN's own list and at the next one. */
    g_swap_volume = "SK64G";
    g_swap_runs = 150;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 150);
    next_page();
    ++g_mounts;                        /* mounted by another event */
    g_card_runs = 130;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 130);
}


/* ------------------------------------------------- folders and tabs --- */

/*
 * A card of folders, as panel_card.c serves it: folder() points the calls
 * that follow, list() walks that folder's .csv files and leaves out what
 * the logger still writes there (log_marks_busy()), remove() takes a
 * run's report with it (log_name_report_of()), written() counts the files
 * the logger created.
 */
#define FC_MAX 24

static struct {
    struct { char dir[CARD_DIR_MAX]; char name[LOG_RUN_NAME_MAX + 8];
             bool gone; } e[FC_MAX];
    int n;
    char dir[CARD_DIR_MAX];          /* where folder() pointed last  */
    char opened_in[CARD_DIR_MAX];    /* where the last open() looked */
    char removed_in[CARD_DIR_MAX];
    int folder_calls;
    log_marks_t marks;
    unsigned written;
    log_viewer_tab_t written_tab;
} g_fc;

static void fc_put(const char *dir, const char *name)
{
    if (g_fc.n < FC_MAX) {
        snprintf(g_fc.e[g_fc.n].dir, sizeof(g_fc.e[0].dir), "%s", dir);
        snprintf(g_fc.e[g_fc.n].name, sizeof(g_fc.e[0].name), "%s", name);
        g_fc.e[g_fc.n].gone = false;
        ++g_fc.n;
    }
}

static int fc_find(const char *dir, const char *name)
{
    for (int i = 0; i < g_fc.n; ++i) {
        if (!g_fc.e[i].gone && log_name_case_cmp(g_fc.e[i].dir, dir) == 0
            && log_name_case_cmp(g_fc.e[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void fc_folder(const char *dir, void *ctx)
{
    (void)ctx;
    ++g_fc.folder_calls;
    snprintf(g_fc.dir, sizeof(g_fc.dir), "%s", dir);
}

static int fc_list(log_viewer_file_t *out, int max_entries,
                   log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    log_select_page_t sel;
    log_select_page_begin(&sel, out, max_entries, page);
    int n = 0;
    for (int i = 0; i < g_fc.n; ++i) {
        const char *name = g_fc.e[i].name;
        const size_t len = strlen(name);
        if (g_fc.e[i].gone || strcmp(g_fc.e[i].dir, g_fc.dir) != 0
            || len < 4u || log_name_case_cmp(name + len - 4u, ".csv") != 0
            || log_marks_busy(&g_fc.marks, g_fc.dir, name)) {
            continue;
        }
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", name);
        f.size = 2048u;
        log_select_page_offer(&sel, &f);
        ++n;
    }
    (void)log_select_page_end(&sel, page);
    return n;
}

static bool fc_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    snprintf(g_fc.opened_in, sizeof(g_fc.opened_in), "%s", g_fc.dir);
    if (fc_find(g_fc.dir, name) < 0) {
        return false;
    }
    log_source_memory(src, &g_ctx, k_en, strlen(k_en));
    return true;
}

static bool fc_remove(const char *name, void *ctx)
{
    (void)ctx;
    snprintf(g_fc.removed_in, sizeof(g_fc.removed_in), "%s", g_fc.dir);
    if (log_marks_busy(&g_fc.marks, g_fc.dir, name)) {
        return false;
    }
    const int i = fc_find(g_fc.dir, name);
    if (i < 0) {
        return false;
    }
    g_fc.e[i].gone = true;
    char txt[LOG_RUN_NAME_MAX];
    log_name_report_of(txt, sizeof(txt), name);
    const int t = (txt[0] != '\0') ? fc_find(g_fc.dir, txt) : -1;
    if (t >= 0) {
        g_fc.e[t].gone = true;
    }
    return true;
}

static unsigned fc_written(log_viewer_tab_t *tab, void *ctx)
{
    (void)ctx;
    *tab = g_fc.written_tab;
    return g_fc.written;
}

static const log_viewer_io_t k_fc_io = {
    .list    = fc_list,
    .open    = fc_open,
    .close   = NULL,
    .volume  = fake_volume,
    .remove  = fc_remove,
    .folder  = fc_folder,
    .written = fc_written,
    .ctx     = NULL,
};

/* The tabs' middles, in the header row (BR_TABS in the screen).  The row
 * runs ROOT, USER, BENCH: the position of a tab id is one more than the id,
 * modulo 3 (USER 0, BENCH 1, ROOT 2). */
#define TAB_Y       21
#define TAB_POS(t)  (((t) + 1) % 3)
#define TAB_X(t)    (19 + TAB_POS(t) * 84 + 40)

/* A card in the target layout, behind the folder I/O, LOGS just opened. */
static void folder_card(void)
{
    fresh();
    memset(&g_fc, 0, sizeof(g_fc));
    fc_put(CARD_AUTO_LOGS, "MOTOR004.CSV");
    fc_put(CARD_AUTO_LOGS, "SERVO005.CSV");
    fc_put(CARD_AUTO_LOGS, "SUPPL003.CSV");
    fc_put(CARD_USER_LOGS, "ATEST005.CSV");
    fc_put(CARD_USER_LOGS, "ATEST005.TXT");
    fc_put(CARD_USER_LOGS, "LIMIT006.TXT");
    fc_put(CARD_USER_LOGS, "ATEST002.CSV");
    fc_put(CARD_ROOT, "BENCH041.CSV");          /* firmware 0.21.0 */
    fc_put(CARD_ROOT, "SERVO042.CSV");
    fc_put(CARD_ROOT, "SERVO042.TXT");
    fc_put(CARD_ROOT, "ESC040.CSV");
    fc_put(CARD_ROOT, "SWEEP_920KV.CSV");
    screen()->reset();
    log_viewer_set_io(&k_fc_io);
    screen()->enter();
}

/* Whether the folder the screen last pointed at lists @p name: the list
 * read again through the I/O, as the screen reads it. */
static bool listed_now(const char *name)
{
    log_viewer_file_t files[LOG_VIEWER_MAX_FILES];
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    const int n = fc_list(files, LOG_VIEWER_MAX_FILES, &q, NULL);
    for (int i = 0; i < n && i < q.held; ++i) {
        if (strcmp(files[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}


static void reopen(void);

/* The top row's name: opened, read, and back to the list. */
static void top_name(char *out, size_t n)
{
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    snprintf(out, n, "%s", log_viewer_open_name());
    tap(BACK_X, IM_BTN_CY);
}

TEST_CASE(three_tabs_list_three_folders_and_bench_comes_first)
{
    folder_card();
    char name[LOG_VIEWER_NAME_MAX];
    /* After a start, BENCH: the automatic logs. */
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_STR_EQ(g_fc.dir, CARD_AUTO_LOGS);
    check_tab(0, 0, 0);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "SERVO005.CSV");
    CHECK_STR_EQ(g_fc.opened_in, CARD_AUTO_LOGS);

    /* USER: the tests, newest first; a report is not listed. */
    tap(TAB_X(0), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST005.CSV");
    CHECK_STR_EQ(g_fc.opened_in, CARD_USER_LOGS);
    CHECK(!listed_now("ATEST005.TXT"));
    CHECK(!listed_now("LIMIT006.TXT"));

    /* ROOT: 0.21.0's names, ranked by number across the prefixes; a file
     * nobody numbered after them. */
    tap(TAB_X(2), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    CHECK_STR_EQ(g_fc.dir, CARD_ROOT);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "SERVO042.CSV");
    CHECK(listed_now("BENCH041.CSV"));
    CHECK(listed_now("ESC040.CSV"));
    CHECK(listed_now("SWEEP_920KV.CSV"));

    /* RESCAN reads the tab on screen; a tap on the tab shown reads
     * nothing new. */
    const int calls = g_fc.folder_calls;
    tap(RESCAN_X, FOOT_CY);
    CHECK_STR_EQ(g_fc.dir, CARD_ROOT);
    CHECK(g_fc.folder_calls > calls);
    tap(TAB_X(2), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);

    /* A slide off the tab is no tap on it. */
    send(TOUCH_EVENT_DOWN, TAB_X(1), TAB_Y);
    send(TOUCH_EVENT_UP, TAB_X(1), TAB_Y + 200);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    /* A cancelled press leaves the tab as it was. */
    send(TOUCH_EVENT_DOWN, TAB_X(1), TAB_Y);
    screen()->cancel();
    send(TOUCH_EVENT_UP, TAB_X(1), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    tap(TAB_X(1), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    draw();
}

/* The tabs stand ROOT, USER, BENCH from the left: a tap at each position
 * chooses that folder, and the tab ids and their folders are as they were. */
TEST_CASE(the_tabs_stand_root_user_bench_from_the_left)
{
    folder_card();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    /* The selected tab is the right-most one, BENCH, at a start. */
    draw();
    const gfx_color_t on = gfx_pixel_get(&s_c, 19 + 2 * 84 + 6, TAB_Y);
    CHECK(gfx_pixel_get(&s_c, 19 + 0 * 84 + 6, TAB_Y) != on);
    CHECK(gfx_pixel_get(&s_c, 19 + 1 * 84 + 6, TAB_Y) != on);

    tap(19 + 0 * 84 + 40, TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    CHECK_STR_EQ(g_fc.dir, CARD_ROOT);
    tap(19 + 1 * 84 + 40, TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);
    tap(19 + 2 * 84 + 40, TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_STR_EQ(g_fc.dir, CARD_AUTO_LOGS);

    /* Ids unchanged, and an id set from outside draws its own position. */
    CHECK_EQ((int)LOG_TAB_USER, 0);
    CHECK_EQ((int)LOG_TAB_BENCH, 1);
    CHECK_EQ((int)LOG_TAB_ROOT, 2);
    log_viewer_set_tab(LOG_TAB_ROOT);
    draw();
    const gfx_color_t on_root = gfx_pixel_get(&s_c, 19 + 6, TAB_Y);
    CHECK(gfx_pixel_get(&s_c, 19 + 84 + 6, TAB_Y) != on_root);
    CHECK(gfx_pixel_get(&s_c, 19 + 2 * 84 + 6, TAB_Y) != on_root);
}

/* The right-most column of the header row (y 2 to 40, right of the tabs)
 * that differs between a card named @p a and one named @p b, or -1. */
static int name_edge(const char *a, const char *b, int runs)
{
    static gfx_color_t *first;
    if (first == NULL) {
        first = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(first);
    }
    runs_card(runs);
    g_volume = a;
    log_viewer_refresh();
    draw();
    memcpy(first, s_fb, (size_t)W * H * sizeof(gfx_color_t));
    runs_card(runs);
    g_volume = b;
    log_viewer_refresh();
    draw();
    int edge = -1;
    for (int y = 2; y < 40; ++y) {
        for (int x = 270; x < W; ++x) {
            if (first[y * W + x] != s_fb[y * W + x]) {
                edge = (x > edge) ? x : edge;
            }
        }
    }
    return edge;
}

TEST_CASE(the_card_name_ends_the_header_row_and_stops_short_of_prev)
{
    /* The last letter differs: the difference is the name's right end.
     * Without PREV and NEXT it ends at the list's right edge, 784 px. */
    int edge = name_edge("UNITEST", "UNITESX", 3);
    CHECK(edge >= 784 - 8 && edge < 784);
    /* Exactly one page: still no PREV and NEXT. */
    edge = name_edge("UNITEST", "UNITESX", LOG_VIEWER_MAX_FILES);
    CHECK(edge >= 784 - 8 && edge < 784);
    /* With PREV and NEXT showing (more than 48 runs) it ends 8 px short
     * of PREV, which starts at 536 px. */
    edge = name_edge("UNITEST", "UNITESX", 150);
    CHECK(edge >= 528 - 8 && edge < 528);
    /* The longest label, 11 characters, and the longest folder share the
     * 248 px to PREV: the name's left end stays right of the folder's. */
    edge = name_edge("ABCDEFGHIJK", "ABCDEFGHIJX", 150);
    CHECK(edge >= 528 - 8 && edge < 528);
}

/*
 * With no card the tabs switch as with one: each tap selects its tab, the
 * tab is drawn selected and the others not, the page names that tab's
 * folder, and the empty page says there is no card.  The card is asked
 * again with each tab, so a card put in is found as RESCAN finds it.
 */
TEST_CASE(the_tabs_switch_with_no_card)
{
    fresh();
    g_no_card = true;
    log_viewer_refresh();
    for (int round = 0; round < 2; ++round) {
        for (int t = 0; t < (int)LOG_TAB_COUNT; ++t) {
            const int calls = g_list_calls;
            tap(TAB_X(t), TAB_Y);
            CHECK_EQ(log_viewer_tab(), (log_viewer_tab_t)t);
            CHECK_EQ(g_list_calls, calls + 1);
            CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
            draw();
            /* The selected tab's fill against the other two's, 6 px in
             * from each tab's left edge. */
            const gfx_color_t on = gfx_pixel_get(&s_c, 19 + TAB_POS(t) * 84 + 6,
                                                 TAB_Y);
            for (int o = 0; o < (int)LOG_TAB_COUNT; ++o) {
                if (o != t) {
                    CHECK(gfx_pixel_get(&s_c, 19 + TAB_POS(o) * 84 + 6,
                                        TAB_Y) != on);
                }
            }
        }
    }
    /* A card put in: the next tab lists it. */
    g_no_card = false;
    tap(TAB_X(1), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    draw();
}

/*
 * USER and BENCH list the folders SETUP > SD CARD names in force: a new
 * folder's files, and not the files left in the folder before.  Both
 * kinds in one folder list the same files.  A folder the check refuses
 * lists the default, as the logger writes there; lower case is upper.
 */
TEST_CASE(the_tabs_follow_the_folders_of_setup)
{
    folder_card();
    fc_put("RUNS", "SERVO012.CSV");
    fc_put("RUNS", "ATEST013.CSV");
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "RUNS");
    screen()->enter();                          /* LOGS opened again */
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_BENCH), "RUNS");
    CHECK_STR_EQ(g_fc.dir, "RUNS");
    CHECK(listed_now("SERVO012.CSV"));
    CHECK(!listed_now("MOTOR004.CSV"));         /* left in BENCH/LOGS */
    char name[LOG_VIEWER_NAME_MAX];
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST013.CSV");
    CHECK_STR_EQ(g_fc.opened_in, "RUNS");
    tap(TAB_X(0), TAB_Y);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);

    /* Both kinds in one folder: USER lists what BENCH lists. */
    settings_set_text(SET_TEXT_LOG_USER_DIR, "runs");
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_USER), "RUNS");
    tap(RESCAN_X, FOOT_CY);
    CHECK_STR_EQ(g_fc.dir, "RUNS");
    CHECK(listed_now("SERVO012.CSV"));
    CHECK(listed_now("ATEST013.CSV"));

    /* A folder the check refuses is the default. */
    settings_set_text(SET_TEXT_LOG_USER_DIR, "A/B/C");
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_USER), CARD_USER_LOGS);
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "BENCH/KST");
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_BENCH), CARD_AUTO_LOGS);
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_ROOT), CARD_ROOT);
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_COUNT), CARD_ROOT);
    /* The longest folder fits the header. */
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "ABCDEFGH/12345678");
    tap(TAB_X(1), TAB_Y);
    CHECK_STR_EQ(g_fc.dir, "ABCDEFGH/12345678");
    draw();
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, CARD_AUTO_LOGS);
    settings_set_text(SET_TEXT_LOG_USER_DIR, CARD_USER_LOGS);
}

TEST_CASE(delete_on_root_takes_the_report_and_stays_in_its_folder)
{
    folder_card();
    tap(TAB_X(2), TAB_Y);
    tap(DELETE_X, FOOT_CY);         /* SERVO042.CSV, the newest, chosen */
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_fc.removed_in, CARD_ROOT);
    CHECK(fc_find(CARD_ROOT, "SERVO042.CSV") < 0);
    CHECK(fc_find(CARD_ROOT, "SERVO042.TXT") < 0);
    CHECK(fc_find(CARD_ROOT, "BENCH041.CSV") >= 0);
    /* The tab and its folder stay. */
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    char name[LOG_VIEWER_NAME_MAX];
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "BENCH041.CSV");

    /* In USER, a test takes its report and leaves the limit run's. */
    tap(TAB_X(0), TAB_Y);
    tap(DELETE_X, FOOT_CY);         /* ATEST005.CSV, chosen */
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_fc.removed_in, CARD_USER_LOGS);
    CHECK(fc_find(CARD_USER_LOGS, "ATEST005.TXT") < 0);
    CHECK(fc_find(CARD_USER_LOGS, "LIMIT006.TXT") >= 0);
}

TEST_CASE(an_open_test_in_user_does_not_hide_the_same_number_in_bench)
{
    folder_card();
    g_fc.marks = (log_marks_t){ .run_dir = CARD_AUTO_LOGS, .run = 0u,
                                .test_dir = CARD_USER_LOGS, .test = 5u,
                                .txt = 0u };
    tap(RESCAN_X, FOOT_CY);
    char name[LOG_VIEWER_NAME_MAX];
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "SERVO005.CSV");      /* BENCH 005 is listed */
    tap(TAB_X(0), TAB_Y);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST002.CSV");      /* USER 005 is held back */
    /* And the open run in BENCH holds back BENCH 005 alone. */
    g_fc.marks = (log_marks_t){ .run_dir = CARD_AUTO_LOGS, .run = 5u,
                                .test_dir = CARD_USER_LOGS, .test = 0u,
                                .txt = 0u };
    tap(RESCAN_X, FOOT_CY);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST005.CSV");
    tap(TAB_X(1), TAB_Y);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "MOTOR004.CSV");
}

/* The empty list's two lines, as drawn: the reason at box.y + 52 and what
 * the folder holds at box.y + 76. */
static void empty_lines_drawn(gfx_color_t *out)
{
    draw();
    int k = 0;
    for (int y = 205; y < 250; ++y) {
        for (int x = 140; x < 660; ++x) {
            out[k++] = s_fb[y * W + x];
        }
    }
}

#define EMPTY_PX (45 * 520)

TEST_CASE(each_empty_folder_says_what_it_holds_and_what_is_off)
{
    static gfx_color_t a[EMPTY_PX], b[EMPTY_PX], c[EMPTY_PX], d[EMPTY_PX],
                       e[EMPTY_PX];
    folder_card();
    for (int i = 0; i < g_fc.n; ++i) {
        g_fc.e[i].gone = true;              /* every folder empty */
    }
    tap(RESCAN_X, FOOT_CY);
    empty_lines_drawn(a);                   /* BENCH */
    tap(TAB_X(0), TAB_Y);
    empty_lines_drawn(b);                   /* USER */
    tap(TAB_X(2), TAB_Y);
    empty_lines_drawn(c);                   /* ROOT */
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    CHECK(memcmp(b, c, sizeof(b)) != 0);
    CHECK(memcmp(a, c, sizeof(a)) != 0);

    /* AUTO LOGS off: BENCH says where to switch it on. */
    settings_set(SET_LOG_AUTO, 0.0f);
    tap(TAB_X(1), TAB_Y);
    empty_lines_drawn(d);
    CHECK(memcmp(a, d, sizeof(a)) != 0);
    /* USER LOGS off: USER likewise. */
    settings_set(SET_LOG_USER, 0.0f);
    tap(TAB_X(0), TAB_Y);
    empty_lines_drawn(e);
    CHECK(memcmp(b, e, sizeof(b)) != 0);
    CHECK(memcmp(d, e, sizeof(d)) != 0);
    settings_init();

    /* The words, through the same texts. */
    char line[96];
    snprintf(line, sizeof(line), TR(LOG_NO_CSV_IN), CARD_AUTO_LOGS);
    CHECK_STR_EQ(line, "No .csv files in BENCH/LOGS.");
}

TEST_CASE(logs_opens_on_the_tab_of_the_file_written_last)
{
    ui_router_init();
    folder_card();
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);       /* nothing written */

    /* A test written: LOGS opens on USER. */
    g_fc.written = 1u;
    g_fc.written_tab = LOG_TAB_USER;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);

    /* Nothing written since: the tab the operator chose stays. */
    tap(TAB_X(2), TAB_Y);
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);

    /* A run written: BENCH. */
    g_fc.written = 2u;
    g_fc.written_tab = LOG_TAB_BENCH;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);

    /* The count wraps: a change is a change. */
    g_fc.written = 0u;
    g_fc.written_tab = LOG_TAB_USER;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);

    /* A tab outside the three is not taken. */
    g_fc.written = 5u;
    g_fc.written_tab = LOG_TAB_COUNT;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    log_viewer_set_tab(LOG_TAB_COUNT);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
}

TEST_CASE(a_file_opened_in_one_tab_is_read_again_from_its_folder)
{
    ui_router_init();
    folder_card();
    ui_router_goto(SCREEN_LOGS);
    tap(TAB_X(0), TAB_Y);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));                   /* ATEST005.CSV, imported */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "ATEST005.CSV");
    /* Left with the import on screen; a run is written meanwhile, so LOGS
     * comes back on BENCH with the import still showing. */
    g_fc.written = 1u;
    g_fc.written_tab = LOG_TAB_BENCH;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    /* A forced separator reads the file again: from USER/LOGS. */
    tap(SEP_X, IM_BTN_CY);
    CHECK_STR_EQ(g_fc.opened_in, CARD_USER_LOGS);
    CHECK(log_viewer_analysis() != NULL);
}

/* ------------------------------------------------------ the way back in --- */

/* LOGS left for the menu and opened again from it, through the router. */
static void reopen(void)
{
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
}

/*
 * A run written while LOGS is closed is on the list the next time it opens,
 * without RESCAN: the first page, the new run at its top and chosen.  One
 * listing per way in, whichever page was on screen when it was left.
 */
TEST_CASE(a_run_written_while_logs_is_closed_is_listed_on_the_next_open)
{
    ui_router_init();
    runs_card(60);
    ui_router_goto(SCREEN_LOGS);
    check_tab(1, 48, 60);
    next_page();
    check_tab(49, 60, 60);

    g_card_runs = 61;
    int calls = g_list_calls;
    reopen();
    CHECK_EQ(g_list_calls, calls + 1);
    check_tab(1, 48, 61);
    check_top(61);

    /* Nothing new: the same first page, read once more. */
    calls = g_list_calls;
    reopen();
    CHECK_EQ(g_list_calls, calls + 1);
    check_tab(1, 48, 61);
    check_top(61);

    /* An empty card that gets its first run. */
    g_card_runs = 0;
    g_empty_card = true;
    calls = g_list_calls;
    reopen();
    CHECK_EQ(g_list_calls, calls + 1);
    g_empty_card = false;
    g_card_runs = 1;
    reopen();
    check_tab(0, 0, 0);
    check_top(1);

    /* No card when it was left, a card with runs when it opens. */
    g_no_card = true;
    reopen();
    g_no_card = false;
    g_card_runs = 49;
    reopen();
    check_tab(1, 48, 49);
    check_top(49);
}

/* After a DELETE, after a card swap and after leaving from a file's view or
 * from WI-FI, the next way in lists the run written meanwhile. */
TEST_CASE(the_way_back_in_lists_new_runs_after_delete_swap_and_sub_views)
{
    ui_router_init();
    runs_card(50);
    ui_router_goto(SCREEN_LOGS);

    /* DELETE of the newest run, chosen on the way in, leaves nothing
     * chosen; a new run is chosen on the next way in, the deleted number
     * gone from the list. */
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[50]);
    g_card_runs = 51;
    reopen();
    check_tab(1, 48, 50);
    check_top(51);

    /* Another card in the slot, on page 2 when LOGS was left. */
    next_page();
    g_volume = "UNITEST";
    memset(g_run_gone, 0, sizeof(g_run_gone));
    g_card_run0 = 300;
    g_card_runs = 97;
    reopen();
    check_tab(1, 48, 97);
    check_top(396);

    /* Left from a file's import view: the view is kept and the list
     * behind it is read again. */
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    g_card_runs = 98;
    reopen();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    check_tab(1, 48, 98);
    check_top(397);

    /* WI-FI opened from LOGS and closed by its home tag, which returns to
     * LOGS. */
    ui_router_goto(SCREEN_WIFI);
    g_card_runs = 99;
    ui_router_goto(ui_router_parent(SCREEN_WIFI));
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    check_tab(1, 48, 99);
    check_top(398);
}

/* ---------------------------------------------------------- scroll bar --- */


TEST_CASE(the_scroll_bar_jumps_drags_and_stays_on_its_page)
{
    /* 48 runs: rows are runs 48 to 1 top down, and 42 is the last scroll. */
    runs_card(48);
    /* The thumb is the rows shown against the page: 260 * 6 / 48 = 32 px,
     * drawn 2 px in from each end, at the top of the track. */
    const int th = BAR_H * 6 / 48;
    draw();
    CHECK_EQ(thumb_top(), BAR_TOP + 2);
    CHECK_EQ(thumb_len(), th - 4);
    /* A tap at the bottom of the track brings the thumb there. */
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    draw();
    CHECK_EQ(thumb_top(), BAR_TOP + BAR_H - th + 2);
    CHECK_EQ(top_row_run(), 6);
    /* And at the top, back. */
    tap(BAR_X, BAR_TOP + 1);
    CHECK_EQ(top_row_run(), 48);

    /* A tap off the thumb brings the thumb's middle to the finger: 200 px
     * down the track puts its top at 184 of 228 px of travel, scroll
     * (184 * 42 + 114) / 228 = 34, and the top row is run 48 - 34. */
    tap(BAR_X, BAR_TOP + 200);
    draw();
    CHECK_EQ(top_row_run(), 14);
    CHECK_EQ(thumb_top(), BAR_TOP + (BAR_H - th) * 34 / 42 + 2);
    tap(BAR_X, BAR_TOP + 1);

    /* Dragging the thumb (32 px at the top) by half the travel scrolls half
     * the page; past the track's end it stops at the end, and on a 49th
     * row there is nothing to scroll to. */
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + th / 2);
    send(TOUCH_EVENT_MOVE, BAR_X, BAR_TOP + th / 2 + (BAR_H - th) / 2);
    draw();
    send(TOUCH_EVENT_UP, BAR_X, BAR_TOP + th / 2 + (BAR_H - th) / 2);
    CHECK_EQ(top_row_run(), 27);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + BAR_H / 2);
    send(TOUCH_EVENT_MOVE, BAR_X, 2000);
    send(TOUCH_EVENT_UP, BAR_X, 2000);
    CHECK_EQ(top_row_run(), 6);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + BAR_H - 1);
    send(TOUCH_EVENT_MOVE, BAR_X, -2000);
    send(TOUCH_EVENT_UP, BAR_X, -2000);
    CHECK_EQ(top_row_run(), 48);

    /* A tap on the bar selects no row and opens nothing, and reads no
     * card. */
    const int calls = g_list_calls;
    tap(BAR_X, BAR_TOP + 100);
    tap(BAR_X, BAR_TOP + 100);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK_EQ(g_list_calls, calls);

    /* With every row showing there is nothing to scroll: the thumb is the
     * whole track, and a tap or a drag on it leaves the top row. */
    runs_card(4);
    draw();
    CHECK_EQ(thumb_len(), BAR_H - 4);
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    CHECK_EQ(top_row_run(), 4);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + 10);
    send(TOUCH_EVENT_MOVE, BAR_X, BAR_TOP + BAR_H);
    send(TOUCH_EVENT_UP, BAR_X, BAR_TOP + BAR_H);
    CHECK_EQ(top_row_run(), 4);

    /* The bar scrolls within the page: at the bottom of page 2 of 97 the
     * last row is the oldest run on that page, and NEXT does not move. */
    runs_card(97);
    next_page();
    tap(BAR_X, BAR_TOP + 1);
    CHECK_EQ(top_row_run(), 49);
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    CHECK_EQ(top_row_run(), 7);
    check_tab(49, 96, 97);

    /* A touch loss ends a drag: the next move belongs to no press. */
    runs_card(48);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + 1);
    screen()->cancel();
    send(TOUCH_EVENT_MOVE, BAR_X, BAR_TOP + BAR_H);
    send(TOUCH_EVENT_UP, BAR_X, BAR_TOP + BAR_H);
    CHECK_EQ(top_row_run(), 48);
}

TEST_CASE(the_header_names_the_card_or_says_sd_card)
{
    /* No name, and the header reads what it reads for a card without one:
     * SD CARD.  A name is drawn instead. */
    static gfx_color_t *named;
    if (named == NULL) {
        named = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(named);
    }
    fresh();
    g_volume = "SD CARD";
    log_viewer_refresh();
    draw();
    memcpy(named, s_fb, (size_t)W * H * sizeof(gfx_color_t));

    fresh();
    g_volume = "";
    log_viewer_refresh();
    draw();
    CHECK(memcmp(named, s_fb, (size_t)W * H * sizeof(gfx_color_t)) == 0);

    fresh();
    g_volume = "UNITEST";
    log_viewer_refresh();
    draw();
    CHECK(memcmp(named, s_fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
}

TEST_CASE(delete_asks_first_and_deletes_only_the_file_it_named)
{
    fresh();
    ask_to_delete(0);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* While the question is open, nothing behind it answers: not OPEN, not
     * RESCAN, not a row. */
    tap(OPEN_X, FOOT_CY);
    tap(RESCAN_X, FOOT_CY);
    tap(400, BR_ROW_Y(1));
    draw();
    CHECK_EQ(g_remove_calls, 0);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");

    /* The list is read again, so the first row is now the second file. */
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 2);
    CHECK_STR_EQ(g_removed_name, "PRUEFUNG.CSV");
}

TEST_CASE(cancel_deletes_nothing_and_keeps_the_selection)
{
    fresh();
    ask_to_delete(2);
    tap(DQ_CANCEL_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* Back on the list with the row still chosen: DELETE asks about it again
     * without a second tap on the row. */
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "README.CSV");
}

TEST_CASE(a_finger_that_slides_off_delete_deletes_nothing)
{
    fresh();
    ask_to_delete(0);
    send(TOUCH_EVENT_DOWN, DQ_DELETE_X, DQ_CY);
    send(TOUCH_EVENT_MOVE, DQ_DELETE_X, DQ_CY + 120);
    send(TOUCH_EVENT_UP, DQ_DELETE_X, DQ_CY + 120);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* And from CANCEL onto DELETE is neither: the question stays open. */
    send(TOUCH_EVENT_DOWN, DQ_CANCEL_X, DQ_CY);
    send(TOUCH_EVENT_UP, DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 0);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
}

TEST_CASE(only_the_finger_that_pressed_delete_can_release_it)
{
    /* The panel reports up to five contacts.  A second finger that lands
     * beside the button, slides onto it and lifts did not press DELETE. */
    fresh();
    ask_to_delete(0);
    send_id(TOUCH_EVENT_DOWN, 2, 400, 160);              /* B, off the buttons */
    send_id(TOUCH_EVENT_DOWN, 1, DQ_DELETE_X, DQ_CY);    /* A, on DELETE       */
    send_id(TOUCH_EVENT_MOVE, 2, DQ_DELETE_X, DQ_CY);
    send_id(TOUCH_EVENT_UP, 2, DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* Nor does a finger landing on CANCEL while A is down take the press. */
    send_id(TOUCH_EVENT_DOWN, 3, DQ_CANCEL_X, DQ_CY);
    send_id(TOUCH_EVENT_UP, 3, DQ_CANCEL_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 0);

    send_id(TOUCH_EVENT_UP, 1, DQ_DELETE_X, DQ_CY);      /* A lifts on DELETE  */
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");
}

TEST_CASE(a_touch_loss_drops_the_press_on_delete)
{
    /*
     * Lost events leave this screen's record of the glass stale.  The release
     * of the contact holding DELETE may be one of them, so the question has
     * to take the next fresh press rather than wait for a release that will
     * not come.
     */
    fresh();
    ask_to_delete(0);
    send_id(TOUCH_EVENT_DOWN, 1, DQ_DELETE_X, DQ_CY);
    log_viewer_screen()->cancel();                 /* its UP was lost      */
    send_id(TOUCH_EVENT_DOWN, 2, DQ_DELETE_X, DQ_CY);
    send_id(TOUCH_EVENT_UP, 2, DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 1);

    /* And a release of that contact that does arrive after the cancel is not
     * an answer. */
    fresh();
    ask_to_delete(0);
    send_id(TOUCH_EVENT_DOWN, 1, DQ_DELETE_X, DQ_CY);
    log_viewer_screen()->cancel();
    send_id(TOUCH_EVENT_UP, 1, DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 0);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
}

TEST_CASE(delete_needs_a_selected_file)
{
    fresh();
    tap(DELETE_X, FOOT_CY);            /* nothing selected */
    tap(DQ_DELETE_X, DQ_CY);           /* lands on the list, not a question */
    draw();
    CHECK_EQ(g_remove_calls, 0);

    g_dir_index = 0;
    log_viewer_refresh();
    ask_to_delete(0);                  /* a folder */
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);
}

TEST_CASE(without_remove_there_is_no_delete)
{
    fresh();
    log_viewer_io_t io = k_io;
    io.remove = NULL;
    log_viewer_set_io(&io);
    log_viewer_refresh();
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);
}

TEST_CASE(deleting_the_open_file_drops_what_was_read_from_it)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    CHECK(log_viewer_data() != NULL);
    tap(80, FOOT_CY);                  /* FIELDS */
    tap(BACK_X, IM_BTN_CY);            /* the list, row 0 still chosen */

    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");
    CHECK(log_viewer_data() == NULL);
    CHECK(log_viewer_analysis() == NULL);
    CHECK_STR_EQ(log_viewer_open_name(), "");
}

TEST_CASE(deleting_another_file_keeps_the_open_one)
{
    fresh();
    open_file(0);
    tap(BACK_X, IM_BTN_CY);
    ask_to_delete(1);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_removed_name, "PRUEFUNG.CSV");
    CHECK(log_viewer_analysis() != NULL);
    CHECK_STR_EQ(log_viewer_open_name(), "BENCH_01.CSV");
}

TEST_CASE(a_range_of_1000_or_more_drops_the_decimal)
{
    char out[40];
    log_viewer_range_text(out, sizeof(out), 2392.0, 14639.0, "rpm");
    CHECK_STR_EQ(out, "2392..14639 rpm");
    log_viewer_range_text(out, sizeof(out), 11.94, 12.61, "V");
    CHECK_STR_EQ(out, "11.9..12.6 V");
    /* The limit is 1000 in magnitude at either end, below zero too. */
    log_viewer_range_text(out, sizeof(out), 0.0, 999.9, "A");
    CHECK_STR_EQ(out, "0.0..999.9 A");
    log_viewer_range_text(out, sizeof(out), 0.0, 1000.0, "A");
    CHECK_STR_EQ(out, "0..1000 A");
    log_viewer_range_text(out, sizeof(out), -1000.0, 5.0, "g");
    CHECK_STR_EQ(out, "-1000..5 g");
    /* An end that one decimal rounds up to 1000.0 counts as 1000. */
    log_viewer_range_text(out, sizeof(out), 999.95, 999.95, "rpm");
    CHECK_STR_EQ(out, "1000..1000 rpm");
    log_viewer_range_text(out, sizeof(out), -999.96, 0.0, "g");
    CHECK_STR_EQ(out, "-1000..0 g");
    log_viewer_range_text(out, sizeof(out), 0.0, 999.94, "A");
    CHECK_STR_EQ(out, "0.0..999.9 A");
    /* Wider than the 17 cells of the box: no decimal, then no space. */
    log_viewer_range_text(out, sizeof(out), -999.9, -999.9, "rpm");
    CHECK_STR_EQ(out, "-1000..-1000 rpm");
    log_viewer_range_text(out, sizeof(out), -99.9, -99.9, "rpm");
    CHECK_STR_EQ(out, "-99.9..-99.9 rpm");
    log_viewer_range_text(out, sizeof(out), -14639.0, -14639.0, "rpm");
    CHECK_STR_EQ(out, "-14639..-14639rpm");
    CHECK(gfx_text_cells(out) == (int)LOG_RANGE_CELLS);
    /* Glyphs are counted, not bytes: "µs" is 2 cells in 3 bytes, and the
     * 17 cells of "-999.9..-999.9 µs" fit with the decimal. */
    log_viewer_range_text(out, sizeof(out), -999.9, -999.9, "\xC2\xB5s");
    CHECK_STR_EQ(out, "-999.9..-999.9 \xC2\xB5s");
    log_viewer_range_text(out, sizeof(out), -999.9, -999.9, "\xC2\xB5sx");
    CHECK_STR_EQ(out, "-1000..-1000 \xC2\xB5sx");
    /* Past that the box cuts it; the text stays whole and terminated. */
    log_viewer_range_text(out, sizeof(out), -1234567.0, -1234567.0, "rpm");
    CHECK_STR_EQ(out, "-1234567..-1234567rpm");
    /* No unit, no trailing space; a value that is not a number shows "--". */
    log_viewer_range_text(out, sizeof(out), 1.0, 2.0, "");
    CHECK_STR_EQ(out, "1.0..2.0");
    log_viewer_range_text(out, sizeof(out), NAN, 2.0, NULL);
    CHECK_STR_EQ(out, "--..2.0");
    /* A short buffer is cut and stays terminated. */
    char small[6];
    log_viewer_range_text(small, sizeof(small), 2392.0, 14639.0, "rpm");
    CHECK_STR_EQ(small, "2392.");
}

TEST_CASE(a_refused_delete_keeps_the_file_listed)
{
    fresh();
    g_remove_refuses = true;
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 1);

    /* Still listed and still chosen: DELETE asks about the same file. */
    g_remove_refuses = false;
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 2);
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");
}

TEST_CASE(a_rescan_or_leaving_the_screen_closes_the_question)
{
    /* A re-read can be of another card, where the same name is another
     * file. */
    fresh();
    ask_to_delete(0);
    log_viewer_refresh();
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    ask_to_delete(0);
    screen()->enter();
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);
}

TEST_CASE(the_question_leaves_no_stale_pixels)
{
    /* The question replaces the list; drawn over a frame of the list, it must
     * leave nothing of it behind, and the list drawn back must too. */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_t cb;

    fresh();
    tap(400, BR_ROW_Y(3));
    screen()->render(&s_c, 0);
    tap(DELETE_X, FOOT_CY);
    screen()->render(&s_c, 0);

    memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cb, s_fb_b, W, H, W);
    reset_screen();
    tap(400, BR_ROW_Y(3));
    tap(DELETE_X, FOOT_CY);
    screen()->render(&cb, 0);

    int count = 0;
    int at = first_diff(s_fb, s_fb_b, &count);
    if (at >= 0) {
        T_FAIL("question over list: %d stale pixel(s), first at (%d,%d)",
               count, at % W, at / W);
    }

    tap(DQ_CANCEL_X, DQ_CY);
    screen()->render(&s_c, 0);
    memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
    reset_screen();
    tap(400, BR_ROW_Y(3));
    screen()->render(&cb, 0);
    at = first_diff(s_fb, s_fb_b, &count);
    if (at >= 0) {
        T_FAIL("list over question: %d stale pixel(s), first at (%d,%d)",
               count, at % W, at / W);
    }
}


/* ------------------------------------------------------------ zoom ------ */

#define LONG_ROWS 400

/* Write LONG.CSV and open it in the plot: the first three fixture files are
 * taken off the card so it is the sixth row and needs no scrolling. */
static void long_file(void)
{
    fresh();
    size_t at = (size_t)snprintf(g_long, sizeof(g_long), "t,v\n");
    for (int i = 0; i < LONG_ROWS; ++i) {
        at += (size_t)snprintf(g_long + at, sizeof(g_long) - at, "%d,%d\n",
                               i, i % 50);
    }
    g_removed[0] = g_removed[1] = g_removed[2] = true;
    log_viewer_refresh();
    open_file(5);
    tap(PLOT_X, IM_BTN_CY);
    draw();
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

/* Two fingers down on the plot at x0 and x1, both moved to y0 = 200. */
static void two_down(int x0, int x1)
{
    send_id(TOUCH_EVENT_DOWN, 1, x0, 200);
    send_id(TOUCH_EVENT_DOWN, 2, x1, 200);
}

static void two_move(int x0, int x1)
{
    send_id(TOUCH_EVENT_MOVE, 1, x0, 200);
    send_id(TOUCH_EVENT_MOVE, 2, x1, 200);
}

static void two_up(int x0, int x1)
{
    send_id(TOUCH_EVENT_UP, 2, x1, 200);
    send_id(TOUCH_EVENT_UP, 1, x0, 200);
}

TEST_CASE(re_reading_the_card_selects_the_newest_run)
{
    fresh();
    g_card_runs = 30;          /* more than six rows: the newest scrolls in */
    log_viewer_refresh();
    tap(OPEN_X, FOOT_CY);
    draw();
    char want[LOG_VIEWER_NAME_MAX];
    log_run_name(want, sizeof(want), 30);
    CHECK_STR_EQ(log_viewer_open_name(), want);

    /* A card with no numbered run selects nothing: OPEN opens nothing. */
    fresh();
    log_viewer_refresh();
    tap(OPEN_X, FOOT_CY);
    CHECK_STR_EQ(log_viewer_open_name(), "");
}

TEST_CASE(two_fingers_spread_zoom_in_and_the_view_stays)
{
    long_file();
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->count, LONG_ROWS);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS);

    /* Spread from 100 px apart to 400 px apart around x = 400: a quarter
     * of the samples, centred where the fingers were. */
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    draw();
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    CHECK(first > 100 && first < 200);

    /* The view stays after the fingers lift, and the position bar shows. */
    int first2 = -1, count2 = -1;
    log_viewer_window(&first2, &count2);
    CHECK_EQ(first2, first);
    CHECK_EQ(count2, count);
}

TEST_CASE(two_fingers_moved_together_pan_and_pinching_zooms_out)
{
    long_file();
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    const int zoomed_first = first;

    /* Both fingers 200 px to the right, same distance: the trace follows
     * them, so earlier samples come into view. */
    two_down(300, 500);
    two_move(500, 700);
    two_up(500, 700);
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    CHECK(first < zoomed_first);

    /* Pinching far in returns the whole file, and never more. */
    two_down(100, 700);
    two_move(390, 410);
    two_up(390, 410);
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS);
    draw();
}

TEST_CASE(the_arrows_still_pick_values_and_carry_the_view)
{
    long_file();
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);

    /* A tap on the plot puts the cursor inside the zoomed view. */
    tap(PV_TAP_X, 200);
    const int c0 = log_viewer_cursor();
    CHECK(c0 >= first && c0 < first + count);

    /* Pressing > past the right edge moves the view along with the cursor. */
    for (int i = 0; i < count; ++i) {
        tap(NEXT_X, FOOT_PLOT_CY);
    }
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    const int c1 = log_viewer_cursor();
    CHECK_EQ(n2, count);
    CHECK(f2 > first);
    CHECK(c1 >= f2 && c1 < f2 + n2);
    draw();
}

TEST_CASE(a_third_finger_and_a_cancel_leave_the_view_alone)
{
    long_file();
    two_down(350, 450);
    send_id(TOUCH_EVENT_DOWN, 3, 600, 200);   /* ignored */
    send_id(TOUCH_EVENT_MOVE, 3, 700, 200);
    two_move(200, 600);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    screen()->cancel();
    /* After the cancel one finger moves the cursor, not the view. */
    send_id(TOUCH_EVENT_MOVE, 1, 600, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
}


/* Zoom to a quarter of LONG.CSV, then put the cursor at a known sample. */
static int zoom_in_with_cursor(void)
{
    long_file();
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    tap(PV_TAP_X, 200);
    return log_viewer_cursor();
}

/* One finger from x0 to x1 in steps of 25 px, then up. */
/* The plot's width in pixels, as log_viewer_screen.c draws it. */
#define PV_W_TEST 752

static void drag(int x0, int x1)
{
    send(TOUCH_EVENT_DOWN, x0, 200);
    const int dir = (x1 >= x0) ? 25 : -25;
    for (int x = x0 + dir; (dir > 0) ? x < x1 : x > x1; x += dir) {
        send(TOUCH_EVENT_MOVE, x, 200);
    }
    send(TOUCH_EVENT_MOVE, x1, 200);
    send(TOUCH_EVENT_UP, x1, 200);
}

TEST_CASE(one_finger_drag_moves_the_cursor_while_the_setting_is_off)
{
    const int c0 = zoom_in_with_cursor();
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);

    drag(600, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
    CHECK(log_viewer_cursor() < c0);   /* the cursor followed the finger */
    draw();
}

TEST_CASE(one_finger_drag_pans_a_zoomed_view_when_the_setting_is_on)
{
    const int c0 = zoom_in_with_cursor();
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    settings_set(SET_PLOT_PAN, 1.0f);

    /* A drag of 100 px left, less the 10 px threshold, moves the view
     * right by 90 px worth of samples; the cursor stays. */
    drag(600, 500);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(n2, count);
    const int want = first + (90 * count + PV_W_TEST / 2) / PV_W_TEST;
    CHECK(f2 >= want - 1 && f2 <= want + 1);
    CHECK_EQ(log_viewer_cursor(), c0);
    draw();

    /* Dragging right brings earlier samples back. */
    drag(300, 400);
    int f3 = -1, n3 = -1;
    log_viewer_window(&f3, &n3);
    CHECK(f3 < f2);
    CHECK_EQ(log_viewer_cursor(), c0);

    /* The view stops at both ends of the file. */
    for (int i = 0; i < 4; ++i) {
        drag(100, 780);
    }
    log_viewer_window(&f3, &n3);
    CHECK_EQ(f3, 0);
    for (int i = 0; i < 8; ++i) {
        drag(780, 24);
    }
    log_viewer_window(&f3, &n3);
    CHECK_EQ(f3 + n3, LONG_ROWS);
    CHECK_EQ(n3, count);
    draw();
}

TEST_CASE(one_move_past_the_threshold_pans_by_what_lies_past_it)
{
    zoom_in_with_cursor();
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    settings_set(SET_PLOT_PAN, 1.0f);
    /* Down, one move of 60 px, up: 50 px of pan. */
    send(TOUCH_EVENT_DOWN, 600, 200);
    send(TOUCH_EVENT_MOVE, 540, 200);
    send(TOUCH_EVENT_UP, 540, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    const int want = first + (50 * count + PV_W_TEST / 2) / PV_W_TEST;
    CHECK(f2 >= want - 1 && f2 <= want + 1);
    CHECK(f2 > first);
}

TEST_CASE(a_tap_still_selects_and_a_short_slide_does_not_pan_with_the_setting_on)
{
    zoom_in_with_cursor();
    settings_set(SET_PLOT_PAN, 1.0f);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);

    tap(PV_TAP_X, 200);
    const int c1 = log_viewer_cursor();
    CHECK(c1 >= first && c1 < first + count);

    /* 8 px of travel stays under the threshold: the cursor follows, the
     * view stays. */
    send(TOUCH_EVENT_DOWN, 500, 200);
    send(TOUCH_EVENT_MOVE, 504, 200);
    send(TOUCH_EVENT_MOVE, 508, 200);
    send(TOUCH_EVENT_UP, 508, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
    CHECK(log_viewer_cursor() >= first && log_viewer_cursor() < first + count);
    draw();
}

/* The cursor a tap at @p x gives in the view as it is. */
static int cursor_under(int x)
{
    tap(x, 200);
    return log_viewer_cursor();
}

/* A slide of exactly the 10 px threshold is still a tap that moved: the
 * cursor follows the finger and the view stays.  1 px more is a pan: the
 * cursor is back where it was before the press. */
TEST_CASE(a_slide_of_10_px_moves_the_cursor_and_11_px_pans)
{
    for (int dir = -1; dir <= 1; dir += 2) {
        zoom_in_with_cursor();
        settings_set(SET_PLOT_PAN, 1.0f);
        int first = -1, count = -1;
        log_viewer_window(&first, &count);
        const int at_end = cursor_under(500 + dir * 10);
        const int c0     = cursor_under(300);
        CHECK(at_end != c0);

        send(TOUCH_EVENT_DOWN, 500, 200);
        send(TOUCH_EVENT_MOVE, 500 + dir * 10, 200);
        CHECK_EQ(log_viewer_cursor(), at_end);
        send(TOUCH_EVENT_UP, 500 + dir * 10, 200);
        int f2 = -1, n2 = -1;
        log_viewer_window(&f2, &n2);
        CHECK_EQ(f2, first);
        CHECK_EQ(n2, count);
        CHECK_EQ(log_viewer_cursor(), at_end);

        (void)cursor_under(300);
        send(TOUCH_EVENT_DOWN, 500, 200);
        send(TOUCH_EVENT_MOVE, 500 + dir * 11, 200);
        CHECK_EQ(log_viewer_cursor(), c0);
        send(TOUCH_EVENT_UP, 500 + dir * 11, 200);
        CHECK_EQ(log_viewer_cursor(), c0);
    }
}

/* A pan that ends a fraction of a sample past the end of the file leaves
 * the view on the last samples: 1 to 9 px past the threshold, at 7.52 px a
 * sample, from a view that already ends on the last one. */
TEST_CASE(a_pan_a_fraction_of_a_sample_past_the_end_stops_at_the_end)
{
    zoom_in_with_cursor();
    settings_set(SET_PLOT_PAN, 1.0f);
    for (int i = 0; i < 8; ++i) {
        drag(780, 24);
    }
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    CHECK_EQ(first, LONG_ROWS - LONG_ROWS / 4);
    for (int px = 1; px <= 9; ++px) {
        send(TOUCH_EVENT_DOWN, 600, 200);
        send(TOUCH_EVENT_MOVE, 600 - 10 - px, 200);
        send(TOUCH_EVENT_UP, 600 - 10 - px, 200);
        int f2 = -1, n2 = -1;
        log_viewer_window(&f2, &n2);
        CHECK_EQ(f2, first);
        CHECK_EQ(n2, count);
    }
    /* And the same at the start. */
    for (int i = 0; i < 8; ++i) {
        drag(24, 780);
    }
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS / 4);
    for (int px = 1; px <= 9; ++px) {
        send(TOUCH_EVENT_DOWN, 300, 200);
        send(TOUCH_EVENT_MOVE, 300 + 10 + px, 200);
        send(TOUCH_EVENT_UP, 300 + 10 + px, 200);
        int f2 = -1, n2 = -1;
        log_viewer_window(&f2, &n2);
        CHECK_EQ(f2, 0);
        CHECK_EQ(n2, count);
    }
}

TEST_CASE(a_view_that_shows_the_whole_file_is_not_panned_by_one_finger)
{
    long_file();
    settings_set(SET_PLOT_PAN, 1.0f);
    CHECK_EQ(log_viewer_cursor(), LONG_ROWS - 1);
    drag(600, 200);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS);
    CHECK(log_viewer_cursor() < LONG_ROWS - 1);   /* it selected */
}

TEST_CASE(a_second_finger_after_a_pan_zooms_and_a_cancel_ends_the_pan)
{
    zoom_in_with_cursor();
    settings_set(SET_PLOT_PAN, 1.0f);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    send(TOUCH_EVENT_DOWN, 600, 200);
    send(TOUCH_EVENT_MOVE, 500, 200);
    send(TOUCH_EVENT_MOVE, 400, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK(f2 > first);

    send_id(TOUCH_EVENT_DOWN, 2, 300, 200);        /* the pinch takes over */
    send_id(TOUCH_EVENT_MOVE, 2, 460, 200);
    log_viewer_window(&f2, &n2);
    CHECK(n2 > count);                             /* zoomed out */
    send_id(TOUCH_EVENT_UP, 2, 460, 200);
    send_id(TOUCH_EVENT_UP, 1, 400, 200);

    /* A touch loss mid-pan leaves the view where it is. */
    send(TOUCH_EVENT_DOWN, 600, 200);
    send(TOUCH_EVENT_MOVE, 500, 200);
    send(TOUCH_EVENT_MOVE, 400, 200);
    log_viewer_window(&first, &count);
    screen()->cancel();
    send(TOUCH_EVENT_MOVE, 300, 200);
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
    draw();
}

TEST_CASE(a_steep_trace_longer_than_the_plot_is_one_line)
{
    /*
     * 1200 rows, more than the plot's 752 columns, of a steep sawtooth, with
     * the first 60 cells empty.  Each column's span has to reach its
     * neighbour's: drawn as min..max of its own samples alone, a steep
     * stretch came apart into dashes.  The empty start draws nothing, not a
     * line along the bottom.
     */
    fresh();
    size_t at = (size_t)snprintf(g_long, sizeof(g_long), "t,v\n");
    for (int i = 0; i < 1200; ++i) {
        char v[8] = "";
        if (i >= 60) {
            snprintf(v, sizeof(v), "%d", (i * 37) % 1000);
        }
        at += (size_t)snprintf(g_long + at, sizeof(g_long) - at, "%d,%s\n",
                               i, v);
    }
    g_removed[0] = g_removed[1] = g_removed[2] = true;
    log_viewer_refresh();
    open_file(5);
    tap(PLOT_X, IM_BTN_CY);
    /* Park the cursor at the start so its marker is not in the way. */
    tap(PV_X0, 200);
    draw();


    int top_prev = -1, bot_prev = -1, breaks = 0, cols = 0;
    const gfx_color_t col = log_viewer_series_color(0);
    for (int x = PV_X0 + 60; x < PV_X0 + PV_COLS - 4; ++x) {
        int top = -1, bot = -1;
        for (int yy = PV_TOP_Y; yy <= PV_BOTTOM_Y; ++yy) {
            if (gfx_pixel_get(&s_c, x, yy) == col) {
                if (top < 0) {
                    top = yy;
                }
                bot = yy;
            }
        }
        if (top < 0) {
            ++breaks;            /* a column with no trace at all */
        } else if (top_prev >= 0 &&
                   (top > bot_prev + 1 || bot < top_prev - 1)) {
            ++breaks;            /* does not reach its neighbour */
        }
        top_prev = top;
        bot_prev = bot;
        ++cols;
    }
    CHECK(cols > 600);
    CHECK_EQ(breaks, 0);

    /* The empty start: nothing along the bottom of the plot there. */
    int bottom = 0;
    for (int x = PV_X0 + 2; x < PV_X0 + 30; ++x) {
        bottom += (gfx_pixel_get(&s_c, x, PV_BOTTOM_Y) == col);
    }
    CHECK_EQ(bottom, 0);
}

/* ------------------------------------------------- taps, tracker-fed */

/*
 * The cases below put finger frames through the tracker (touch_feed.h) and
 * hand its events to the screen.  A button or a row is a tap: pressed and
 * released in place it acts, and a finger that leaves it, crosses it or
 * travels past 8 px does nothing.
 */
#define TAP_ID 3

static void f_at(int x, int y)    { finger(TAP_ID, x, y + UI_BAND_H); }
static void f_to(int x, int y, int step)
{
    glide(TAP_ID, x, y + UI_BAND_H, step);
}
static void f_lift(void)          { lift(TAP_ID); }

/* Everything the screen shows and what it has asked of the card, as one
 * number. */
static uint32_t look(void)
{
    uint32_t h = 2166136261u;
    log_viewer_invalidate();
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    screen()->render(&s_c, 0);
    for (int i = 0; i < W * H; ++i) {
        h = (h ^ (uint32_t)s_fb[i]) * 16777619u;
    }
    const uint32_t more[] = {
        (uint32_t)log_viewer_view(), (uint32_t)g_remove_calls,
        (uint32_t)g_list_calls, (uint32_t)log_viewer_cursor(),
    };
    for (size_t i = 0; i < sizeof(more) / sizeof(more[0]); ++i) {
        h = (h ^ more[i]) * 16777619u;
    }
    return h;
}

static void at_list(void)     { fresh(); tap(400, BR_ROW_Y(1)); }
static void at_import(void)   { fresh(); open_file(0); }
static void at_plot(void)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    tap(PV_TAP_X, 200);
}
static void at_question(void) { fresh(); ask_to_delete(0); }

typedef struct {
    const char *name;
    void (*page)(void);
    int x, y;
} control_t;

static const control_t k_controls[] = {
    { "RESCAN",          at_list,     RESCAN_X, FOOT_CY },
    { "OPEN",            at_list,     OPEN_X, FOOT_CY },
    { "DELETE",          at_list,     DELETE_X, FOOT_CY },
    { "file row",        at_list,     400, BR_ROW_Y(3) },
    { "separator",       at_import,   SEP_X, IM_BTN_CY },
    { "number format",   at_import,   NUM_X, IM_BTN_CY },
    { "BACK",            at_import,   BACK_X, IM_BTN_CY },
    { "PLOT",            at_import,   PLOT_X, IM_BTN_CY },
    { "column row",      at_import,   560, IM_ROW_Y(2) },
    { "FIELDS",          at_plot,     100, FOOT_PLOT_CY },
    { "previous sample", at_plot,     236, FOOT_PLOT_CY },
    { "next sample",     at_plot,     NEXT_X, FOOT_PLOT_CY },
    { "question CANCEL", at_question, DQ_CANCEL_X, DQ_CY },
    { "question DELETE", at_question, DQ_DELETE_X, DQ_CY },
};
#define CONTROLS ((int)(sizeof(k_controls) / sizeof(k_controls[0])))

/* The defect: the footers acted on the release wherever the finger was, so
 * a press on DELETE that slid away still asked the question. */
TEST_CASE(a_button_or_a_row_acts_on_a_tap_and_on_nothing_less)
{
    for (int i = 0; i < CONTROLS; ++i) {
        const control_t *c = &k_controls[i];
        c->page();
        feed_reset();
        feed_to_screen(screen());
        const uint32_t was = look();
        int moved = 0;

        /* Pressed, carried away down the glass or across it, and lifted. */
        f_at(c->x, c->y);
        f_to(c->x, c->y + 60, 9);
        f_lift();
        moved += (look() != was) ? 1 : 0;
        f_at(c->x, c->y);
        f_to(c->x - 110, c->y, 9);
        f_lift();
        moved += (look() != was) ? 2 : 0;
        /* A swipe across it from 110 px to its left. */
        f_at(c->x - 110, c->y);
        f_to(c->x + 110, c->y, 9);
        f_lift();
        moved += (look() != was) ? 4 : 0;
        /* Pressed, carried 60 px away and back onto it. */
        f_at(c->x, c->y);
        f_to(c->x + 60, c->y, 9);
        f_to(c->x, c->y, 9);
        f_lift();
        moved += (look() != was) ? 8 : 0;
        /* 9 px from the press, in one report and px by px. */
        f_at(c->x, c->y);
        f_to(c->x + 9, c->y, 9);
        f_lift();
        f_at(c->x, c->y);
        f_to(c->x - 9, c->y, 1);
        f_lift();
        moved += (look() != was) ? 16 : 0;
        /* The contact next reported 300 px away: the tracker's release. */
        f_at(c->x, c->y);
        f_at((c->x < 400) ? c->x + 300 : c->x - 300, 20);
        f_lift();
        moved += (look() != was) ? 32 : 0;
        if (moved != 0) {
            T_FAIL("%s acted without a tap (%d)", c->name, moved);
        }

        /* A tap acts; so does one that moves 8 px, or wobbles. */
        for (int k = 0; k < 3; ++k) {
            c->page();
            feed_reset();
            feed_to_screen(screen());
            const uint32_t before = look();
            f_at(c->x, c->y);
            if (k == 1) {
                f_to(c->x - 8, c->y, 8);
            } else if (k == 2) {
                f_to(c->x + 2, c->y - 3, 1);
                f_to(c->x, c->y, 1);
            }
            f_lift();
            if (look() == before) {
                T_FAIL("%s did not act on a tap (%d)", c->name, k);
            }
        }
    }
}

/* One contact holds a press on the list views; a second one presses
 * nothing and ends nothing. */
TEST_CASE(a_second_finger_presses_nothing_on_the_list_views)
{
    at_list();
    feed_reset();
    feed_to_screen(screen());
    /* The first rests on a row; the second taps OPEN and DELETE. */
    finger(1, 400, BR_ROW_Y(3) + UI_BAND_H);
    finger(2, OPEN_X, FOOT_CY + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    finger(2, DELETE_X, FOOT_CY + UI_BAND_H);
    lift(2);
    lift(1);                                  /* selects its row: README */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    feed_tap(2, DELETE_X, FOOT_CY + UI_BAND_H);
    feed_tap(2, DQ_DELETE_X, DQ_CY + UI_BAND_H);
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "MIXED.CSV");

    /* The first holds OPEN; the second's release ends nothing. */
    at_list();
    feed_reset();
    feed_to_screen(screen());
    finger(1, OPEN_X, FOOT_CY + UI_BAND_H);
    finger(2, 400, 200 + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    lift(1);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);

    /* On the import view the same. */
    finger(1, 560, IM_ROW_Y(2) + UI_BAND_H);
    finger(2, BACK_X, IM_BTN_CY + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    lift(1);
    feed_tap(2, BACK_X, IM_BTN_CY + UI_BAND_H);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);

    /* On the plot, a finger on a footer button keeps its press while
     * another comes and goes. */
    at_plot();
    feed_reset();
    feed_to_screen(screen());
    const int c0 = log_viewer_cursor();
    finger(1, NEXT_X, FOOT_PLOT_CY + UI_BAND_H);
    finger(2, 100, FOOT_PLOT_CY + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK_EQ(log_viewer_cursor(), c0);
    lift(1);
    CHECK_EQ(log_viewer_cursor(), c0 + 1);
}

/* A press held when the screen is left has no release coming; it holds
 * nothing when the screen is entered again.  The way in chooses the newest
 * run, which OPEN then opens. */
TEST_CASE(a_press_left_behind_holds_nothing_on_the_way_back_in)
{
    runs_card(3);
    feed_reset();
    feed_to_screen(screen());
    finger(1, OPEN_X, FOOT_CY + UI_BAND_H);
    screen()->enter();
    feed_tap(2, OPEN_X, FOOT_CY + UI_BAND_H);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "BENCH003.CSV");
}

/*
 * The router's alert band covers the bottom UI_ALERT_H px of the panel
 * while an alert shows, and takes the touches there.  Nothing of the log
 * viewer that acts lies under it: with no alert showing, on each view and
 * on the DELETE question, a tap changes nothing and goes nowhere on every
 * 8th px of the band's top row, middle row and bottom row (body y 398, 415
 * and 431).  A control that reaches into the band from above crosses the
 * top row; one inside it at least 8 px wide and 17 px high crosses a row
 * and a column of the grid.  1,500 taps, each followed by a full frame:
 * a finer grid pushes gfx.c's fill loop past the 2^32 hits gcovr accepts.
 */
static void at_nocard(void)
{
    fresh();
    g_no_card = true;
    log_viewer_refresh();
}

TEST_CASE(nothing_that_acts_is_under_the_alert_band)
{
    static const struct {
        const char *name;
        void (*page)(void);
    } pages[] = {
        { "file list", at_list },     { "no card", at_nocard },
        { "import", at_import },      { "plot", at_plot },
        { "question", at_question },
    };
    ui_router_init();
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        pages[i].page();
        const uint32_t was = look();
        const uint32_t nav = ui_router_navigations();
        const int top = UI_ALERT_Y - UI_BAND_H;
        const int rows[] = { top, top + UI_ALERT_H / 2, top + UI_ALERT_H - 1 };
        int acted_at = -1;
        for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]) && acted_at < 0;
             ++r) {
            const int y = rows[r];
            for (int x = 0; x < W; x += 8) {
                tap(x, y);
                if (ui_router_navigations() != nav || look() != was) {
                    acted_at = y;
                    break;
                }
            }
        }
        if (acted_at >= 0) {
            T_FAIL("%s: a tap at body y %d acted", pages[i].name, acted_at);
        }
    }
}

/*
 * The import's warnings break into lines of DETECTED's 268 px, 33 cells,
 * and its file messages into the footer's 248 px between WI-FI and DELETE:
 * ui_text.def declares two of those lines, 66 cells, for each warning, and
 * this holds each to the two lines as the screen breaks it, the longest
 * count and file name included.
 */
TEST_CASE(every_warning_and_file_message_takes_two_lines_at_most)
{
    static const ui_text_id_t k_warn[] = {
        TX_LOG_MIXED, TX_LOG_ROWS_DIFFER, TX_LOG_LAST_CUT, TX_LOG_UNSETTLED,
    };
    static const ui_text_id_t k_file[] = {
        TX_LOG_CANNOT_OPEN, TX_LOG_IS_FOLDER, TX_LOG_CANNOT_DELETE,
        TX_LOG_DELETED, TX_LOG_TOO_BIG,
    };
    const gfx_rect_t warn = gfx_rect_make(32, 0, 268, 18);
    const gfx_rect_t foot = gfx_rect_make(272, 0, 248, 18);
    char line[96];
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(k_warn) / sizeof(k_warn[0]); ++i) {
            CHECK(ui_text_wrap(NULL, warn, 18, ui_tr(k_warn[i]),
                               UI_FONT_LABEL, UI_TEXT) <= 2);
        }
        snprintf(line, sizeof(line), ui_tr(TX_LOG_TOO_LONG), 2147483647);
        CHECK(ui_text_wrap(NULL, warn, 18, line, UI_FONT_LABEL, UI_TEXT)
              <= 2);
        for (size_t i = 0; i < sizeof(k_file) / sizeof(k_file[0]); ++i) {
            /* A run's name, as the logger writes it. */
            snprintf(line, sizeof(line), ui_tr(k_file[i]), "BENCH999.CSV");
            CHECK(ui_text_wrap(NULL, foot, 18, line, UI_FONT_LABEL, UI_TEXT)
                  <= 2);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}


/*
 * The home tag climbs one level, as each view's own way back: BACK from
 * the plot to the import, BACK from the import to the file list, CANCEL
 * over the DELETE question.  The view after back() looks as the view after the
 * tap.  On the file list back() climbs nothing: the router leaves the
 * screen.
 */
TEST_CASE(the_home_tag_climbs_as_each_views_way_back)
{
    static const control_t k_ways[] = {
        { "plot",     at_plot,     100, FOOT_PLOT_CY },
        { "import",   at_import,   BACK_X, IM_BTN_CY },
        { "question", at_question, DQ_CANCEL_X, DQ_CY },
    };
    for (size_t i = 0; i < sizeof(k_ways) / sizeof(k_ways[0]); ++i) {
        const control_t *w = &k_ways[i];
        g_list_calls = 0;                   /* each build lists the card */
        w->page();
        const uint32_t before = look();
        tap(w->x, w->y);
        const uint32_t want = look();
        if (want == before) {
            T_FAIL("%s: the way back changed nothing", w->name);
        }
        g_list_calls = 0;
        w->page();
        if (!screen()->back()) {
            T_FAIL("%s: the home tag left the screen", w->name);
        }
        if (look() != want) {
            T_FAIL("%s: the home tag did not do what the way back does",
                   w->name);
        }
    }
    CHECK_EQ(g_remove_calls, 0);

    at_list();
    const uint32_t was = look();
    CHECK(!screen()->back());
    CHECK_EQ(look(), was);

    /* Through the router: plot, import, list, then the menu. */
    ui_router_init();
    at_plot();
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    const log_viewer_view_t k_climb[] = { LOG_VIEW_IMPORT, LOG_VIEW_BROWSE };
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_LOGS));
    for (size_t i = 0; i < 2; ++i) {
        touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 4, .x = (int16_t)(home.x + 20),
                                       .y = (int16_t)(home.y + 10) } };
        ui_router_event(&e);
        e.type = TOUCH_EVENT_UP;
        ui_router_event(&e);
        CHECK_EQ(ui_router_current(), SCREEN_LOGS);
        CHECK_EQ(log_viewer_view(), k_climb[i]);
    }
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 4, .x = (int16_t)(home.x + 20),
                                   .y = (int16_t)(home.y + 10) } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

int main(void)
{
    RUN(the_home_tag_climbs_as_each_views_way_back);
    RUN(every_view_draws_something);
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
    RUN(plotting_loads_exactly_the_picked_columns);
    RUN(tapping_a_column_toggles_it_and_the_time_axis_is_not_offered);
    RUN(an_override_does_not_discard_the_column_selection);
    RUN(a_directory_row_cannot_be_opened_as_a_log);
    RUN(a_file_of_2_gib_or_more_is_listed_and_refused_at_open);
    RUN(a_size_is_written_in_its_unit_up_to_an_sdxc_card);
    RUN(a_column_of_prose_is_not_plottable);
    RUN(forcing_the_separator_and_the_convention_re_runs_the_analysis);
    RUN(the_cursor_follows_the_touch_and_stays_in_range);
    RUN(fields_returns_to_the_picker_and_keeps_the_selection);
    RUN(back_from_the_picker_returns_to_the_file_list);
    RUN(redraw_leaves_no_stale_pixels);
    RUN(both_framebuffers_follow_an_interaction);
    RUN(a_run_name_and_its_number_are_one_rule);
    RUN(the_newest_run_outranks_the_rest_of_the_card);
    RUN(a_card_of_999_runs_offers_the_newest_that_fit);
    RUN(a_directory_sorts_first_from_either_side_of_the_comparison);
    RUN(a_full_list_keeps_runs_over_what_it_cannot_date);
    RUN(a_list_that_was_cut_says_so);
    RUN(a_count_above_the_list_does_not_reach_past_it);
    RUN(pages_of_a_card_cover_it_once_in_rank_order_both_ways);
    RUN(pages_of_mixed_writers_run_newest_first_across_the_names);
    RUN(runs_that_share_a_number_split_by_a_page_edge_are_each_on_one_page);
    RUN(a_page_key_whose_file_has_gone_still_names_its_place);
    RUN(a_card_of_one_page_or_none_shows_no_page_buttons);
    RUN(forty_nine_runs_are_two_pages_and_the_second_holds_the_oldest);
    RUN(ninety_six_and_ninety_seven_runs_page_at_48);
    RUN(a_full_card_is_reached_to_its_oldest_run_a_walk_a_page);
    RUN(files_the_bench_did_not_write_follow_the_runs_and_are_counted);
    RUN(deleting_the_last_pages_only_file_shows_the_page_before);
    RUN(after_delete_nothing_is_selected_and_the_scroll_stays);
    RUN(rescan_on_page_three_reads_that_page_and_follows_a_shrunk_card);
    RUN(prev_reads_the_first_page_from_the_top_whatever_changed_ahead);
    RUN(a_card_swapped_mid_page_opens_on_its_first_page);
    RUN(a_run_written_while_logs_is_closed_is_listed_on_the_next_open);
    RUN(the_way_back_in_lists_new_runs_after_delete_swap_and_sub_views);
    RUN(the_scroll_bar_jumps_drags_and_stays_on_its_page);
    RUN(the_header_names_the_card_or_says_sd_card);
    RUN(three_tabs_list_three_folders_and_bench_comes_first);
    RUN(the_tabs_stand_root_user_bench_from_the_left);
    RUN(the_card_name_ends_the_header_row_and_stops_short_of_prev);
    RUN(the_tabs_switch_with_no_card);
    RUN(the_tabs_follow_the_folders_of_setup);
    RUN(delete_on_root_takes_the_report_and_stays_in_its_folder);
    RUN(an_open_test_in_user_does_not_hide_the_same_number_in_bench);
    RUN(each_empty_folder_says_what_it_holds_and_what_is_off);
    RUN(logs_opens_on_the_tab_of_the_file_written_last);
    RUN(a_file_opened_in_one_tab_is_read_again_from_its_folder);
    RUN(delete_asks_first_and_deletes_only_the_file_it_named);
    RUN(cancel_deletes_nothing_and_keeps_the_selection);
    RUN(a_finger_that_slides_off_delete_deletes_nothing);
    RUN(only_the_finger_that_pressed_delete_can_release_it);
    RUN(a_touch_loss_drops_the_press_on_delete);
    RUN(delete_needs_a_selected_file);
    RUN(re_reading_the_card_selects_the_newest_run);
    RUN(a_cut_last_row_is_left_out_and_said);
    RUN(a_cut_last_row_is_said_beside_a_convention_conflict);
    RUN(two_fingers_spread_zoom_in_and_the_view_stays);
    RUN(two_fingers_moved_together_pan_and_pinching_zooms_out);
    RUN(the_arrows_still_pick_values_and_carry_the_view);
    RUN(a_third_finger_and_a_cancel_leave_the_view_alone);
    RUN(one_finger_drag_moves_the_cursor_while_the_setting_is_off);
    RUN(one_finger_drag_pans_a_zoomed_view_when_the_setting_is_on);
    RUN(a_tap_still_selects_and_a_short_slide_does_not_pan_with_the_setting_on);
    RUN(one_move_past_the_threshold_pans_by_what_lies_past_it);
    RUN(a_slide_of_10_px_moves_the_cursor_and_11_px_pans);
    RUN(a_pan_a_fraction_of_a_sample_past_the_end_stops_at_the_end);
    RUN(a_view_that_shows_the_whole_file_is_not_panned_by_one_finger);
    RUN(a_second_finger_after_a_pan_zooms_and_a_cancel_ends_the_pan);
    RUN(a_steep_trace_longer_than_the_plot_is_one_line);
    RUN(without_remove_there_is_no_delete);
    RUN(deleting_the_open_file_drops_what_was_read_from_it);
    RUN(deleting_another_file_keeps_the_open_one);
    RUN(a_refused_delete_keeps_the_file_listed);
    RUN(a_range_of_1000_or_more_drops_the_decimal);
    RUN(a_rescan_or_leaving_the_screen_closes_the_question);
    RUN(the_question_leaves_no_stale_pixels);
    RUN(a_button_or_a_row_acts_on_a_tap_and_on_nothing_less);
    RUN(a_second_finger_presses_nothing_on_the_list_views);
    RUN(a_press_left_behind_holds_nothing_on_the_way_back_in);
    RUN(nothing_that_acts_is_under_the_alert_band);
    RUN(every_warning_and_file_message_takes_two_lines_at_most);
    return test_summary("logview");
}
