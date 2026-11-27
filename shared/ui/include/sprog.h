/**
 * @file sprog.h
 * @brief The common frame of the servo programmers ("sprog"): one page
 *        shape, one press state machine, one set of editors and one page
 *        builder for every servo brand.
 *
 * The frame owns the layout, the drawing, the presses, the 2s holds, the
 * keypad, the keyboard, the lists and the hold pages.  A brand owns its
 * model, a const table of tabs and rows (sprog_def_t), a view fill and a
 * switch on what the frame returns.  The frame calls no brand: no function
 * pointer, no callback.  The brand calls the frame and switches on the
 * sprog_result_t it gets back.  No heap: sprog_t is a member of the
 * brand's static state.
 *
 * All figures are pixels inside the area the brand hands sprog_init(); on
 * SERVO's left card that is 620x428 at screen x 8, y 68.
 *
 * Regions of the page:
 *   - Tab strip: x 8, y 8, 16px narrower than the area, 48px tall.  Up to
 *     SPROG_TABS tabs; a tab whose show bits miss the view's mask is left
 *     out and the rest close up.
 *   - Row grid: lines at y 64 + 56 x n, n 0 to 4, rows 48px tall.  The
 *     rows of the tab shown flow in table order: a wide row takes a line,
 *     two narrow rows share one; a wide row after a lone narrow one starts
 *     the next line.  Column 0 at x 10, column 1 at x 20 + (w - 30) / 2,
 *     each (w - 30) / 2 wide: 295px on a 620px area.  A control ends at the
 *     row's right edge: 150px, 260px in a wide row, or the row's own w.
 *   - Head (SPROG_K_HEAD, a wide row): two lines of text at x 10, y + 4
 *     and y + 24 (the first in warn colour while no port is bound), and the
 *     260px button at the right.  A tap on the text returns SPROG_R_PORT
 *     where the view offers the PORT list.
 *   - Notes: from 8px under the last line of rows down to the bottom line,
 *     wrapped at (w - 20) / 10 cells of the 10x20 face, one row each 20px.
 *     Notes that need more rows page: the row above the bottom line shows
 *     "1/2" at the right and a tap on the notes turns the page.
 *   - Bottom line: the view's last message or identity line, its last row
 *     ending at sprog_foot().
 *   A '\n' in a note, the bottom line or a page line starts a new row.
 *   - Overlays over the whole area: keypad, keyboard, list, hold page.
 *
 * How a brand uses it:
 *   1. sprog_init() once, with its sprog_def_t.
 *   2. Before every draw, event and tick: sprog_view_begin(), then
 *      sprog_cell() for each row it shows and sprog_note() per note, and
 *      the head and bottom fields of f->view.
 *   3. sprog_event() in its event hook and sprog_tick() in its tick hook;
 *      a switch on the result's kind acts: TAP opens an editor or acts,
 *      STEP steps, STOP stops, HOLD_DUE connects or writes, KEYPAD, LIST
 *      and NAME take the value, PAGE_APPLY does what the page asked.
 *   4. sprog_render() draws it all.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ sizes */

#define SPROG_TABS        5   /**< tabs a brand's table holds at most        */
#define SPROG_LINES       5   /**< lines of the row grid                     */
#define SPROG_CELLS       10  /**< cells of a tab: 5 lines x 2 columns       */
#define SPROG_NOTES       8   /**< notes a view holds                        */
#define SPROG_LINE  (2 * 116 + 1) /**< bytes of a page line: 116 cells of UTF-8 */
#define SPROG_LIST_MAX    7   /**< entries a list holds                      */
#define SPROG_LIST_LABEL  64  /**< bytes of a list entry's label             */
#define SPROG_PAGE_LINES  12  /**< lines a hold page holds                   */
#define SPROG_PAGE_ROWS   16  /**< rows of text a hold page takes at most    */
#define SPROG_NAME_MAX    16  /**< characters of a file name typed           */
#define SPROG_FILE_CELLS  16  /**< a file entry's name column, cells         */
/** @brief No cap of the brand's own: a page group takes what is left. */
#define SPROG_ALL         0x7FFF
/** @brief A list with no entry marked as the current value. */
#define SPROG_NO_VALUE    INT32_MIN

/* ------------------------------------------------------------------ kinds */

/** @brief What a row is: the table's kind, which the view may override. */
typedef enum {
    SPROG_K_VALUE = 0, /**< a value; a tap opens the brand's editor        */
    SPROG_K_STEP,      /**< -, the value, + in the control's width          */
    SPROG_K_PAIR,      /**< two keys in the value's place                   */
    SPROG_K_ACT,       /**< a button with the brand's label                 */
    SPROG_K_SHOW,      /**< a value that takes no press                     */
    SPROG_K_HEAD,      /**< the SERVO tab's line 0: two lines and a button  */
    SPROG_K_GAP,       /**< a place kept empty: nothing drawn, no press     */
} sprog_kind_t;

/** @brief How a control takes a press. */
typedef enum {
    SPROG_P_TAP = 0,   /**< acts on a tap release inside it                 */
    SPROG_P_HOLD,      /**< a 2s hold: voltage on, writes                   */
    SPROG_P_STOP,      /**< acts on the DOWN, from any contact              */
    SPROG_P_OFF,       /**< drawn off; takes no press                       */
} sprog_press_t;

/** @brief A control's fill. */
typedef enum {
    SPROG_F_SUNK = 0,  /**< UI_C_PANEL_SUNK: a value, a neutral button      */
    SPROG_F_ACCENT,    /**< UI_C_ACCENT: a value that differs, the current  */
    SPROG_F_WARN,      /**< UI_C_WARN                                       */
    SPROG_F_GO,        /**< ui_action_fill(UI_ACTION_AVAILABLE): green      */
    SPROG_F_ACTIVE,    /**< ui_action_fill(UI_ACTION_ACTIVE): red           */
    SPROG_F_WAITING,   /**< ui_action_fill(UI_ACTION_WAITING): amber        */
    SPROG_F_HI,        /**< UI_C_PANEL_HI: an edit key                      */
} sprog_fill_t;

/** @name A cell's look, bits of sprog_cell_t.look */
/** @{ */
/** @brief A flat value: the fill with no hairline, the text in UI_C_TEXT,
 *         UI_C_PANEL while pressed (Hitec's values). */
#define SPROG_L_FLAT         0x01u
#define SPROG_L_INK_WARN     0x02u /**< the value's text in UI_C_WARN        */
#define SPROG_L_LABEL_DIM    0x04u /**< the label dim also while off         */
#define SPROG_L_LABEL_ACCENT 0x08u /**< the label in the accent              */
/** @brief A step's value alone, its keys not drawn (KST with no servo
 *         read); the value keeps its place between them. */
#define SPROG_L_NO_KEYS      0x10u
/** @} */

/** @brief The colour of a note, a bottom line or a page line. */
typedef enum {
    SPROG_LV_TEXT = 0, /**< UI_C_TEXT                                       */
    SPROG_LV_DIM,      /**< UI_C_TEXT_DIM                                   */
    SPROG_LV_FAINT,    /**< UI_C_TEXT_FAINT                                 */
    SPROG_LV_ACCENT,   /**< UI_C_ACCENT                                     */
    SPROG_LV_WARN,     /**< UI_C_WARN                                       */
    SPROG_LV_DANGER,   /**< UI_C_DANGER                                     */
} sprog_level_t;

/** @brief The line a page group folds into. */
typedef enum {
    SPROG_FOLD_VALUES = 0, /**< "and n more" (KP_VALUES_MORE)                */
    /** @brief "And n more warnings." (KP_CHECK_MORE), "And 1 more
     *         warning." (KP_CHECK_MORE_ONE) */
    SPROG_FOLD_WARNINGS,
} sprog_fold_t;

/* ----------------------------------------------------------- brand's data */

/** @brief One row of a brand's table (const, in the brand's file). */
typedef struct {
    uint8_t     tab;   /**< the tab it is on, an index of sprog_def_t.tabs  */
    uint8_t     wide;  /**< 1: a line of its own                            */
    uint8_t     kind;  /**< sprog_kind_t                                    */
    uint8_t     what;  /**< the brand's field id or action code             */
    /** @brief Show bits: the row shows while (show & view.mask) != 0; 0
     *         shows it always. */
    uint8_t     show;
    uint16_t    w;     /**< the control's width, px; 0: 150, 260 in a wide row */
    int16_t     label; /**< ui_text_id_t of the label, or -1 for name        */
    const char *name;  /**< a vendor term, the same in every language       */
} sprog_row_t;

/** @brief A brand's fixed data. */
typedef struct {
    const ui_text_id_t *tabs;     /**< tab labels, tab_count of them        */
    /** @brief Each tab's show bits, as a row's; NULL shows every tab. */
    const uint8_t      *tab_show;
    uint8_t             tab_count; /**< tabs, SPROG_TABS at most            */
    const sprog_row_t  *rows;      /**< the rows, in flow order per tab     */
    uint8_t             row_count; /**< rows                                */
    ui_text_id_t        hint;      /**< the value tabs' hint (sprog_hint()) */
} sprog_def_t;

/* ------------------------------------------------------------- the view */

/** @brief What a shown row looks like now; sprog_view_begin() sets the
 *         defaults (the table's kind, a tap, sunk, empty), the brand the
 *         rest. */
typedef struct {
    char        value[24]; /**< the control's text, 23 bytes              */
    const char *value2;    /**< SPROG_K_PAIR's second key, or NULL         */
    const char *label;     /**< the label instead of the table's, or NULL  */
    uint8_t     kind;      /**< the table's, or another sprog_kind_t       */
    uint8_t     press;     /**< sprog_press_t; a pair's first key          */
    uint8_t     press2;    /**< sprog_press_t of a pair's second key       */
    uint8_t     fill;      /**< sprog_fill_t; a pair's first key           */
    uint8_t     fill2;     /**< sprog_fill_t of a pair's second key        */
    /** @brief sprog_fill_t a hold fades to and its flash settles on; the
     *         brand keeps it after the hold, for the flash. */
    uint8_t     fill_to;
    uint8_t     look;      /**< SPROG_L_* bits                             */
    /** @brief Why a press is refused: a press shows it in the alert band,
     *         takes nothing and returns SPROG_R_REFUSED.  NULL: none. */
    const char *why;
    int16_t     hint;      /**< SPROG_P_HOLD: its TX_HOLD_HINT_*           */
} sprog_cell_t;

/** @brief The page shown, filled by the brand before a draw, an event or
 *         a tick. */
typedef struct {
    uint8_t      mask;              /**< show bits now                      */
    sprog_cell_t cell[SPROG_CELLS]; /**< by flow position of the tab shown  */
    const char  *head[2];           /**< the head's two lines, or NULL      */
    bool         head_warn;         /**< no port bound: line 1 in warn      */
    bool         port_pick;         /**< a tap on the head text: SPROG_R_PORT */
    const char  *note[SPROG_NOTES]; /**< the notes, top-down                */
    uint8_t      note_level[SPROG_NOTES]; /**< each note's sprog_level_t    */
    uint8_t      notes;             /**< notes used                         */
    const char  *bottom;            /**< the last message or the identity line */
    uint8_t      bottom_level;      /**< its sprog_level_t                  */
    /** @brief The brand's signature of all of it: a change counts the
     *         revision at the next event or tick. */
    uint32_t     sig;
} sprog_view_t;

/* ------------------------------------------------------------ the results */

/** @brief What happened; the brand switches on it. */
typedef enum {
    SPROG_R_NONE = 0,
    SPROG_R_TAP,          /**< row, key (0, or 1 for a pair's second)        */
    SPROG_R_STEP,         /**< row, key +1 or -1, value: steps owed (1 or more) */
    SPROG_R_STOP,         /**< row, key: stop now                            */
    /** @brief row: a hold control took the press; the brand may refuse
     *         it with sprog_refuse() before any of the hold is drawn. */
    SPROG_R_HOLD_BEGIN,
    /** @brief row: its 2s hold completed on the tick before, or
     *         sprog_due() asked; the brand checks the row still takes it. */
    SPROG_R_HOLD_DUE,
    SPROG_R_REFUSED,      /**< row: a press on a control with a why          */
    SPROG_R_PORT,         /**< the head text: open the PORT list             */
    SPROG_R_KEYPAD,       /**< what, value x 10^decimals                     */
    SPROG_R_KEYPAD_CANCEL, /**< what                                         */
    SPROG_R_LIST,         /**< what (the list's code), value                 */
    SPROG_R_LIST_CANCEL,  /**< what                                          */
    SPROG_R_NAME,         /**< the keyboard's text in sprog_t.name           */
    SPROG_R_NAME_CANCEL,  /**< the keyboard closed with no name              */
    SPROG_R_PAGE_APPLY,   /**< what (the page's code): its 2s hold completed */
    SPROG_R_PAGE_CANCEL,  /**< what                                          */
    SPROG_R_TAB,          /**< the tab changed: key is the new tab           */
} sprog_result_kind_t;

/** @brief A result. */
typedef struct {
    uint8_t kind;  /**< sprog_result_kind_t                                  */
    int16_t row;   /**< index in def->rows, or -1                            */
    int16_t key;   /**< 0, 1; the step's direction; the tab                  */
    int16_t what;  /**< the brand's code of the editor or the page           */
    int32_t value; /**< list value; keypad value x 10^decimals; steps owed   */
} sprog_result_t;

/* -------------------------------------------------------------- the state */

/** @brief A page's group of lines that folds into "and n more". */
typedef struct {
    bool        on;      /**< a group is open                               */
    bool        folded;  /**< its fold line is written                      */
    uint8_t     first;   /**< its first line                                */
    uint8_t     lines;   /**< its lines, the fold line included             */
    uint8_t     shown;   /**< its items written                             */
    int16_t     total;   /**< the items it has                              */
    int16_t     cap;     /**< lines it may take                             */
    int16_t     cap_rows; /**< rows it may take                             */
    int16_t     used;    /**< rows its items take                           */
    uint8_t     level;   /**< the fold line's sprog_level_t                 */
    uint8_t     fold;    /**< the fold line's words, sprog_fold_t           */
} sprog_group_t;

/** @brief A press's target, sprog_t.on. */
typedef enum {
    SPROG_ON_NONE = 0, SPROG_ON_ABSORB, SPROG_ON_TAB, SPROG_ON_ROW,
    SPROG_ON_MINUS, SPROG_ON_PLUS, SPROG_ON_HOLD, SPROG_ON_PORT,
    SPROG_ON_NOTES, SPROG_ON_KEYPAD, SPROG_ON_TEXTKEY, SPROG_ON_LIST,
    SPROG_ON_LIST_CANCEL, SPROG_ON_APPLY, SPROG_ON_CANCEL,
} sprog_on_t;

/** @brief The frame's state, embedded in the brand's static state. */
typedef struct {
    const sprog_def_t *def;  /**< the brand's table                         */
    gfx_rect_t   area;       /**< the frame's area, body pixels             */
    bool         open;       /**< events and taps reach it                  */
    uint32_t     rev;        /**< counted up when the picture changes       */
    uint32_t     hold_rev;   /**< counted up when a hold advances           */
    uint32_t     sig;        /**< the view's signature last counted         */
    ui_tabs_t    tabs;       /**< the tabs shown                            */
    uint8_t      tab_of[SPROG_TABS]; /**< the table's tab of each tab shown */
    uint8_t      tab_bits;   /**< bit t: the table's tab t shows            */
    sprog_view_t view;       /**< the brand's view                          */
    /** @brief The table's row of each cell of the tab shown, -1 past the
     *         last; sprog_view_begin() places them. */
    int16_t      cell_row[SPROG_CELLS];
    uint8_t      cells;      /**< cells used                                */
    uint8_t      lines;      /**< lines of the grid the tab shown takes     */
    /* the press */
    bool         have;       /**< a press is on the glass                   */
    uint8_t      id;         /**< its touch id                              */
    uint8_t      on;         /**< what it is on, sprog_on_t                 */
    int16_t      row;        /**< its row, or its list entry                */
    int16_t      key;        /**< its key of a pair                         */
    float        held_s;     /**< seconds a step key has been held          */
    /* the row holds: CONNECT, WRITE n VALUES */
    ui_hold_t    hold;       /**< the hold of a SPROG_P_HOLD control        */
    int16_t      hold_row;   /**< its row, -1 before the first              */
    bool         hold_due;   /**< completed: SPROG_R_HOLD_DUE on the next tick */
    /* the editors */
    ui_keypad_t  kp;         /**< the keypad                                */
    int16_t      kp_what;    /**< the brand's code of what it edits         */
    uint8_t      kp_dec;     /**< its digits after the point                */
    ui_textkey_t tk;         /**< the keyboard of a file's name             */
    char         name[UI_TEXTKEY_MAX + 1]; /**< the name of SPROG_R_NAME    */
    struct {
        bool    open;        /**< the list shows                            */
        bool    files;       /**< a files list: a name column of 16 cells   */
        int16_t what;        /**< the brand's code of the list              */
        uint8_t count;       /**< entries                                   */
        int32_t current;     /**< the value marked, or SPROG_NO_VALUE       */
        char    title[SPROG_LIST_LABEL]; /**< the title                     */
        char    label[SPROG_LIST_MAX][SPROG_LIST_LABEL]; /**< the labels    */
        int32_t value[SPROG_LIST_MAX]; /**< the values                      */
    } list;                  /**< the list                                  */
    struct {
        bool      open;      /**< the page shows                            */
        bool      danger;    /**< a danger page: its edge in UI_C_DANGER    */
        bool      drop;      /**< the 380px hold and BACK                   */
        bool      down;      /**< its hold button is held                   */
        int16_t   what;      /**< the brand's code of the page              */
        int16_t   hint;      /**< its hold's TX_HOLD_HINT_*                 */
        ui_hold_t hold;      /**< its hold                                  */
        uint8_t   count;     /**< lines used                                */
        char      title[64]; /**< the title, 20x36                          */
        char      apply[64]; /**< the hold button's label                   */
        char      cancel[24]; /**< CANCEL or BACK                           */
        char      line[SPROG_PAGE_LINES][SPROG_LINE]; /**< the lines        */
        uint8_t   level[SPROG_PAGE_LINES]; /**< each line's sprog_level_t   */
        sprog_group_t group; /**< the last group of lines                   */
    } page;                  /**< the hold page                             */
    uint8_t      note_page;  /**< the notes' page shown                     */
    uint32_t     note_sig;   /**< the notes' set the page is of             */
} sprog_t;

/* -------------------------------------------------------------- lifecycle */

/**
 * @brief Starts the frame on @p def over @p area: nothing open, nothing
 *        pressed, the first tab shown, the tabs of mask 0.
 * @param f    the frame
 * @param def  the brand's table; kept, not copied
 * @param area the area, body pixels
 */
void sprog_init(sprog_t *f, const sprog_def_t *def, gfx_rect_t area);
/** @brief Closes the keypad, the keyboard, the list and the page; a press
 *         on one ends.  A page hold's flash runs on.
 * @param f the frame */
void sprog_close_editors(sprog_t *f);
/** @brief Drops the press and every hold: a touch loss or a leave.  A hold
 *         completed and not yet due is taken back.
 * @param f the frame */
void sprog_cancel(sprog_t *f);
/** @brief Drops the press just taken: a refused SPROG_R_HOLD_BEGIN or
 *         SPROG_R_STEP.  A hold it began is reset, before any of it shows.
 * @param f the frame */
void sprog_refuse(sprog_t *f);
/**
 * @brief Asks SPROG_R_HOLD_DUE for @p row on the next tick, with no press:
 *        the footer's WRITE n VALUES.  A touch loss before it cancels it.
 * @param f   the frame
 * @param row the row
 * @return false while a press, an editor or the page is on the frame
 */
bool sprog_due(sprog_t *f, int row);

/* ------------------------------------------------------------- the view */

/**
 * @brief Begins the view: the mask, the cells of the tab shown placed and
 *        set to their defaults, no note, no head, no bottom line.  A change
 *        of the tabs the mask shows closes the editors, drops the press and
 *        builds the strip again, the tab shown kept where it still shows.
 * @param f    the frame
 * @param mask the show bits now
 */
void sprog_view_begin(sprog_t *f, uint8_t mask);
/**
 * @brief The cell of row @p row while it shows on the tab shown.
 * @param f   the frame
 * @param row the row, an index of def->rows
 * @return the cell, or NULL
 */
sprog_cell_t *sprog_cell(sprog_t *f, int row);
/**
 * @brief Adds a note at @p level; nothing past SPROG_NOTES.
 * @param f     the frame
 * @param s     the text, kept until the next sprog_view_begin()
 * @param level its sprog_level_t
 */
void sprog_note(sprog_t *f, const char *s, uint8_t level);
/** @brief Adds def->hint, dim: the line under a value tab's notes.
 * @param f the frame */
void sprog_hint(sprog_t *f);

/* -------------------------------------------------------- events and time */

/**
 * @brief One touch event.  A DOWN on a SPROG_P_STOP control acts from any
 *        contact, before the one-press rule; a press on a row held by
 *        another contact loses its tap.  Otherwise one press at a time.
 * @param f   the frame, its view filled
 * @param evt the event; NULL does nothing
 * @return what happened
 */
sprog_result_t sprog_event(sprog_t *f, const touch_event_t *evt);
/**
 * @brief One frame: the due hold, the holds, their flashes and the step
 *        keys' repeat.
 * @param f    the frame, its view filled
 * @param dt_s the frame's time, s
 * @return what happened
 */
sprog_result_t sprog_tick(sprog_t *f, float dt_s);
/** @brief Whether contact @p id holds the frame's press.
 * @param f the frame
 * @param id the touch id
 * @return true when it does */
bool sprog_has_press(const sprog_t *f, uint8_t id);
/** @brief Whether a SPROG_P_STOP control is at (@p x, @p y): the router
 *         lets a press there through while another contact holds.
 * @param f the frame
 * @param x the column, px
 * @param y the line, px
 * @return true on a stop control */
bool sprog_safe_at(const sprog_t *f, int x, int y);
/** @brief The revision: the frame's, the keypad's and the keyboard's.
 * @param f the frame
 * @return the revision */
uint32_t sprog_revision(const sprog_t *f);
/** @brief The hold revision, counted up as a hold advances or flashes.
 * @param f the frame
 * @return the revision */
uint32_t sprog_hold_revision(const sprog_t *f);
/** @brief Where the hold revision shows: the row hold's control while it
 *         runs or flashes, else the page's hold button.
 * @param f the frame
 * @return the rectangle */
gfx_rect_t sprog_hold_rect(const sprog_t *f);
/** @brief The table's tab shown.
 * @param f the frame
 * @return the tab */
int sprog_tab(const sprog_t *f);
/** @brief Shows the table's tab @p tab where it shows; notes from page 1.
 * @param f the frame
 * @param tab the tab */
void sprog_tab_set(sprog_t *f, int tab);

/* ---------------------------------------------------------- the editors */

/**
 * @brief Opens the keypad over the area; ENTER returns SPROG_R_KEYPAD with
 *        @p what and the value x 10^@p decimals.
 * @param f        the frame
 * @param what     the brand's code
 * @param title    the title
 * @param unit     the unit
 * @param value    the value now, shown faint
 * @param lo       the range's lower end
 * @param hi       its upper end
 * @param decimals digits after the point, 0 to 3
 */
void sprog_keypad(sprog_t *f, int16_t what, const char *title,
                  const char *unit, float value, float lo, float hi,
                  int decimals);
/**
 * @brief Opens a list with no entry; sprog_list_add() fills it.
 * @param f     the frame
 * @param what  the brand's code, returned with the choice
 * @param title the title
 * @param files a files list: an entry with a sub is its name padded to
 *              SPROG_FILE_CELLS cells and the sub
 */
void sprog_list_open(sprog_t *f, int16_t what, const char *title,
                     bool files);
/**
 * @brief Adds an entry.  Up to 5 entries stand one to a line; more two to
 *        a line, an entry wider than a column on a line of its own.
 * @param f     the frame
 * @param label the label
 * @param sub   the type or model beside it, or NULL
 * @param value the value returned on its choice
 * @return false when the list is full
 */
bool sprog_list_add(sprog_t *f, const char *label, const char *sub,
                    int32_t value);
/** @brief Marks the entry of @p value with the accent fill.
 * @param f the frame
 * @param value the value, or SPROG_NO_VALUE */
void sprog_list_current(sprog_t *f, int32_t value);
/**
 * @brief Opens the keyboard of a file's name, SPROG_NAME_MAX characters;
 *        OK returns SPROG_R_NAME with the name in f->name.
 * @param f      the frame
 * @param title  the title
 * @param preset the name it starts with
 */
void sprog_name(sprog_t *f, const char *title, const char *preset);

/* ------------------------------------------------------------- the page */

/**
 * @brief Opens a hold page with no line.  A flash of the last page's hold
 *        runs on.
 * @param f      the frame
 * @param what   the brand's code, returned with PAGE_APPLY or PAGE_CANCEL
 * @param danger true: the edge and the hold in UI_C_DANGER, else UI_C_WARN
 * @param title  the title
 * @param apply  the hold button's label
 * @param cancel CANCEL or BACK
 * @param hint   the hold's TX_HOLD_HINT_*
 * @param drop   true: the hold button 380px wide, beside BACK
 */
void sprog_page_open(sprog_t *f, int16_t what, bool danger,
                     const char *title, const char *apply,
                     const char *cancel, int16_t hint, bool drop);
/**
 * @brief Adds a line.
 * @param f     the frame
 * @param s     the text
 * @param level its sprog_level_t
 * @return false when the page holds SPROG_PAGE_LINES lines
 */
bool sprog_page_line(sprog_t *f, const char *s, uint8_t level);
/** @brief The rows of text the page's lines take.
 * @param f the frame
 * @return the rows */
int sprog_page_rows(const sprog_t *f);
/** @brief The rows left for lines at the narrowest pitch: the rows from
 *         y 64 to 8px above the buttons, SPROG_PAGE_ROWS at most, less
 *         those used.
 * @param f the frame
 * @return the rows */
int sprog_page_room(const sprog_t *f);
/**
 * @brief Opens a group of @p total items that folds: an item that would
 *        leave no line or row for the fold line is not written, and one
 *        line counts the rest instead.
 * @param f        the frame
 * @param total    the items the brand has
 * @param cap      lines the group may take, or SPROG_ALL
 * @param cap_rows rows it may take, or SPROG_ALL
 * @param fold     the fold line's words, sprog_fold_t: they count the
 *                 items left
 * @param level    the fold line's sprog_level_t
 */
void sprog_page_group(sprog_t *f, int total, int cap, int cap_rows,
                      uint8_t fold, uint8_t level);
/**
 * @brief Adds an item of the open group.
 * @param f     the frame
 * @param s     the text
 * @param level its sprog_level_t
 * @return false when it is folded: the fold line is written, and every
 *         later item returns false
 */
bool sprog_page_item(sprog_t *f, const char *s, uint8_t level);
/**
 * @brief Folds the last group until the page fits above its buttons at a
 *        28px pitch: its last line goes, the line before it counts the rest.
 * @param f the frame
 * @return true when the page fits
 */
bool sprog_page_fit(sprog_t *f);
/**
 * @brief Keeps the part of the page from line @p from: a later part keeps
 *        line 0, which names what it is about, and the lines that fit.
 * @param f    the frame
 * @param from the first line of the part
 * @return the line after the part's last, of the page as it was
 */
int sprog_page_part(sprog_t *f, int from);
/**
 * @brief The rows @p s takes, wrapped at the area's cells.
 * @param f the frame
 * @param s the text; NULL takes none
 * @return the rows
 */
int sprog_text_rows(const sprog_t *f, const char *s);

/* -------------------------------------------------------------- drawing */

/** @brief Draws the frame: the keypad or the keyboard alone while open,
 *         else the page, the list or the tab with its notes.
 * @param f the frame, its view filled
 * @param c the canvas */
void sprog_render(sprog_t *f, gfx_canvas_t *c);

/* ------------------------------------------------------------- geometry */

/** @brief Where a control at the foot ends: 8px above the area's foot, or
 *         4px above the alert band where that is higher.
 * @param f the frame
 * @return the y, body pixels */
int sprog_foot(const sprog_t *f);
/** @brief The table's tab @p tab.
 * @param f the frame
 * @param tab the tab
 * @return the rectangle; empty for a tab not shown */
gfx_rect_t sprog_tab_rect(const sprog_t *f, int tab);
/** @brief Row @p row, label and control, at its place on its tab.
 * @param f the frame
 * @param row the row
 * @return the rectangle; empty for a row not shown */
gfx_rect_t sprog_row_rect(const sprog_t *f, int row);
/** @brief Row @p row's control, between the keys of a step.
 * @param f the frame
 * @param row the row
 * @return the rectangle; empty for a row not shown */
gfx_rect_t sprog_value_rect(const sprog_t *f, int row);
/** @brief Row @p row's + key (@p up) or - key.
 * @param f the frame
 * @param row the row
 * @param up true for +
 * @return the rectangle; empty for a row not shown */
gfx_rect_t sprog_step_rect(const sprog_t *f, int row, bool up);
/** @brief Key @p key of row @p row's pair: 0 left, 1 right.
 * @param f the frame
 * @param row the row
 * @param key the key
 * @return the rectangle; empty for a row not shown */
gfx_rect_t sprog_pair_rect(const sprog_t *f, int row, int key);
/** @brief The head's text, where a tap opens the PORT list.
 * @param f the frame
 * @return the rectangle; empty with no head on the tab shown */
gfx_rect_t sprog_head_rect(const sprog_t *f);
/** @brief Entry @p i of the open list.
 * @param f the frame
 * @param i the entry
 * @return the rectangle; empty past the last */
gfx_rect_t sprog_list_rect(const sprog_t *f, int i);
/** @brief The list's CANCEL.
 * @param f the frame
 * @return the rectangle */
gfx_rect_t sprog_list_cancel_rect(const sprog_t *f);
/** @brief The page's hold button.
 * @param f the frame
 * @return the rectangle */
gfx_rect_t sprog_apply_rect(const sprog_t *f);
/** @brief The page's CANCEL or BACK.
 * @param f the frame
 * @return the rectangle */
gfx_rect_t sprog_page_cancel_rect(const sprog_t *f);
/** @brief The notes, where a tap turns their page.
 * @param f the frame
 * @return the rectangle; empty while the notes take one page */
gfx_rect_t sprog_notes_rect(const sprog_t *f);
/** @brief The pages the notes take.
 * @param f the frame
 * @return the pages, 1 or more */
int sprog_note_pages(const sprog_t *f);

#ifdef __cplusplus
}
#endif
