/**
 * @file log_viewer_int.h
 * @brief The log viewer's private seam between log_viewer_screen.c, the state,
 *        the card's listing and paging and the screen's interface,
 *        log_viewer_load.c, a file's analysis, data and plot traces,
 *        log_viewer_draw.c, the drawing, and log_viewer_events.c, the touch
 *        events: the layout the four work on, the state, the plot's traces and
 *        the functions one calls in another.  Nothing outside shared/ui
 *        includes this header; the screen's interface is log_viewer_screen.h.
 *
 * Included last: its layout macros have short names (FOOT_Y, PV_W) and it
 * names the state s, which no header included after it may meet.  What
 * one file calls or reads in another carries the prefix lv_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "card_state.h"
#include "gfx.h"
#include "log_text.h"
#include "log_viewer_screen.h"
#include "touch_types.h"
#include "ui_screen.h"
#include "ui_tabs.h"
#include "ui_theme.h"

#define SCREEN_W UI_PANEL_W
/* The router owns the band and hands this screen a sub-canvas of what is
 * left, so the screen's own height is the body rather than the panel. */
#define SCREEN_H UI_BODY_H
#define MAX_FBS  3

/* ------------------------------------------------------------- browse ---- */

/**
 * @brief The faces of the 1024 x 600 type scale this screen draws in.
 *
 * LV_FONT_LABEL (10 x 20) for every label, button, row, value, warning and
 * the cursor line; LV_FONT_BODY (12 x 24) for prose read at a distance: the
 * empty list's lines, the DELETE question, the file messages in the footer
 * and the boxes that say why nothing shows; LV_FONT_FINE (8 x 16) for the
 * plot's data figures: the time axis and the series' ranges.
 */
#define LV_FONT_LABEL (&gfx_font_10x20)
#define LV_FONT_BODY  UI_FONT_BODY   /**< 12 x 24: prose. */
#define LV_FONT_FINE  UI_FONT_FINE   /**< 8 x 16: the plot's figures. */

/**
 * @brief The screen's columns: UI_MARGIN (8 px) in from either edge, LV_W
 *        (1008 px) between.
 */
#define LV_L  UI_MARGIN
#define LV_R  (SCREEN_W - UI_MARGIN)  /**< The right edge, x 1016. */
#define LV_W  (LV_R - LV_L)           /**< The width between, 1008 px. */

/**
 * @brief Each view's footer row of buttons: FOOT_H (52 px) tall, ending
 *        4 px above the router's alert band.
 *
 * The alert band covers the bottom UI_ALERT_H (44) px of the 540 px body
 * while an alert shows and takes the touches there: nothing that acts sits
 * under it.  The footer is body y 440 to 491; the band starts at 496.
 * FOOT_GAP is the room between what a view shows above the footer and the
 * footer.
 */
#define FOOT_H    52
/** @brief The footer's top. */
#define FOOT_Y    (SCREEN_H - UI_ALERT_H - 4 - FOOT_H)
#define FOOT_GAP  8   /**< Above the footer, px. */

/**
 * @brief The header row, body y 4 to 52: ROOT / USER / BENCH, the folder
 *        and the card's name, and PREV and NEXT while the card holds more
 *        than one page.
 *
 * PREV and NEXT are 140 x 48 px over DELETE and OPEN.  The tabs are 110 x
 * 40 px, 4 px apart, centred on the row; ui_tabs draws its trough 3 px
 * round them.  The folder follows from BR_HEAD_X and the card's name ends
 * the row, right-aligned: at BR_HEAD_END with PREV and NEXT showing, at
 * BR_HEAD_FAR without them.  BR_HEAD_X to BR_HEAD_END is 352 px, 35 cells
 * of the 10 x 20 face: an 11-character volume label and a 17-character
 * folder with the 16 px between them.
 */
#define BR_PG_Y     4
#define BR_PG_H     UI_TAP_MIN                /**< PREV and NEXT, 48 px. */
#define BR_PG_W     140                       /**< Their width. */
#define BR_TAB_W    110                       /**< A folder tab's width. */
#define BR_TAB_H    40                        /**< Its height. */
/** @brief ROOT, USER, BENCH. */
#define BR_TABS     gfx_rect_make(LV_L + 3, BR_PG_Y + 4, \
                                  3 * BR_TAB_W + 2 * 4, BR_TAB_H)
#define BR_HEAD_X   368                       /**< The folder's left. */
#define BR_HEAD_TY  (BR_PG_Y + (BR_PG_H - 20) / 2)  /**< Its text's top. */
#define BR_PREV     gfx_rect_make(LV_R - 2 * BR_PG_W - 8, BR_PG_Y, BR_PG_W, \
                                  BR_PG_H)  /**< PREV, over DELETE. */
/** @brief NEXT, over OPEN. */
#define BR_NEXT     gfx_rect_make(LV_R - BR_PG_W, BR_PG_Y, BR_PG_W, BR_PG_H)
#define BR_HEAD_END (LV_R - 2 * BR_PG_W - 8 - 8)  /**< 8 px short of PREV. */
#define BR_HEAD_FAR LV_R                      /**< The row's right end. */

/**
 * @brief The file list under the header row, BR_ROWS rows at a pitch of
 *        BR_ROW_H under its 30 px heading.
 *
 * A row is drawn BR_ROW_H - 4 (44) px tall; the pitch, 48 px, is what a
 * finger hits, UI_TAP_MIN.  The list is 372 px tall: seven rows under the
 * heading take 366 px of it.  A row shows the file's name and, right-
 * aligned, its size.
 */
#define BR_LIST_Y 60
/** @brief The list's panel. */
#define BR_LIST   gfx_rect_make(LV_L, BR_LIST_Y, LV_W, \
                                FOOT_Y - FOOT_GAP - BR_LIST_Y)
#define BR_ROW_H  48  /**< A row's pitch, UI_TAP_MIN. */
#define BR_ROWS   7        /**< The rows the list shows at once. */

/**
 * @brief The scroll bar along the list's right edge, BR_BAR_W px wide:
 *        8.4 mm on the panel at the 0.150 mm a pixel ui_layout.h assumes
 *        (not measured).
 *
 * The track runs the height of the rows, and the thumb's share of it is
 * the rows' share of the page.  A row runs from the list's left inset to
 * 8 px short of the bar.
 */
#define BR_BAR_W  56
/** @brief The track. */
#define BR_BAR    gfx_rect_make(LV_R - 8 - BR_BAR_W, BR_LIST_Y + 30, \
                                BR_BAR_W, BR_ROWS * BR_ROW_H - 4)
#define BR_ROW_X  (LV_L + 8)                          /**< A row's left. */
#define BR_ROW_W  (LV_R - 8 - BR_BAR_W - 8 - BR_ROW_X) /**< Its width. */
/** @brief The size's box at a row's right end: LOG_DIR's 15 cells. */
#define BR_SIZE_W 160

/**
 * @brief The footer's buttons, 140 x 52 px, in the order browse_event()
 *        hit-tests them: DELETE and OPEN under PREV and NEXT.
 */
#define BR_RESCAN gfx_rect_make(LV_L, FOOT_Y, BR_PG_W, FOOT_H)
/** @brief WI-FI. */
#define BR_WIFI   gfx_rect_make(LV_L + BR_PG_W + 8, FOOT_Y, BR_PG_W, FOOT_H)
/** @brief OPEN. */
#define BR_OPEN   gfx_rect_make(LV_R - BR_PG_W, FOOT_Y, BR_PG_W, FOOT_H)
#define BR_DELETE gfx_rect_make(LV_R - 2 * BR_PG_W - 8, FOOT_Y, BR_PG_W, \
                                FOOT_H)  /**< DELETE. */
/**
 * @brief A message's left edge in the footer: two lines of 33 cells of the
 *        12 x 24 face to DELETE, the 28 a message about a file declares and
 *        the file's name.  BR_MSG_LH is a line's pitch.
 */
#define BR_MSG_X  (LV_L + 2 * BR_PG_W + 8 + 16)
#define BR_MSG_LH 24  /**< A line's pitch. */

/** @brief The box an empty list shows in its place: two lines of 64 cells
 *         of the 12 x 24 face inside 24 px margins. */
#define BR_EMPTY  gfx_rect_make(104, 150, 816, 160)

/**
 * @brief The TEXT view: a report's rows in the browse list's panel,
 *        TX_ROW_H px apart under its 30 px heading, beside its scroll bar.
 *
 * A row is TX_COLS cells of the 10 x 20 face, 920 px, from 8 px into the
 * browse row's box to 8 px short of the bar.  The thumb is BR_BAR_MIN px
 * at least: a report of 400 rows would make it 12.  BACK stands where the
 * plot's BACK does.
 */
#define TX_ROW_H  22  /**< the pitch of a text row, px */
#define TX_ROWS   15  /**< text rows on screen */
#define TX_COLS   92  /**< cells a text row holds */
#define TX_ROW_X  (BR_ROW_X + 8)  /**< a text row's left edge */
/** @brief BACK under the text, 140 x 52 px. */
#define TX_BACK   gfx_rect_make(LV_L, FOOT_Y, 140, FOOT_H)
#define BR_BAR_MIN 32  /**< the scroll bar thumb's least height, px */

/**
 * @brief The DELETE question, drawn in place of the list and the footer:
 *        a box of 700 x 280 px, CANCEL and DELETE 300 x 56 px at its foot.
 */
#define DQ_BOX    gfx_rect_make(162, 100, 700, 280)
#define DQ_CANCEL gfx_rect_make(186, 304, 300, 56)  /**< CANCEL. */
#define DQ_DELETE gfx_rect_make(538, 304, 300, 56)  /**< DELETE. */

/* ------------------------------------------------------------- import ---- */

/**
 * @brief The import: the file's name over two panels, DETECTED 400 px wide
 *        and the column list 600 px, and four footer buttons of 246 px.
 *
 * DETECTED holds a 16-character key and a 20-cell value on one row of the
 * 10 x 20 face, and its warnings break into lines of 36 cells.  The column
 * list shows IM_ROWS rows at a pitch of IM_ROW_H, drawn 4 px shorter.
 */
#define IM_TOP    44
/** @brief DETECTED. */
#define IM_LEFT   gfx_rect_make(LV_L, IM_TOP, 400, FOOT_Y - FOOT_GAP - IM_TOP)
/** @brief The column list. */
#define IM_RIGHT  gfx_rect_make(LV_L + 408, IM_TOP, LV_W - 408, \
                                FOOT_Y - FOOT_GAP - IM_TOP)
#define IM_ROW_H  48  /**< A column row's pitch. */
#define IM_ROWS   7        /**< (388 - 30) / 48: seven rows. */
#define IM_KEY_LH 24       /**< DETECTED's key rows' pitch. */
#define IM_WARN_LH 22      /**< A warning line's pitch. */
#define IM_BTN_Y  FOOT_Y  /**< The footer's top. */
#define IM_BTN_H  FOOT_H  /**< Its height. */
#define IM_BTN_W  ((LV_W - 3 * 8) / 4)   /**< 246 px. */
/** @brief The import's footer button @p i: 0 SEP, 1 NUM, 2 BACK, 3 PLOT. */
#define IM_BTN(i) gfx_rect_make(LV_L + (i) * (IM_BTN_W + 8), IM_BTN_Y, \
                                IM_BTN_W, IM_BTN_H)
/** @brief The box a file that cannot be read shows: 74 cells of the
 *         12 x 24 face inside 24 px margins. */
#define IM_FAIL   gfx_rect_make(44, 150, 936, 160)

/* --------------------------------------------------------------- plot ---- */

/**
 * @brief The plot: the summary line, the series' chips, the plot's panel
 *        and under it a 16 px row of time labels 4 px down, above the
 *        footer.
 *
 * The chips are CHIP_W x CHIP_H (240 x 44) px, CHIP_GAP apart, from y
 * CHIP_Y.  The plot is PV_W x PV_H (992 x 316) px inside its panel; PV_BAND
 * rows of it are drawn at a time.
 */
#define PV_SUM_Y   4  /**< The summary line's top. */
#define CHIP_Y     28  /**< The chips' top. */
#define CHIP_H     44  /**< A chip's height. */
#define CHIP_X     LV_L  /**< The first chip's left. */
#define CHIP_W     240  /**< A chip's width. */
#define CHIP_GAP   16  /**< Between two chips. */
#define PV_TOP     (CHIP_Y + CHIP_H + 8)  /**< The plot's panel's top. */
#define PV_PANEL_H (FOOT_Y - FOOT_GAP - 16 - 4 - PV_TOP)  /**< Its height. */
/** @brief The panel. */
#define PV_PANEL   gfx_rect_make(LV_L, PV_TOP, LV_W, PV_PANEL_H)
#define PV_X       (LV_L + 8)  /**< The plot's left column. */
#define PV_Y       (PV_TOP + 8)  /**< Its top row. */
#define PV_W       (LV_W - 16)  /**< Its width, 992 px. */
#define PV_H       (PV_PANEL_H - 16)  /**< Its height, 316 px. */
#define PV_BAND    10  /**< Rows drawn at a time. */
#define PV_DIV_X   6  /**< Grid columns. */
#define PV_DIV_Y   4  /**< Grid rows. */
/** @brief The bar under the plot that shows where a zoomed view sits. */
#define PV_POS_Y   (PV_Y + PV_H + 2)
#define PV_POS_H   4  /**< Its height. */
/** @brief The narrowest window two fingers can zoom to, in samples. */
#define PV_MIN_WIN 8
/** @brief With SET_PLOT_PAN on, a finger that moves further than this many
 *         pixels from where it touched down pans the zoomed view instead of
 *         selecting. */
#define PV_PAN_SLOP 10
/** @brief A pixel column with no number in it: drawn as nothing. */
#define PV_EMPTY   (-1)
/**
 * @brief The plot's footer: BACK, then < and >, 52 px tall; the cursor's
 *        line in 10 x 20 from PV_LINE_X.
 */
#define PV_BACK    gfx_rect_make(LV_L, FOOT_Y, 140, FOOT_H)
#define PV_PREV    gfx_rect_make(LV_L + 148, FOOT_Y, 96, FOOT_H)  /**< <. */
#define PV_NEXT    gfx_rect_make(LV_L + 252, FOOT_Y, 96, FOOT_H)  /**< >. */
#define PV_LINE_X  (LV_L + 364)  /**< The cursor's line. */
/** @brief The box the plot shows with nothing loaded: 64 cells of the
 *         12 x 24 face inside 24 px margins. */
#define PV_NONE    gfx_rect_make(104, 150, 816, 120)

_Static_assert(FOOT_Y + FOOT_H + 4 <= SCREEN_H - UI_ALERT_H,
               "the footer ends 4 px above the alert band");
_Static_assert(FOOT_H >= UI_TAP_MIN, "the footer's buttons are a finger tall");
_Static_assert(BR_ROW_H >= UI_TAP_MIN, "a file row is a finger tall");
_Static_assert(IM_ROW_H >= UI_TAP_MIN, "a column row is a finger tall");
_Static_assert(30 + BR_ROWS * BR_ROW_H <= FOOT_Y - FOOT_GAP - BR_LIST_Y,
               "the file list's rows fit its panel");
_Static_assert(BR_PG_Y + BR_PG_H + 8 <= BR_LIST_Y,
               "PREV and NEXT end 8 px above the list");
_Static_assert(LV_L + 3 + 3 * BR_TAB_W + 2 * 4 + 3 + 16 <= BR_HEAD_X,
               "the tabs end before the header's text");
_Static_assert(BR_HEAD_X + (11 + 17) * 10 + 16 <= BR_HEAD_END,
               "the header's text ends at PREV");
_Static_assert(BR_MSG_X + 33 * 12 + 8 <= LV_R - 2 * BR_PG_W - 8,
               "a footer message's 33 cells stand between WI-FI and DELETE");
_Static_assert(BR_SIZE_W >= 15 * 10 + 8,
               "a row's size box holds LOG_DIR's 15 cells");
_Static_assert((BR_ROWS * BR_ROW_H - 4) * BR_ROWS / LOG_VIEWER_MAX_FILES >= 32,
               "the scroll bar's thumb is 32 px or more on a full page");
_Static_assert(TX_ROWS * TX_ROW_H <= BR_ROWS * BR_ROW_H - 4,
               "the text's rows fit beside the scroll bar");
_Static_assert(TX_COLS * 10 <= BR_ROW_W - 8,
               "a text row fits the browse row's box");
_Static_assert(30 + IM_ROWS * IM_ROW_H <= FOOT_Y - FOOT_GAP - IM_TOP,
               "the column list's rows fit its panel");
_Static_assert(PV_POS_Y + PV_POS_H <= PV_TOP + PV_PANEL_H,
               "the position bar is inside the plot's panel");
_Static_assert(CHIP_X + 4 * CHIP_W + 3 * CHIP_GAP <= LV_R,
               "the four chips stand in the screen's width");

/** @brief The view on screen. */
typedef enum { VIEW_BROWSE = 0, VIEW_IMPORT, VIEW_PLOT, VIEW_TEXT } view_t;

typedef struct {
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

    log_text_t text;      /**< the report the TEXT view shows */
    int text_scroll;      /**< the TEXT view's first row on screen */

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
} lv_state_t;

/* Defined in log_viewer_screen.c. */
extern lv_state_t lv_s;
#define s lv_s

/* --- log_viewer_screen.c: the series' colours, the plot's traces, the
 *     tabs, the card's listing and paging -------------------------------- */

extern const ui_color_id_t lv_series_color[LOG_MAX_SERIES];
/* Filled by lv_compute_spans() (log_viewer_load.c). */
extern int16_t lv_span[LOG_MAX_SERIES][PV_W][2];
extern gfx_color_t lv_band[PV_W * PV_BAND];
extern float lv_lo[LOG_MAX_SERIES];
extern float lv_hi[LOG_MAX_SERIES];
extern const log_viewer_tab_t lv_tab_at[LOG_TAB_COUNT];

void lv_point(log_viewer_tab_t tab);
const char *lv_volume_now(void);
unsigned lv_mounts_now(void);
const char *lv_page_edge(int sign);
void lv_read_page(log_page_dir_t dir, const char *key);
void lv_rescan(void);
bool lv_paged(void);
bool lv_can_prev(void);
bool lv_can_next(void);
void lv_drop_data(void);
bool lv_can_delete(void);
void lv_bar_thumb(int *y, int *h);
int lv_bar_scroll_at(int top);

/**
 * @brief The thumb of a scroll bar on BR_BAR: the whole track while every
 *        row shows.
 *
 * @param total    Rows of the list.
 * @param visible  Rows on screen.
 * @param scroll   The first row on screen.
 * @param y        Takes the thumb's top.
 * @param h        Takes its height, BR_BAR_MIN px at least.
 */
void lv_bar_thumb_of(int total, int visible, int scroll, int *y, int *h);

/**
 * @brief The scroll that puts a thumb's top on BR_BAR nearest @p top.
 *
 * @param total    Rows of the list.
 * @param visible  Rows on screen.
 * @param top      The thumb's top the finger asks for.
 * @return The first row on screen, 0 to @p total - @p visible.
 */
int lv_bar_scroll_of(int total, int visible, int top);

/**
 * @brief Rows the TEXT view scrolls through: the text's, and the line that
 *        says it was cut.
 *
 * @return The rows.
 */
int lv_text_total(void);

/**
 * @brief Leave the TEXT view for the list and free the report's text.
 *
 * The text is up to 32 KiB of the file; the list does not need it, and OPEN
 * reads it again.
 */
void lv_close_text(void);

/* --- log_viewer_load.c -------------------------------------------------- */

/* A format, so the compiler checks every call against its arguments --
 * tools/check_formats.py sees the translated ones the same way. */
void lv_set_message(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
const char *lv_delim_text(char delimiter);
void lv_run_analysis(void);
void lv_compute_spans(void);
void lv_load_data(void);
void lv_open_selected(void);

/* --- log_viewer_draw.c -------------------------------------------------- */

void lv_render(gfx_canvas_t *c, int buffer_index);

/* --- log_viewer_events.c ------------------------------------------------ */

void lv_event(const touch_event_t *e);
