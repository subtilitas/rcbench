/**
 * @file hitec_panel_int.h
 * @brief The seam between the files of the Hitec programmer's overlay.
 *
 * hitec_panel.c holds the state, the rows, the geometry and the caller's
 * side; hitec_panel_text.c the texts of the messages and the values;
 * hitec_panel_edit.c the actions, the lists and the keypad;
 * hitec_panel_page.c the hold pages; hitec_panel_touch.c the presses;
 * hitec_panel_draw.c the notes and the pages drawn; hitec_panel_file.c the
 * FILES list, the name keyboard, the pages of a file, a backup and FACTORY
 * DEFAULT and their texts.  The functions one calls of another are prefixed
 * hpn_.  Nothing outside these seven files includes this header; the
 * interface is hitec_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <limits.h>
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
#include "ui_textkey.h"
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
/** @brief WRITE n VALUES, a row with no label: 22 cells, "20 WERTE
 *         SCHREIBEN" and room. */
#define HP_WRITE_W 220
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
#define HP_PAGE_LINES 12
/** @brief The changed values a write page lists, the count of the rest
 *         after them. */
#define HP_PAGE_VALUES 6
/** @brief The entries a list holds: FILES' save, 5 files and the next
 *         page. */
#define HP_CHOICE_MAX 7
/** @brief The characters the name keyboard takes, KST's. */
#define HP_NAME_TYPED 16

/** @brief What a row is. */
enum { K_ACT = 0, K_SHOW };

/** @brief The actions of the SERVO tab's rows. */
enum { A_PORT = 0, A_SERIES, A_READ, A_MORE, A_CLEAR, A_ENABLE, A_RESTORE,
       A_WRITE, A_FILES };

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
#define HP_ROW_COUNT 44
/** @brief The rows, in hitec_panel.c. */
extern const hpn_row_t hpn_rows[HP_ROW_COUNT];

/* -------------------------------------------------------------------- state */

/** @brief What a press is on. */
enum {
    P_NONE = 0, P_TAB, P_CLOSE, P_ROW, P_KEYPAD, P_CHOICE, P_CHOICE_CANCEL,
    P_APPLY, P_CANCEL, P_TEXTKEY
};

/** @brief A line of Hitec D receive's list that sets nothing. */
#define HP_RX_LINE (-1)

/** @brief What a list was opened for. */
enum { C_SERIES = 0, C_MORE, C_RESTORE, C_VALUE, C_FILES, C_RX };
/** @brief The entries of MORE: the supply's in PWM, the session's while
 *         held. */
enum { MORE_VOLTAGE = 0, MORE_CURRENT, MORE_DISCARD, MORE_REPAIR,
       MORE_FACTORY, MORE_FIXED, MORE_RX };
/** @brief The entries of RESTORE. */
enum { RS_RESTORE = 0, RS_FORGET, RS_BACKUP };
/** @brief FILES' entries besides the files, numbered from 0. */
enum { FL_SAVE = -2, FL_NEXT = -3 };
/** @brief A D series FAILSAFE list's POSITION: its keypad follows. */
#define HP_FS_POSITION (-1)
/** @brief The hold pages. */
enum { W_LEAVE = 0, W_RELEASE, W_ENABLE, W_WRITE, W_SOFT, W_REPAIR,
       W_RESTORE, W_LOAD, W_FACTORY, W_BACKUP, W_LOAD_MORE };
/** @brief What the keypad edits. */
enum { KP_NONE = 0, KP_VOLTAGE, KP_CURRENT, KP_VALUE };

/** @brief The overlay's state. */
typedef struct {
    hitec_prog_t prog;          /**< the programmer                       */
    gfx_rect_t   area;          /**< the overlay's area, body pixels      */
    bool         open;          /**< the overlay is shown                 */
    bool         loaded;        /**< the options were read                */
    ui_tabs_t    tabs;          /**< the five tabs                        */
    ui_keypad_t  kp;            /**< the keypad of the supply or a value  */
    int          kp_what;       /**< KP_*                                 */
    uint8_t      ed_id;         /**< the hitec_param_t a value editor is
                                     for                                  */

    struct {
        bool open;              /**< the list shows                       */
        int  kind;              /**< C_*                                  */
        int  count;             /**< entries                              */
        char title[64];         /**< its title                            */
        char labels[HP_CHOICE_MAX][HITEC_PANEL_LINE]; /**< the entries    */
        int  values[HP_CHOICE_MAX]; /**< their values                     */
        int  current;           /**< the value drawn as chosen; INT_MIN
                                     none                                 */
    } ch;                       /**< the list                             */

    struct {
        bool      open;         /**< the page shows                       */
        int       kind;         /**< W_*                                  */
        bool      down;         /**< its hold button is down              */
        ui_hold_t hold;         /**< the hold                             */
        int       count;        /**< lines used                           */
        bool      danger;       /**< the edge in the danger colour        */
        char      title[64];    /**< the title                            */
        char      apply[64];    /**< the hold button's label              */
        int hint;      /**< the band's hint for a short release  */
        char      lines[HP_PAGE_LINES][HITEC_PANEL_LINE]; /**< the lines */
        uint8_t   level[HP_PAGE_LINES]; /**< each line's hitec_prog_level_t */
        int       vfirst;       /**< the first line of the changed values */
        int       vcount;       /**< their lines, "and n more" included    */
        int       vtotal;       /**< the changed values in all             */
        int       next;         /**< W_LOAD_MORE: the first line of the
                                     next part                            */
    } page;                     /**< the hold page                        */

    ui_textkey_t tk;            /**< the keyboard of a new file's name    */
    char     name[HITEC_CARD_NAME_MAX]; /**< the name typed               */
    bool     name_wait;         /**< a save by name waits for the card    */
    bool     files_wait;        /**< FILES waits for the card's list      */
    uint32_t files_seq;         /**< the list count FILES waits past      */
    bool     card_look;         /**< a tap asks the panel to look at the
                                     card slot                            */

    bool     have;              /**< a press is on the glass              */
    uint8_t  id;                /**< its touch id                         */
    int      pressed;           /**< P_*                                  */
    int      row;               /**< the row or entry it is on            */

    hitec_prog_state_t state_seen; /**< the state as last drawn           */
    uint32_t leaves_seen;          /**< hitec_prog_leaves() as last told
                                        to the supply screen              */
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
/**
 * @brief A value of parameter @p id in its unit, without the stored value:
 *        "30%", "CW", "1500.00us".
 * @param id    hitec_param_t
 * @param value the decoded value
 * @param out   the text
 * @param n     bytes at @p out
 */
void hpn_fmt_param(uint8_t id, int32_t value, char *out, size_t n);
/**
 * @brief The words of a CHOICE value of @p id: "CW", "ON".
 * @param id    hitec_param_t
 * @param value the value
 * @return the text
 */
const char *hpn_choice_text(uint8_t id, int32_t value);
/**
 * @brief The label of parameter @p id's row in the series in use.
 * @param id hitec_param_t
 * @return the label; "" for none
 */
const char *hpn_param_label(uint8_t id);
/**
 * @brief "LABEL: was -> now" of parameter @p id, the servo's value and
 *        @p img's.
 * @param id  hitec_param_t
 * @param img the image the write leaves
 * @param out the text
 * @param n   bytes at @p out
 */
void hpn_change_line(uint8_t id, const hitec_image_t *img, char *out,
                     size_t n);
/**
 * @brief Whether row @p r shows a value the draft changes.
 * @param r the row
 * @return true when edited
 */
bool hpn_row_edited(const hpn_row_t *r);

/* hitec_panel_edit.c */

/** @brief Every editor shut: the keypad, the keyboard, the list and the
 *         page. */
void hpn_close_editors(void);
/**
 * @brief Opens the list of @p kind with its title; hpn_choice_add() fills
 *        it.
 * @param kind  C_*
 * @param title the title
 */
void hpn_choice_open(int kind, const char *title);
/**
 * @brief Adds an entry to the open list while it has room.
 * @param label the entry's label
 * @param value its value
 */
void hpn_choice_add(const char *label, int value);
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
/** @brief The page of ENABLE WRITE TO SERVO. */
void hpn_page_enable(void);
/** @brief The page of WRITE n VALUES. */
void hpn_page_write(void);
/** @brief CHECK THIS CHANGE for the edit that waits. */
void hpn_page_soft(void);
/** @brief The page of REPAIR CHECKSUM. */
void hpn_page_repair(void);
/** @brief The page of RESTORE from the record. */
void hpn_page_restore(void);
/**
 * @brief Adds a line to the open page at @p level.
 * @param level hitec_prog_level_t
 * @param s     the line
 */
void hpn_page_add(unsigned level, const char *s);

/**
 * @brief Fit the open page above its buttons: the lines of the changed
 *        values give way, the last of them counting the rest
 *        (KP_VALUES_MORE), until every other line ends HP_FOOT_GAP above
 *        the hold button.
 * @return true when the page fits
 */
bool hpn_page_fit(void);

/**
 * @brief Keep the lines of the open page from @p from on that fit above
 *        its buttons, 1 at least; with @p from above 0 the page's first
 *        line stays in front of them.
 * @param from the first line kept after the first
 * @return the line after the last one kept; the page's count when all are
 */
int hpn_page_part(int from);
/**
 * @brief Opens a hold page of @p kind.
 * @param kind   W_*
 * @param danger the edge in the danger colour
 * @param title  its title
 * @param apply  the hold button's label
 * @param hint   the hint the band shows when the hold lifts early
 */
void hpn_page_open(int kind, bool danger, const char *title,
                   const char *apply, int hint);
/**
 * @brief The editor of a value row: its list or its keypad.
 * @param r the row
 */
void hpn_edit_open(const hpn_row_t *r);
/**
 * @brief An edit's value to the model; CHECK THIS CHANGE where it waits.
 * @param value the value in the parameter's unit
 * @return what the edit came to
 */
hitec_prog_edit_t hpn_edit_take(int32_t value);
/** @brief A page's hold completed: what it asked for. */
void hpn_page_apply(void);
/** @brief The programmer's options from the settings. */
void hpn_options_load(void);

/* hitec_panel_file.c */

/** @brief FILES tapped: the card asked for its first page. */
void hpn_files_ask(void);
/**
 * @brief An entry of FILES: the save, the next page or a file.
 * @param value FL_SAVE, FL_NEXT or the file's place in the list
 */
void hpn_files_choice(int value);
/**
 * @brief The name typed: the save, or the keyboard again.
 * @param name the name
 */
void hpn_name_done(const char *name);
/** @brief The card's answer came: the list, the name again, a file's
 *         page. */
void hpn_file_answer(void);
/** @brief CHECK THIS FILE for the load that waits. */
void hpn_page_load(void);
/**
 * @brief CHECK THIS FILE from its line @p from on: a part of the page that
 *        does not fit above the hold, the next part behind HOLD 2s TO
 *        CONTINUE (W_LOAD_MORE).
 * @param from the first line shown
 */
void hpn_page_load_at(int from);
/** @brief The page of FACTORY DEFAULT. */
void hpn_page_factory(void);
/** @brief The page of RESTORE of the session's backup. */
void hpn_page_backup(void);
/** @brief A write page's lines on the backup file: the file to come, or
 *         none without a card. */
void hpn_page_saves(void);
/**
 * @brief The lines of a message of the files and backups.
 * @param m   the message
 * @param out the lines
 * @return the lines used; 0 for a message of another kind
 */
int hpn_file_lines(const hitec_prog_msg_t *m,
                   char out[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE]);
/**
 * @brief The SERVO tab's note on the session's backup: its file, or that
 *        it is in RAM only.
 * @return the note; NULL for none
 */
const char *hpn_backup_note(void);

/* hitec_panel_page.c, for hitec_panel_file.c */

/**
 * @brief The values @p to changes on at most @p room lines of the open
 *        page.
 * @param to   the image the write leaves
 * @param room the lines
 */
void hpn_page_values(const hitec_image_t *to, int room);
/**
 * @brief The warning of each rule of @p rules on the open page.
 * @param rules the rule set
 */
void hpn_page_rules(uint16_t rules);

/* hitec_panel_touch.c */

/** @brief The supply's own controls follow the port: held, SUPPLY and the
 *         SERVO screen's SET row take an OFF and nothing else.  A LEAVE
 *         ended since the last call is told first
 *         (supply_screen_hitec_leave_done()). */
void hpn_supply_sync(void);
