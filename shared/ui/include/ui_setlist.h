/**
 * @file
 * @brief A settings list: SETUP's category list as a widget, for SETUP and for
 *        every screen's SETTINGS.
 *
 * Full width under the band: rows from x 10 (a host may move the left
 * edge, SENSORS' to 240) to 994, body y 8 to 436, a scroll bar at x 1002.
 * A row is 64px tall at a 70px pitch, 6 rows whole; a heading row 34px at a
 * 40px pitch.  Under the list the strip of three 328 x 48px columns at body
 * y 444 (ui_footer.h's slots): the list's name, RESET name and SAVE.
 *
 * A row is one of:
 * - a setting (shared/settings): its label in the head face (20 x 36), its
 *   help in the label face (10 x 20), its value in the body face (12 x 24)
 *   between a "-" and a "+" key of 56 x 48px, or "<" and ">" for a switch
 *   and a choice.  A tap on a number's value opens the keypad over the list
 *   and the strip (ui_setting_keypad.h);
 * - a string: its text at the right, a tap opens the keyboard;
 * - a custom row, {label, help, value_text(), step(dir), tap()} for what is
 *   not a setting: a value with keys, a row that does something on a tap,
 *   or a choice list that opens over the list;
 * - a heading over the rows of a section, with a line beside its name.
 *
 * A key is a tap: its release steps it once; held, it repeats after 0.45s
 * at 8Hz, at 30Hz 1.8s later.  The list scrolls by a drag that starts
 * anywhere on it.  One contact holds the list from its DOWN to its UP.
 *
 * SAVE asks for a write (settings_request_save()) and reads SAVED, SAVE,
 * WHEN IDLE or NOT SAVED; the values are one set, so SAVE writes every
 * change, not only this list's.  RESET name sets the list's own settings
 * to their defaults and no other.  The leave warning (ui_setlist_warn())
 * lists what differs from the stored values and offers STAY, DISCARD and
 * SAVE; DISCARD takes no press while the bench is ARMED.
 *
 * The host gives the rows, the name and what a change does through
 * ui_setlist_host_t, and draws what is its own (SETUP's tiles) through the
 * hit hooks.  The widget asks the host to repaint through its invalidate()
 * hook; it keeps no frame buffer cache of its own.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "settings.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_text.h"
#include "ui_textkey.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A row's height, px: 64. */
#define UI_SL_ROW_H 64
/** @brief The gap under a row, px: 6, a 70px pitch. */
#define UI_SL_ROW_GAP 6
/** @brief A heading row's height, px: 34, a 40px pitch. */
#define UI_SL_HEAD_H 34
/** @brief The list's top in the body, px. */
#define UI_SL_LIST_Y 8
/** @brief The list's right edge, px: 994, 8px left of the scroll bar. */
#define UI_SL_LIST_R 994
/** @brief The rows a choice list shows at most. */
#define UI_SL_CHOICES_MAX 7
/** @brief The change lines the leave warning shows, the last of them the
 *         count of the rest when there are more. */
#define UI_SL_WARN_LINES 10

/** @brief What a row of the list is. */
typedef enum {
    UI_SL_NONE = 0,  /**< past the last row                      */
    UI_SL_SETTING,   /**< a setting of shared/settings            */
    UI_SL_TEXT,      /**< a string of shared/settings             */
    UI_SL_CUSTOM,    /**< a custom row                           */
    UI_SL_HEADING,   /**< a section's heading                    */
} ui_sl_kind_t;

/**
 * @brief A row that is not a setting.  Each hook may be NULL.
 *
 * With step() the row draws "-" and "+" keys ("<" and ">" with cycles); with
 * tap() a tap on the row calls it; with choices() a tap opens a choice list
 * of that many rows over the list, choice() names one, chosen() is the one
 * in force and pick() takes a tap on one.  locked() names why the row takes
 * no press now; its help line shows it in the warning colour.  On a
 * heading only value_text() and locked() are read: the line beside its
 * name, in the warning colour while locked() names something.
 */
typedef struct ui_setlist_custom {
    ui_text_id_t label;                 /**< the row's name                   */
    ui_text_id_t help;                  /**< its help line, TX_COUNT for none */
    bool cycles;                        /**< "<" and ">" keys, not "-" and "+" */
    /** @brief The row's value, "" for none. */
    void (*value_text)(char *out, size_t n);
    /** @brief One step, @p dir -1 or +1. */
    void (*step)(int dir);
    /** @brief A tap on the row. */
    void (*tap)(void);
    /** @brief The choice list's rows, 0 for none. */
    int (*choices)(void);
    /** @brief Choice @p i's text. */
    const char *(*choice)(int i);
    /** @brief The choice in force, -1 for none. */
    int (*chosen)(void);
    /** @brief A tap on choice @p i. */
    void (*pick)(int i);
    /** @brief Why the row takes no press, NULL while it takes one. */
    const char *(*locked)(void);
} ui_setlist_custom_t;

/** @brief One row of a list. */
typedef struct {
    uint8_t                    kind;    /**< ui_sl_kind_t                     */
    int8_t                     text;    /**< UI_SL_TEXT: a setting_text_id_t  */
    setting_id_t               id;      /**< UI_SL_SETTING: the setting       */
    ui_text_id_t               heading; /**< UI_SL_HEADING: its name          */
    const ui_setlist_custom_t *custom;  /**< UI_SL_CUSTOM's, a heading's line */
} ui_setlist_row_t;

/** @brief A setting's row. */
#define UI_SL_ROW_SETTING(id_) \
    { (uint8_t)UI_SL_SETTING, -1, (id_), TX_COUNT, NULL }
/** @brief A heading's row, @p line_ a custom row whose value_text() is its
 *         line, or NULL. */
#define UI_SL_ROW_HEADING(name_, line_) \
    { (uint8_t)UI_SL_HEADING, -1, (setting_id_t)0, (name_), (line_) }
/** @brief A custom row. */
#define UI_SL_ROW_CUSTOM(custom_) \
    { (uint8_t)UI_SL_CUSTOM, -1, (setting_id_t)0, TX_COUNT, (custom_) }

/** @brief The hit kinds from which a host's own controls count. */
#define UI_SL_HIT_HOST 32

/**
 * @brief What a host gives the list.  count() and row() are required; every
 *        other hook may be NULL, which takes the default named.
 */
typedef struct ui_setlist_host {
    /** @brief The rows. */
    int (*count)(void);
    /** @brief Row @p i, a UI_SL_NONE row past the last. */
    ui_setlist_row_t (*row)(int i);
    /** @brief Whether a row may be a heading: false keeps every row at the
     *         70px pitch without asking row() for each one. */
    bool headings;
    /** @brief The list's left edge, px; NULL for 10. */
    int (*left)(void);
    /** @brief The name in the strip's first column. */
    const char *(*name)(void);
    /** @brief The line under the name, false for none (the name alone, as
     *         SETUP's); @p warn takes the warning colour. */
    bool (*name_line)(char *out, size_t n, bool *warn);
    /** @brief RESET's label; NULL for "RESET name". */
    void (*reset_label)(char *out, size_t n);
    /** @brief RESET; NULL resets the list's settings (settings_reset_rows())
     *         and calls changed(SETTING_COUNT). */
    void (*reset)(void);
    /** @brief A setting's help line; NULL for ui_setting_help(). */
    const char *(*help)(setting_id_t id);
    /** @brief Whether the IOmcu does not hold a setting's value; NULL for
     *         never.  The row's edge, help and value take the warning colour. */
    bool (*unheld)(setting_id_t id);
    /** @brief Why a setting's row takes no press now, NULL while it takes
     *         one; NULL for none.  No keys, no keypad, the reason in place of
     *         the help in the warning colour. */
    const char *(*locked)(setting_id_t id);
    /** @brief A setting changed by a step, the keypad or a reset;
     *         SETTING_COUNT for several (RESET, DISCARD). */
    void (*changed)(setting_id_t id);
    /** @brief A typed value before it is set; NULL takes it as typed. */
    float (*typed)(setting_id_t id, float v);
    /** @brief Narrows the keypad's range of a setting, the schema's handed
     *         in; NULL keeps the schema's.  A value outside is refused. */
    void (*range)(setting_id_t id, float *min, float *max);
    /** @brief The list's look changed. */
    void (*invalidate)(void);
    /** @brief The leave warning's SAVE and DISCARD: the way out it held. */
    void (*way_out)(void);
    /** @brief A DOWN on the host's own controls: a kind from UI_SL_HIT_HOST,
     *         and @p index, or 0 for none.  @p jump: a DOWN the tracker made
     *         for a jump. */
    int (*press)(int x, int y, bool jump, int *index);
    /** @brief Whether a host's press still stands at a MOVE; NULL for yes. */
    bool (*still)(int kind, int index, int x, int y);
    /** @brief The tap UP of a host's press. */
    void (*release)(int kind, int index, int x, int y);
} ui_setlist_host_t;

/** @brief A list's state; zero it with ui_setlist_init(). */
typedef struct ui_setlist {
    const ui_setlist_host_t *host;      /**< the host                         */
    bool     open;                      /**< the rows show (SETUP's hub: no)  */
    int      scroll;                    /**< the scroll, px                   */
    int      scroll_max;                /**< the largest scroll, px           */

    bool     have_press;                /**< a contact holds the list         */
    uint8_t  press_id;                  /**< its touch id                     */
    int      hit_kind;                  /**< what it pressed, UI_SL_HIT_*     */
    int      hit_index;                 /**< the row, or the host's index     */
    bool     dragging;                  /**< the press became a scroll        */
    int      press_x;                   /**< x of the DOWN, px                */
    int      press_y;                   /**< y of the DOWN, px                */
    int      last_y;                    /**< y of the last MOVE, px           */
    float    held_for;                  /**< a key held within the slop, s    */
    bool     repeating;                 /**< the held key has repeated        */

    bool     warn;                      /**< the leave warning shows          */
    bool     drawn_dirty;               /**< SAVE's inputs as last drawn      */
    bool     drawn_asked;               /**< ditto: a save asked              */
    bool     drawn_failed;              /**< ditto: the last save refused     */
    bool     drawn_armed;               /**< ARMED as last drawn              */

    ui_textkey_t      tk;               /**< the keyboard of a string         */
    setting_text_id_t tk_text;          /**< the string it types              */
    bool     drawn_tk;                  /**< it was open as last drawn        */
    uint32_t drawn_tk_rev;              /**< its revision as last drawn       */

    ui_keypad_t  kp;                    /**< the keypad of a number           */
    setting_id_t kp_id;                 /**< the number it types              */
    bool     drawn_kp;                  /**< it was open as last drawn        */
    uint32_t drawn_kp_rev;              /**< its revision as last drawn       */

    int      choice_row;                /**< the row whose choices show, -1   */
} ui_setlist_t;

/** @brief The list's own hit kinds. */
enum {
    UI_SL_HIT_NONE = 0,
    UI_SL_HIT_MINUS,      /**< a "-" or "<" key                       */
    UI_SL_HIT_PLUS,       /**< a "+" or ">" key                       */
    UI_SL_HIT_RESET,      /**< RESET name                           */
    UI_SL_HIT_LIST,       /**< the list, for a scroll                 */
    UI_SL_HIT_SAVE,       /**< SAVE                                   */
    UI_SL_HIT_STAY,       /**< the warning's STAY                     */
    UI_SL_HIT_DISCARD,    /**< the warning's DISCARD                  */
    UI_SL_HIT_LEAVE_SAVE, /**< the warning's SAVE                     */
    UI_SL_HIT_TEXT,       /**< a string's row                         */
    UI_SL_HIT_VALUE,      /**< a number's value                       */
    UI_SL_HIT_ROW,        /**< a custom row's tap or choice           */
    UI_SL_HIT_CHOICE,     /**< a row of the open choice list          */
};

/** @brief The parts of a row a test aims at. */
typedef enum {
    UI_SL_PART_MINUS = 0, /**< the "-" key's column over the row's height */
    UI_SL_PART_VALUE,     /**< the value between the keys                */
    UI_SL_PART_PLUS,      /**< the "+" key's column                      */
} ui_sl_part_t;

/**
 * @brief A list for @p host, closed, at its top, no press.
 * @param sl   The list.
 * @param host The host; it outlives the list.
 */
void ui_setlist_init(ui_setlist_t *sl, const ui_setlist_host_t *host);

/**
 * @brief The list shown at its top, no press, nothing over it.
 * @param sl The list.
 */
void ui_setlist_open(ui_setlist_t *sl);

/**
 * @brief The list closed: the warning, the keyboard, the keypad and a choice
 *        list close, the press drops, and what was typed is not taken.  The
 *        values stay as they are, unsaved where they are.
 * @param sl The list.
 */
void ui_setlist_close(ui_setlist_t *sl);

/**
 * @brief The warning, the keyboard, the keypad and the choice list close
 *        and the press drops; whether the list shows stays.
 * @param sl The list.
 */
void ui_setlist_drop(ui_setlist_t *sl);

/**
 * @brief The home tag over the list: the keypad and the keyboard close as
 *        their CANCEL does, a choice list closes, the warning closes as
 *        STAY does.
 * @param sl The list.
 * @return True when it closed one of them.
 */
bool ui_setlist_back(ui_setlist_t *sl);

/**
 * @brief The warning opened for a way out, while something is unsaved and
 *        no save is asked for.
 * @param sl The list.
 * @return True when it opened: the way out is held.
 */
bool ui_setlist_warn(ui_setlist_t *sl);

/**
 * @brief Whether something covers the list: the keypad, the keyboard, the
 *        warning or a choice list.
 * @param sl The list.
 * @return True while one shows.
 */
bool ui_setlist_covered(const ui_setlist_t *sl);

/**
 * @brief A touch event, in the body's coordinates: to the keypad or the
 *        keyboard while one is open, else to the press, the scroll and the
 *        release it ends in.
 * @param sl  The list.
 * @param evt The event.
 */
void ui_setlist_event(ui_setlist_t *sl, const touch_event_t *evt);

/**
 * @brief Touch events were lost: the press drops, a key held on the keypad
 *        or the keyboard is let go.
 * @param sl The list.
 */
void ui_setlist_cancel(ui_setlist_t *sl);

/**
 * @brief A frame: SAVE's inputs followed, a keypad whose row is held since
 *        it opened closed, a held key repeated.
 * @param sl   The list.
 * @param dt_s The frame's time, s.
 */
void ui_setlist_tick(ui_setlist_t *sl, float dt_s);

/**
 * @brief The list drawn over the body: the keypad alone while it is open,
 *        else the warning, or the rows, the strip and SAVE, with a choice
 *        list and the keyboard over them.  The body is cleared first.
 * @param sl The list.
 * @param c  The canvas.
 */
void ui_setlist_render(ui_setlist_t *sl, gfx_canvas_t *c);

/**
 * @brief The rows and the scroll bar, clipped to the list.
 * @param sl The list.
 * @param c  The canvas.
 */
void ui_setlist_draw_rows(ui_setlist_t *sl, gfx_canvas_t *c);

/**
 * @brief The strip's first two columns: the name, with the host's line
 *        under it, and RESET name or the host's label for it.
 * @param sl The list.
 * @param c  The canvas.
 */
void ui_setlist_draw_strip(ui_setlist_t *sl, gfx_canvas_t *c);

/**
 * @brief SAVE in the strip's third column, in its four states.
 * @param sl The list.
 * @param c  The canvas.
 */
void ui_setlist_draw_save(ui_setlist_t *sl, gfx_canvas_t *c);

/**
 * @brief The leave warning over the body down to the strip, and its three
 *        answers in the strip.
 * @param sl The list.
 * @param c  The canvas.
 */
void ui_setlist_draw_warning(ui_setlist_t *sl, gfx_canvas_t *c);

/**
 * @brief The open choice list over the rows, if one is open.
 * @param sl The list.
 * @param c  The canvas.
 */
void ui_setlist_draw_choices(const ui_setlist_t *sl, gfx_canvas_t *c);

/**
 * @brief Line @p i of the warning: a change as "name: saved -> now", or
 *        past UI_SL_WARN_LINES - 1 changes their count.
 * @param sl   The list.
 * @param i    The line, from 0.
 * @param line Takes the text.
 * @param n    Bytes at @p line.
 * @return False past the last line, or while the warning is closed.
 */
bool ui_setlist_warn_line(const ui_setlist_t *sl, int i, char *line,
                          size_t n);

/* --------------------------------------------------------- screen guide */

struct ui_help_page;

/**
 * @brief The screen guide's pages of an open list: its rows, name and RESET
 *        with the dock on the right, then one row's keys and value and SAVE
 *        with the dock on the left; none while something covers it.
 * @param sl The list.
 * @return The count, 0 or 2.
 */
int ui_setlist_help_count(const ui_setlist_t *sl);

/**
 * @brief Page @p i of the list's guide.
 * @param sl  The list.
 * @param i   The page, from 0.
 * @param out The page, filled.
 * @return False past the last page.
 */
bool ui_setlist_help_page(const ui_setlist_t *sl, int i,
                          struct ui_help_page *out);

/* ------------------------------------------------------------- geometry */

/**
 * @brief Column @p col of the strip: ui_footer.h's slot @p col.
 * @param col 0 to 2.
 * @return Its rectangle in the body.
 */
gfx_rect_t ui_setlist_strip_rect(int col);

/**
 * @brief The list's pane: from its left edge to 994, body y 8 to 436.
 * @param sl The list.
 * @return The pane.
 */
gfx_rect_t ui_setlist_pane(const ui_setlist_t *sl);

/**
 * @brief Row @p i's rectangle at the list's scroll, partly or wholly outside
 *        the pane when scrolled.
 * @param sl The list.
 * @param i  The row.
 * @return Its rectangle.
 */
gfx_rect_t ui_setlist_row_rect(const ui_setlist_t *sl, int i);

/**
 * @brief A part of row @p i: a key's column or the value between them, over
 *        the row's height.
 * @param sl   The list.
 * @param i    The row.
 * @param part The part.
 * @return Its rectangle.
 */
gfx_rect_t ui_setlist_row_part(const ui_setlist_t *sl, int i,
                               ui_sl_part_t part);

/**
 * @brief The row under a point of the pane.
 * @param sl The list.
 * @param x  The point's x, px.
 * @param y  Its y.
 * @return The row; -1 off every row.
 */
int ui_setlist_row_at(const ui_setlist_t *sl, int x, int y);

/**
 * @brief The rows' count.
 * @param sl The list.
 * @return The count.
 */
int ui_setlist_count(const ui_setlist_t *sl);

/**
 * @brief The scroll held to 0 to the content's height less the pane's.
 * @param sl The list.
 */
void ui_setlist_clamp(ui_setlist_t *sl);

/**
 * @brief The list scrolled so row @p i shows whole, at the pane's top where
 *        the content allows.
 * @param sl The list.
 * @param i  The row.
 */
void ui_setlist_scroll_to(ui_setlist_t *sl, int i);

/**
 * @brief The choice list's row @p k, while one is open.
 * @param sl The list.
 * @param k  The choice.
 * @return Its rectangle; an empty one at (0, 0) with none open.
 */
gfx_rect_t ui_setlist_choice_rect(const ui_setlist_t *sl, int k);

/**
 * @brief Whether SAVE takes a press: a setting is changed and no save is
 *        asked for.
 * @return True when it does.
 */
bool ui_setlist_save_offered(void);

/**
 * @brief The setting the open keypad types.
 * @param sl The list.
 * @return The setting; -1 with the keypad closed.
 */
int ui_setlist_keypad_setting(const ui_setlist_t *sl);

/**
 * @brief A string's name, for its row, its keyboard and the warning.
 * @param id The string.
 * @return The name.
 */
const char *ui_setlist_text_label(setting_text_id_t id);

/**
 * @brief A list host's rows from a table: the count of @p rows and row @p i
 *        of it, for a host whose rows are fixed.
 * @param rows The table.
 * @param n    Its length.
 * @param i    The row.
 * @return Row @p i, a UI_SL_NONE row past the last.
 */
ui_setlist_row_t ui_setlist_table_row(const ui_setlist_row_t *rows, int n,
                                      int i);

#ifdef __cplusplus
}
#endif
