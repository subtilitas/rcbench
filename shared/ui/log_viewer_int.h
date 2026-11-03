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
#include "log_viewer_screen.h"
#include "logs_strip.h"
#include "touch_types.h"
#include "ui_screen.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"

/** @brief The screen's width, UI_PANEL_W px. */
#define SCREEN_W UI_PANEL_W
/* The router owns the band and hands this screen a sub-canvas of what is
 * left, so the screen's own height is the body rather than the panel. */
/** @brief The screen's height, UI_BODY_H px: the body under the band. */
#define SCREEN_H UI_BODY_H
/** @brief Frame buffers the router renders into. */
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
 * @brief The screen's columns: UI_MARGIN (8px) in from either edge, LV_W
 *        (1008px) between.
 */
#define LV_L  UI_MARGIN
#define LV_R  (SCREEN_W - UI_MARGIN)  /**< The right edge, x 1016. */
#define LV_W  (LV_R - LV_L)           /**< The width between, 1008px. */

/**
 * @brief Each view's footer row of buttons: FOOT_H (52px) tall, ending
 *        4px above the router's alert band.
 *
 * The alert band covers the bottom UI_ALERT_H (44) px of the 540px body
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
 * @brief The header row under the tile's strip (logs_strip.h), body y 52 to
 *        100: ROOT / USER / BENCH, the folder and the card's name, and PREV
 *        and NEXT while the card holds more than one page.
 *
 * PREV and NEXT are 140 x 48px over DELETE and OPEN.  The tabs are 110 x
 * 40px, 4px apart, centred on the row; ui_tabs draws its trough 3px
 * round them.  The folder follows from BR_HEAD_X and the card's name ends
 * the row, right-aligned: at BR_HEAD_END with PREV and NEXT showing, at
 * BR_HEAD_FAR without them.  BR_HEAD_X to BR_HEAD_END is 352px, 35 cells
 * of the 10 x 20 face: an 11-character volume label and a 17-character
 * folder with the 16px between them.
 */
#define BR_PG_Y     52
#define BR_PG_H     UI_TAP_MIN                /**< PREV and NEXT, 48px. */
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
#define BR_HEAD_END (LV_R - 2 * BR_PG_W - 8 - 8)  /**< 8px short of PREV. */
#define BR_HEAD_FAR LV_R                      /**< The row's right end. */

/**
 * @brief The file list under the header row, BR_ROWS rows at a pitch of
 *        BR_ROW_H under its 30px heading.
 *
 * A row is drawn BR_ROW_H - 4 (44) px tall; the pitch, 48px, is what a
 * finger hits, UI_TAP_MIN.  The list is 324px tall: six rows under the
 * heading take 318px of it.  A row shows the file's name, its date and,
 * right-aligned, its size.
 */
#define BR_LIST_Y 108
/** @brief The list's panel. */
#define BR_LIST   gfx_rect_make(LV_L, BR_LIST_Y, LV_W, \
                                FOOT_Y - FOOT_GAP - BR_LIST_Y)
#define BR_ROW_H  48  /**< A row's pitch, UI_TAP_MIN. */
#define BR_ROWS   6        /**< The rows the list shows at once. */

/**
 * @brief The scroll bar along the list's right edge, BR_BAR_W px wide:
 *        8.4mm on the panel at the 0.150mm a pixel ui_layout.h assumes
 *        (not measured).
 *
 * The track runs the height of the rows, and the thumb's share of it is
 * the rows' share of the page.  A row runs from the list's left inset to
 * 8px short of the bar.
 */
#define BR_BAR_W  56
/** @brief The track. */
#define BR_BAR    gfx_rect_make(LV_R - 8 - BR_BAR_W, BR_LIST_Y + 30, \
                                BR_BAR_W, BR_ROWS * BR_ROW_H - 4)
#define BR_ROW_X  (LV_L + 8)                          /**< A row's left. */
#define BR_ROW_W  (LV_R - 8 - BR_BAR_W - 8 - BR_ROW_X) /**< Its width. */
/**
 * @brief A row's three columns of the 10 x 20 face, as offsets from the
 *        row's left: the name from 16px, the date in a box of 16 cells
 *        ("2026-10-26 09:41"), and the size right-aligned in a box of 10
 *        cells (LOG_DIR's) ending 16px short of the row's end.  16px stand
 *        between the columns, so the name takes 604px, LOG_VIEWER_NAME_CELLS
 *        (60) cells.
 */
#define BR_SIZE_W  100
#define BR_DATE_W  160                                    /**< 16 cells. */
#define BR_NAME_IN 16                                     /**< name's left */
#define BR_SIZE_IN (BR_ROW_W - 16 - BR_SIZE_W)            /**< size's left */
#define BR_DATE_IN (BR_SIZE_IN - 16 - BR_DATE_W)          /**< date's left */

/**
 * @brief The footer's buttons, 52px tall, in the order browse_event()
 *        hit-tests them: RESCAN, DELETE and OPEN 140px wide, DELETE and
 *        OPEN under PREV and NEXT; WI-FI BR_WIFI_W (100px) wide after RESCAN.
 */
#define BR_RESCAN gfx_rect_make(LV_L, FOOT_Y, BR_PG_W, FOOT_H)
#define BR_WIFI_W 100  /**< WI-FI's width. */
/** @brief WI-FI. */
#define BR_WIFI   gfx_rect_make(LV_L + BR_PG_W + 8, FOOT_Y, BR_WIFI_W, FOOT_H)
/** @brief OPEN. */
#define BR_OPEN   gfx_rect_make(LV_R - BR_PG_W, FOOT_Y, BR_PG_W, FOOT_H)
#define BR_DELETE gfx_rect_make(LV_R - 2 * BR_PG_W - 8, FOOT_Y, BR_PG_W, \
                                FOOT_H)  /**< DELETE. */
/**
 * @brief A message's left edge in the footer: two lines of 37 cells of the
 *        12 x 24 face from WI-FI to DELETE, room for the 28 a message about
 *        a file declares and the file's name.  BR_MSG_LH is a line's pitch.
 */
#define BR_MSG_X  (LV_L + BR_PG_W + 8 + BR_WIFI_W + 8 + 8)
#define BR_MSG_LH 24  /**< A line's pitch. */

/** @brief The box an empty list shows in its place: two lines of 64 cells
 *         of the 12 x 24 face inside 24px margins. */
#define BR_EMPTY  gfx_rect_make(104, 150, 816, 160)

#define BR_BAR_MIN 32  /**< the scroll bar thumb's least height, px */

/**
 * @brief The DELETE question, drawn in place of the list and the footer:
 *        a box of 700 x 280px, CANCEL and DELETE 300 x 56px at its foot.
 */
#define DQ_BOX    gfx_rect_make(162, 100, 700, 280)
#define DQ_CANCEL gfx_rect_make(186, 304, 300, 56)  /**< CANCEL. */
#define DQ_DELETE gfx_rect_make(538, 304, 300, 56)  /**< DELETE. */

/* ------------------------------------------------------------- import ---- */

/**
 * @brief The import: the file's name over two panels, DETECTED 400px wide
 *        and the column list 600px, and four footer buttons of 246px.
 *
 * DETECTED holds a 16-character key and a 20-cell value on one row of the
 * 10 x 20 face, and its warnings break into lines of 36 cells.  The column
 * list shows IM_ROWS rows at a pitch of IM_ROW_H, drawn 4px shorter.
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
#define IM_BTN_W  ((LV_W - 3 * 8) / 4)   /**< 246px. */
/** @brief The import's footer button @p i: 0 SEP, 1 NUM, 2 BACK, 3 PLOT. */
#define IM_BTN(i) gfx_rect_make(LV_L + (i) * (IM_BTN_W + 8), IM_BTN_Y, \
                                IM_BTN_W, IM_BTN_H)
/** @brief The box a file that cannot be read shows: 74 cells of the
 *         12 x 24 face inside 24px margins. */
#define IM_FAIL   gfx_rect_make(44, 150, 936, 160)

/* --------------------------------------------------------------- plot ---- */

/**
 * @brief The plot: the summary line, the series' chips, the plot's panel
 *        and under it a 16px row of time labels 4px down, above the
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
#define PV_W       (LV_W - 16)  /**< Its width, 992px. */
#define PV_H       (PV_PANEL_H - 16)  /**< Its height, 316px. */
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
 * @brief The plot's footer: BACK, then < and >, 52px tall; the cursor's
 *        line in 10 x 20 from PV_LINE_X.
 */
#define PV_BACK    gfx_rect_make(LV_L, FOOT_Y, 140, FOOT_H)
#define PV_PREV    gfx_rect_make(LV_L + 148, FOOT_Y, 96, FOOT_H)  /**< <. */
#define PV_NEXT    gfx_rect_make(LV_L + 252, FOOT_Y, 96, FOOT_H)  /**< >. */
#define PV_LINE_X  (LV_L + 364)  /**< The cursor's line. */
/** @brief The box the plot shows with nothing loaded: 64 cells of the
 *         12 x 24 face inside 24px margins. */
#define PV_NONE    gfx_rect_make(104, 150, 816, 120)

_Static_assert(FOOT_Y + FOOT_H + 4 <= SCREEN_H - UI_ALERT_H,
               "the footer ends 4px above the alert band");
_Static_assert(FOOT_H >= UI_TAP_MIN, "the footer's buttons are a finger tall");
_Static_assert(BR_ROW_H >= UI_TAP_MIN, "a file row is a finger tall");
_Static_assert(IM_ROW_H >= UI_TAP_MIN, "a column row is a finger tall");
_Static_assert(30 + BR_ROWS * BR_ROW_H <= FOOT_Y - FOOT_GAP - BR_LIST_Y,
               "the file list's rows fit its panel");
_Static_assert(BR_PG_Y + BR_PG_H + 8 <= BR_LIST_Y,
               "PREV and NEXT end 8px above the list");
_Static_assert(LV_L + 3 + 3 * BR_TAB_W + 2 * 4 + 3 + 16 <= BR_HEAD_X,
               "the tabs end before the header's text");
_Static_assert(BR_HEAD_X + (11 + 17) * 10 + 16 <= BR_HEAD_END,
               "the header's text ends at PREV");
_Static_assert(BR_MSG_X + 28 * 12 + 8 <= LV_R - 2 * BR_PG_W - 8,
               "a footer message's 28 cells stand between WI-FI and DELETE");
_Static_assert(BR_WIFI_W >= 5 * 10 + 2 * 8, "WI-FI holds its 5 cells of 10 x 20");
_Static_assert(LOGS_STRIP_FOOT + 4 <= BR_PG_Y,
               "the header row starts below the tile's strip");
_Static_assert(BR_SIZE_W >= 10 * 10,
               "a row's size box holds LOG_DIR's 10 cells");
_Static_assert(BR_DATE_W >= 16 * 10, "a row's date box holds 16 cells");
_Static_assert(BR_NAME_IN + LOG_VIEWER_NAME_CELLS * 10 + 16 <= BR_DATE_IN,
               "a row's name ends 16px before its date");
_Static_assert(LOG_VIEWER_NAME_CELLS < LOG_VIEWER_NAME_MAX,
               "a name of LOG_VIEWER_NAME_CELLS fits its array");
_Static_assert((BR_ROWS * BR_ROW_H - 4) * BR_ROWS / LOG_VIEWER_MAX_FILES >= 32,
               "the scroll bar's thumb is 32px or more on a full page");
_Static_assert(30 + IM_ROWS * IM_ROW_H <= FOOT_Y - FOOT_GAP - IM_TOP,
               "the column list's rows fit its panel");
_Static_assert(PV_POS_Y + PV_POS_H <= PV_TOP + PV_PANEL_H,
               "the position bar is inside the plot's panel");
_Static_assert(CHIP_X + 4 * CHIP_W + 3 * CHIP_GAP <= LV_R,
               "the four chips stand in the screen's width");

/** @brief The view on screen. */
typedef enum { VIEW_BROWSE = 0, VIEW_IMPORT, VIEW_PLOT } view_t;

/** @brief The footer message's longest text, bytes with its NUL: a text
 *         of the table with a LOG_VIEWER_NAME_MAX name in it. */
#define LV_MSG_MAX 160

/** @brief What the footer message holds. */
typedef enum {
    LV_MSG_NONE = 0,      /**< nothing                              */
    LV_MSG_TEXT,          /**< a text of the table: msg_id          */
    LV_MSG_ERR            /**< the reader's verdict: msg_err        */
} lv_msg_kind_t;

/**
 * @brief The log viewer's state: the browse list and its page of the card,
 *        the footer message, the DELETE question, the import's analysis,
 *        the plot's samples, window and cursor, and the touches tracked.
 */
typedef struct {
    view_t view;                        /**< the view on screen */
    log_viewer_io_t io;                 /**< the I/O the screen reads the card through */

    /* The folder the browse view lists, and its three tabs. */
    log_viewer_tab_t tab;               /**< folder tab the browse view lists */
    ui_tabs_t tabs;                     /**< the three folder tabs */
    /* The tab of the file s.name names: the plot and the import read it
     * again from there whatever the tabs show since. */
    log_viewer_tab_t open_tab;          /**< tab of the file named by name */
    /* The logger's count of files created, as this screen last saw it. */
    unsigned written_seen;              /**< the logger's count of files created, as last seen */

    log_viewer_file_t files[LOG_VIEWER_MAX_FILES];  /**< the page of the card's entries */
    int n_files;                        /**< rows on the page, at most LOG_VIEWER_MAX_FILES */
    int n_card;                         /**< entries the volume holds; above n_files when paged */
    int first;                          /**< entries ahead of the page, in log_name_rank() order */
    int listed;                         /**< -1 no volume, 0 not read, 1 read */
    card_state_t card;                  /**< what the slot holds while listed is -1 */
    int sel;                            /**< highlighted file, -1 for none */
    int scroll;                         /**< first row shown */
    /* The card's name when the page was read: another name on RESCAN is
     * another card, and it opens on its first page. */
    char volume[LOG_VIEWER_NAME_MAX];   /**< the card's name when the page was read */
    unsigned mounts;                    /**< the I/O's mount number when the page was read */
    unsigned open_mounts;               /**< the mount number when the open file's page was read */
    bool to_viewer;                     /**< the open report went to the text viewer: the list
                                             selects it again on the way back */

    /* A finger on the scroll bar: the thumb follows it, held where it took
     * the thumb, grab px below the thumb's top. */
    bool bar_drag;                      /**< whether a finger holds the scroll bar's thumb */
    int bar_grab;                       /**< px below the thumb's top where the finger took it */

    char name[LOG_VIEWER_NAME_MAX];     /**< the open file's name */
    /* The footer message, kept as its text's ID and name, not as words,
     * so lv_message() words it in the language showing when it is drawn. */
    lv_msg_kind_t msg_kind;           /**< what the message holds        */
    ui_text_id_t msg_id;              /**< LV_MSG_TEXT: the text          */
    log_err_t msg_err;                /**< LV_MSG_ERR: the verdict        */
    char msg_name[LOG_VIEWER_NAME_MAX]; /**< the name it formats, or ""   */
    /** @brief Whether the message reports a result rather than a failure */
    bool message_ok;

    /*
     * The file DELETE asked about, or "" while no question is open.  A copy
     * of the name rather than an index into the list: the question has to
     * delete the file it named, whatever happens to the list behind it.
     */
    /**
     * @brief Name of the file DELETE asked about; "" while no question is open
     */
    char doomed[LOG_VIEWER_NAME_MAX];
    uint64_t doomed_size;               /**< its size, bytes */
    /* The one contact whose press on the question counts: the panel reports
     * up to five, and a second finger lifting off DELETE did not press it. */
    /** @brief Whether the contact that counts is down on the question */
    bool q_down;
    uint8_t q_id;                       /**< touch id of that contact */

    log_analysis_t an;                  /**< the import's analysis of the open file */
    bool have_analysis;                 /**< whether an holds an analysis */
    int delim_index;                    /**< -1 auto, else index into log_csv_delimiters */
    log_conv_t conv;                    /**< number convention; LOG_CONV_AUTO respects the file */
    int picked[LOG_MAX_SERIES];         /**< column indices picked as series */
    int n_picked;                       /**< number of picked columns */
    int col_scroll;                     /**< first column row shown on the import view */

    log_data_t data;                    /**< the plot's samples */
    bool have_data;                     /**< whether data is loaded */
    int cursor;                         /**< cursor sample index, -1 for none */

    /*
     * The samples the plot shows: [win0, win0 + win_n).  The whole file
     * until two fingers zoom in, and it stays where they leave it.
     */
    int win0;                           /**< first sample shown */
    int win_n;                          /**< samples shown */

    /*
     * Two fingers on the plot: spreading or pinching zooms, moving both
     * together pans.  The sample under the fingers' midpoint at the start
     * stays under the midpoint as they move.
     */
    bool pinch;                         /**< whether two fingers are on the plot */
    uint8_t pinch_id[2];                /**< touch ids of the two fingers */
    int16_t pinch_x[2];                 /**< their x at the start, px */
    float pinch_d0;                     /**< distance at the start, px */
    float pinch_s0;                     /**< sample under the start midpoint */
    float pinch_n0;                     /**< window width at the start, samples */

    /*
     * One finger panning a zoomed view (SET_PLOT_PAN).  Until the finger has
     * moved PV_PAN_SLOP px it is a tap: the cursor follows it.  Past that it
     * pans, the cursor goes back to where it was before the touch, and the
     * sample under the finger stays under it.
     */
    bool panning;                       /**< whether one finger pans the zoomed view */
    int16_t pan_x0;                     /**< finger x where the pan began, px */
    int pan_win0;                       /**< window start where the pan began */
    int pan_cursor0;                    /**< cursor before the touch */

    /* press tracking, shared by the list views */
    bool pressing;                      /**< whether a press is on a list row or button */
    int press_row;                      /**< row pressed */
    int press_y0;                       /**< y where the press began, px */
    int press_scroll0;                  /**< scroll where the press began */
    bool dragged;                       /**< whether the press moved far enough to be a drag */
    int press_btn;                      /**< button pressed */
    uint8_t press_id;                   /**< the finger that holds the plot cursor */
    int16_t press_x;                    /**< x of the press, px */

    /* The contact that holds a press on a button or a row, where it came
     * down, and whether it has been further than the slop from there: its
     * release is asked whether it is a tap (tap_on()). */
    bool tap_have;                      /**< whether a contact holds a press on a button or row */
    bool tap_moved;                     /**< whether it has moved further than the slop */
    uint8_t tap_id;                     /**< touch id of that contact */
    int16_t tap_x0,                         /**< x where it came down, px */
            tap_y0;                         /**< y where it came down, px */

    bool valid[MAX_FBS];                /**< per frame buffer: whether its content is current */
} lv_state_t;

/* Defined in log_viewer_screen.c. */
/** @brief The viewer's state, in log_viewer_screen.c. */
extern lv_state_t lv_s;
/** @brief Short name for lv_s inside the viewer's files. */
#define s lv_s

/* --- log_viewer_screen.c: the series' colours, the plot's traces, the
 *     tabs, the card's listing and paging -------------------------------- */

/** @brief Colour id of each plot series. */
extern const ui_color_id_t lv_series_color[LOG_MAX_SERIES];
/* Filled by lv_compute_spans() (log_viewer_load.c). */
/**
 * @brief Per series and pixel column, the plot's minimum and maximum row,
 *        filled by lv_compute_spans().
 */
extern int16_t lv_span[LOG_MAX_SERIES][PV_W][2];
/** @brief One band of PV_BAND rows of the plot, drawn at a time. */
extern gfx_color_t lv_band[PV_W * PV_BAND];
/** @brief Per series, the lowest value in the window. */
extern float lv_lo[LOG_MAX_SERIES];
/** @brief Per series, the highest value in the window. */
extern float lv_hi[LOG_MAX_SERIES];
/** @brief Folder tab shown for each tab index. */
extern const log_viewer_tab_t lv_tab_at[LOG_TAB_COUNT];

/**
 * @brief Points the I/O at the folder of @p tab, for the call that follows.
 * @param tab the folder tab
 */
void lv_point(log_viewer_tab_t tab);
/**
 * @brief Returns the card's name as the I/O reports it now.
 * @return the name, "" when there is none
 */
const char *lv_volume_now(void);
/**
 * @brief Returns the I/O's mount number now.
 * @return the mount number, 0 without one
 */
unsigned lv_mounts_now(void);
/**
 * @brief Returns the entry on the page that log_name_rank() puts first or
 *        last: the edges PREV, NEXT and RESCAN name.
 * @param sign 1 for the first entry, -1 for the last
 * @return the entry's name, "" with an empty page
 */
const char *lv_page_edge(int sign);
void lv_read_page(log_page_dir_t dir, const char *key);
/**
 * @brief Reads the page on screen again: the first page from the top, any
 *        other from its first entry; another card's name reads from its first
 *        page.
 */
void lv_rescan(void);
/**
 * @brief Returns whether the card holds more than the page shows, either side
 *        of it.
 * @return true when the list is paged
 */
bool lv_paged(void);
/**
 * @brief Returns whether a page precedes this one.
 * @return true when PREV is enabled
 */
bool lv_can_prev(void);
/**
 * @brief Returns whether a page follows this one.
 * @return true when NEXT is enabled
 */
bool lv_can_next(void);
/** @brief Frees the plot's samples and clears the cursor. */
void lv_drop_data(void);
/**
 * @brief Returns whether DELETE is offered: the I/O can delete and a file is
 *        selected.
 * @return true when DELETE is enabled
 */
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

/* --- log_viewer_load.c -------------------------------------------------- */

/**
 * @brief The footer message says @p id, a refusal or a failure.
 *
 * The message keeps the ID and the name, and lv_message() words it in the
 * language showing, so a change of SET_LANGUAGE rewords it.
 *
 * @param id    the text: LOG_CANNOT_OPEN, LOG_IS_FOLDER, LOG_TOO_BIG,
 *              LOG_CANNOT_DELETE and LOG_DELETED format @p name; any other
 *              is shown as it is
 * @param name  the file's name, or NULL
 */
void lv_say(ui_text_id_t id, const char *name);

/**
 * @brief The footer message says the reader's verdict @p err, a failure.
 * @param err  the verdict of log_csv_analyse() or log_csv_build()
 */
void lv_say_err(log_err_t err);

/** @brief The footer message says nothing. */
void lv_quiet(void);

/**
 * @brief The footer message in the language showing.
 * @param buf  takes the message; never NULL
 * @param n    bytes at @p buf, 1 or more, LV_MSG_MAX for the longest
 * @return @p buf, "" for no message
 */
const char *lv_message(char *buf, size_t n);
/**
 * @brief Returns a delimiter's name in the language showing.
 * @param delimiter ';', ',', tab or '|'
 * @return the name, "?" for any other
 */
const char *lv_delim_text(char delimiter);
/**
 * @brief Analyses the open file with the chosen delimiter and number
 *        convention, keeping the picked columns by name.
 */
void lv_run_analysis(void);
/**
 * @brief Reduces the window's samples to a minimum and maximum row per pixel
 *        column and series, and the series' ranges.
 */
void lv_compute_spans(void);
/**
 * @brief Loads the picked columns of the open file into the plot's samples.
 */
void lv_load_data(void);
/**
 * @brief Opens the selected row: a CSV file to the import view, a report to
 *        the text viewer FILES hosts (files_screen_open_text()); a folder
 *        or an oversized file is refused with a message.
 */
void lv_open_selected(void);

/* --- log_viewer_draw.c -------------------------------------------------- */

/**
 * @brief Draws the view on screen into frame buffer @p buffer_index.
 * @param c canvas drawn on
 * @param buffer_index the frame buffer, 0 to MAX_FBS - 1
 */
void lv_render(gfx_canvas_t *c, int buffer_index);

/* --- log_viewer_events.c ------------------------------------------------ */

/**
 * @brief Takes a touch event for the view on screen.
 * @param e the event
 */
void lv_event(const touch_event_t *e);
