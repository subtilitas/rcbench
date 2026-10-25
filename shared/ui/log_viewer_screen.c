/**
 * @file log_viewer_screen.c
 * @brief The log viewer: browse the card, review the import, plot the file.
 *
 * Three views behind one tile: a file browser, an import view that shows
 * what the CSV (comma-separated values) reader made of the file, and the
 * plot.  The reader detects the delimiter, the decimal convention and the
 * time column (see shared/logfile); the import view shows the detection and
 * its evidence before anything is plotted, and offers overrides.
 *
 * lv_compute_spans() turns the chosen columns into scaled traces when the
 * data changes, so the plot render walks prepared spans rather than
 * reparsing on every frame.
 *
 * This file holds the state, the card's listing and paging and the
 * screen's interface; log_viewer_load.c opens a file, log_viewer_draw.c
 * draws the four views, log_viewer_events.c takes their touch events, and
 * log_viewer_int.h is what the four share.
 *
 * DELETE in the browse view asks before it acts: a second panel names the
 * file and its size, and only its own DELETE, pressed and released on that
 * button, calls the I/O's remove().
 *
 * The browse view lists one folder at a time, chosen by three tabs in its
 * header row: ROOT, USER and BENCH, in that order (log_viewer_tab_t holds
 * the ids; the tabs' order on screen is lv_tab_at).
 *
 * A report, ATESTnnn.TXT or LIMITnnn.TXT, opens in a fourth view as text:
 * the browse list's panel, scroll bar and BACK, with the file's rows in
 * place of the files (log_text.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_viewer_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "card_tree.h"
#include "log_name.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"

#include "log_viewer_int.h"

const ui_color_id_t lv_series_color[LOG_MAX_SERIES] = {
    UI_C_VOLT, UI_C_CURR, UI_C_POWER, UI_C_RPM,
};

/* The screen's state; log_viewer_int.h names it s. */
lv_state_t lv_s;

/* Column spans, in screen y, one pair per pixel column per series.  Computed
 * when the data changes: recomputing them per redraw would make the cursor
 * lag, and recomputing them per band would make it crawl. */
int16_t lv_span[LOG_MAX_SERIES][PV_W][2];
gfx_color_t lv_band[PV_W * PV_BAND];
float lv_lo[LOG_MAX_SERIES];
float lv_hi[LOG_MAX_SERIES];

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

const log_text_t *log_viewer_text(void)
{
    return (s.text.buf != NULL) ? &s.text : NULL;
}

int log_viewer_text_first(void)
{
    return (s.text.buf != NULL) ? s.text_scroll : 0;
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
    return ui_theme_color(lv_series_color[k]);
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
    /* One decimal below 1000, then the width decides: a text wider than
     * the box drops the decimal. */
    int decimals = (fabsf(lo_f) >= 999.95f || fabsf(hi_f) >= 999.95f) ? 0 : 1;
    for (;;) {
        char a[16];
        char b[16];
        ui_fmt(a, sizeof(a), lo_f, decimals);
        ui_fmt(b, sizeof(b), hi_f, decimals);
        snprintf(out, n, "%s..%s%s", a, b, has_unit ? unit : "");
        if (gfx_text_cells(out) <= (int)LOG_RANGE_CELLS || decimals == 0) {
            return;
        }
        decimals = 0;
    }
}

int log_viewer_cursor(void)
{
    return s.have_data ? s.cursor : -1;
}

const char *log_viewer_message(void)
{
    static char line[LV_MSG_MAX];
    return lv_message(line, sizeof(line));
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
void lv_point(log_viewer_tab_t tab)
{
    if (s.io.folder != NULL) {
        s.io.folder(log_viewer_tab_dir(tab), s.io.ctx);
    }
}

/* The card's name as the I/O reports it now, or "". */
const char *lv_volume_now(void)
{
    const char *vol = (s.io.volume != NULL) ? s.io.volume(s.io.ctx) : NULL;
    return (vol != NULL) ? vol : "";
}

/* The I/O's mount number now, 0 without one. */
unsigned lv_mounts_now(void)
{
    return (s.io.mounts != NULL) ? s.io.mounts(s.io.ctx) : 0u;
}

/* Whether the volume is another than the one the page was read from. */
static bool volume_changed(void)
{
    return strcmp(s.volume, lv_volume_now()) != 0 || s.mounts != lv_mounts_now();
}

/*
 * The entry on the page that log_name_rank() puts first (@p sign 1) or last
 * (-1), or "" with an empty page: the edges PREV, NEXT and RESCAN name.
 */
const char *lv_page_edge(int sign)
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
    lv_point(s.tab);
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

/**
 * @brief Read the page @p dir and @p key name, and select its newest run.
 *
 * Pages are cut from the top, LOG_VIEWER_MAX_FILES at a time.  A page
 * before the key that starts less than a page from the top is the first
 * page, read again from the top: a run recorded since the key was read
 * stands ahead of it, and so does nothing when everything ahead of the key
 * has gone.  A page that comes back empty from a card that is not, is a
 * page past the end: the card has fewer entries than when the key was
 * read, after a DELETE or behind the screen's back.  The last page is read
 * instead, as the pages from the first one cut it: what is left of the
 * count after whole pages.  The newest is the highest number on the page,
 * a report included.
 *
 * @param dir  Which side of @p key the page is on.
 * @param key  The name at the page's edge; "" or NULL for the top.
 */
void lv_read_page(log_page_dir_t dir, const char *key)
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
    snprintf(s.volume, sizeof(s.volume), "%s", lv_volume_now());
    s.mounts = lv_mounts_now();
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
        snprintf(s.volume, sizeof(s.volume), "%s", lv_volume_now());
        s.mounts = lv_mounts_now();
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
     * the list opens on it: the highest run number on the page, a limit
     * run's report included.  Runs rank ahead of everything the panel lists,
     * so it is the top row and the scroll stays at 0.  A page with no
     * numbered run keeps no selection.
     */
    int best = -1;
    for (int i = 0; i < s.n_files; ++i) {
        if (!s.files[i].is_dir &&
            log_name_number(s.files[i].name) >= 0 &&
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
    lv_read_page(LOG_PAGE_FROM, "");
}

/*
 * Read the page on screen again.  The first page is read from the top, so a
 * run recorded since shows; any other from its first entry, and an entry
 * deleted since still names its place.  Another card's name in the header
 * is another card, read from its first page.
 */
void lv_rescan(void)
{
    if (s.first == 0 || s.listed != 1 || volume_changed()) {
        lv_read_page(LOG_PAGE_FROM, "");
    } else {
        lv_read_page(LOG_PAGE_FROM, lv_page_edge(1));
    }
}

/* Whether the card holds more than the page shows, either side of it. */
bool lv_paged(void)
{
    return s.listed == 1 && s.n_files > 0
           && (s.first > 0 || s.first + s.n_files < s.n_card);
}

bool lv_can_prev(void)
{
    return lv_paged() && s.first > 0;
}

bool lv_can_next(void)
{
    return lv_paged() && s.first + s.n_files < s.n_card;
}

void log_viewer_browse_title(char *out, size_t n)
{
    if (out == NULL || n == 0) {
        return;
    }
    if (lv_paged()) {
        snprintf(out, n, TR(LOG_N_OF_FILES), s.first + 1,
                 s.first + s.n_files, s.n_card);
    } else {
        snprintf(out, n, "%s", TR(LOG_FILES));
    }
}

void lv_drop_data(void)
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

/* DELETE is offered for a file the list has selected, and only when the I/O
 * can delete at all. */
bool lv_can_delete(void)
{
    return s.io.remove != NULL && s.sel >= 0 && s.sel < s.n_files
           && !s.files[s.sel].is_dir;
}

void lv_bar_thumb_of(int total, int visible, int scroll, int *y, int *h)
{
    const gfx_rect_t t = BR_BAR;
    if (total <= visible) {
        *y = t.y;
        *h = t.h;
        return;
    }
    int th = t.h * visible / total;
    if (th < BR_BAR_MIN) {
        th = BR_BAR_MIN;
    }
    const int max = total - visible;
    *y = t.y + (t.h - th) * scroll / max;
    *h = th;
}

/**
 * @brief The browse list's thumb (lv_bar_thumb_of()).
 *
 * @param y  Takes the thumb's top.
 * @param h  Takes its height.
 */
void lv_bar_thumb(int *y, int *h)
{
    lv_bar_thumb_of(s.n_files, BR_ROWS, s.scroll, y, h);
}

int lv_bar_scroll_of(int total, int visible, int top)
{
    int ty;
    int th;
    lv_bar_thumb_of(total, visible, 0, &ty, &th);
    const int travel = BR_BAR.h - th;
    const int max = total - visible;
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

/**
 * @brief The browse list's scroll for a thumb at @p top (lv_bar_scroll_of()).
 *
 * @param top  The thumb's top the finger asks for.
 * @return The first row on screen.
 */
int lv_bar_scroll_at(int top)
{
    return lv_bar_scroll_of(s.n_files, BR_ROWS, top);
}

int lv_text_total(void)
{
    return s.text.rows + (s.text.cut ? 1 : 0);
}

void lv_close_text(void)
{
    log_text_free(&s.text);
    s.text_scroll = 0;
    s.view = VIEW_BROWSE;
}

/* The tabs as the header row shows them, left to right: the id of each. */
const log_viewer_tab_t lv_tab_at[LOG_TAB_COUNT] = {
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
        if (lv_tab_at[i] == tab) {
            return i;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- screen -- */

/**
 * @brief The panel could not hand over every touch event, so this screen's record
 *        of the glass is stale.  Every view here acts on the release -- a button
 *        refreshes the card, opens a file or changes an import setting, a row
 *        selects or opens -- so a press left standing would let a later contact's
 *        release do any of those without a press of its own.
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
    lv_quiet();
    lv_read_page(LOG_PAGE_FROM, "");
}

log_viewer_show_t log_viewer_show(log_viewer_tab_t tab, const char *name,
                                  unsigned mount)
{
    if ((unsigned)tab >= (unsigned)LOG_TAB_COUNT || name == NULL
        || name[0] == '\0') {
        return LOG_SHOW_NOT_LISTED;
    }
    s.tab = tab;
    s.tabs.selected = tab_pos(tab);
    lv_quiet();
    /* The list, not a view left open before: an OPEN that refuses the
     * file leaves the list with the reason, never another file's data. */
    s.view = VIEW_BROWSE;
    /* The page that starts at the name: the file is its first row when
     * the list holds it. */
    lv_read_page(LOG_PAGE_FROM, name);
    if (s.mounts != mount) {
        /* The same name on the card put in since FILES listed it is
         * another file: it is not opened. */
        return LOG_SHOW_OTHER_CARD;
    }
    for (int i = 0; i < s.n_files; ++i) {
        if (strcmp(s.files[i].name, name) == 0) {
            s.sel = i;
            s.scroll = (i < BR_ROWS) ? 0 : i - BR_ROWS + 1;
            lv_open_selected();
            return LOG_SHOW_OPEN;
        }
    }
    return LOG_SHOW_NOT_LISTED;
}

/**
 * @brief The screen as a start leaves it: the BENCH tab, nothing open,
 *        nothing selected.
 *
 * The I/O stays; the data, the analysis and a report's text are freed.
 */
static void reset(void)
{
    log_viewer_io_t io = s.io;
    lv_drop_data();
    log_text_free(&s.text);
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

/**
 * @brief The home tag: one level up, as each view's BACK.
 *
 * The plot to the import, the import and a report's text to the file list;
 * the text is freed (lv_close_text()).  Over the DELETE question it is
 * CANCEL: the file stays.  On the file list it leaves the screen.  A press
 * held, a drag, a pan and a pinch end with the view they were made on.
 *
 * @return True when it climbed inside the screen, false to leave it.
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
    if (s.view == VIEW_TEXT) {
        lv_close_text();
        return true;
    }
    if (s.view == VIEW_IMPORT) {
        s.view = VIEW_BROWSE;
        return true;
    }
    return false;
}

/**
 * @brief Leaving LOGS: a report's text is freed, and LOGS opens on the list
 *        next time.  The import and the plot stay as they were left.
 */
static void leave(void)
{
    if (s.view == VIEW_TEXT) {
        lv_close_text();
    }
}

static const ui_screen_t s_screen = {
    .title = "LOGS",
    .reset = reset,
    .enter = enter,
    .leave = leave,
    .tick = NULL,
    .event = lv_event,
    .cancel = cancel,
    .render = lv_render,
    .back = back,
};

const ui_screen_t *log_viewer_screen(void)
{
    return &s_screen;
}
