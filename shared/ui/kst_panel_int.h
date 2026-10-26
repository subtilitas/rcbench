/**
 * @file kst_panel_int.h
 * @brief The seam between the files of the KST and Chaservo programmer's
 *        overlay.
 *
 * The private seam between the files of the programmer's overlay for KST
 * and Chaservo servos: kst_panel.c, the state, the rows, the helpers and
 * the caller's side; kst_panel_text.c, the texts of the messages;
 * kst_panel_warn.c, the warning pages; kst_panel_edit.c, the editors and
 * the actions; kst_panel_touch.c, the presses; kst_panel_run.c, the limit
 * run's sessions; and kst_panel_draw.c, the notes and the pages drawn.
 * The layout they draw on, the rows, the state they read and write, and
 * the functions one calls of another, prefixed kpn_.  Nothing outside
 * these seven files includes this header; the interface is kst_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "kst_panel.h"
#include "kst_prog.h"
#include "touch_types.h"
#include "ui_card_text.h"
#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* ------------------------------------------------------------------ geometry */

/**
 * @brief The overlay's layout, in pixels from the area's top left.  Every
 *        width that is not a control's own comes from the area's width.
 *
 * On SERVO's left card of 620px: five tabs of 95px and CLOSE of 120px
 * in one row 48px tall, rows 48px tall at a 56px pitch, two columns of
 * 295px, a value of 150px (15 cells of KP_FONT), a wide row's of 260px
 * with minus and plus keys of 56px.  Every control ends at kpn_foot().
 */
#define TAB_Y    8            /**< the tabs' and CLOSE's top */
#define TAB_H    UI_TAP_MIN   /**< their height, 48px, as every row's */
#define CLOSE_W  120          /**< CLOSE's width */
#define ROW0     64           /**< the first row's top: the tabs and 8px */
#define PITCH    56           /**< a row's pitch: 8px between two */
#define ROW_H    UI_TAP_MIN   /**< a row's height, 48px */
#define VAL_W    150          /**< a value, 15 cells of KP_FONT */
#define WIDE_W   260          /**< a wide row's value, keys included */
#define STEP_W   56           /**< a stepped row's minus and plus keys */
/** @brief WRITE n VALUES, a row with no label: 22 cells, "20 WERTE
 *         SCHREIBEN" and room, as the Hitec programmer's. */
#define WRITE_W  220

/**
 * @brief The faces: KP_FONT, the type scale's LABEL face (10 x 20), for
 *        the labels, the buttons, the notes and the warning pages' lines;
 *        KP_TITLE_FONT, the HEAD face (20 x 36), for a warning page's
 *        title.  LINE_H is a wrapped row's pitch in KP_FONT and KP_CELL
 *        its cell's width.
 */
#define KP_FONT       (&gfx_font_10x20) /**< labels, buttons, lines */
#define KP_TITLE_FONT (&gfx_font_20x36) /**< a warning page's title    */
#define KP_CELL       10                /**< KP_FONT's cell, px        */
#define LINE_H        20                /**< a wrapped row's pitch, px */

/** @brief The space between the bottom of a control at the foot and the
 *         alert band's top: 4px, as every screen's footer. */
#define FOOT_GAP 4

/* The plus and minus keys repeat as SETUP's do. */
#define REPEAT_DELAY_S 0.45f            /**< delay before the first repeat, 0.45s */
#define REPEAT_SLOW_HZ 8.0f             /**< repeat rate after the delay, 8Hz */
#define REPEAT_FAST_S  1.8f             /**< time from the first press until the fast rate, 1.8s */
#define REPEAT_FAST_HZ 30.0f            /**< repeat rate once fast, 30Hz */

enum { PG_SERVO = 0, PG_TUNE, PG_PULSE, PG_FAILSAFE, PG_LOCKED, PG_COUNT };

enum { K_FIELD = 0, K_STEP, K_LOCKED, K_ACT };

enum {
    A_PORT = 0, A_READ, A_FILES, A_RESTORE, A_MORE, A_ENABLE,
    A_WRITE, A_TYPE
};

/**
 * @brief One row of the overlay: where it sits, what its control does and
 *        what it is called.
 */
typedef struct {
    uint8_t page,                           /**< page the row is on, PG_*                */
            row,                            /**< row on the page, from 0                 */
            col,                            /**< column in the row: 0 left, 1 right      */
            wide,                           /**< 1 for a row that takes both columns     */
            kind,                           /**< K_*: how the control behaves            */
            what;                           /**< kst_field_id_t of the field, or A_* for an action */
    int16_t label;                      /**< ui_text_id_t of the label, or -1 to use name */
    const char *name;                   /**< the field's name, as the vendor's tool has it */
} row_t;

/** @brief The rows of the five pages, in kst_panel.c, and their count. */
#define ROW_COUNT 34
/** @brief The overlay's rows, ROW_COUNT of them. */
extern const row_t kpn_rows[ROW_COUNT];

/** @brief A soft limit's words, by rule, in kst_panel_text.c. */
extern const int16_t kpn_rule[KST_RULE_COUNT];

/* -------------------------------------------------------------------- state */

enum {
    P_NONE = 0, P_TAB, P_CLOSE, P_ROW, P_MINUS, P_PLUS, P_KEYPAD, P_CHOICE,
    P_CHOICE_CANCEL, P_APPLY, P_CANCEL, P_TEXTKEY, P_NOTES
};

enum { C_STRETCH = 0, C_RESTORE, C_MORE, C_FILES, C_TYPE };
#define FILES_SAVE (-1)                 /**< list entry value: the entry that saves a new file */
/**
 * @brief List entry value: the entry that pages on to the files behind these
 */
#define FILES_NEXT (-2)
enum { MORE_VERIFY = 0, MORE_DISCARD, MORE_RELEASE, MORE_VOLTAGE,
       MORE_CURRENT, MORE_WAIT };
/** @brief List value of a restore's source list that forgets the restore. */
#define RESTORE_FORGET ((int)KST_PROG_FROM_COUNT)

enum { W_SOFT = 0, W_ENABLE, W_WRITE, W_RESTORE, W_RELEASE, W_LEAVE,
       W_CHECK, W_HV };

/* The keypad edits what an ENTER asks of the supply, not a row. */
#define KP_VOLTAGE (-1)                 /**< keypad target: the supply voltage an ENTER asks for */
/** @brief Keypad target: the supply current limit an ENTER asks for */
#define KP_CURRENT (-2)
#define KP_WAIT    (-3)                 /**< keypad target: the wait before ENTER */
/** @brief The entries a list holds: the fullest, FILES, has
 *         KST_PROG_FILES_MAX files between the entry that saves and the one
 *         that pages on; 7 rows of ROW_H at PITCH fit above CANCEL on
 *         SERVO's left card. */
#define CHOICE_MAX 7
/** @brief Lines a warning page holds, WARN_LINES of KST_PANEL_LINE bytes. */
#define WARN_LINES 12
/** @brief The rows a page's lines take at most, wrapped to the page's
 *         width: 16 rows at LINE_H take 320px of the 356px from WARN_TOP
 *         to 8px above the buttons on SERVO's left card.  A shorter area
 *         takes fewer (kpn_warn_room()). */
#define WARN_ROWS 16
/* The lines a page gives to the warnings of soft limits. */
/** @brief Lines of a warning page given to the soft limits' warnings. */
#define WARN_RULES 7
/* The lines the page of a file gives to its values. */
/** @brief Lines of a file's page given to its values. */
#define FILE_VALUES 4
/** @brief A warning page's first line under the area's top: the title in
 *         KP_TITLE_FONT from 16px, 36px tall, and 12px. */
#define WARN_TOP 64

/** @brief The overlay's state. */
typedef struct {
    kst_prog_t prog;                    /**< the programmer's session */
    gfx_rect_t area;                    /**< the overlay's area, body pixels */
    bool       open;                    /**< whether the overlay is open */
    uint8_t    note_page;               /**< notes page shown, when the notes take more than one */
    /** @brief Signature of the notes' places and rows the page is of */
    uint32_t   note_sig;
    ui_tabs_t  tabs;                    /**< the five page tabs */
    bool       hv;                      /**< HV SERVO: above KST_PANEL_STD_MV may be set */
    bool       loaded;                  /**< whether the options were read from the settings */
    bool       card_look;               /**< whether the card in the slot is asked for */

    ui_keypad_t kp;                     /**< the keypad that edits a field or the supply */
    /** @brief Row the keypad edits, or KP_VOLTAGE, KP_CURRENT, KP_WAIT */
    int        kp_row;

    ui_textkey_t tk;                    /**< keyboard that types a new file's name */
    char       name[KST_PROG_NAME_MAX];  /**< the name typed */
    bool       name_wait;               /**< whether the save of that name is on its way */
    bool       files_wait;              /**< whether the files list was asked for */
    uint32_t   files_seq;               /**< files sequence number of the list asked for */
    /** @brief The file a load waits with for the servo type: the TYPE list
     *         is open for it, and the load follows the choice */
    char       type_file[KST_PROG_NAME_MAX];

    struct {
        bool open;                      /**< whether the list shows */
        int  kind;                      /**< what the list was opened for, C_* */
        int  count;                     /**< entries */
        char title[64];                 /**< the list's title */
        char labels[CHOICE_MAX][KST_PANEL_LINE];  /**< the entries' labels */
        int  values[CHOICE_MAX];        /**< the entries' values */
    } ch;                               /**< the list of choices */

    struct {
        bool      open;                 /**< whether the page shows */
        int       kind;                 /**< which page, W_* */
        bool      danger;               /**< whether the page is a danger page */
        bool      down;                 /**< whether the hold button is down */
        ui_hold_t hold;                 /**< the hold counter */
        int       count;                /**< lines used */
        char      title[64];            /**< the page's title */
        char      apply[64];            /**< the label of the hold button */
        char      lines[WARN_LINES][KST_PANEL_LINE];  /**< the page's lines */
        uint8_t   level[WARN_LINES];    /**< each line's level */
        bool      no_file;              /**< the page says: no card, no file */
        uint16_t  mv;                   /**< W_HV: the voltage the hold sets, mV */
        kst_prog_restore_t plan;        /**< the restore the page asks for */
    } warn;                             /**< the warning page */

    bool    have;                       /**< whether a press is on the glass */
    uint8_t id;                         /**< touch id of the press */
    int     pressed;                    /**< what the press is on, P_* */
    int     row;                        /**< row the press is on */
    float   held_s;                     /**< seconds a plus or minus key has been held */

    kst_prog_state_t state_seen;        /**< the programmer's state as last drawn */
    uint32_t sig;                       /**< signature of the last draw */
    /** @brief Revision, counted up when the overlay's picture changes */
    uint32_t rev;
    uint32_t hold_rev;                  /**< revision, counted up when a hold advances */

    /* A limit run's step of the programmer (kst_panel_run_connect()). */
    struct {
        uint8_t  op;                    /**< RUN_OP_* */
        bool     kept;                  /**< whether the operator's supply values are kept */
        uint16_t keep_mv,                   /**< kept supply voltage, mV                  */
                 keep_ma;                   /**< kept supply current limit, mA            */
        kst_field_id_t want_id[2];      /**< the write's two fields */
        uint16_t want[2];               /**< and their raw values */
        unsigned writes;                /**< writes of the run that ended well */
        uint32_t said;          /**< programmer's message count at CONNECT */
    } run;                              /**< the limit run's step */
} kst_panel_state_t;

/** @brief The state, kst_panel_k, in kst_panel.c, so its .bss stays
 *         where psram_bss.lf puts kst_panel's.  The files read it as k,
 *         the name it had as the one file's static: the object's own name
 *         keeps it apart from every other screen's state. */
extern kst_panel_state_t kst_panel_k;
/** @brief Short name for kst_panel_k inside the overlay's seven files. */
#define k kst_panel_k

/* --- the functions one file calls of another ---------------------------- */

/* kst_panel.c */

/**
 * @brief The card's words for @p at, for the slot as the feed says it.
 * @param at where the card is
 * @return the text id
 */
ui_text_id_t kpn_card_text_id(ui_card_at_t at);
/**
 * @brief The same words, translated.
 * @param at where the card is
 * @return the text
 */
const char *kpn_card_tr(ui_card_at_t at);
/**
 * @brief The bits set in @p v.
 * @param v the bit set
 * @return the number of ones
 */
unsigned kpn_ones(uint64_t v);
/** @brief The programming port the overlay works on: the SERVO screen's
 *         target, and the first port while that is the servo outputs.
 * @return the port number
 */
uint8_t kpn_port_no(void);
/**
 * @brief Whether the SERVO screen's tester drives a programming port.
 * @return true when the tester drives a programming port
 */
bool kpn_on_port(void);
/**
 * @brief Whether flag @p f is set in the port's report.
 * @param f the flag
 * @return true when the flag is set
 */
bool kpn_flag(uint16_t f);
/**
 * @brief Whether the port is in programming.
 * @return true when the port is in programming
 */
bool kpn_programming(void);
/** @brief A row's label: its text, or for PORT what it holds now.
 * @param r the row
 * @return the label
 */
const char *kpn_row_label(const row_t *r);
/**
 * @brief The name a field has on its row; NULL for one without a row.
 * @param id the field id
 * @return the name, or NULL
 */
const char *kpn_field_name(unsigned id);
/**
 * @brief Whether field @p id is in @p set.
 * @param set the set of fields
 * @param id the field id
 * @return true when the field is in the set
 */
bool kpn_in_set(kst_field_set_t set, unsigned id);
/**
 * @brief A field's unit as its value shows it; "" for none.
 * @param id the field
 * @return the unit
 */
const char *kpn_unit_of(kst_field_id_t id);
/**
 * @brief Digits after the point a field's displayed value has.
 * @param id the field
 * @return the number of digits
 */
int kpn_decimals_of(kst_field_id_t id);
/**
 * @brief A field's value as its row shows it.
 * @param id the field
 * @param raw its raw value
 * @param out the text
 * @param n the size of @p out, bytes
 * @param unit with the unit; without where a line gives it once behind two
 *        values
 */
void kpn_fmt_value(kst_field_id_t id, uint16_t raw, char *out, size_t n,
                   bool unit);
/**
 * @brief kpn_fmt_value() with the unit.
 * @param id the field
 * @param raw its raw value
 * @param out the text
 * @param n the size of @p out, bytes
 */
void kpn_fmt_field(kst_field_id_t id, uint16_t raw, char *out, size_t n);
/**
 * @brief Whether the draft holds another value of @p id than the servo.
 * @param id the field
 * @return true when the draft differs from the servo
 */
bool kpn_changed(kst_field_id_t id);
/**
 * @brief @p s in rows of the area's width, a row each LINE_H.
 * @return the y under the last row
 * @param c the canvas
 * @param x the left edge, px
 * @param y the top edge, px
 * @param s the text
 * @param color the text colour
 */
int kpn_text_rows(gfx_canvas_t *c, int x, int y, const char *s,
                  gfx_color_t color);
/**
 * @brief The tabs.
 * @return the tabs' rectangle
 */
gfx_rect_t kpn_tabs_rect(void);
/**
 * @brief Where a control at the area's foot ends.
 * @return the lower of 8px above the area's foot and FOOT_GAP above the
 *         alert band, in the area's coordinates (body pixels)
 */
int kpn_foot(void);
/**
 * @brief Where the notes and the message under a page end.
 * @return the lower of 6px above the area's foot and FOOT_GAP above the
 *         alert band, in the area's coordinates (body pixels): no text
 *         lies under the band
 */
int kpn_notes_foot(void);
/**
 * @brief Row @p r, its label and its control.
 * @param r the row
 * @return the rectangle
 */
gfx_rect_t kpn_row_rect(const row_t *r);
/**
 * @brief Row @p r's value, between its keys where it has them.
 * @param r the row
 * @return the rectangle
 */
gfx_rect_t kpn_value_rect(const row_t *r);
/**
 * @brief Row @p r's plus key (@p up) or minus key.
 * @param r the row
 * @param up true for the plus key, false for the minus key
 * @return the key's rectangle
 */
gfx_rect_t kpn_step_rect(const row_t *r, bool up);
/**
 * @brief The row at (@p row, @p col) of page @p page, or NULL.
 * @param page the page
 * @param row the row on the page
 * @param col the column in the row
 * @return the row, or NULL
 */
const row_t *kpn_row_at(int page, int row, int col);
/** @brief A file name as a line shows it: whole up to
 *         KST_PANEL_NAME_TYPED characters, else its start and "..".
 * @param name the file name
 * @return the text, static
 */
const char *kpn_shown(const char *name);

/* kst_panel_text.c */

/** @brief Into @p out, KST_PANEL_LINE bytes: how many values stay as they
 *         are, @p n.
 * @param out the text
 * @param n the number of values
 */
void kpn_stay_line(char *out, unsigned n);
/** @brief Into @p out: a line of the interrupted write's record, the GPIO
 *         or the port of kst_prog_journal_arg() in @p arg.
 * @param out the text
 * @param text the text id of the line
 * @param arg the journal's argument
 */
void kpn_journal_line(char *out, int text, unsigned arg);
/** @brief Into @p out: what an edited file is, with the fields it gives a
 *         value for where the card's side counted them.
 * @param out the text
 * @param text the text id of the line
 */
void kpn_kind_line(char *out, int text);
/** @brief Into @p out, @p size bytes: the title of a write of @p n
 *         values.
 * @param out the text
 * @param size the size of @p out, bytes
 * @param n the number of values
 */
void kpn_write_n(char *out, size_t size, unsigned n);

/* kst_panel_warn.c */

/**
 * @brief The rows the page's lines take.
 * @return the number of rows
 */
int kpn_warn_rows(void);
/**
 * @brief The rows a warning page's lines may take in the area: those that
 *        fit between WARN_TOP and 8px above the buttons, WARN_ROWS at
 *        most; 14 in an area 420px tall.
 * @return the rows
 */
int kpn_warn_room(void);
/**
 * @brief The page of an edit of row @p r that passes a soft limit.
 * @param r the row
 */
void kpn_warn_soft(const row_t *r);
/** @brief The page of ENABLE WRITE TO SERVO. */
void kpn_warn_enable(void);
/** @brief The page of a file whose values pass soft limits: its
 *         warnings, and the draft takes it after the hold. */
void kpn_warn_file(void);
/** @brief The page of WRITE: the values, the horn and the card. */
void kpn_warn_write(void);
/**
 * @brief The words for where a restore's values come from.
 * @param from the source
 * @return the text id
 */
ui_text_id_t kpn_from_text(unsigned from);
/**
 * @brief The page of a restore by @p plan.
 * @param plan the restore
 */
void kpn_warn_restore(const kst_prog_restore_t *plan);
/** @brief The page of a backup whose values pass soft limits the servo
 *         keeps: its warnings first, the restore's page after the hold.
 * @param plan the restore
 */
void kpn_warn_check(const kst_prog_restore_t *plan);
/** @brief The page of RELEASE. */
void kpn_warn_release(void);
/** @brief The page of the way back to PWM: the supply cycle after a write
 *         with a PD mini, the note of the one the values wait for without. */
void kpn_warn_leave(void);
/** @brief The page of a programming voltage @p mv, mV, above what a
 *         standard servo is rated for.
 * @param mv the voltage, mV
 */
void kpn_warn_hv(uint16_t mv);
/** @brief The hold completed: what the page asked for. */
void kpn_warn_apply(void);

/* kst_panel_edit.c */

/** @brief Every editor shut: the keypad, the keyboard, the list and the
 *         page; a soft limit's edit is cancelled. */
void kpn_close_editors(void);
/** @brief A file's name typed: the save, or the keyboard again for a name
 *         the card's side does not take.
 * @param name the name typed
 */
void kpn_name_done(const char *name);
/** @brief What the card's side brought: the list, the name again, a
 *         file's warnings. */
void kpn_card_moved(void);
/**
 * @brief Row @p r's editor: a toggle, the list or the keypad.
 * @param r the row
 */
void kpn_edit_field(const row_t *r);
/** @brief The programmer's options into the settings. */
void kpn_options_keep(void);
/** @brief The programmer's options from the settings. */
void kpn_options_load(void);
/**
 * @brief The keypad's value @p v for the row or the supply it edits.
 * @param v the value
 */
void kpn_keypad_done(float v);
/**
 * @brief One step of a plus (@p up) or minus key on row @p r.
 * @param r the row
 * @param up true for plus, false for minus
 */
void kpn_step(const row_t *r, bool up);
/**
 * @brief Whether a LEAVE's wait runs: an abort keeps the session.
 * @param p the session
 * @return true when the wait runs
 */
bool kpn_leave_waits(const kst_prog_t *p);
/**
 * @brief The action of row @p r of the SERVO page.
 * @param r the row
 */
void kpn_act(const row_t *r);
/**
 * @brief The list's choice @p value for what the list was opened for.
 * @param value the entry's value
 */
void kpn_choice_done(int value);
/**
 * @brief Whether row @p r's control takes a tap now.
 * @param r the row
 * @return true when the control takes a tap
 */
bool kpn_row_live(const row_t *r);

/* kst_panel_touch.c */

/** @brief The supply's own controls follow the port: while one is out of
 *         PWM they offer an OFF and nothing else. */
void kpn_supply_sync(void);

/* kst_panel_draw.c */

/** @brief Whether (@p x, @p y) is on notes that take more than one
 *         page.
 * @param x the column, px
 * @param y the row, px
 * @return true when the point is on paged notes
 */
bool kpn_notes_paged_at(int x, int y);
