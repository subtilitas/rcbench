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
#include "touch_types.h"
#include "ui_screen.h"
#include "ui_tabs.h"
#include "ui_theme.h"

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
