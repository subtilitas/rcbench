/**
 * @file logview_rig.h
 * @brief The log viewer screen as the logview suites drive it: a card in memory
 * behind the screen's I/O (input/output) vtable, the screen on one canvas,
 * touch helpers, the places of the controls, and the page, folder and
 * scroll bar fakes the cases share.
 * test_logview_*.c.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_logview.c
 * - test_logview_plot.c
 * - test_logview_rank.c
 * - test_logview_pages.c
 * - test_logview_tabs.c
 * - test_logview_delete.c
 * - test_logview_touch.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "greatest.h"

#include <math.h>
#include <stdlib.h>

#include "card_tree.h"
#include "log_name.h"
#include "log_select.h"
#include "log_take.h"
#include "log_viewer_screen.h"
#include "logview_spots.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "touch_feed.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

/* --------------------------------------------------------- the fake card -- */

static const char k_en[] __attribute__((unused)) =
    "time,voltage,current,rpm,temp\n"
    "0.000,22.34V,10.23A,1234,25\n"
    "0.020,22.31V,11.05A,1567,25\n"
    "0.040,22.28V,12.40A,1890,26\n"
    "0.060,22.20V,15.02A,2345,26\n";

static const char k_de[] __attribute__((unused)) =
    "Zeit;Spannung;Strom;Drehzahl;Temperatur\n"
    "0,000;22,34V;10,23A;1.234;25\n"
    "0,020;22,31V;11,05A;1.567;25\n"
    "0,040;22,28V;12,40A;1.890;26\n"
    "0,060;22,20V;15,02A;2.345;26\n";

static const char k_prose[] __attribute__((unused)) = "note\nnothing numeric here\nnor here\n";

/*
 * Both conventions proven in one file: the screen reports the conflict rather
 * than picking a winner.
 *
 * Semicolon-delimited, so that "10,23" is one cell.  With a comma delimiter it
 * splits into two fields and the fixture holds no comma-decimal cell at all.
 */
static const char k_mixed[] __attribute__((unused)) = "t;a;b\n0;10.23;1\n1;10,23;2\n2;11.5;3\n";

/* Ragged for its own sake, so the two conditions can be told apart. */
static const char k_ragged[] __attribute__((unused)) = "t,a,b\n0,1,2\n1,2\n2,3,4\n";

/* No time column, and a column that is mostly but not entirely numeric. */
static const char k_plain[] __attribute__((unused)) =
    "left,right\n5,1.0\n3,2.0\n9,n/a\n4,4.0\n8,5.0\n7,6.0\n";

/* 400 rows of a ramp, written by long_file() before a zoom case opens it. */
static char g_long[16384] __attribute__((unused));

static const struct {
    const char *name;
    const char *text;
} k_card[] __attribute__((unused)) = {
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

static bool g_no_card __attribute__((unused));
static bool g_empty_card __attribute__((unused));
/* The card's name in the header; another name is another card. */
static const char *g_volume __attribute__((unused)) = "BENCH01";
/* A card the next listing finds in the slot and mounts, as card_list()'s
 * look does on the panel: its name, its runs; NULL for none. */
static const char *g_swap_volume __attribute__((unused));
static int g_swap_runs __attribute__((unused));
/* The mount number the io reports; a swap counts it up. */
static unsigned g_mounts __attribute__((unused));

/*
 * A card holding more runs than the list has room for, or 0 for the fixture
 * card above.  The lister writes the newest that fit, in the order the browse
 * list draws them, and returns what the card holds -- what card_list() does
 * on the panel with storage_walk()'s count.
 */
static int g_card_runs __attribute__((unused));
/* Beside those runs, this many files the bench did not write, OTHERnnn.CSV,
 * and the first run number, so a card can hold runs 61 to 120 only. */
static int g_card_others __attribute__((unused));
static int g_card_run0 __attribute__((unused)) = 1;
/* Runs deleted from that card, by number. */
static bool g_run_gone[LOG_RUN_LAST + 1] __attribute__((unused));
/* What the last listing asked for, and how many listings there were. */
static log_viewer_page_t g_last_page __attribute__((unused));

/* Which listed entry is a subdirectory, or -1 for none.  A card full of
 * directories is not the normal case, and every other case here wants to open
 * the file it selected. */
static int g_dir_index __attribute__((unused)) = -1;
static log_mem_ctx_t g_ctx __attribute__((unused));
/* A size the fixture card lists for one entry in place of its own, and the
 * entry, or -1 for none: an exFAT card holds files of 4GiB and more. */
static int g_big_index __attribute__((unused)) = -1;
static uint64_t g_big_size __attribute__((unused));
static int g_open_calls __attribute__((unused));        /* how often open() was asked */

/* What DELETE has done to the fixture card: which entries are gone, how many
 * times remove() was asked, the last name it was asked for, and whether it
 * refuses -- the panel's refuses the run the logger still has open. */
static bool g_removed[16] __attribute__((unused));
static int g_remove_calls __attribute__((unused));
static char g_removed_name[LOG_VIEWER_NAME_MAX] __attribute__((unused));
static bool g_remove_refuses __attribute__((unused));
static int g_list_calls __attribute__((unused));        /* how often the card was listed */

/**
 * @brief The runs card as the panel reads it: every entry offered to the page
 * selection in directory order, which is creation order on FAT -- the others
 * first, then the runs oldest first -- as card_list() does.
 */
static inline int runs_list(log_viewer_file_t *out, int max_entries,
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

/** @brief List hook over the fake card: fills the entries of the page asked for and counts the call. */
static inline int fake_list(log_viewer_file_t *out, int max_entries,
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

/** @brief Open hook: opens the named fake file as an in-memory source and counts the call; a name off the card, a removed file and a file with no text are refused. */
static inline bool fake_open(const char *name, log_source_t *src, void *ctx)
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

/** @brief Volume hook: the fake card's label, empty with no card in. */
static inline const char *fake_volume(void *ctx)
{
    (void)ctx;
    return g_no_card ? "" : g_volume;
}

/** @brief Remove hook: records the name removed and refuses it while g_remove_refuses is set. */
static inline bool fake_remove(const char *name, void *ctx)
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
static card_state_t g_card_state __attribute__((unused));
static unsigned g_card_asks __attribute__((unused));

/** @brief Card-state hook: the state the case set, counting each ask. */
static inline card_state_t fake_card(void *ctx)
{
    (void)ctx;
    ++g_card_asks;
    return g_card_state;
}

/* A card another task mounts after the listing read the old one: its
 * name and runs, NULL for none. */
static const char *g_swap_late_volume __attribute__((unused));
static int g_swap_late_runs __attribute__((unused));

/** @brief List hook that lists as fake_list() does, then swaps the card for the late one. */
static inline int fake_list_late(log_viewer_file_t *out, int max_entries,
                          log_viewer_page_t *page, void *ctx)
{
    const int n = fake_list(out, max_entries, page, ctx);
    if (g_swap_late_volume != NULL) {
        ++g_mounts;
        g_volume = g_swap_late_volume;
        g_card_runs = g_swap_late_runs;
        g_swap_late_volume = NULL;
    }
    return n;
}

/** @brief Mount-count hook: how often the fake card was mounted. */
static inline unsigned fake_mounts(void *ctx)
{
    (void)ctx;
    return g_mounts;
}

static const log_viewer_io_t k_io __attribute__((unused)) = {
    .list = fake_list_late,
    .open = fake_open,
    .close = NULL,
    .volume = fake_volume,
    .remove = fake_remove,
    .card = fake_card,
    .mounts = fake_mounts,
    .ctx = NULL,
};

/* ------------------------------------------------------------- harness --- */

static gfx_color_t *s_fb __attribute__((unused));
static gfx_color_t *s_fb_b __attribute__((unused));
static gfx_canvas_t s_c __attribute__((unused));

/** @brief The log viewer screen under test. */
static inline const ui_screen_t *screen(void)
{
    return log_viewer_screen();
}

/** @brief Put the screen back to its start state without touching any framebuffer --
 * the stale-pixel case needs to replay the same interaction into a second
 * buffer, and wiping the first one would compare a render against a blank. */
static inline void reset_screen(void)
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
    g_swap_late_volume = NULL;
    g_mounts = 0u;
    memset(g_removed, 0, sizeof(g_removed));
    g_remove_calls = 0;
    g_removed_name[0] = '\0';
    g_remove_refuses = false;
    screen()->reset();
    log_viewer_set_io(&k_io);
    screen()->enter();
}

/** @brief Clears the canvas, the fake card's counters and the screen: the browse view of the default card. */
static inline void fresh(void)
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

/** @brief Hands the screen one touch event of contact @p id at (@p x, @p y). */
static inline void send_id(int type, int id, int x, int y)
{
    touch_event_t e = {
        .type = (touch_event_type_t)type,
        .point = { .id = (uint8_t)id, .x = (int16_t)x, .y = (int16_t)y },
    };
    screen()->event(&e);
}

/** @brief Hands the screen one touch event of contact 1 at (@p x, @p y). */
static inline void send(int type, int x, int y)
{
    send_id(type, 1, x, y);
}

/** @brief Presses and releases contact 1 at (@p x, @p y). */
static inline void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
}

/** @brief Draw the current state.  Called after every interaction in the cases below,
 * because a screen that decides correctly and then draws nothing is still
 * broken -- and because a crash in a rarely-reached panel should surface
 * here rather than on the bench. */
static inline void draw(void)
{
    screen()->render(&s_c, 0);
}

/* Geometry mirrored from the screen (logview_spots.h); if it moves, these
 * move with it.  The screen's own coordinates: the cases hand it events
 * directly, under the band. */
#define BR_ROW_Y(i)  LV_ROW_CY(i)
/* BR_ROWS: the file rows the list shows at once. */
#define BR_ROWS_SEEN LV_ROWS
#define IM_ROW_Y(i)  LV_IM_ROW_CY(i)
#define IM_BTN_CY    LV_FOOT_CY
#define SEP_X        LV_IM_BTN_CX(0)
#define NUM_X        LV_IM_BTN_CX(1)
#define BACK_X       LV_IM_BTN_CX(2)
#define PLOT_X       LV_IM_BTN_CX(3)
#define FOOT_CY      LV_FOOT_CY
#define OPEN_X       LV_OPEN_CX
#define DELETE_X     LV_DELETE_CX
#define RESCAN_X     LV_RESCAN_CX
/* The plot view: a point inside the plot, and the > button's centre. */
#define PV_TAP_X     LV_PV_CX
#define NEXT_X       LV_PV_NEXT_CX
#define FOOT_PLOT_CY LV_FOOT_CY
/* The DELETE question's two buttons. */
#define DQ_CY        LV_DQ_CY
#define DQ_CANCEL_X  LV_DQ_CANCEL_CX
#define DQ_DELETE_X  LV_DQ_DELETE_CX

/** @brief Selects row @p index of the file list with one tap and opens it with a second. */
static inline void open_file(int index)
{
    tap(400, BR_ROW_Y(index)); /* select */
    tap(400, BR_ROW_Y(index)); /* open   */
}

/** @brief Index of the first pixel at which two frames differ, -1 when equal; @p count receives the number that differ. */
static inline int first_diff(const gfx_color_t *a, const gfx_color_t *b, int *count)
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

/* ------------------------------------------------- what a full card shows -- */

/*
 * The accent tab on the browse panel's top edge is the title's width plus
 * 26px, and its top row is the one row the chamfer does not cut, so the run
 * of accent pixels along it measures the title.  Cheaper than reading pixels
 * back as letters, and it fails when the title stops saying what it should.
 */
#define BR_TAB_X LV_LIST_X
#define BR_TAB_Y LV_LIST_Y

/** @brief Width in px of the active folder tab, read off the canvas. */
static inline int browse_tab_width(void)
{
    int w = 0;
    while (BR_TAB_X + w < W &&
           gfx_pixel_get(&s_c, BR_TAB_X + w, BR_TAB_Y) == UI_ACCENT) {
        ++w;
    }
    return w;
}

/** @brief Width in px a tab with @p title draws at. */
static inline int tab_width_for(const char *title)
{
    return gfx_text_width(UI_FONT_LABEL, title, 1) + 26;
}

/* ------------------------------------------------------------- deleting -- */

/** @brief Select a row and ask to delete it: the question, not the deletion. */
static inline void ask_to_delete(int row)
{
    tap(400, BR_ROW_Y(row));
    tap(DELETE_X, FOOT_CY);
}
#define NEXT_X_PG LV_NEXT_CX
#define PG_CY     LV_HEAD_CY

/** @brief Taps NEXT. */
static inline void next_page(void)
{
    tap(NEXT_X_PG, PG_CY);
}

/** @brief The tab reads "FILES a TO b OF n", or FILES with @p n 0: the text the
 * list draws, compared as text, and its width on the drawn frame. */
static inline void check_tab(int a, int b, int n)
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

/** @brief The run OPEN opens, which is the newest run on the page: the page's
 * identity, read without a getter.  Back to the list afterwards. */
static inline void check_newest(int run)
{
    tap(OPEN_X, FOOT_CY);
    char want[LOG_RUN_NAME_MAX];
    log_run_name(want, sizeof(want), run);
    CHECK_STR_EQ(log_viewer_open_name(), want);
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
}

/** @brief Open the top row; its run number says where the list is scrolled. */
static inline int top_row_run(void)
{
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    const int run = log_run_number(log_viewer_open_name());
    tap(BACK_X, IM_BTN_CY);
    return run;
}

/** @brief The page is drawn newest at the top: row 0 is the page's newest run, the
 * one OPEN opens. */
static inline void check_top(int run)
{
    check_newest(run);
    CHECK_EQ(top_row_run(), run);
}

/** @brief Resets the screen to a card of @p runs runs and reads its first page. */
static inline void runs_card(int runs)
{
    fresh();
    g_card_runs = runs;
    log_viewer_refresh();
}
