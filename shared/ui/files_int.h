/**
 * @file files_int.h
 * @brief FILES' private seam between files_screen.c, the state, the
 *        requests to the card's task and the screen's interface,
 *        files_draw.c, the drawing, and files_events.c, the touch events:
 *        the layout the three work on, the state and the functions one
 *        calls in another.  Nothing outside shared/ui includes this header;
 *        the screen's interface is files_screen.h.
 *
 * Included last: its layout macros have short names (FS_ROW_H, FOOT_Y) and
 * it names the state s, which no header included after it may meet.  What
 * one file calls or reads in another carries the prefix fs_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "card_tree.h"
#include "files_screen.h"
#include "fm_req.h"
#include "fm_rules.h"
#include "fm_tree.h"
#include "gfx.h"
#include "touch_types.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

#define SCREEN_W UI_PANEL_W  /**< The body's width, 1024px. */
/** @brief The body's height, 540px: the router owns the band and hands
 *         this screen the body. */
#define SCREEN_H UI_BODY_H
#define MAX_FBS  3           /**< The framebuffers the panel turns over. */

/**
 * @brief The faces: FS_FONT_LABEL (10 x 20) for names, sizes, buttons and
 *        INFO's rows; FS_FONT_FINE (8 x 16) for a row's kind line;
 *        FS_FONT_BODY (12 x 24) for the message line and the questions'
 *        sentences.
 */
#define FS_FONT_LABEL (&gfx_font_10x20)
#define FS_FONT_FINE  (&gfx_font_8x16)   /**< 8 x 16: a row's kind line. */
#define FS_FONT_BODY  (&gfx_font_12x24)  /**< 12 x 24: prose. */

/** @brief The screen's columns: UI_MARGIN (8px) in from either edge. */
#define FS_L UI_MARGIN
#define FS_R (SCREEN_W - UI_MARGIN)   /**< The right edge, x 1016. */
#define FS_W (FS_R - FS_L)            /**< The width between, 1008px. */

/* ---------------------------------------------------------------- panes */

/**
 * @brief The two panes, 500px wide at x 8 and x 516, 8px apart.
 *
 * A pane's header row, body y 4 to 52, holds UP: a button with the
 * folder's path that goes up one folder, 312px wide; and while the folder
 * holds more than one page, PREV and NEXT, 88 x 48px, at the pane's right
 * end.  The list's panel from y 60 holds FS_ROWS rows at a pitch of
 * FS_ROW_H (48px, UI_TAP_MIN) under its 30px heading, the rows drawn
 * 44px tall, and the scroll bar, FS_BAR_W (56px) wide, at its right edge.
 */
#define FS_PANE_W   500
#define FS_PANE_GAP 8                                /**< Between them. */
/** @brief Pane @p i's left edge: 8 or 516. */
#define FS_PANE_X(i) (FS_L + (i) * (FS_PANE_W + FS_PANE_GAP))
#define FS_HDR_Y    4                                /**< The header row. */
#define FS_HDR_H    UI_TAP_MIN                       /**< Its height. */
#define FS_PG_W     88          /**< PREV's and NEXT's width. */
#define FS_UP_W     312         /**< UP's width. */
/** @brief Pane @p i's UP. */
#define FS_UP(i)    gfx_rect_make(FS_PANE_X(i), FS_HDR_Y, FS_UP_W, FS_HDR_H)
/** @brief Pane @p i's PREV. */
#define FS_PREV(i)  gfx_rect_make(FS_PANE_X(i) + FS_PANE_W - 2 * FS_PG_W - 4, \
                                  FS_HDR_Y, FS_PG_W, FS_HDR_H)
/** @brief Pane @p i's NEXT. */
#define FS_NEXT(i)  gfx_rect_make(FS_PANE_X(i) + FS_PANE_W - FS_PG_W, \
                                  FS_HDR_Y, FS_PG_W, FS_HDR_H)
#define FS_LIST_Y   60          /**< The list's panel's top. */
#define FS_HEAD_H   30          /**< Its heading. */
#define FS_ROW_H    48          /**< A row's pitch, UI_TAP_MIN. */
#define FS_ROWS     6           /**< The rows a pane shows at once. */
/** @brief Pane @p i's list panel: its heading and its rows. */
#define FS_LIST(i)  gfx_rect_make(FS_PANE_X(i), FS_LIST_Y, FS_PANE_W, \
                                  FS_HEAD_H + FS_ROWS * FS_ROW_H + 4)
#define FS_BAR_W    56          /**< The scroll bar's width: 8.4mm. */
#define FS_BAR_MIN  32          /**< Its thumb's least height. */
/** @brief Pane @p i's scroll bar's track. */
#define FS_BAR(i)   gfx_rect_make(FS_PANE_X(i) + FS_PANE_W - 8 - FS_BAR_W, \
                                  FS_LIST_Y + FS_HEAD_H, FS_BAR_W, \
                                  FS_ROWS * FS_ROW_H - 4)
#define FS_ROW_IN   8           /**< A row's inset from the panel's left. */
/** @brief A row's width: from the inset to 8px short of the bar. */
#define FS_ROW_W    (FS_PANE_W - FS_ROW_IN - 8 - FS_BAR_W - 8)
#define FS_ICON     24          /**< A row's icon box. */
#define FS_SIZE_W   90          /**< A row's size box: 9 cells. */

/**
 * @brief The message line under the panes: two lines of the 12 x 24 face,
 *        84 cells each, 24px apart, ending 4px above the footer.
 */
#define FS_MSG_Y    388
#define FS_MSG_LH   24          /**< A line's pitch. */

/**
 * @brief The footer, body y 440 to 492, ending 4px above the router's
 *        alert band (y 496), in LOGS' order: RESCAN at the left, 140px for
 *        its German AKTUALISIEREN, then COPY, MOVE, RENAME, DELETE, NEW,
 *        INFO and OPEN at the right, 118px wide, 6px apart, 52px tall.
 *
 * fs_foot_t is the one place that orders them: a button's number is its
 * place, so the drawing (FS_FOOT()) and the hit test take the same one.
 */
#define FOOT_H       52
#define FOOT_Y       (SCREEN_H - UI_ALERT_H - 4 - FOOT_H)  /**< y 440. */
#define FOOT_W       118        /**< A button's width but RESCAN's. */
#define FOOT_W_FIRST 140        /**< RESCAN's width, the first button. */
#define FOOT_GAP     6          /**< Between two buttons. */
#define FOOT_PAD     6          /**< Room kept at each side of a label. */
#define FOOT_N       8          /**< The buttons. */
/** @brief Footer button @p i's left edge: x 8 for the first, then 146px
 *         on and 124px a button after it. */
#define FS_FOOT_X(i) (FS_L + (((i) == 0) ? 0 \
                              : FOOT_W_FIRST + FOOT_GAP \
                                    + ((i) - 1) * (FOOT_W + FOOT_GAP)))
/** @brief Footer button @p i: 0 RESCAN to 7 OPEN (fs_foot_t). */
#define FS_FOOT(i)   gfx_rect_make(FS_FOOT_X(i), FOOT_Y, \
                                   ((i) == 0) ? FOOT_W_FIRST : FOOT_W, \
                                   FOOT_H)

/** @brief The footer's buttons, left to right: each one's number is its
 *         place in the footer. */
typedef enum {
    FS_RESCAN = 0,
    FS_COPY,
    FS_MOVE,
    FS_RENAME,
    FS_DELETE,
    FS_NEW,
    FS_INFO,
    FS_OPEN
} fs_foot_t;

_Static_assert(FS_RESCAN == 0, "RESCAN is the first, wide button");
_Static_assert(FS_OPEN == FOOT_N - 1, "OPEN is the last button");

/* ---------------------------------------------------------------- boxes */

/**
 * @brief The DELETE question and the progress box, in place of the panes
 *        and the footer: 700 x 280px, the buttons 300 x 56px at its
 *        foot, as LOGS' question.
 */
#define FS_BOX_Y      100       /**< The box's top. */
#define FS_BOX_H      280       /**< Its height. */
/** @brief The box, 700 x 280px. */
#define FS_BOX        gfx_rect_make(162, FS_BOX_Y, 700, FS_BOX_H)
#define FS_Q_CANCEL   gfx_rect_make(186, 304, 300, 56)  /**< CANCEL. */
#define FS_Q_DELETE   gfx_rect_make(538, 304, 300, 56)  /**< DELETE. */
/** @brief The progress box's CANCEL, in the middle of its foot. */
#define FS_JOB_CANCEL gfx_rect_make(362, 304, 300, 56)
/** @brief The progress box's bar. */
#define FS_JOB_BAR    gfx_rect_make(186, 228, 652, 24)

/**
 * @brief The INFO box: 800 x 420px, its rows in the 10 x 20 face at a
 *        pitch of FS_INFO_LH under the heading, the key in FS_INFO_KEY_W
 *        px, CLOSE 300 x 56px at its foot.
 */
#define FS_INFO_Y     20        /**< The box's top. */
#define FS_INFO_H     420       /**< Its height. */
/** @brief The box, 800 x 420px. */
#define FS_INFO_BOX   gfx_rect_make(112, FS_INFO_Y, 800, FS_INFO_H)
#define FS_INFO_LH    32        /**< A row's pitch. */
#define FS_INFO_KEY_W 160       /**< The key's column: 16 cells. */
#define FS_INFO_CLOSE gfx_rect_make(362, 364, 300, 56)  /**< CLOSE. */

/**
 * @brief NEW's choice: a list of two rows of 56px (UI_CHOICE_ROW_H) over
 *        the panes, its right edge NEW's, its foot 8px above the footer.
 */
#define FS_NEW_W      300                             /**< Its width. */
#define FS_NEW_ROW_H  UI_CHOICE_ROW_H                 /**< A row, 56px. */
#define FS_NEW_H      (2 * FS_NEW_ROW_H + 8)          /**< Its height. */
/** @brief Its left edge, x 468: NEW's right edge less its width. */
#define FS_NEW_X      (FS_FOOT_X(FS_NEW) + FOOT_W - FS_NEW_W)
#define FS_NEW_Y      (FOOT_Y - 8 - FS_NEW_H)        /**< Its top. */
/** @brief The list. */
#define FS_NEW_LIST   gfx_rect_make(FS_NEW_X, FS_NEW_Y, FS_NEW_W, FS_NEW_H)
/** @brief Its row @p i: 0 FOLDER, 1 TEXT FILE. */
#define FS_NEW_ROW(i) gfx_rect_make(FS_NEW_X + 4, \
                                    FS_NEW_Y + 4 + (i) * FS_NEW_ROW_H, \
                                    FS_NEW_W - 8, FS_NEW_ROW_H)

/**
 * @brief The keyboard for a name: over the body from y 4 to the footer's
 *        foot, 1008px wide; its keys end at UI_KEYS_FOOT.
 */
#define FS_TK_AREA gfx_rect_make(FS_L, 4, FS_W, FOOT_Y + FOOT_H - 4)

/** @brief A single call's progress box waits this long, s: a rename that
 *         is done at once shows no box. */
#define FS_JOB_BOX_S 0.25f

/** @brief A press that moves further than this, px, in x or in y, is not
 *         a tap; on a list, vertical travel past it is a scroll. */
#define DRAG_SLOP 8

/** @brief What a name is typed for. */
typedef enum {
    FS_NAME_FOLDER = 0,   /**< NEW > FOLDER    */
    FS_NAME_FILE,         /**< NEW > TEXT FILE */
    FS_NAME_RENAME        /**< RENAME          */
} fs_name_for_t;

/** @brief What a press came down on. */
typedef enum {
    FS_HIT_NONE = -1,
    /* 0 to 7: the footer's buttons, fs_foot_t. */
    FS_HIT_UP = FOOT_N,   /**< a pane's UP: + the pane                 */
    FS_HIT_PREV = FOOT_N + 2,  /**< a pane's PREV: + the pane          */
    FS_HIT_NEXT = FOOT_N + 4,  /**< a pane's NEXT: + the pane          */
    FS_HIT_ROW = FOOT_N + 6,   /**< a row: press_pane, press_row       */
    FS_HIT_BAR,                /**< a scroll bar: bar_pane             */
    FS_HIT_Q_CANCEL,           /**< the DELETE question's CANCEL       */
    FS_HIT_Q_DELETE,           /**< and DELETE                         */
    FS_HIT_INFO_CLOSE,         /**< INFO's CLOSE                       */
    FS_HIT_NEW_ROW,            /**< NEW's choice: + the row            */
    FS_HIT_JOB_CANCEL = FS_HIT_NEW_ROW + 2  /**< the progress box's CANCEL */
} fs_hit_t;

_Static_assert(FOOT_Y + FOOT_H + 4 <= SCREEN_H - UI_ALERT_H,
               "the footer ends 4px above the alert band");
_Static_assert(FOOT_H >= UI_TAP_MIN, "the footer's buttons are a finger tall");
_Static_assert(FS_FOOT_X(FOOT_N - 1) + FOOT_W == FS_R,
               "the footer's buttons end at the right margin");
_Static_assert(FS_ROW_H >= UI_TAP_MIN, "a row is a finger tall");
_Static_assert(FS_HDR_H >= UI_TAP_MIN && FS_PG_W >= UI_TAP_MIN,
               "UP, PREV and NEXT are a finger tall and wide");
_Static_assert(FS_UP_W + 8 <= FS_PANE_W - 2 * FS_PG_W - 4,
               "UP ends 8px short of PREV");
_Static_assert(FS_PANE_X(1) + FS_PANE_W == FS_R, "the right pane ends at the margin");
_Static_assert(FS_HDR_Y + FS_HDR_H + 8 <= FS_LIST_Y,
               "the header row ends 8px above the list");
_Static_assert(FS_LIST_Y + FS_HEAD_H + FS_ROWS * FS_ROW_H + 4 + 6 <= FS_MSG_Y,
               "the lists end above the message line");
_Static_assert(FS_MSG_Y + 2 * FS_MSG_LH + 4 <= FOOT_Y,
               "the message line's two lines end 4px above the footer");
_Static_assert(FS_ICON + 8 + 28 * 10 + 8 + FS_SIZE_W + 8 <= FS_ROW_W,
               "a row holds its icon, 28 cells of name and the size");
_Static_assert((FS_ROWS * FS_ROW_H - 4) * FS_ROWS / FM_PAGE_MAX >= 32,
               "the scroll bar's thumb is 32px or more on a full page");
_Static_assert(FS_NEW_Y + FS_NEW_H + 8 <= FOOT_Y,
               "NEW's choice ends 8px above the footer");
_Static_assert(FS_NEW_ROW_H >= UI_TAP_MIN, "a choice row is a finger tall");
_Static_assert(FS_INFO_Y + FS_INFO_H <= SCREEN_H - UI_ALERT_H - 4,
               "INFO ends 4px above the alert band");
_Static_assert(FS_BOX_Y + FS_BOX_H <= SCREEN_H - UI_ALERT_H - 4,
               "the question ends 4px above the alert band");

/** @brief The message line's longest text, bytes with its NUL. */
#define FS_MSG_MAX 192

/** @brief What the message line holds. */
typedef enum {
    FS_MSG_NONE = 0,      /**< nothing: the gate's sentence, if any    */
    FS_MSG_TEXT,          /**< a text of the table: msg_id             */
    FS_MSG_ERR            /**< a refusal or a failure: msg_err         */
} fs_msg_kind_t;

/** @brief The screen's state. */
typedef struct {
    files_io_t io;            /**< the card's task                       */
    bool have_io;             /**< files_screen_set_io() gave one        */
    files_view_t view;        /**< what is on top                        */

    fm_tree_t pane[2];        /**< the two panes as last read            */
    int scroll[2];            /**< each pane's first row on screen       */
    int active;               /**< the pane OPEN and the rest act in     */
    bool read[2];             /**< the pane was read since the start     */
    fm_err_t pane_err[2];     /**< what its last read answered           */
    char enter_name[FM_NAME_BUF]; /**< the folder an ENTER goes into     */
    unsigned mount;           /**< the card the panes are of; 0 none yet */

    /* The listings waiting for the card's task, one per pane. */
    bool need[2];             /**< a page of the pane is to be read      */
    fm_go_t need_go[2];       /**< which page                            */
    bool need_look;           /**< the next one looks at the slot first  */
    /** @brief The pane's wish waiting is RESCAN's: its answer keeps the
     *         pane's place (place_keep() in files_screen.c). */
    bool keep_place[2];

    /* The request under way, the card's task's until it is answered. */
    fm_req_t req;             /**< the request                           */
    bool busy;                /**< handed over and not answered yet      */
    int req_pane;             /**< PAGE, ENTER, UP: its pane; -1 else    */
    bool req_keep;            /**< a PAGE of RESCAN's: keep the place    */
    float busy_s;             /**< how long it has been under way, s     */
    uint64_t prog_done;       /**< a job's bytes done, as last drawn     */
    uint64_t prog_total;      /**< and to do                             */
    bool cancel_asked;        /**< CANCEL was pressed on the job         */

    /** @brief The name a pane selects once its next page is read: a new
     *         folder, a new file, a renamed entry. */
    char select_after[2][FM_NAME_BUF];

    /* The DELETE question: the entry it names, copied, and its pane. */
    fm_entry_t doomed;        /**< the entry                             */
    int doomed_pane;          /**< its pane                              */

    /* INFO: the entry, its folder, and what the card answered. */
    fm_entry_t info_entry;    /**< the entry                             */
    char info_dir[FM_PATH_SIZE]; /**< its folder                         */
    fm_info_t info;           /**< the answer                            */
    fm_err_t info_err;        /**< FM_OK, or why the card did not answer */

    /* The keyboard for a name, and what it is typed for. */
    ui_textkey_t tk;          /**< the keyboard                          */
    fs_name_for_t name_for;   /**< NEW > FOLDER, NEW > TEXT FILE, RENAME */
    uint32_t drawn_tk_rev;    /**< its revision as last drawn            */

    /* The bench as the gate and the rules see it, read each frame. */
    fm_bench_t bench;         /**< the io's bench, the log folders ours  */
    char user_dir[CARD_DIR_MAX]; /**< USER LOG FOLDER in force           */
    char auto_dir[CARD_DIR_MAX]; /**< AUTO LOG FOLDER in force           */
    bool drawn_gate;          /**< the gate as last drawn: shut          */

    /* The message line, kept as its text's ID and names, not as words,
     * so it is worded in the language showing when it is drawn. */
    fs_msg_kind_t msg_kind;   /**< what the message line holds           */
    ui_text_id_t msg_id;      /**< FS_MSG_TEXT: the text                 */
    fm_err_t msg_err;         /**< FS_MSG_ERR: the answer                */
    char msg_a[FM_NAME_BUF];  /**< the first name it formats, or ""      */
    char msg_b[FM_NAME_BUF];  /**< the second, RENAME's new name, or ""  */
    bool message_ok;          /**< a result, not a refusal               */

    /* The contact that holds a press, where it came down, and what on. */
    bool tap_have;            /**< a press is held                       */
    bool tap_moved;           /**< it went past the slop                 */
    uint8_t tap_id;           /**< its contact                           */
    int16_t tap_x0;           /**< where it came down                    */
    int16_t tap_y0;           /**< where it came down                    */
    int press;                /**< an fs_hit_t                           */
    int press_pane;           /**< a row's or a bar's pane               */
    int press_row;            /**< a row's index on the page             */
    int press_scroll0;        /**< the pane's scroll at the press        */
    bool dragged;             /**< a list press that scrolled            */
    int bar_grab;             /**< px below the thumb's top it was taken */

    bool valid[MAX_FBS];      /**< each framebuffer holds the last frame */
} fs_state_t;

/** @brief The screen's state, defined in files_screen.c. */
extern fs_state_t fs_s;
#define s fs_s   /**< The state by its short name. */

/* --- files_screen.c ----------------------------------------------------- */

/**
 * @brief The selected entry of the active pane.
 * @return the entry, or NULL for none
 */
const fm_entry_t *fs_selected(void);

/**
 * @brief Whether footer button @p b takes a press now, and why not.
 * @param b    the button
 * @param why  takes the refusal, FM_OK when it takes one; may be NULL
 * @return true when it acts
 */
bool fs_foot_live(fs_foot_t b, fm_err_t *why);

/**
 * @brief Whether a pane's folder holds more than its page.
 * @param i  the pane
 * @return true when PREV and NEXT show
 */
bool fs_paged(int i);

/**
 * @brief Act on footer button @p b: a tap on it, live or dimmed.
 * @param b  the button
 */
void fs_foot(fs_foot_t b);

/**
 * @brief Read another page of pane @p i, or the folder above it.
 * @param i   the pane
 * @param go  which page; FM_GO_FIRST with @p up for the folder above
 * @param up  go up one folder
 */
void fs_turn(int i, fm_go_t go, bool up);

/**
 * @brief A tap on row @p row of pane @p i: the pane active and the row
 *        selected; a tap on the selected row of the active pane opens it.
 * @param i    the pane
 * @param row  the row's index on the page
 */
void fs_row_tap(int i, int row);

/** @brief The DELETE question's DELETE: the job that deletes it. */
void fs_delete_doomed(void);

/**
 * @brief NEW's choice: the keyboard for the new entry's name.
 * @param folder  a folder, else an empty text file
 */
void fs_new_name(bool folder);

/**
 * @brief The keyboard closed with @p name: the job that makes or renames.
 * @param name  the name typed
 */
void fs_name_typed(const char *name);

/** @brief CANCEL on the progress box. */
void fs_job_cancel(void);

/**
 * @brief The message line says @p id, with up to two names.
 *
 * The line keeps the ID and the names, and fs_message() words it in the
 * language showing, so a change of SET_LANGUAGE rewords it.
 *
 * @param ok  a result, not a refusal
 * @param id  the text: FM_COPIED, FM_MOVED, LOG_DELETED and FM_MADE format
 *            @p a, FM_RENAMED @p a and @p b; any other is shown as it is
 * @param a   the first name, or NULL
 * @param b   the second name, or NULL
 */
void fs_say(bool ok, ui_text_id_t id, const char *a, const char *b);

/**
 * @brief The message line says why @p err, for @p name (fs_err_text()).
 * @param err   the answer
 * @param name  the entry's name, or NULL
 */
void fs_say_err(fm_err_t err, const char *name);

/** @brief The message line says nothing. */
void fs_quiet(void);

/**
 * @brief The message line in the language showing.
 * @param buf  takes the line
 * @param n    bytes at @p buf, FS_MSG_MAX for the longest
 * @return @p buf, "" for no message; "" also for a NULL @p buf or a zero @p n
 */
const char *fs_message(char *buf, size_t n);

/**
 * @brief The words of a refusal or a failure, for @p name.
 * @param err   the answer
 * @param name  the entry's name, for the formats that name it
 * @param out   takes the sentence
 * @param n     bytes at @p out
 */
void fs_err_text(fm_err_t err, const char *name, char *out, size_t n);

/**
 * @brief The scroll bar's thumb of pane @p i.
 * @param i  the pane
 * @param y  takes its top
 * @param h  takes its height
 */
void fs_bar_thumb(int i, int *y, int *h);

/**
 * @brief The scroll of pane @p i that puts the thumb's top nearest @p top.
 * @param i    the pane
 * @param top  the thumb's top the finger asks for
 * @return the first row on screen
 */
int fs_bar_scroll_at(int i, int top);

/**
 * @brief Pane @p i's scroll held to its rows.
 * @param i  the pane
 */
void fs_clamp_scroll(int i);

/* --- files_draw.c ------------------------------------------------------- */

/**
 * @brief FILES drawn: the panes and the footer, or a box over them, and
 *        the keyboard on top.
 * @param c             the canvas
 * @param buffer_index  the framebuffer, whose cache it follows
 */
void fs_render(gfx_canvas_t *c, int buffer_index);

/**
 * @brief The words of entry kind @p kind.
 * @param kind  an fm_kind_t
 * @return the word
 */
const char *fs_kind_word(int kind);

/* --- files_events.c ----------------------------------------------------- */

/**
 * @brief One touch event, in the screen's coordinates.
 * @param e  the event
 */
void fs_event(const touch_event_t *e);

/** @brief Drop a press under way: the touch stream lost an event. */
void fs_cancel_press(void);
