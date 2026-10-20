/*
 * The log viewer: browse the card, review the import, plot the file.
 *
 * Three views behind one tile: a file browser, an import view that shows
 * what the CSV (comma-separated values) reader made of the file, and the
 * plot.  The reader detects the delimiter, the decimal convention and the
 * time column (see shared/logfile); the import view shows the detection and
 * its evidence before anything is plotted, and offers overrides.
 *
 * compute_spans() turns the chosen columns into scaled traces when the data
 * changes, so the plot render walks prepared spans rather than reparsing on
 * every frame.
 *
 * DELETE in the browse view asks before it acts: a second panel names the
 * file and its size, and only its own DELETE, pressed and released on that
 * button, calls the I/O's remove().
 *
 * The browse view lists one folder at a time, chosen by three tabs in its
 * header row: ROOT, USER and BENCH, in that order (log_viewer_tab_t holds
 * the ids; the tabs' order on screen is k_tab_at).
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_viewer_screen.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "card_tree.h"
#include "log_name.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_card_text.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"

#define SCREEN_W 800
/* The router owns the band and hands this screen a sub-canvas of what is
 * left, so the screen's own height is the body rather than the panel. */
#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/* ------------------------------------------------------------- browse ---- */

/*
 * Layout in the 432 px body the router hands this screen (480 minus the
 * 48 px band).  Each view's footer row of buttons ends 4 px above the
 * router's alert band, which covers the bottom UI_ALERT_H (34) px of the
 * body while an alert shows and takes the touches there: nothing that acts
 * sits under it.  The footer is body y 352 to 393; the band starts at 398.
 */
#define FOOT_H    42
#define FOOT_Y    (SCREEN_H - UI_ALERT_H - 4 - FOOT_H)
/* What a view shows above the footer ends this far above it. */
#define FOOT_GAP  6

/* The list starts under the header row, which holds the card's name and,
 * when the card holds more than one page, PREV and NEXT. */
#define BR_LIST_Y 44
#define BR_LIST   gfx_rect_make(16, BR_LIST_Y, 768, FOOT_Y - FOOT_GAP - BR_LIST_Y)
#define BR_ROW_H  44
/* (302 - 30) / 44 = 6 rows fit the list.  A seventh needs 338 px and would
 * cross the panel border into the footer. */
#define BR_ROWS   6

/*
 * The scroll bar along the list's right edge, BR_BAR_W px wide: 8.1 mm on
 * the panel.  The Waveshare ESP32-S3-Touch-LCD-7 shows 800 x 480 px on a
 * 154.88 x 86.72 mm viewing area (CNX Software's listing of the board,
 * https://www.cnx-software.com/2024/05/30/7-inch-esp32-s3-touchscreen-display-rs485-can-bus-i2c-uart-analog-sensor-interfaces/;
 * Waveshare's pages give no active area), so a pixel is
 * 154.88 / 800 = 0.1936 mm across and 42 px is 8.13 mm.  The track runs the
 * height of the rows, and the thumb's share of it is the rows' share of the
 * page.
 */
#define BR_BAR_W  42
#define BR_BAR    gfx_rect_make(16 + 768 - 8 - BR_BAR_W, BR_LIST_Y + 30, \
                                BR_BAR_W, BR_ROWS * BR_ROW_H - 4)
/* A row runs from the list's left inset to 8 px short of the bar. */
#define BR_ROW_X  (16 + 8)
#define BR_ROW_W  (16 + 768 - 8 - BR_BAR_W - 8 - BR_ROW_X)

/* PREV and NEXT, over DELETE and OPEN in the header row. */
#define BR_PG_Y   2
#define BR_PG_H   38
/*
 * ROOT, USER and BENCH at the header row's left, 80 px a tab: ui_tabs draws
 * its trough 3 px round the tabs, so the trough is the header row, 2 to
 * 40 px, as PREV and NEXT are.  The folder follows from BR_HEAD_X and the
 * card's name ends the row, right-aligned: at BR_HEAD_END with PREV and NEXT
 * showing, at the list panel's right edge without them.  The two share
 * BR_HEAD_X to BR_HEAD_END, 248 px, 31 cells of the 8 px face: an
 * 11-character volume label, 2 spaces and a 17-character folder.
 */
#define BR_TABS   gfx_rect_make(19, BR_PG_Y + 3, 3 * 80 + 2 * 4, BR_PG_H - 6)
#define BR_HEAD_X 280
#define BR_HEAD_END (536 - 8)
#define BR_HEAD_FAR (16 + 768)
#define BR_PREV   gfx_rect_make(536, BR_PG_Y, 120, BR_PG_H)
#define BR_NEXT   gfx_rect_make(664, BR_PG_Y, 120, BR_PG_H)

/* Footer buttons, in the order browse_event() hit-tests them. */
#define BR_RESCAN gfx_rect_make(16, FOOT_Y, 120, FOOT_H)
#define BR_WIFI   gfx_rect_make(144, FOOT_Y, 120, FOOT_H)
#define BR_OPEN   gfx_rect_make(664, FOOT_Y, 120, FOOT_H)
#define BR_DELETE gfx_rect_make(536, FOOT_Y, 120, FOOT_H)
/* Two lines of 32 cells to DELETE: the 28 a message about a file declares,
 * and the file's name. */
#define BR_MSG_X  272

/* The DELETE question, drawn in place of the list. */
#define DQ_BOX    gfx_rect_make(120, 110, 560, 220)
#define DQ_CANCEL gfx_rect_make(144, 258, 240, 48)
#define DQ_DELETE gfx_rect_make(416, 258, 240, 48)

/* ------------------------------------------------------------- import ---- */

#define IM_LEFT   gfx_rect_make(16, 36, 300, FOOT_Y - FOOT_GAP - 36)
#define IM_RIGHT  gfx_rect_make(328, 36, 456, FOOT_Y - FOOT_GAP - 36)
#define IM_ROW_H  36
/* (310 - 30) / 36 = 7 rows fit the column list. */
#define IM_ROWS   7
#define IM_BTN_Y  FOOT_Y
#define IM_BTN_H  FOOT_H

/* --------------------------------------------------------------- plot ---- */

/* The plot's panel, and under it a 16 px row of time labels 4 px down,
 * above the footer. */
#define PV_PANEL_H (FOOT_Y - FOOT_GAP - 16 - 4 - 68)
#define PV_PANEL  gfx_rect_make(16, 68, 768, PV_PANEL_H)
#define PV_X      24
#define PV_Y      76
#define PV_W      752
#define PV_H      (PV_PANEL_H - 16)
#define PV_BAND   10
#define PV_DIV_X  6
#define PV_DIV_Y  4
/* The bar under the plot that shows where a zoomed view sits. */
#define PV_POS_Y  (PV_Y + PV_H + 2)
#define PV_POS_H  4
/* The narrowest window two fingers can zoom to, in samples. */
#define PV_MIN_WIN 8
/* With SET_PLOT_PAN on, a finger that moves further than this many pixels
 * from where it touched down pans the zoomed view instead of selecting. */
#define PV_PAN_SLOP 10
/* A pixel column with no number in it: drawn as nothing. */
#define PV_EMPTY   (-1)

#define CHIP_Y    26
#define CHIP_H    36
#define CHIP_X    8
#define CHIP_W    190
#define CHIP_GAP  6

_Static_assert(FOOT_Y + FOOT_H + 4 <= SCREEN_H - UI_ALERT_H,
               "the footer ends 4 px above the alert band");
_Static_assert(30 + BR_ROWS * BR_ROW_H <= FOOT_Y - FOOT_GAP - BR_LIST_Y,
               "the file list's rows fit its panel");
_Static_assert(BR_PG_Y + BR_PG_H + 4 <= BR_LIST_Y,
               "PREV and NEXT end above the list");
_Static_assert(19 + 3 * 80 + 2 * 4 + 3 + 8 <= BR_HEAD_X,
               "the tabs end before the header's text");
_Static_assert(BR_HEAD_X + 31 * 8 <= BR_HEAD_END + 8,
               "the header's text ends at PREV");
_Static_assert((BR_ROWS * BR_ROW_H - 4) * BR_ROWS / LOG_VIEWER_MAX_FILES >= 32,
               "the scroll bar's thumb is 32 px or more on a full page");
_Static_assert(30 + IM_ROWS * IM_ROW_H <= FOOT_Y - FOOT_GAP - 36,
               "the column list's rows fit its panel");
_Static_assert(PV_POS_Y + PV_POS_H <= 68 + PV_PANEL_H,
               "the position bar is inside the plot's panel");

typedef enum { VIEW_BROWSE = 0, VIEW_IMPORT, VIEW_PLOT } view_t;

static const ui_color_id_t k_series_color[LOG_MAX_SERIES] = {
    UI_C_VOLT, UI_C_CURR, UI_C_POWER, UI_C_RPM,
};

static struct {
    view_t view;
    log_viewer_io_t io;

    /* The folder the browse view lists, and its three tabs. */
    log_viewer_tab_t tab;
    ui_tabs_t tabs;
    /* The tab of the file s.name names: the plot and the import read it
     * again from there whatever the tabs show since. */
    log_viewer_tab_t open_tab;
    /* The logger's count of files created, as this screen last saw it. */
    unsigned written_seen;

    log_viewer_file_t files[LOG_VIEWER_MAX_FILES];
    int n_files;  /* rows on the page, never above LOG_VIEWER_MAX_FILES */
    int n_card;   /* what the volume holds; above n_files when paged */
    int first;    /* entries ahead of the page, in log_name_rank() order */
    int listed;   /* -1 no volume, 0 not read, 1 read     */
    card_state_t card;  /* what the slot holds while listed is -1     */
    int sel;      /* highlighted file                     */
    int scroll;
    /* The card's name when the page was read: another name on RESCAN is
     * another card, and it opens on its first page. */
    char volume[LOG_VIEWER_NAME_MAX];
    unsigned mounts;    /* the io's mount number when the page was read */
    unsigned open_mounts;   /* and when the open file's page was read */

    /* A finger on the scroll bar: the thumb follows it, held where it took
     * the thumb, grab px below the thumb's top. */
    bool bar_drag;
    int bar_grab;

    char name[LOG_VIEWER_NAME_MAX];
    char message[80];
    bool message_ok;  /* a result to report, not a failure */

    /*
     * The file DELETE asked about, or "" while no question is open.  A copy
     * of the name rather than an index into the list: the question has to
     * delete the file it named, whatever happens to the list behind it.
     */
    char doomed[LOG_VIEWER_NAME_MAX];
    uint64_t doomed_size;
    /* The one contact whose press on the question counts: the panel reports
     * up to five, and a second finger lifting off DELETE did not press it. */
    bool q_down;
    uint8_t q_id;

    log_analysis_t an;
    bool have_analysis;
    int delim_index;      /* -1 auto, else index into log_csv_delimiters */
    log_conv_t conv;      /* LOG_CONV_AUTO respects the file            */
    int picked[LOG_MAX_SERIES];
    int n_picked;
    int col_scroll;

    log_data_t data;
    bool have_data;
    int cursor;

    /*
     * The samples the plot shows: [win0, win0 + win_n).  The whole file
     * until two fingers zoom in, and it stays where they leave it.
     */
    int win0;
    int win_n;

    /*
     * Two fingers on the plot: spreading or pinching zooms, moving both
     * together pans.  The sample under the fingers' midpoint at the start
     * stays under the midpoint as they move.
     */
    bool pinch;
    uint8_t pinch_id[2];
    int16_t pinch_x[2];
    float pinch_d0;      /* distance at the start, px          */
    float pinch_s0;      /* sample under the start midpoint    */
    float pinch_n0;      /* window width at the start, samples */

    /*
     * One finger panning a zoomed view (SET_PLOT_PAN).  Until the finger has
     * moved PV_PAN_SLOP px it is a tap: the cursor follows it.  Past that it
     * pans, the cursor goes back to where it was before the touch, and the
     * sample under the finger stays under it.
     */
    bool panning;
    int16_t pan_x0;      /* finger x where the pan began       */
    int pan_win0;        /* window start where the pan began   */
    int pan_cursor0;     /* cursor before the touch            */

    /* press tracking, shared by the list views */
    bool pressing;
    int press_row;
    int press_y0;
    int press_scroll0;
    bool dragged;
    int press_btn;
    uint8_t press_id;    /* the finger that holds the plot cursor */
    int16_t press_x;

    /* The contact that holds a press on a button or a row, where it came
     * down, and whether it has been further than the slop from there: its
     * release is asked whether it is a tap (tap_on()). */
    bool tap_have;
    bool tap_moved;
    uint8_t tap_id;
    int16_t tap_x0, tap_y0;

    bool valid[MAX_FBS];
} s;

/* Column spans, in screen y, one pair per pixel column per series.  Computed
 * when the data changes: recomputing them per redraw would make the cursor
 * lag, and recomputing them per band would make it crawl. */
static int16_t s_span[LOG_MAX_SERIES][PV_W][2];
static gfx_color_t s_band[PV_W * PV_BAND];
static float s_lo[LOG_MAX_SERIES];
static float s_hi[LOG_MAX_SERIES];

log_viewer_view_t log_viewer_view(void)
{
    return (log_viewer_view_t)s.view;
}

const char *log_viewer_open_name(void)
{
    return s.name;
}

const log_analysis_t *log_viewer_analysis(void)
{
    return s.have_analysis ? &s.an : NULL;
}

const log_data_t *log_viewer_data(void)
{
    return s.have_data ? &s.data : NULL;
}

void log_viewer_window(int *first, int *count)
{
    const bool on = s.have_data && s.data.count > 0;
    if (first != NULL) {
        *first = on ? s.win0 : 0;
    }
    if (count != NULL) {
        *count = on ? s.win_n : 0;
    }
}

gfx_color_t log_viewer_series_color(int k)
{
    if (k < 0 || k >= LOG_MAX_SERIES) {
        return 0;
    }
    return ui_theme_color(k_series_color[k]);
}

void log_viewer_range_text(char *out, size_t n, double lo, double hi,
                           const char *unit)
{
    if (!out || n == 0) {
        return;
    }
    /* Decided on the floats ui_fmt() prints: from 999.95 one decimal rounds
     * up to 1000.0, which is as wide as 1000 plus 2 glyphs. */
    const float lo_f = (float)lo;
    const float hi_f = (float)hi;
    const bool has_unit = unit && unit[0];
    /* One decimal below 1000, then the widths decide: a text wider than the
     * box drops the decimal, and then the space before the unit. */
    int decimals = (fabsf(lo_f) >= 999.95f || fabsf(hi_f) >= 999.95f) ? 0 : 1;
    bool space = has_unit;
    for (;;) {
        char a[16];
        char b[16];
        ui_fmt(a, sizeof(a), lo_f, decimals);
        ui_fmt(b, sizeof(b), hi_f, decimals);
        snprintf(out, n, "%s..%s%s%s", a, b, space ? " " : "",
                 has_unit ? unit : "");
        if (gfx_text_cells(out) <= (int)LOG_RANGE_CELLS) {
            return;
        }
        if (decimals > 0) {
            decimals = 0;
        } else if (space) {
            space = false;
        } else {
            return;
        }
    }
}

int log_viewer_cursor(void)
{
    return s.have_data ? s.cursor : -1;
}

const char *log_viewer_message(void)
{
    return s.message;
}

void log_viewer_invalidate(void)
{
    memset(s.valid, 0, sizeof(s.valid));
}

void log_viewer_set_io(const log_viewer_io_t *io)
{
    if (io != NULL) {
        s.io = *io;
    } else {
        memset(&s.io, 0, sizeof(s.io));
    }
    s.listed = 0;
    log_viewer_invalidate();
}

const char *log_viewer_tab_dir(log_viewer_tab_t tab)
{
    static char dirs[2][CARD_DIR_MAX];
    switch (tab) {
    case LOG_TAB_USER:
        return card_folder_use(settings_text(SET_TEXT_LOG_USER_DIR),
                               CARD_USER_LOGS, dirs[0], sizeof(dirs[0]));
    case LOG_TAB_BENCH:
        return card_folder_use(settings_text(SET_TEXT_LOG_AUTO_DIR),
                               CARD_AUTO_LOGS, dirs[1], sizeof(dirs[1]));
    case LOG_TAB_ROOT:
    default:
        return CARD_ROOT;
    }
}

log_viewer_tab_t log_viewer_tab(void)
{
    return s.tab;
}

/* Point the I/O at @p tab's folder, for the call that follows. */
static void point(log_viewer_tab_t tab)
{
    if (s.io.folder != NULL) {
        s.io.folder(log_viewer_tab_dir(tab), s.io.ctx);
    }
}

/* The card's name as the I/O reports it now, or "". */
static const char *volume_now(void)
{
    const char *vol = (s.io.volume != NULL) ? s.io.volume(s.io.ctx) : NULL;
    return (vol != NULL) ? vol : "";
}

/* The I/O's mount number now, 0 without one. */
static unsigned mounts_now(void)
{
    return (s.io.mounts != NULL) ? s.io.mounts(s.io.ctx) : 0u;
}

/* Whether the volume is another than the one the page was read from. */
static bool volume_changed(void)
{
    return strcmp(s.volume, volume_now()) != 0 || s.mounts != mounts_now();
}

/*
 * The entry on the page that log_name_rank() puts first (@p sign 1) or last
 * (-1), or "" with an empty page: the edges PREV, NEXT and RESCAN name.
 */
static const char *page_edge(int sign)
{
    int best = -1;
    for (int i = 0; i < s.n_files; ++i) {
        if (best < 0
            || sign * log_name_rank(s.files[i].name, s.files[best].name) < 0) {
            best = i;
        }
    }
    return (best >= 0) ? s.files[best].name : "";
}

/*
 * One walk of the directory for the page @p dir and @p key name, of at most
 * @p cap entries.
 */
static int list_once(log_page_dir_t dir, const char *key, int cap)
{
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = dir;
    snprintf(q.key, sizeof(q.key), "%s", (key != NULL) ? key : "");
    point(s.tab);
    const int n = s.io.list(s.files, cap, &q, s.io.ctx);
    if (n < 0) {
        return n;
    }
    /*
     * The count is what the volume holds, which can be more than was
     * written: a card takes up to 999 runs and this screen holds a page of
     * LOG_VIEWER_MAX_FILES of them.  Rows are clamped to what is in the
     * array -- every list index below runs off the end otherwise -- and the
     * place to what the count leaves room for, so the tab never names a row
     * past the card's last.
     */
    int held = q.held;
    if (held > n) {
        held = n;
    }
    if (held > cap) {
        held = cap;
    }
    if (held < 0) {
        held = 0;
    }
    int ahead = q.ahead;
    if (ahead > n - held) {
        ahead = n - held;
    }
    if (ahead < 0) {
        ahead = 0;
    }
    s.n_card = n;
    s.n_files = held;
    s.first = ahead;
    return n;
}

/*
 * Read the page @p dir and @p key name, and select its newest run.
 *
 * Pages are cut from the top, LOG_VIEWER_MAX_FILES at a time.  A page
 * before the key that starts less than a page from the top is the first
 * page, read again from the top: a run recorded since the key was read
 * stands ahead of it, and so does nothing when everything ahead of the key
 * has gone.  A page that comes back empty from a card that is not, is a
 * page past the end: the card has fewer entries than when the key was
 * read, after a DELETE or behind the screen's back.  The last page is read
 * instead, as the pages from the first one cut it: what is left of the
 * count after whole pages.
 */
static void read_page(log_page_dir_t dir, const char *key)
{
    /* A re-read can be of another card, and a name on it is another file. */
    s.doomed[0] = '\0';
    s.n_files = 0;
    s.n_card = 0;
    s.first = 0;
    s.sel = -1;
    s.scroll = 0;
    s.bar_drag = false;
    /* Copied: @p key may point into the array the listing overwrites. */
    char k[LOG_VIEWER_NAME_MAX];
    snprintf(k, sizeof(k), "%s", (key != NULL) ? key : "");
    snprintf(s.volume, sizeof(s.volume), "%s", volume_now());
    s.mounts = mounts_now();
    if (s.io.list == NULL) {
        s.listed = -1;
        s.card = CARD_STATE_NONE;
        log_viewer_invalidate();
        return;
    }
    int n = list_once(dir, k, LOG_VIEWER_MAX_FILES);
    /* The list looks at the slot, and it or another task can mount
     * another card: what was read may be the old one's, so the new one's
     * top is read again. */
    if (volume_changed()) {
        snprintf(s.volume, sizeof(s.volume), "%s", volume_now());
        s.mounts = mounts_now();
        if (n >= 0) {
            dir = LOG_PAGE_FROM;
            n = list_once(LOG_PAGE_FROM, "", LOG_VIEWER_MAX_FILES);
        }
    }
    if (n > 0 && dir == LOG_PAGE_BEFORE && s.first < LOG_VIEWER_MAX_FILES
        && (s.first > 0 || s.n_files < LOG_VIEWER_MAX_FILES)
        && s.n_files < n) {
        n = list_once(LOG_PAGE_FROM, "", LOG_VIEWER_MAX_FILES);
    } else if (n > 0 && s.n_files == 0) {
        n = list_once(LOG_PAGE_BEFORE, "",
                      n - (n - 1) / LOG_VIEWER_MAX_FILES * LOG_VIEWER_MAX_FILES);
    }
    if (n < 0) {
        s.n_files = 0;
        s.n_card = 0;
        s.first = 0;
        s.listed = -1;
        /* After the list: it is what tries the mount again. */
        s.card = (s.io.card != NULL) ? s.io.card(s.io.ctx) : CARD_STATE_NONE;
        log_viewer_invalidate();
        return;
    }
    s.listed = 1;
    /*
     * The run just recorded is the one an operator re-reads the card for, so
     * the list opens on it: the highest run number on the page.  Runs rank
     * ahead of everything the panel lists, so it is the top row and the
     * scroll stays at 0.  A page with no numbered run keeps no selection.
     */
    int best = -1;
    for (int i = 0; i < s.n_files; ++i) {
        if (!s.files[i].is_dir &&
            log_run_number(s.files[i].name) >= 0 &&
            (best < 0 || log_name_rank(s.files[i].name,
                                       s.files[best].name) < 0)) {
            best = i;
        }
    }
    s.sel = best;
    log_viewer_invalidate();
}

void log_viewer_refresh(void)
{
    read_page(LOG_PAGE_FROM, "");
}

/*
 * Read the page on screen again.  The first page is read from the top, so a
 * run recorded since shows; any other from its first entry, and an entry
 * deleted since still names its place.  Another card's name in the header
 * is another card, read from its first page.
 */
static void rescan(void)
{
    if (s.first == 0 || s.listed != 1 || volume_changed()) {
        read_page(LOG_PAGE_FROM, "");
    } else {
        read_page(LOG_PAGE_FROM, page_edge(1));
    }
}

/* Whether the card holds more than the page shows, either side of it. */
static bool paged(void)
{
    return s.listed == 1 && s.n_files > 0
           && (s.first > 0 || s.first + s.n_files < s.n_card);
}

static bool can_prev(void)
{
    return paged() && s.first > 0;
}

static bool can_next(void)
{
    return paged() && s.first + s.n_files < s.n_card;
}

void log_viewer_browse_title(char *out, size_t n)
{
    if (out == NULL || n == 0) {
        return;
    }
    if (paged()) {
        snprintf(out, n, TR(LOG_N_OF_FILES), s.first + 1,
                 s.first + s.n_files, s.n_card);
    } else {
        snprintf(out, n, "%s", TR(LOG_FILES));
    }
}

static void drop_data(void)
{
    if (s.have_data) {
        log_data_free(&s.data);
        s.have_data = false;
    }
    s.cursor = -1;
    s.win0 = 0;
    s.win_n = 0;
    s.pinch = false;
    s.panning = false;
}

/* ------------------------------------------------------------- loading --- */

/* A format, so the compiler checks every call against its arguments --
 * tools/check_formats.py sees the translated ones the same way. */
static void set_message(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static void set_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* The analyser cannot follow va_start through this wrapper and reports the
     * list as uninitialised; it is initialised on the line above. */
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
    vsnprintf(s.message, sizeof(s.message), fmt, ap);
    va_end(ap);
    s.message_ok = false;
}

/* The reader's verdict in the language showing; log_err_str() is the
 * English the console logs. */
static const char *err_text(log_err_t err)
{
    switch (err) {
    case LOG_OK:                   return TR(LOG_ERR_OK);
    case LOG_ERR_READ:             return TR(LOG_ERR_READ);
    case LOG_ERR_NO_HEADER:        return TR(LOG_ERR_NO_HEADER);
    case LOG_ERR_NO_ROWS:          return TR(LOG_ERR_NO_ROWS);
    case LOG_ERR_NO_NUMERIC:       return TR(LOG_ERR_NO_NUMERIC);
    case LOG_ERR_TOO_MANY_COLUMNS: return TR(LOG_ERR_COLUMNS);
    case LOG_ERR_MEMORY:           return TR(LOG_ERR_MEMORY);
    case LOG_ERR_ARG:              return TR(LOG_ERR_ARG);
    default:                       return TR(LOG_ERR_UNKNOWN);
    }
}

/* A delimiter's name, as log_csv_delimiter_label() gives it in English. */
static const char *delim_text(char delimiter)
{
    switch (delimiter) {
    case ';':  return TR(LOG_SEMICOLON);
    case ',':  return TR(LOG_COMMA);
    case '\t': return TR(LOG_TAB);
    case '|':  return TR(LOG_PIPE);
    default:   return "?";
    }
}

/* The open file again: from the mount its page was read from, or not at
 * all, so a list read since from another card lends it no file. */
static bool open_name(log_source_t *src)
{
    return s.open_mounts == s.mounts && s.mounts == mounts_now()
           && s.io.open(s.name, src, s.io.ctx);
}

static void run_analysis(void)
{
    /*
     * Keep the names of the picked columns before the analysis overwrites
     * them.  The selection is stored as indices; both override buttons
     * re-run the analysis, and a delimiter change renumbers the columns, so
     * the selection is carried across by name.
     */
    char kept[LOG_MAX_SERIES][LOG_NAME_MAX];
    int n_kept = 0;
    if (s.have_analysis) {
        for (int i = 0; i < s.n_picked && i < LOG_MAX_SERIES; ++i) {
            int c = s.picked[i];
            if (c >= 0 && c < s.an.n_columns) {
                snprintf(kept[n_kept], sizeof(kept[0]), "%s",
                         s.an.columns[c].name);
                ++n_kept;
            }
        }
    }

    s.have_analysis = false;
    drop_data();

    if (s.io.open == NULL) {
        set_message("%s", TR(LOG_NO_SOURCE));
        return;
    }

    log_source_t src;
    point(s.open_tab);
    if (!open_name(&src)) {
        set_message(TR(LOG_CANNOT_OPEN), s.name);
        return;
    }

    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    if (s.delim_index >= 0) {
        opts.delimiter = log_csv_delimiters[s.delim_index];
    }
    opts.convention = s.conv;

    log_err_t err = log_csv_analyse(&src, &opts, &s.an);
    if (s.io.close != NULL) {
        s.io.close(s.io.ctx);
    }

    if (err != LOG_OK) {
        set_message("%s", err_text(err));
        return;
    }

    s.have_analysis = true;
    s.message[0] = '\0';

    /*
     * Keep what the operator chose: run_analysis() runs again from both
     * override buttons, and the selection has to survive it.  Re-resolve by
     * name rather than by index, because a delimiter change renumbers the
     * columns.
     */
    s.n_picked = 0;
    for (int k = 0; k < n_kept; ++k) {
        for (int c = 0; c < s.an.n_columns && s.n_picked < LOG_MAX_SERIES; ++c) {
            if (c != s.an.time_index && s.an.columns[c].numeric &&
                strcmp(s.an.columns[c].name, kept[k]) == 0) {
                s.picked[s.n_picked++] = c;
                break;
            }
        }
    }

    /* Nothing survived (a different file, or a re-read that renamed every
     * column): fall back to the first numeric columns that are not the time
     * axis. */
    if (s.n_picked == 0) {
        s.col_scroll = 0;
        for (int c = 0; c < s.an.n_columns && s.n_picked < LOG_MAX_SERIES; ++c) {
            if (c != s.an.time_index && s.an.columns[c].numeric) {
                s.picked[s.n_picked++] = c;
            }
        }
    }
}

static void compute_spans(void)
{
    /* Zero rather than return: leaving the arrays alone would let the previous
     * file's trace be drawn under this file's legend. */
    memset(s_span, 0, sizeof(s_span));
    memset(s_lo, 0, sizeof(s_lo));
    memset(s_hi, 0, sizeof(s_hi));
    if (!s.have_data || s.data.count <= 0) {
        return;
    }
    if (s.win_n <= 0 || s.win0 < 0 || s.win0 + s.win_n > s.data.count) {
        s.win0 = 0;
        s.win_n = s.data.count;
    }
    const int base = s.win0;
    const int count = s.win_n;

    for (int k = 0; k < s.data.n_fields; ++k) {
        float lo = s.data.field[k].min;
        float hi = s.data.field[k].max;
        /* A cell out of float range parses to an infinity, and an infinite
         * bound makes every scaling below (inf - inf) / inf = NaN, whose
         * conversion to int is undefined. */
        if (!isfinite(lo) || !isfinite(hi)) {
            lo = 0.0f;
            hi = 1.0f;
        }
        if (!(hi > lo)) {
            /* A constant trace still deserves a line through the middle. */
            lo -= 1.0f;
            hi += 1.0f;
        } else {
            float pad = (hi - lo) * 0.06f;
            float min = s.data.field[k].min;
            float max = s.data.field[k].max;
            lo -= pad;
            hi += pad;
            /* Never pad across zero: a current that never went negative should
             * not be drawn on an axis that says it might have. */
            if (min >= 0.0f && lo < 0.0f) {
                lo = 0.0f;
            }
            if (max <= 0.0f && hi > 0.0f) {
                hi = 0.0f;
            }
        }
        s_lo[k] = lo;
        s_hi[k] = hi;
        float span = hi - lo;

        const float *v = s.data.value[k] + base;
        for (int x = 0; x < PV_W; ++x) {
            int i0 = (int)(((long)x * count) / PV_W);
            int i1 = (int)(((long)(x + 1) * count) / PV_W);
            if (i1 <= i0) {
                i1 = i0 + 1;
            }
            if (i1 > count) {
                i1 = count;
            }
            if (i0 >= count) {
                i0 = count - 1;
            }

            /* Seed from the first sample that is a number: seeding from a
             * NaN and comparing against it leaves the whole bucket NaN even
             * when there are real samples in it. */
            float vmin = 0.0f;
            float vmax = 0.0f;
            bool any = false;
            for (int i = i0; i < i1; ++i) {
                if (!isfinite(v[i])) {
                    continue;
                }
                if (!any) {
                    vmin = vmax = v[i];
                    any = true;
                } else if (v[i] < vmin) {
                    vmin = v[i];
                } else if (v[i] > vmax) {
                    vmax = v[i];
                }
            }
            if (!any) {
                /*
                 * No number in this column.  The loader holds the previous
                 * value across an empty cell, so this is only the stretch
                 * before a column's first number: drawn as nothing rather
                 * than as a line along the bottom of the scale.
                 */
                s_span[k][x][0] = PV_EMPTY;
                s_span[k][x][1] = PV_EMPTY;
                continue;
            }

            int y_hi = PV_Y + PV_H - 1 -
                       (int)((vmax - lo) / span * (float)(PV_H - 1));
            int y_lo = PV_Y + PV_H - 1 -
                       (int)((vmin - lo) / span * (float)(PV_H - 1));
            if (y_hi < PV_Y) {
                y_hi = PV_Y;
            }
            if (y_hi > PV_Y + PV_H - 1) {
                y_hi = PV_Y + PV_H - 1;
            }
            if (y_lo > PV_Y + PV_H - 1) {
                y_lo = PV_Y + PV_H - 1;
            }
            if (y_lo < PV_Y) {
                y_lo = PV_Y;
            }
            s_span[k][x][0] = (int16_t)y_hi;
            s_span[k][x][1] = (int16_t)y_lo;
        }

        /* Join neighbouring columns so the trace is a line rather than a
         * dotted row of marks: each column reaches the one before it.  An
         * empty column joins nothing, so a gap stays a gap. */
        for (int x = 1; x < PV_W; ++x) {
            if (s_span[k][x][0] == PV_EMPTY || s_span[k][x - 1][0] == PV_EMPTY) {
                continue;
            }
            if (s_span[k][x][0] > s_span[k][x - 1][1]) {
                s_span[k][x][0] = (int16_t)(s_span[k][x - 1][1]);
            } else if (s_span[k][x][1] < s_span[k][x - 1][0]) {
                s_span[k][x][1] = (int16_t)(s_span[k][x - 1][0]);
            }
        }
    }
}

static void load_data(void)
{
    drop_data();
    if (!s.have_analysis || s.io.open == NULL) {
        return;
    }

    log_source_t src;
    point(s.open_tab);
    if (!open_name(&src)) {
        set_message(TR(LOG_CANNOT_OPEN), s.name);
        return;
    }
    log_err_t err = log_csv_build(&src, &s.an, s.picked, s.n_picked, &s.data);
    if (s.io.close != NULL) {
        s.io.close(s.io.ctx);
    }

    if (err != LOG_OK) {
        set_message("%s", err_text(err));
        return;
    }

    s.have_data = true;
    s.cursor = s.data.count - 1;
    s.win0 = 0;
    s.win_n = s.data.count;
    s.pinch = false;
    s.panning = false;
    s.message[0] = '\0';
    compute_spans();
    s.view = VIEW_PLOT;
}

static void open_selected(void)
{
    if (s.sel < 0 || s.sel >= s.n_files) {
        return;
    }
    if (s.files[s.sel].is_dir) {
        /* Subdirectories are listed (storage_list applies the suffix filter
         * to files only).  There is no directory navigation, so opening one
         * reports a message rather than running the analysis on it. */
        set_message(TR(LOG_IS_FOLDER), s.files[s.sel].name);
        return;
    }
    if (s.files[s.sel].size > LOG_VIEWER_OPEN_MAX) {
        /* Past what the reader's seek reaches.  Said, not read in part:
         * an import of the first 2 GiB would look like the whole run. */
        set_message(TR(LOG_TOO_BIG), s.files[s.sel].name);
        return;
    }
    snprintf(s.name, sizeof(s.name), "%s", s.files[s.sel].name);
    s.open_tab = s.tab;
    s.open_mounts = s.mounts;
    s.delim_index = -1;
    s.conv = LOG_CONV_AUTO;
    run_analysis();
    s.view = VIEW_IMPORT;
}

/* -------------------------------------------------------------- browse --- */

void log_viewer_size_text(char *out, size_t n, uint64_t bytes)
{
    if (bytes >= 1024ull * 1024u * 1024u) {
        snprintf(out, n, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024u) {
        snprintf(out, n, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, n, "%u kB", (unsigned)(bytes / 1024u));
    } else {
        snprintf(out, n, "%u B", (unsigned)bytes);
    }
}

/* DELETE is offered for a file the list has selected, and only when the I/O
 * can delete at all. */
static bool can_delete(void)
{
    return s.io.remove != NULL && s.sel >= 0 && s.sel < s.n_files
           && !s.files[s.sel].is_dir;
}

static void render_question(gfx_canvas_t *c)
{
    const gfx_rect_t box = DQ_BOX;
    ui_panel(c, box, TR(LOG_DELETE_TITLE), UI_DANGER);

    char size[16];
    char line[LOG_VIEWER_NAME_MAX + 24];
    log_viewer_size_text(size, sizeof(size), s.doomed_size);
    snprintf(line, sizeof(line), "%s   %s", s.doomed, size);
    gfx_text_in(c, gfx_rect_make(box.x + 24, box.y + 44, box.w - 48, 20), line,
                UI_FONT_LABEL, UI_TEXT, 1, GFX_ALIGN_LEFT);
    gfx_text(c, box.x + 24, box.y + 76, TR(LOG_DELETE_WHAT),
             UI_FONT_LABEL, UI_TEXT_DIM, 1);
    gfx_text(c, box.x + 24, box.y + 98, TR(LOG_DELETE_FINAL),
             UI_FONT_LABEL, UI_TEXT_DIM, 1);

    ui_button(c, DQ_CANCEL, TR(CANCEL), UI_PANEL_HI, s.press_btn == 0, true);
    ui_button(c, DQ_DELETE, TR(LOG_DELETE), UI_DANGER, s.press_btn == 1, true);
}

/*
 * The thumb of the scroll bar: its top in @p y and its height in @p h.  The
 * whole track while every row of the page shows.
 */
static void bar_thumb(int *y, int *h)
{
    const gfx_rect_t t = BR_BAR;
    if (s.n_files <= BR_ROWS) {
        *y = t.y;
        *h = t.h;
        return;
    }
    const int th = t.h * BR_ROWS / s.n_files;
    const int max = s.n_files - BR_ROWS;
    *y = t.y + (t.h - th) * s.scroll / max;
    *h = th;
}

/* The scroll that puts the thumb's top nearest @p top. */
static int bar_scroll_at(int top)
{
    int ty;
    int th;
    bar_thumb(&ty, &th);
    const int travel = BR_BAR.h - th;
    const int max = s.n_files - BR_ROWS;
    if (travel <= 0 || max <= 0) {
        return 0;
    }
    int off = top - BR_BAR.y;
    if (off < 0) {
        off = 0;
    }
    if (off > travel) {
        off = travel;
    }
    return (off * max + travel / 2) / travel;
}

static void render_bar(gfx_canvas_t *c)
{
    const gfx_rect_t t = BR_BAR;
    gfx_fill_chamfer_rect(c, t.x, t.y, t.w, t.h, UI_CHAMFER_SM, UI_PANEL_SUNK);
    int ty;
    int th;
    bar_thumb(&ty, &th);
    /* Accent while there is something to scroll, as the SETUP lists' bar;
     * the raised panel colour when the page shows whole. */
    const gfx_color_t col = (s.n_files > BR_ROWS) ? UI_ACCENT : UI_PANEL_HI;
    gfx_fill_chamfer_rect(c, t.x + 4, ty + 2, t.w - 8, th - 4, UI_CHAMFER_SM,
                          col);
}

/* The tabs as the header row shows them, left to right: the id of each. */
static const log_viewer_tab_t k_tab_at[LOG_TAB_COUNT] = {
    LOG_TAB_ROOT, LOG_TAB_USER, LOG_TAB_BENCH,
};

/* The tabs' labels, in that order. */
static const ui_text_id_t k_tab_text[LOG_TAB_COUNT] = {
    TX_LOG_TAB_ROOT, TX_LOG_TAB_USER, TX_LOG_TAB_BENCH,
};

/* The position in the header row of the tab with id @p tab. */
static int tab_pos(log_viewer_tab_t tab)
{
    for (int i = 0; i < (int)LOG_TAB_COUNT; ++i) {
        if (k_tab_at[i] == tab) {
            return i;
        }
    }
    return 0;
}

/* The empty list's two lines: why it is empty, and what the folder holds
 * when it does not. */
static void empty_lines(char *why, size_t n, const char **hint)
{
    if (s.listed == -1) {
        snprintf(why, n, "%s", ui_card_text(s.card, UI_CARD_AT_LOGS));
    } else if (s.tab == LOG_TAB_ROOT) {
        snprintf(why, n, "%s", TR(LOG_NO_CSV));
    } else {
        snprintf(why, n, TR(LOG_NO_CSV_IN), log_viewer_tab_dir(s.tab));
    }
    switch (s.tab) {
    case LOG_TAB_USER:
        *hint = settings_get_bool(SET_LOG_USER) ? TR(LOG_TESTS_HERE)
                                                : TR(LOG_USER_OFF);
        break;
    case LOG_TAB_BENCH:
        *hint = settings_get_bool(SET_LOG_AUTO) ? TR(LOG_RUNS_HERE)
                                                : TR(LOG_AUTO_OFF);
        break;
    case LOG_TAB_ROOT:
    default:
        *hint = TR(LOG_ROOT_HERE);
        break;
    }
}

static void render_browse(gfx_canvas_t *c)
{
    /* The folder on screen after the tabs, "BENCH/LOGS" (nothing for the
     * root), and the card's name at the row's right end, "UNITEST". */
    const char *vol = volume_now();
    const char *dir = log_viewer_tab_dir(s.tab);
    const char *name = (vol[0] != '\0') ? vol : TR(LOG_SD_CARD);
    const int end = paged() ? BR_HEAD_END : BR_HEAD_FAR;
    const int dir_w = gfx_text_width(UI_FONT_LABEL, dir, 1);
    gfx_text(c, BR_HEAD_X, 12, dir, UI_FONT_LABEL, UI_TEXT_DIM, 1);
    const int name_x = BR_HEAD_X + dir_w + 16;
    gfx_text_in(c, gfx_rect_make(name_x, 2, end - name_x, BR_PG_H), name,
                UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_RIGHT);

    /*
     * The question replaces the list and the footer rather than sitting on
     * top of them: a footer left on screen is a RESCAN or an OPEN that looks
     * live while the only answers are CANCEL and DELETE.
     */
    if (s.doomed[0] != '\0') {
        render_question(c);
        return;
    }
    /* Not under the question: its only answers are CANCEL and DELETE. */
    ui_tabs_render(&s.tabs, c);

    if (s.listed != 1 || s.n_files == 0) {
        gfx_rect_t box = gfx_rect_make(120, 160, 560, 160);
        ui_panel(c, box, TR(LOG_NO_LOGS), UI_WARN);
        char why[96];
        const char *hint = "";
        empty_lines(why, sizeof(why), &hint);
        gfx_text(c, box.x + 24, box.y + 52, why, UI_FONT_LABEL, UI_TEXT, 1);
        gfx_text(c, box.x + 24, box.y + 76, hint, UI_FONT_LABEL,
                 UI_TEXT_FAINT, 1);
    } else {
        /*
         * The tab names the page's place in the card when the volume holds
         * more than the page does: a file missing from a list headed FILES
         * reads as a file that is not on the card.  Pages run newest run
         * first, see log_select.h.  The buffer takes the three counts
         * written out in full, so a volume that reports a nonsense one still
         * leaves a terminated title.
         */
        char title[64];
        log_viewer_browse_title(title, sizeof(title));
        ui_panel(c, BR_LIST, title, UI_ACCENT);
        render_bar(c);
        int y = BR_LIST.y + 30;
        for (int r = 0; r < BR_ROWS; ++r) {
            int i = s.scroll + r;
            if (i >= s.n_files) {
                break;
            }
            gfx_rect_t row = gfx_rect_make(BR_ROW_X, y, BR_ROW_W,
                                           BR_ROW_H - 4);
            bool on = (i == s.sel);
            gfx_fill_chamfer_rect(c, row.x, row.y, row.w, row.h, UI_CHAMFER_SM,
                                  on ? UI_PANEL_HI : UI_PANEL_SUNK);
            if (on) {
                gfx_fill_rect(c, row.x, row.y, 4, row.h, UI_ACCENT);
            }
            gfx_text(c, row.x + 16, row.y + 10, s.files[i].name, UI_FONT_LABEL,
                     on ? UI_TEXT : UI_TEXT_DIM, 1);

            char size[16];
            log_viewer_size_text(size, sizeof(size), s.files[i].size);
            gfx_rect_t sz = gfx_rect_make(row.x + row.w - 132, row.y, 120, row.h);
            gfx_text_in(c, sz, s.files[i].is_dir ? TR(LOG_DIR) : size,
                        UI_FONT_LABEL,
                        UI_TEXT_FAINT, 1, GFX_ALIGN_RIGHT);
            y += BR_ROW_H;
        }
    }

    if (paged()) {
        ui_button(c, BR_PREV, TR(LOG_PREV), UI_PANEL_HI, s.press_btn == 4,
                  can_prev());
        ui_button(c, BR_NEXT, TR(LOG_NEXT), UI_PANEL_HI, s.press_btn == 5,
                  can_next());
    }
    ui_button(c, BR_RESCAN, TR(LOG_RESCAN), UI_PANEL_HI, s.press_btn == 0,
              true);
    /* WI-FI: the card's files to a browser, over the panel's
     * Wi-Fi.  The screen there says when Wi-Fi may not start. */
    ui_button(c, BR_WIFI, TR(LOG_WIFI), UI_PANEL_HI, s.press_btn == 3, true);
    int msg_end = BR_OPEN.x;
    if (s.n_files > 0) {
        ui_button(c, BR_OPEN, TR(LOG_OPEN), UI_ACCENT, s.press_btn == 1,
                  s.sel >= 0);
        if (s.io.remove != NULL) {
            ui_button(c, BR_DELETE, TR(LOG_DELETE), UI_DANGER,
                      s.press_btn == 2,
                      can_delete());
            msg_end = BR_DELETE.x;
        }
    }
    if (s.message[0] != '\0') {
        /* In the gap between the buttons, on two 18 px lines of the 42 px
         * footer when one is too short.  A message longer than two lines is
         * one line clipped there: a long name runs out of room rather than
         * over DELETE. */
        const gfx_color_t col = s.message_ok ? UI_TEXT_DIM : UI_DANGER;
        const gfx_rect_t two = gfx_rect_make(BR_MSG_X, FOOT_Y + 3,
                                             msg_end - 8 - BR_MSG_X, 18);
        const int n = ui_text_wrap(NULL, two, 18, s.message, UI_FONT_LABEL,
                                   col);
        if (n == 2) {
            (void)ui_text_wrap(c, two, 18, s.message, UI_FONT_LABEL, col);
        } else {
            gfx_text_in(c, gfx_rect_make(BR_MSG_X, FOOT_Y,
                                         msg_end - 8 - BR_MSG_X, FOOT_H),
                        s.message, UI_FONT_LABEL, col, 1, GFX_ALIGN_LEFT);
        }
    }
}

/* -------------------------------------------------------------- import --- */

static const char *conv_label(log_conv_t v)
{
    switch (v) {
    case LOG_CONV_DE: return "1.234,5";
    case LOG_CONV_EN: return "1,234.5";
    default:          return "AUTO";
    }
}

static void render_import(gfx_canvas_t *c)
{
    gfx_text(c, 300, 12, s.name, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    if (!s.have_analysis) {
        /* 74 cells of text inside 24 px margins. */
        gfx_rect_t box = gfx_rect_make(80, 160, 640, 150);
        ui_panel(c, box, TR(LOG_CANNOT_READ), UI_DANGER);
        gfx_text(c, box.x + 24, box.y + 52,
                 s.message[0] ? s.message : TR(LOG_UNREADABLE), UI_FONT_LABEL,
                 UI_TEXT, 1);
        gfx_text(c, box.x + 24, box.y + 76,
                 TR(LOG_TRY_ANOTHER),
                 UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    } else {
        ui_panel(c, IM_LEFT, TR(LOG_DETECTED), UI_ACCENT);

        /* Rows 20 px apart, so that under the evidence the warnings take
         * up to four lines of 33 cells above the hints. */
        char line[64];
        int y = IM_LEFT.y + 34;
        const int lh = 20;

        struct {
            const char *key;
            char value[48];
            gfx_color_t color;
        } rows[6];
        int n = 0;

        /* cppcheck-suppress legacyUninitvar
         * snprintf writes rows[n].value, it does not read it; the array is
         * filled here and every field set before it is drawn. */
        snprintf(rows[n].value, sizeof(rows[n].value), "%s",
                 delim_text(s.an.delimiter));
        rows[n].key = TR(LOG_KEY_SEPARATOR);
        rows[n].color = UI_TEXT;
        n++;

        const char *flag = s.an.convention_forced ? TR(LOG_SET)
                           : (s.an.convention_confident ? "" : "(?)");
        snprintf(rows[n].value, sizeof(rows[n].value), "%s%s%s",
                 (s.an.convention == LOG_CONV_DE) ? TR(LOG_GERMAN)
                                                  : TR(LOG_ENGLISH),
                 (flag[0] != '\0') ? " " : "", flag);
        rows[n].key = TR(LOG_KEY_NUMBERS);
        rows[n].color = (s.an.convention_forced || s.an.convention_confident)
                            ? UI_TEXT
                            : UI_WARN;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d%s%s",
                 s.an.row_count, s.an.truncated ? " " : "",
                 s.an.truncated ? TR(LOG_CAPPED) : "");
        rows[n].key = TR(LOG_KEY_ROWS);
        rows[n].color = (s.an.truncated || s.an.cut_row) ? UI_WARN : UI_TEXT;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d", s.an.n_columns);
        rows[n].key = TR(LOG_KEY_COLUMNS);
        rows[n].color = UI_TEXT;
        n++;

        if (s.an.time_index >= 0) {
            snprintf(rows[n].value, sizeof(rows[n].value), "%s %s",
                     s.an.columns[s.an.time_index].name, s.an.time_unit);
        } else {
            snprintf(rows[n].value, sizeof(rows[n].value), "%s",
                     TR(LOG_ROW_INDEX));
        }
        rows[n].key = TR(LOG_KEY_TIME);
        rows[n].color = (s.an.time_index >= 0) ? UI_TEXT : UI_WARN;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d%s%s",
                 s.an.ragged_rows, (s.an.long_rows > 0) ? " " : "",
                 (s.an.long_rows > 0) ? TR(LOG_PLUS_LONG) : "");
        rows[n].key = TR(LOG_KEY_RAGGED);
        rows[n].color = (s.an.ragged_rows > 0 || s.an.long_rows > 0) ? UI_DANGER
                                                                     : UI_TEXT;
        n++;

        for (int i = 0; i < n; ++i) {
            gfx_text(c, IM_LEFT.x + 16, y, rows[i].key, UI_FONT_LABEL,
                     UI_TEXT_FAINT, 1);
            gfx_rect_t box = gfx_rect_make(IM_LEFT.x + 120, y - 2,
                                           IM_LEFT.w - 136, 20);
            gfx_text_in(c, box, rows[i].value, UI_FONT_LABEL, rows[i].color, 1,
                        GFX_ALIGN_RIGHT);
            y += lh;
        }

        ui_rule(c, IM_LEFT.x + 16, y + 2, IM_LEFT.w - 32, UI_EDGE);
        y += 12;

        /* The evidence behind the detection. */
        snprintf(line, sizeof(line), TR(LOG_EVIDENCE),
                 s.an.votes.de, s.an.votes.en, s.an.votes.ambiguous);
        gfx_text(c, IM_LEFT.x + 16, y, line, UI_FONT_LABEL, UI_TEXT_FAINT, 1);
        y += 18;

        /* A warning breaks onto a second line of 33 cells, 18 px lower. */
        const gfx_rect_t warn = gfx_rect_make(IM_LEFT.x + 16, y,
                                              IM_LEFT.w - 32, 18);
        int lines = 0;
        if (s.an.convention_conflict) {
            lines = ui_text_wrap(c, warn, 18, TR(LOG_MIXED), UI_FONT_LABEL,
                                 UI_DANGER);
        } else if (s.an.long_rows > 0) {
            /* Those rows lost every cell after the overflow, and a lost cell
             * looks exactly like a blank one further down. */
            snprintf(line, sizeof(line), TR(LOG_TOO_LONG),
                     s.an.long_rows);
            lines = ui_text_wrap(c, warn, 18, line, UI_FONT_LABEL,
                                 UI_DANGER);
        } else if (s.an.ragged_rows > 0) {
            lines = ui_text_wrap(c, warn, 18, TR(LOG_ROWS_DIFFER),
                                 UI_FONT_LABEL, UI_DANGER);
        } else if (!s.an.convention_confident) {
            lines = ui_text_wrap(c, warn, 18, TR(LOG_UNSETTLED),
                                 UI_FONT_LABEL, UI_WARN);
        }
        if (s.an.cut_row) {
            /* Independent of the lines above: the row left out has no other
             * place on screen.  It follows them when they are shown. */
            gfx_rect_t cut = warn;
            cut.y = (int16_t)(warn.y + 18 * lines);
            (void)ui_text_wrap(c, cut, 18, TR(LOG_LAST_CUT), UI_FONT_LABEL,
                               UI_WARN);
        }

        /* The override controls for the two detections above. */
        int hy = IM_LEFT.y + IM_LEFT.h - 44;
        ui_rule(c, IM_LEFT.x + 16, hy - 6, IM_LEFT.w - 32, UI_EDGE);
        gfx_text(c, IM_LEFT.x + 16, hy, TR(LOG_HINT_NUM),
                 UI_FONT_LABEL, UI_TEXT_FAINT, 1);
        gfx_text(c, IM_LEFT.x + 16, hy + 18, TR(LOG_HINT_SEP),
                 UI_FONT_LABEL, UI_TEXT_FAINT, 1);

        /* --- columns -------------------------------------------------- */
        char title[40];
        snprintf(title, sizeof(title), TR(LOG_PLOT_N), s.n_picked,
                 LOG_MAX_SERIES);
        ui_panel(c, IM_RIGHT, title, UI_ACCENT);

        int ry = IM_RIGHT.y + 30;
        for (int r = 0; r < IM_ROWS; ++r) {
            int i = s.col_scroll + r;
            if (i >= s.an.n_columns) {
                break;
            }
            const log_column_t *col = &s.an.columns[i];
            int slot = -1;
            for (int k = 0; k < s.n_picked; ++k) {
                if (s.picked[k] == i) {
                    slot = k;
                }
            }
            bool is_time = (i == s.an.time_index);

            gfx_rect_t row = gfx_rect_make(IM_RIGHT.x + 8, ry, IM_RIGHT.w - 16,
                                           IM_ROW_H - 4);
            gfx_fill_chamfer_rect(c, row.x, row.y, row.w, row.h, UI_CHAMFER_SM,
                                  (slot >= 0) ? UI_PANEL_HI : UI_PANEL_SUNK);
            gfx_color_t mark = is_time      ? UI_TEAL
                               : (slot >= 0) ? ui_theme_color(k_series_color[slot])
                               : col->numeric ? UI_EDGE_HI
                                              : UI_PANEL;
            gfx_fill_rect(c, row.x, row.y, 5, row.h, mark);

            gfx_color_t text = col->numeric ? UI_TEXT : UI_TEXT_FAINT;
            gfx_text(c, row.x + 16, row.y + 8, col->name, UI_FONT_LABEL, text, 1);

            /* The file's own first value, next to what was made of it: the
             * cheapest way to see a mis-read before plotting it. */
            gfx_rect_t sb = gfx_rect_make(row.x + 168, row.y, 110, row.h);
            gfx_text_in(c, sb, col->sample[0], UI_FONT_LABEL, UI_TEXT_DIM, 1,
                        GFX_ALIGN_RIGHT);

            char right[40];
            if (is_time) {
                snprintf(right, sizeof(right), "%s", TR(LOG_TIME));
            } else if (!col->numeric) {
                snprintf(right, sizeof(right), "%s", TR(LOG_TEXT));
            } else {
                log_viewer_range_text(right, sizeof(right), col->min,
                                      col->max, col->unit);
            }
            gfx_rect_t rb = gfx_rect_make(row.x + 288, row.y, row.w - 300,
                                          row.h);
            gfx_text_in(c, rb, right, UI_FONT_LABEL,
                        is_time ? UI_TEAL : UI_TEXT_FAINT, 1, GFX_ALIGN_RIGHT);
            ry += IM_ROW_H;
        }
    }

    /* --- controls ------------------------------------------------------ */
    char btn[40];
    snprintf(btn, sizeof(btn), TR(LOG_BTN_SEP),
             (s.delim_index < 0) ? "AUTO"
                                 : delim_text(log_csv_delimiters[s.delim_index]));
    ui_button(c, gfx_rect_make(16, IM_BTN_Y, 190, IM_BTN_H), btn, UI_PANEL_HI,
              s.press_btn == 0, true);

    snprintf(btn, sizeof(btn), TR(LOG_BTN_NUM), conv_label(s.conv));
    ui_button(c, gfx_rect_make(214, IM_BTN_Y, 190, IM_BTN_H), btn, UI_PANEL_HI,
              s.press_btn == 1, true);

    ui_button(c, gfx_rect_make(412, IM_BTN_Y, 180, IM_BTN_H), TR(LOG_BACK),
              UI_PANEL_HI, s.press_btn == 2, true);

    ui_button(c, gfx_rect_make(600, IM_BTN_Y, 184, IM_BTN_H), TR(LOG_PLOT),
              UI_ACCENT,
              s.press_btn == 3, s.have_analysis && s.n_picked > 0);
}

/* ---------------------------------------------------------------- plot --- */

static void draw_grid_band(gfx_canvas_t *band, int band_y0, int rows)
{
    for (int i = 1; i < PV_DIV_X; ++i) {
        int x = (PV_W * i) / PV_DIV_X;
        gfx_vline(band, x, 0, rows, UI_GRID);
    }
    for (int i = 0; i <= PV_DIV_Y; ++i) {
        int y = PV_Y + (PV_H * i) / PV_DIV_Y;
        if (y >= band_y0 && y < band_y0 + rows) {
            gfx_hline(band, 0, y - band_y0, PV_W,
                      (i == 0 || i == PV_DIV_Y) ? UI_GRID_STRONG : UI_GRID);
        }
    }
}

static void draw_trace_band(gfx_canvas_t *band, int k, int band_y0, int rows)
{
    gfx_color_t col = ui_theme_color(k_series_color[k]);
    for (int x = 0; x < PV_W; ++x) {
        int y0 = s_span[k][x][0];
        int y1 = s_span[k][x][1];
        if (y0 == PV_EMPTY || y1 < band_y0 || y0 >= band_y0 + rows) {
            continue;
        }
        int a = (y0 < band_y0) ? band_y0 : y0;
        int b = (y1 >= band_y0 + rows) ? band_y0 + rows - 1 : y1;
        gfx_vline(band, x, a - band_y0, b - a + 1, col);
    }
}

static int cursor_x(void)
{
    if (!s.have_data || s.win_n <= 1 || s.cursor < s.win0 ||
        s.cursor >= s.win0 + s.win_n) {
        return -1;
    }
    return PV_X + (int)(((long)(s.cursor - s.win0) * (PV_W - 1)) /
                        (s.win_n - 1));
}

static void render_plot(gfx_canvas_t *c)
{
    char line[160];

    if (!s.have_data) {
        gfx_rect_t box = gfx_rect_make(120, 170, 560, 140);
        ui_panel(c, box, TR(LOG_NOTHING_LOADED), UI_WARN);
        gfx_text(c, box.x + 24, box.y + 52,
                 s.message[0] ? s.message : TR(LOG_PICK_FIRST),
                 UI_FONT_LABEL, UI_TEXT, 1);
        return;
    }

    snprintf(line, sizeof(line), TR(LOG_SUMMARY), s.name,
             s.data.count, s.data.duration_s, s.data.rate_hz);
    gfx_text(c, 260, 12, line, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    /* --- legend, doubling as the cursor readout ------------------------ */
    int ci = (s.cursor >= 0 && s.cursor < s.data.count) ? s.cursor : 0;
    for (int k = 0; k < s.data.n_fields; ++k) {
        gfx_rect_t chip = gfx_rect_make(CHIP_X + k * (CHIP_W + CHIP_GAP), CHIP_Y,
                                        CHIP_W, CHIP_H);
        gfx_color_t col = ui_theme_color(k_series_color[k]);
        gfx_fill_chamfer_rect(c, chip.x, chip.y, chip.w, chip.h, UI_CHAMFER_SM,
                              UI_PANEL);
        gfx_fill_rect(c, chip.x, chip.y, 5, chip.h, col);
        gfx_text(c, chip.x + 12, chip.y + 3, s.data.field[k].name,
                 UI_FONT_LABEL, UI_TEXT, 1);

        char val[16];
        ui_fmt(val, sizeof(val), s.data.value[k][ci], 2);
        snprintf(line, sizeof(line), "%s%s", val, s.data.field[k].unit);
        gfx_rect_t box = gfx_rect_make(chip.x + 12, chip.y + 18, chip.w - 24, 16);
        gfx_text_in(c, box, line, UI_FONT_LABEL, col, 1, GFX_ALIGN_RIGHT);

        char lo[10];
        char hi[10];
        ui_fmt(lo, sizeof(lo), s_lo[k], 0);
        ui_fmt(hi, sizeof(hi), s_hi[k], 0);
        snprintf(line, sizeof(line), "%s-%s", lo, hi);
        gfx_text(c, chip.x + 12, chip.y + 19, line, UI_FONT_LABEL,
                 UI_TEXT_FAINT, 1);
    }

    /* --- plot ----------------------------------------------------------- */
    ui_panel(c, PV_PANEL, NULL, UI_ACCENT);

    gfx_canvas_t band;
    for (int by = 0; by < PV_H; by += PV_BAND) {
        int rows = PV_H - by;
        if (rows > PV_BAND) {
            rows = PV_BAND;
        }
        gfx_canvas_init(&band, s_band, PV_W, rows, PV_W);
        gfx_clear(&band, UI_PANEL_SUNK);
        draw_grid_band(&band, PV_Y + by, rows);
        for (int k = 0; k < s.data.n_fields; ++k) {
            draw_trace_band(&band, k, PV_Y + by, rows);
        }
        gfx_blit(c, PV_X, PV_Y + by, s_band, PV_W, rows, PV_W);
    }

    int cx = cursor_x();
    if (cx >= 0) {
        gfx_vline(c, cx, PV_Y, PV_H, UI_TEXT_DIM);
        for (int k = 0; k < s.data.n_fields; ++k) {
            int x = cx - PV_X;
            if (s_span[k][x][0] == PV_EMPTY) {
                continue;
            }
            int y = (s_span[k][x][0] + s_span[k][x][1]) / 2;
            gfx_fill_circle(c, cx, y, 3, ui_theme_color(k_series_color[k]));
        }
    }

    /* --- time axis ------------------------------------------------------ */
    /* --- position of a zoomed view ------------------------------------ */
    if (s.win_n > 0 && s.win_n < s.data.count) {
        gfx_fill_rect(c, PV_X, PV_POS_Y, PV_W, PV_POS_H, UI_PANEL_SUNK);
        int bx = PV_X + (int)(((long)s.win0 * PV_W) / s.data.count);
        int bw = (int)(((long)s.win_n * PV_W) / s.data.count);
        if (bw < 6) {
            bw = 6;
        }
        if (bx + bw > PV_X + PV_W) {
            bx = PV_X + PV_W - bw;
        }
        gfx_fill_rect(c, bx, PV_POS_Y, bw, PV_POS_H, UI_ACCENT);
    }

    for (int i = 0; i <= PV_DIV_X; ++i) {
        int idx = s.win0 + (s.win_n - 1) * i / PV_DIV_X;
        char t[16];
        ui_fmt(t, sizeof(t), s.data.time[idx], 2);
        int x = PV_X + (PV_W - 1) * i / PV_DIV_X;
        int w = gfx_text_width(UI_FONT_LABEL, t, 1);
        int tx = x - w / 2;
        if (tx < PV_X) {
            tx = PV_X;
        }
        if (tx + w > PV_X + PV_W) {
            tx = PV_X + PV_W - w;
        }
        gfx_text(c, tx, PV_PANEL.y + PV_PANEL.h + 4, t, UI_FONT_LABEL,
                 UI_TEXT_FAINT, 1);
    }

    /* --- footer --------------------------------------------------------- */
    ui_button(c, gfx_rect_make(16, FOOT_Y, 170, FOOT_H), TR(LOG_BACK),
              UI_PANEL_HI,
              s.press_btn == 0, true);
    ui_button(c, gfx_rect_make(196, FOOT_Y, 80, FOOT_H), "<", UI_PANEL_HI,
              s.press_btn == 1, true);
    ui_button(c, gfx_rect_make(284, FOOT_Y, 80, FOOT_H), ">", UI_PANEL_HI,
              s.press_btn == 2, true);

    if (s.data.time_name[0] != '\0') {
        snprintf(line, sizeof(line), TR(LOG_AT_TIME),
                 (double)s.data.time[ci], ci + 1, s.data.count);
    } else {
        /* No time column, so the x axis is the row number.  Say that rather
         * than printing a seconds value that is really an index. */
        snprintf(line, sizeof(line), TR(LOG_AT_ROW), ci + 1,
                 s.data.count);
    }
    gfx_text(c, 384, FOOT_Y + 13, line, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    if (s.data.unparsed_cells > 0) {
        snprintf(line, sizeof(line), TR(LOG_UNREADABLE_CELLS),
                 s.data.unparsed_cells);
        gfx_text(c, 384, FOOT_Y + 27, line, UI_FONT_LABEL, UI_WARN, 1);
    }
}

/* --------------------------------------------------------------- events -- */

static int hit_button(const gfx_rect_t *rects, int n, int x, int y)
{
    for (int i = 0; i < n; ++i) {
        if (gfx_rect_contains(rects[i], x, y)) {
            return i;
        }
    }
    return -1;
}

/* A press that moves further than this, in x or in y, is not a tap; on a
 * list, vertical travel past it is a scroll. */
#define DRAG_SLOP 8

/*
 * A button or a row is a tap: the press arms it and the release activates
 * it.  One contact holds the press from its DOWN to its UP; another
 * presses nothing and ends nothing.
 */
static bool tap_begin(const touch_event_t *e)
{
    /* Another contact's DOWN is refused.  A DOWN with the id of the press
     * held says that contact has gone and its release with it, and is
     * taken. */
    if (s.tap_have && e->point.id != s.tap_id) {
        return false;
    }
    s.tap_have = true;
    s.tap_moved = false;
    s.tap_id = e->point.id;
    s.tap_x0 = e->point.x;
    s.tap_y0 = e->point.y;
    return true;
}

static bool tap_mine(const touch_event_t *e)
{
    return s.tap_have && e->point.id == s.tap_id;
}

/* Whether @p e is further than the slop from the press, in x or in y. */
static bool tap_far(const touch_event_t *e)
{
    int dx = e->point.x - s.tap_x0, dy = e->point.y - s.tap_y0;
    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    return dx > DRAG_SLOP || dy > DRAG_SLOP;
}

/*
 * Whether the release @p e is a tap on @p r: inside it, the contact never
 * further than the slop from its press, and the release made by the finger,
 * not for it (TOUCH_FLAG_NO_TAP).
 */
static bool tap_on(const touch_event_t *e, gfx_rect_t r)
{
    return touch_event_is_tap_up(e) && !s.tap_moved && !tap_far(e)
           && gfx_rect_contains(r, e->point.x, e->point.y);
}

/* Row @p r of the rows showing in a list: under its 30 px heading. */
static gfx_rect_t list_row_rect(gfx_rect_t list, int row_h, int r)
{
    return gfx_rect_make(list.x, list.y + 30 + r * row_h, list.w, row_h);
}

static void clamp_scroll(int *scroll, int total, int visible)
{
    int max = total - visible;
    if (max < 0) {
        max = 0;
    }
    if (*scroll > max) {
        *scroll = max;
    }
    if (*scroll < 0) {
        *scroll = 0;
    }
}

static void ask_delete(void)
{
    if (!can_delete()) {
        return;
    }
    snprintf(s.doomed, sizeof(s.doomed), "%s", s.files[s.sel].name);
    s.doomed_size = s.files[s.sel].size;
    s.q_down = false;
    s.message[0] = '\0';
}

static void delete_doomed(void)
{
    char name[LOG_VIEWER_NAME_MAX];
    snprintf(name, sizeof(name), "%s", s.doomed);
    s.doomed[0] = '\0';

    point(s.tab);
    if (s.io.remove == NULL || !s.io.remove(name, s.io.ctx)) {
        /* The list stays as it was, selection included: the file is still
         * there as far as anybody knows, and RESCAN says otherwise if not. */
        set_message(TR(LOG_CANNOT_DELETE), name);
        return;
    }
    /* What the import and plot views hold was read from that file. */
    if (strcmp(s.name, name) == 0 && s.open_tab == s.tab) {
        drop_data();
        s.have_analysis = false;
        s.name[0] = '\0';
    }
    /* Re-read rather than drop the row here: the card says what is on it.
     * The page stays, or is the last one when the file was the last page's
     * only entry.  The selection goes with the file: the re-read's own
     * choice, the newest run, is the top row and can be scrolled out of
     * sight, so nothing is selected and a second DELETE needs a second
     * choice.  The scroll stays near the gap. */
    const int scroll = s.scroll;
    rescan();
    s.sel = -1;
    s.scroll = scroll;
    clamp_scroll(&s.scroll, s.n_files, BR_ROWS);
    set_message(TR(LOG_DELETED), name);
    s.message_ok = true;
}

static void question_event(const touch_event_t *e)
{
    const gfx_rect_t btns[2] = { DQ_CANCEL, DQ_DELETE };

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        /* The first contact on a button owns the press; any other finger
         * that lands while it is down is not part of it. */
        if (!s.q_down) {
            s.press_btn = hit_button(btns, 2, e->point.x, e->point.y);
            if (s.press_btn >= 0) {
                s.q_down = true;
                s.q_id = e->point.id;
                s.tap_have = false;
                (void)tap_begin(e);
            }
            log_viewer_invalidate();
        }
        break;

    case TOUCH_EVENT_MOVE:
        break;

    case TOUCH_EVENT_UP:
        if (!s.q_down || e->point.id != s.q_id) {
            break;
        }
        /*
         * Pressed and released on the same button by the same contact, as
         * a tap: a finger that slides off DELETE has changed its mind, and
         * this is the one control on the screen that cannot be taken back.
         */
        s.q_down = false;
        s.tap_have = false;
        if (s.press_btn >= 0 && tap_on(e, btns[s.press_btn])) {
            if (s.press_btn == 0) {
                s.doomed[0] = '\0';
            } else if (s.press_btn == 1) {
                delete_doomed();
            }
        }
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
}

static void browse_event(const touch_event_t *e)
{
    if (s.doomed[0] != '\0') {
        question_event(e);
        return;
    }

    /* A tab tapped lists its folder from the first page. */
    if (ui_tabs_event(&s.tabs, e)) {
        s.tap_have  = false;
        s.pressing  = false;
        s.press_btn = -1;
        s.bar_drag  = false;
        log_viewer_set_tab(k_tab_at[s.tabs.selected]);
        return;
    }
    if (e->type == TOUCH_EVENT_DOWN && s.tabs.pressed >= 0) {
        log_viewer_invalidate();
        return;     /* the tab's own press */
    }

    /* PREV and NEXT are buttons only while they show. */
    const gfx_rect_t btns[6] = { BR_RESCAN, BR_OPEN, BR_DELETE, BR_WIFI,
                                 BR_PREV, BR_NEXT };
    const int n_btns = paged() ? 6 : 4;

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (!tap_begin(e)) {
            break;
        }
        s.press_btn = hit_button(btns, n_btns, e->point.x, e->point.y);
        s.press_row = -1;
        s.dragged = false;
        s.pressing = false;
        s.bar_drag = false;
        if (s.press_btn < 0 && s.listed == 1 && s.n_files > 0
            && gfx_rect_contains(BR_BAR, e->point.x, e->point.y)) {
            /*
             * The scroll bar.  A press on the thumb takes it where it is
             * held; a press on the track beside it brings the thumb's middle
             * to the finger.  Either way the finger then drags it.
             */
            int ty;
            int th;
            bar_thumb(&ty, &th);
            s.bar_drag = true;
            if (e->point.y >= ty && e->point.y < ty + th) {
                s.bar_grab = e->point.y - ty;
            } else {
                s.bar_grab = th / 2;
                s.scroll = bar_scroll_at(e->point.y - s.bar_grab);
            }
        } else if (s.press_btn < 0
                   && gfx_rect_contains(BR_LIST, e->point.x, e->point.y)) {
            s.pressing = true;
            s.press_y0 = e->point.y;
            s.press_scroll0 = s.scroll;
            int rel = e->point.y - (BR_LIST.y + 30);
            int r = (rel >= 0) ? rel / BR_ROW_H : -1;
            if (r >= 0 && r < BR_ROWS) {
                s.press_row = s.scroll + r;
            }
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.bar_drag && tap_mine(e)) {
            const int scroll = bar_scroll_at(e->point.y - s.bar_grab);
            if (scroll != s.scroll) {
                s.scroll = scroll;
                log_viewer_invalidate();
            }
        } else if (s.pressing && tap_mine(e)) {
            int dy = e->point.y - s.press_y0;
            if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
                s.dragged = true;
                s.scroll = s.press_scroll0 - dy / BR_ROW_H;
                clamp_scroll(&s.scroll, s.n_files, BR_ROWS);
                log_viewer_invalidate();
            }
        }
        break;

    case TOUCH_EVENT_UP: {
        if (!tap_mine(e)) {
            break;
        }
        s.tap_have = false;
        const int btn = s.press_btn;
        const bool row = s.pressing && !s.dragged && s.press_row >= 0
                         && s.press_row < s.n_files
                         && tap_on(e, list_row_rect(BR_LIST, BR_ROW_H,
                                                    s.press_row - s.scroll));
        s.pressing = false;
        s.bar_drag = false;
        s.press_btn = -1;
        /* A press that slid off its button is not a tap on it. */
        const bool on = btn >= 0 && tap_on(e, btns[btn]);
        if (on && btn == 0) {
            rescan();
        } else if (on && btn == 4) {
            if (can_prev()) {
                read_page(LOG_PAGE_BEFORE, page_edge(1));
            }
        } else if (on && btn == 5) {
            if (can_next()) {
                read_page(LOG_PAGE_AFTER, page_edge(-1));
            }
        } else if (on && btn == 1) {
            open_selected();
        } else if (on && btn == 3) {
            ui_router_goto(SCREEN_WIFI);
            return;
        } else if (on) {
            ask_delete();
        } else if (btn < 0 && row) {
            if (s.sel == s.press_row) {
                open_selected(); /* second tap opens */
            } else {
                s.sel = s.press_row;
            }
        }
        log_viewer_invalidate();
        break;
    }
    }
}

static void toggle_column(int i)
{
    if (i < 0 || i >= s.an.n_columns || i == s.an.time_index) {
        return;
    }
    for (int k = 0; k < s.n_picked; ++k) {
        if (s.picked[k] == i) {
            for (int j = k; j + 1 < s.n_picked; ++j) {
                s.picked[j] = s.picked[j + 1];
            }
            s.n_picked--;
            return;
        }
    }
    if (!s.an.columns[i].numeric || s.n_picked >= LOG_MAX_SERIES) {
        return;
    }
    s.picked[s.n_picked++] = i;
}

static void import_event(const touch_event_t *e)
{
    const gfx_rect_t btns[4] = {
        gfx_rect_make(16, IM_BTN_Y, 190, IM_BTN_H),
        gfx_rect_make(214, IM_BTN_Y, 190, IM_BTN_H),
        gfx_rect_make(412, IM_BTN_Y, 180, IM_BTN_H),
        gfx_rect_make(600, IM_BTN_Y, 184, IM_BTN_H),
    };

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (!tap_begin(e)) {
            break;
        }
        s.press_btn = hit_button(btns, 4, e->point.x, e->point.y);
        s.press_row = -1;
        s.dragged = false;
        s.pressing = false;
        if (s.press_btn < 0 && s.have_analysis &&
            gfx_rect_contains(IM_RIGHT, e->point.x, e->point.y)) {
            s.pressing = true;
            s.press_y0 = e->point.y;
            s.press_scroll0 = s.col_scroll;
            int rel = e->point.y - (IM_RIGHT.y + 30);
            int r = (rel >= 0) ? rel / IM_ROW_H : -1;
            if (r >= 0 && r < IM_ROWS) {
                s.press_row = s.col_scroll + r;
            }
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.pressing && tap_mine(e)) {
            int dy = e->point.y - s.press_y0;
            if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
                s.dragged = true;
                s.col_scroll = s.press_scroll0 - dy / IM_ROW_H;
                clamp_scroll(&s.col_scroll, s.an.n_columns, IM_ROWS);
                log_viewer_invalidate();
            }
        }
        break;

    case TOUCH_EVENT_UP: {
        if (!tap_mine(e)) {
            break;
        }
        s.tap_have = false;
        /* A press that slid off its button or its row is not a tap on it:
         * it ends here and nothing acts. */
        int btn = s.press_btn;
        if (btn >= 0 && !tap_on(e, btns[btn])) {
            btn = 4;
        }
        const bool row = s.pressing && !s.dragged && s.press_row >= 0
                         && tap_on(e, list_row_rect(IM_RIGHT, IM_ROW_H,
                                                    s.press_row
                                                        - s.col_scroll));
        switch (btn) {
        case 0:
            /* AUTO, then each candidate in turn. */
            s.delim_index++;
            if (s.delim_index >= (int)sizeof(log_csv_delimiters)) {
                s.delim_index = -1;
            }
            run_analysis();
            break;
        case 1:
            s.conv = (s.conv == LOG_CONV_AUTO)  ? LOG_CONV_DE
                     : (s.conv == LOG_CONV_DE) ? LOG_CONV_EN
                                                : LOG_CONV_AUTO;
            run_analysis();
            break;
        case 2:
            s.view = VIEW_BROWSE;
            break;
        case 3:
            if (s.have_analysis && s.n_picked > 0) {
                load_data();
            }
            break;
        case 4:
            break;
        default:
            if (row) {
                toggle_column(s.press_row);
            }
            break;
        }
        s.pressing = false;
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
    }
}

static void set_cursor_from_x(int x)
{
    if (!s.have_data || s.data.count <= 1) {
        return;
    }
    int rel = x - PV_X;
    if (rel < 0) {
        rel = 0;
    }
    if (rel > PV_W - 1) {
        rel = PV_W - 1;
    }
    s.cursor = s.win0 + (int)(((long)rel * (s.win_n - 1)) / (PV_W - 1));
}

/* Keep the cursor's sample on screen after an arrow press. */
static void follow_cursor(void)
{
    if (s.cursor < s.win0) {
        s.win0 = s.cursor;
        compute_spans();
    } else if (s.cursor >= s.win0 + s.win_n) {
        s.win0 = s.cursor - s.win_n + 1;
        compute_spans();
    }
}

static float pinch_dist(void)
{
    float d = (float)(s.pinch_x[1] - s.pinch_x[0]);
    return d < 0.0f ? -d : d;
}

static float pinch_mid(void)
{
    return 0.5f * (float)(s.pinch_x[0] + s.pinch_x[1]);
}

/* A second finger lands on the plot: the gesture starts from here. */
static void pinch_start(uint8_t id0, int16_t x0, uint8_t id1, int16_t x1)
{
    s.pinch = true;
    s.panning = false;
    s.pinch_id[0] = id0;
    s.pinch_id[1] = id1;
    s.pinch_x[0] = x0;
    s.pinch_x[1] = x1;
    s.pinch_d0 = pinch_dist();
    if (s.pinch_d0 < 20.0f) {
        s.pinch_d0 = 20.0f;
    }
    s.pinch_n0 = (float)s.win_n;
    s.pinch_s0 = (float)s.win0 +
                 (pinch_mid() - (float)PV_X) * s.pinch_n0 / (float)PV_W;
}

/* Both fingers' positions changed: set the window from them. */
static void pinch_apply(void)
{
    const int count = s.data.count;
    float d = pinch_dist();
    if (d < 20.0f) {
        d = 20.0f;
    }
    float n = s.pinch_n0 * s.pinch_d0 / d;
    const float min_n = (float)((count < PV_MIN_WIN) ? count : PV_MIN_WIN);
    if (n < min_n) {
        n = min_n;
    }
    if (n > (float)count) {
        n = (float)count;
    }
    float w0 = s.pinch_s0 - (pinch_mid() - (float)PV_X) * n / (float)PV_W;
    if (w0 < 0.0f) {
        w0 = 0.0f;
    }
    if (w0 + n > (float)count) {
        w0 = (float)count - n;
    }
    const int nw = (int)(n + 0.5f);
    int nw0 = (int)(w0 + 0.5f);
    if (nw0 + nw > count) {
        nw0 = count - nw;
    }
    if (nw != s.win_n || nw0 != s.win0) {
        s.win_n = nw;
        s.win0 = nw0;
        compute_spans();
    }
}

/* A drag pans only a view that is zoomed in, and only when the setting asks. */
static bool pan_enabled(void)
{
    return settings_get_bool(SET_PLOT_PAN) && s.have_data &&
           s.win_n < s.data.count;
}

/* The finger is at @p x: keep the sample that was under it where it began. */
static void pan_apply(int x)
{
    const int max0 = s.data.count - s.win_n;
    float w0 = (float)s.pan_win0 +
               (float)(s.pan_x0 - x) * (float)s.win_n / (float)PV_W;
    if (w0 < 0.0f) {
        w0 = 0.0f;
    }
    if (w0 > (float)max0) {
        w0 = (float)max0;
    }
    const int nw0 = (int)(w0 + 0.5f);
    if (nw0 != s.win0) {
        s.win0 = nw0;
        compute_spans();
    }
}

static void plot_event(const touch_event_t *e)
{
    const gfx_rect_t btns[3] = {
        gfx_rect_make(16, FOOT_Y, 170, FOOT_H),
        gfx_rect_make(196, FOOT_Y, 80, FOOT_H),
        gfx_rect_make(284, FOOT_Y, 80, FOOT_H),
    };

    /* Two fingers on the plot own it until one lifts; a third is ignored. */
    if (s.pinch) {
        int f = (e->point.id == s.pinch_id[0]) ? 0
              : (e->point.id == s.pinch_id[1]) ? 1 : -1;
        if (f < 0) {
            return;
        }
        if (e->type == TOUCH_EVENT_MOVE) {
            s.pinch_x[f] = e->point.x;
            pinch_apply();
            log_viewer_invalidate();
        } else if (e->type == TOUCH_EVENT_UP) {
            /* The view stays where the fingers left it. */
            s.pinch = false;
            s.pressing = false;
            log_viewer_invalidate();
        }
        return;
    }

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (s.pressing && e->point.id != s.press_id && s.have_data &&
            s.data.count > 1 &&
            gfx_rect_contains(PV_PANEL, e->point.x, e->point.y)) {
            pinch_start(s.press_id, s.press_x, e->point.id, e->point.x);
            log_viewer_invalidate();
            break;
        }
        if (s.pressing || s.press_btn >= 0) {
            break;   /* a second finger elsewhere presses nothing */
        }
        s.press_btn = hit_button(btns, 3, e->point.x, e->point.y);
        if (s.press_btn >= 0) {
            s.tap_have = false;
            (void)tap_begin(e);
        }
        if (s.press_btn < 0 && gfx_rect_contains(PV_PANEL, e->point.x, e->point.y)) {
            s.pressing = true;
            s.press_id = e->point.id;
            s.press_x = e->point.x;
            s.panning = false;
            s.pan_x0 = e->point.x;
            s.pan_cursor0 = s.cursor;
            set_cursor_from_x(e->point.x);
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.pressing && e->point.id == s.press_id) {
            s.press_x = e->point.x;
            if (!s.panning && pan_enabled()) {
                /* Judge the drag from where the finger touched down. */
                if (e->point.x - s.pan_x0 > PV_PAN_SLOP ||
                    s.pan_x0 - e->point.x > PV_PAN_SLOP) {
                    /* Anchored where the finger crossed the threshold, so
                     * the movement past it pans, also in one event. */
                    s.panning = true;
                    s.cursor = s.pan_cursor0;
                    s.pan_x0 = (int16_t)((e->point.x > s.pan_x0)
                                             ? s.pan_x0 + PV_PAN_SLOP
                                             : s.pan_x0 - PV_PAN_SLOP);
                    s.pan_win0 = s.win0;
                }
            }
            if (s.panning) {
                pan_apply(e->point.x);
            } else {
                set_cursor_from_x(e->point.x);
            }
            log_viewer_invalidate();
        }
        break;

    case TOUCH_EVENT_UP: {
        if (s.pressing && e->point.id != s.press_id) {
            break;
        }
        if (s.press_btn >= 0 && !tap_mine(e)) {
            break;   /* another finger's release ends no press */
        }
        /* A press that slid off its button is not a tap on it. */
        const int btn = (s.press_btn >= 0 && tap_on(e, btns[s.press_btn]))
                            ? s.press_btn : -1;
        if (btn == 0) {
            s.view = VIEW_IMPORT;
        } else if (btn == 1 && s.cursor > 0) {
            s.cursor--;
            follow_cursor();
        } else if (btn == 2 && s.have_data &&
                   s.cursor < s.data.count - 1) {
            s.cursor++;
            follow_cursor();
        }
        s.tap_have = false;
        s.pressing = false;
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
    }
}

/* ---------------------------------------------------------------- screen -- */

static void render(gfx_canvas_t *c, int buffer_index)
{
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.valid[buffer_index];
    }
    if (!stale) {
        return;
    }

    gfx_clear(c, UI_BG);

    switch (s.view) {
    case VIEW_IMPORT:
        render_import(c);
        break;
    case VIEW_PLOT:
        render_plot(c);
        break;
    case VIEW_BROWSE:
    default:
        render_browse(c);
        break;
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.valid[buffer_index] = true;
    }
}

static void event(const touch_event_t *e)
{
    if (e == NULL) {
        return;
    }
    /* A press that goes further than the slop is no tap, wherever it is
     * when it lifts. */
    if (e->type == TOUCH_EVENT_MOVE && tap_mine(e) && tap_far(e)) {
        s.tap_moved = true;
    }
    switch (s.view) {
    case VIEW_IMPORT:
        import_event(e);
        break;
    case VIEW_PLOT:
        plot_event(e);
        break;
    case VIEW_BROWSE:
    default:
        browse_event(e);
        break;
    }
}

/*
 * The panel could not hand over every touch event, so this screen's record
 * of the glass is stale.  Every view here acts on the release -- a button
 * refreshes the card, opens a file or changes an import setting, a row
 * selects or opens -- so a press left standing would let a later contact's
 * release do any of those without a press of its own.
 *
 * The DELETE question's press goes with them, and that one matters most: the
 * contact it is bound to may be among the events lost, and the controller
 * reuses track ids, so a later finger lifting off DELETE could carry it.  The
 * question itself stays open; it needs a fresh press to answer it.
 */
static void cancel(void)
{
    s.pressing  = false;
    s.press_btn = -1;
    s.press_row = -1;
    s.dragged   = false;
    s.tap_have  = false;
    s.q_down    = false;
    s.pinch     = false;
    s.panning   = false;
    s.bar_drag  = false;
    ui_tabs_cancel(&s.tabs);
    log_viewer_invalidate();
}

void log_viewer_set_tab(log_viewer_tab_t tab)
{
    if ((unsigned)tab >= (unsigned)LOG_TAB_COUNT) {
        return;
    }
    s.tab = tab;
    s.tabs.selected = tab_pos(tab);
    s.message[0] = '\0';
    read_page(LOG_PAGE_FROM, "");
}

static void reset(void)
{
    log_viewer_io_t io = s.io;
    drop_data();
    memset(&s, 0, sizeof(s));
    s.io = io;
    ui_tabs_init_text(&s.tabs, k_tab_text, LOG_TAB_COUNT, BR_TABS);
    s.tab = LOG_TAB_BENCH;
    s.tabs.selected = tab_pos(LOG_TAB_BENCH);
    s.open_tab = LOG_TAB_BENCH;
    s.sel = -1;
    s.cursor = -1;
    s.press_btn = -1;
    s.delim_index = -1;
    s.conv = LOG_CONV_AUTO;
}

static void enter(void)
{
    /* A question left open by leaving the screen is not asked again on the
     * way back in: the operator has moved on from it. */
    s.doomed[0] = '\0';
    /* A press held when the screen was left has no release coming. */
    s.tap_have  = false;
    s.pressing  = false;
    s.press_btn = -1;
    s.q_down    = false;
    s.bar_drag  = false;
    ui_tabs_cancel(&s.tabs);
    /*
     * The tab of the file the logger created last, when it has created one
     * since the screen last looked: the run just recorded is what the
     * operator comes here for.  Otherwise the tab stays as it was left,
     * BENCH after a start.
     */
    if (s.io.written != NULL) {
        log_viewer_tab_t tab = s.tab;
        const unsigned n = s.io.written(&tab, s.io.ctx);
        if (n != s.written_seen) {
            s.written_seen = n;
            if ((unsigned)tab < (unsigned)LOG_TAB_COUNT) {
                s.tab = tab;
                s.tabs.selected = tab_pos(tab);
            }
        }
    }
    /*
     * The card is read on every way in, not only the first after a start:
     * a run recorded while the screen was closed is the one the operator
     * comes here for, and a list kept from the last visit does not hold it.
     * The first page, as the empty card says to expect, with the newest run
     * at its top and chosen.  A view of a file stays where it was left; the
     * list behind it is the new one.  One walk of the card per way in, the
     * cost of one page.
     */
    log_viewer_refresh();
    log_viewer_invalidate();
}

/*
 * The home tag climbs one level, as each view's BACK: the plot to the
 * import, the import to the file list.  Over the DELETE question it is
 * CANCEL: the file stays.  On the file list it leaves the screen.  A press
 * held, a drag, a pan and a pinch end with the view they were made on.
 */
static bool back(void)
{
    cancel();
    if (s.doomed[0] != '\0') {
        s.doomed[0] = '\0';
        return true;
    }
    if (s.view == VIEW_PLOT) {
        s.view = VIEW_IMPORT;
        return true;
    }
    if (s.view == VIEW_IMPORT) {
        s.view = VIEW_BROWSE;
        return true;
    }
    return false;
}

static const ui_screen_t s_screen = {
    .title = "LOGS",
    .reset = reset,
    .enter = enter,
    .leave = NULL,
    .tick = NULL,
    .event = event,
    .cancel = cancel,
    .render = render,
    .back = back,
};

const ui_screen_t *log_viewer_screen(void)
{
    return &s_screen;
}
