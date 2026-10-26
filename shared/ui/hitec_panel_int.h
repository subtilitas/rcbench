/**
 * @file hitec_panel_int.h
 * @brief The seam between the files of the Hitec programmer's overlay.
 *
 * hitec_panel.c holds the state, the rows, the geometry and the caller's
 * side; hitec_panel_text.c the texts of the messages and the values;
 * hitec_panel_edit.c the actions, the lists, the keypad and the hold pages;
 * hitec_panel_touch.c the presses; hitec_panel_draw.c the notes and the
 * pages drawn.  The functions one calls of another are prefixed hpn_.
 * Nothing outside these five files includes this header; the interface is
 * hitec_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "hitec_panel.h"
#include "hitec_prog.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* ------------------------------------------------------------------ geometry */

/**
 * @brief The overlay's layout, in pixels from the area's top left, the KST
 *        overlay's: on SERVO's left card of 620px five tabs of 95px and
 *        CLOSE of 120px in one row 48px tall, rows 48px tall at a 56px
 *        pitch, two columns of 295px, a button or value of 150px, a wide
 *        row's of 260px.  Every control ends at hpn_foot().
 */
#define HP_TAB_Y   8            /**< the tabs' and CLOSE's top          */
#define HP_TAB_H   UI_TAP_MIN   /**< their height, 48px                 */
#define HP_CLOSE_W 120          /**< CLOSE's width                      */
#define HP_ROW0    64           /**< the first row's top                */
#define HP_PITCH   56           /**< a row's pitch: 8px between two     */
#define HP_ROW_H   UI_TAP_MIN   /**< a row's height, 48px               */
#define HP_VAL_W   150          /**< a button or value, 15 cells        */
#define HP_WIDE_W  260          /**< a wide row's, 26 cells             */
/** @brief The face of labels, buttons, values, notes and pages' lines:
 *         the type scale's LABEL face, 10 x 20. */
#define HP_FONT    (&gfx_font_10x20)
/** @brief A hold page's title: the HEAD face, 20 x 36. */
#define HP_TITLE_FONT (&gfx_font_20x36)
#define HP_CELL    10           /**< HP_FONT's cell, px                 */
#define HP_LINE_H  20           /**< a wrapped row's pitch, px          */
/** @brief The space between a control at the foot and the alert band. */
#define HP_FOOT_GAP 4
/** @brief A hold page's first line: under its title, 36px from 16px, and
 *         12px. */
#define HP_PAGE_TOP 64
/** @brief The lines a hold page holds. */
#define HP_PAGE_LINES 6
/** @brief The entries a list holds. */
#define HP_CHOICE_MAX 4

/** @brief What a row is. */
enum { K_ACT = 0, K_SHOW };

/** @brief The actions of the SERVO tab's rows. */
enum { A_PORT = 0, A_SERIES, A_READ, A_MORE, A_CLEAR };

/** @brief The series a row is for. */
enum { S_D = 1, S_C = 2, S_ANY = 3 };

/** @brief One row: where it sits, what it shows, what it is called. */
typedef struct {
    uint8_t page;     /**< hitec_panel_page_t                          */
    uint8_t row;      /**< from the top                                */
    uint8_t col;      /**< 0 left, 1 right                             */
    uint8_t wide;     /**< 1: both columns                             */
    uint8_t kind;     /**< K_ACT or K_SHOW                             */
    uint8_t what;     /**< A_* or hitec_param_t                        */
    uint8_t series;   /**< S_D, S_C, S_ANY                             */
    int16_t label;    /**< ui_text_id_t, or -1 for name                */
    const char *name; /**< a field term, the same in every language     */
} hpn_row_t;

/** @brief The rows' count. */
#define HP_ROW_COUNT 40
/** @brief The rows, in hitec_panel.c. */
extern const hpn_row_t hpn_rows[HP_ROW_COUNT];

/* -------------------------------------------------------------------- state */

/** @brief What a press is on. */
enum {
    P_NONE = 0, P_TAB, P_CLOSE, P_ROW, P_KEYPAD, P_CHOICE, P_CHOICE_CANCEL,
    P_APPLY, P_CANCEL
};

/** @brief What a list was opened for. */
enum { C_SERIES = 0, C_MORE };
/** @brief The entries of MORE. */
enum { MORE_VOLTAGE = 0, MORE_CURRENT };
/** @brief The hold pages. */
enum { W_LEAVE = 0, W_RELEASE };
/** @brief What the keypad edits. */
enum { KP_NONE = 0, KP_VOLTAGE, KP_CURRENT };

/** @brief The overlay's state. */
typedef struct {
    hitec_prog_t prog;          /**< the programmer                       */
    gfx_rect_t   area;          /**< the overlay's area, body pixels      */
    bool         open;          /**< the overlay is shown                 */
    bool         loaded;        /**< the options were read                */
    ui_tabs_t    tabs;          /**< the five tabs                        */
    ui_keypad_t  kp;            /**< the keypad of the supply             */
    int          kp_what;       /**< KP_*                                 */

    struct {
        bool open;              /**< the list shows                       */
        int  kind;              /**< C_*                                  */
        int  count;             /**< entries                              */
        char title[64];         /**< its title                            */
        char labels[HP_CHOICE_MAX][HITEC_PANEL_LINE]; /**< the entries    */
        int  values[HP_CHOICE_MAX]; /**< their values                     */
    } ch;                       /**< the list                             */

    struct {
        bool      open;         /**< the page shows                       */
        int       kind;         /**< W_*                                  */
        bool      down;         /**< its hold button is down              */
        ui_hold_t hold;         /**< the hold                             */
        int       count;        /**< lines used                           */
        char      title[64];    /**< the title                            */
        char      apply[64];    /**< the hold button's label              */
        char      lines[HP_PAGE_LINES][HITEC_PANEL_LINE]; /**< the lines */
    } page;                     /**< the hold page                        */

    bool     have;              /**< a press is on the glass              */
    uint8_t  id;                /**< its touch id                         */
    int      pressed;           /**< P_*                                  */
    int      row;               /**< the row or entry it is on            */

    hitec_prog_state_t state_seen; /**< the state as last drawn           */
    uint32_t sig;               /**< signature of the last draw           */
    uint32_t rev;               /**< counts the picture's changes         */
    uint32_t hold_rev;          /**< counts a hold's steps                */
} hpn_state_t;

/** @brief The state, in hitec_panel.c; the files read it as hp. */
extern hpn_state_t hitec_panel_h;
/** @brief Short name for hitec_panel_h inside the overlay's files. */
#define hp hitec_panel_h

/* --- the functions one file calls of another ---------------------------- */

/* hitec_panel.c */

/**
 * @brief Whether row @p r is on the tab shown in the series and layout in
 *        use.
 * @param r the row
 * @return true when shown
 */
bool hpn_row_shown(const hpn_row_t *r);
/**
 * @brief The row at (@p page, @p row, @p col) shown now, or NULL.
 * @param page the tab
 * @param row  the row
 * @param col  the column
 * @return the row
 */
const hpn_row_t *hpn_row_at(int page, int row, int col);
/**
 * @brief Row @p r's whole rectangle.
 * @param r the row
 * @return the rectangle
 */
gfx_rect_t hpn_row_rect(const hpn_row_t *r);
/**
 * @brief Row @p r's control.
 * @param r the row
 * @return the rectangle
 */
gfx_rect_t hpn_value_rect(const hpn_row_t *r);
/**
 * @brief The tabs' row.
 * @return its rectangle
 */
gfx_rect_t hpn_tabs_rect(void);
/**
 * @brief Where a control at the foot ends: 8px above the area's foot, and
 *        HP_FOOT_GAP above the alert band at the most.
 * @return the y, body pixels
 */
int hpn_foot(void);
/**
 * @brief Where the notes end: 6px above the area's foot, and HP_FOOT_GAP
 *        above the alert band at the most.
 * @return the y, body pixels
 */
int hpn_notes_foot(void);
/**
 * @brief The cells of HP_FONT a note's row has: the area less 10px a side.
 * @return the cells, 1 at least
 */
int hpn_cells(void);
/**
 * @brief @p s in rows of the area's width from (@p x, @p y).
 * @param c     the canvas
 * @param x     the left, px
 * @param y     the top, px
 * @param s     the text
 * @param color its colour
 * @return the y under the last row
 */
int hpn_text_rows(gfx_canvas_t *c, int x, int y, const char *s,
                  gfx_color_t color);
/**
 * @brief The state changed or an operation started: editors that asked
 *        for the state they were opened in close and the revision counts.
 */
void hpn_moved(void);

/* hitec_panel_text.c */

/**
 * @brief A row's label as it is drawn now.
 * @param r the row
 * @return the label, static for the PORT and SERVO rows
 */
const char *hpn_label(const hpn_row_t *r);
/**
 * @brief The text in a row's control into @p out.
 * @param r     the row
 * @param out   the text
 * @param n     bytes at @p out
 * @param warn  set when the value shows raw, as it fits no encoding
 */
void hpn_value_text(const hpn_row_t *r, char *out, size_t n, bool *warn);
/**
 * @brief Line @p i of the PORT row into @p out.
 * @param i   0 or 1
 * @param out the text
 * @param n   bytes at @p out
 * @return false for no such line
 */
bool hpn_port_line(int i, char *out, size_t n);
/**
 * @brief Whether the PORT row names no pin: the warning colour.
 * @return true when no pin is known
 */
bool hpn_port_unbound(void);
/**
 * @brief The state line of the SERVO tab into @p out.
 * @param out the text
 * @param n   bytes at @p out
 */
void hpn_state_line(char *out, size_t n);

/* hitec_panel_edit.c */

/** @brief Every editor shut: the keypad, the list and the page. */
void hpn_close_editors(void);
/**
 * @brief The action of a SERVO row.
 * @param r the row
 */
void hpn_act(const hpn_row_t *r);
/**
 * @brief The list's choice @p value.
 * @param value the entry's value
 */
void hpn_choice_done(int value);
/**
 * @brief The keypad's value @p v.
 * @param v the value
 */
void hpn_keypad_done(float v);
/**
 * @brief Whether row @p r's control takes a tap now.
 * @param r the row
 * @return true when live
 */
bool hpn_row_live(const hpn_row_t *r);
/** @brief The page of LEAVE. */
void hpn_page_leave(void);
/** @brief The page of RELEASE PORT. */
void hpn_page_release(void);
/** @brief A page's hold completed: what it asked for. */
void hpn_page_apply(void);
/** @brief The programmer's options from the settings. */
void hpn_options_load(void);

/* hitec_panel_touch.c */

/** @brief The supply's own controls follow the port: held, SUPPLY and the
 *         SERVO screen's SET row take an OFF and nothing else. */
void hpn_supply_sync(void);
