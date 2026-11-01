/**
 * @file
 * @brief The seam between the files of the ESC signal-wire session.
 *
 * esc_wire_panel.c holds the state, the stages, the page set and the card's
 * flow; esc_wire_edit.c the rows, the draft and the editors; esc_wire_text.c the
 * words of the messages and the help lines; esc_wire_touch.c the presses;
 * esc_wire_draw.c the pages drawn.  The functions one calls of another are
 * prefixed ewp_.  Nothing outside these files and the test's seam includes this
 * header; the interface is esc_wire_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_WIRE_INT_H
#define RCBENCH_ESC_WIRE_INT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esc_card.h"
#include "esc_prog.h"
#include "esc_wire_panel.h"
#include "ew_map.h"
#include "gfx.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* ------------------------------------------------------------------ geometry */

/**
 * @brief The session's layout in body coordinates, 1024px wide, the area's top at
 *        PROG_FULL_Y: tabs and BACK 48px tall, 2 columns of 6 rows 48px tall at a
 *        56px pitch, one line of messages, and a row of actions whose foot is
 *        UI_KEYS_FOOT.
 */
#define EWP_TAB_DY   4    /**< the tabs' top under the area's, px             */
#define EWP_TAB_H    UI_TAP_MIN /**< their height                             */
#define EWP_BACK_W   110  /**< BACK's width                                   */
#define EWP_ROW_DY   60   /**< the first row's top under the area's           */
#define EWP_PITCH    56   /**< a row's pitch                                  */
#define EWP_ROW_H    UI_TAP_MIN /**< a row's height                           */
#define EWP_VAL_W    232  /**< a value's width: 23 cells                      */
#define EWP_ROWS     6    /**< rows in a column                               */
#define EWP_COLS     2    /**< columns                                        */
#define EWP_ACT_H    UI_TAP_MIN /**< the action row's height                  */
#define EWP_FONT     (&gfx_font_10x20) /**< rows, values and lines            */
#define EWP_CELL     10   /**< its cell, px                                   */
#define EWP_LINE_H   24   /**< a text line's pitch                            */

/* --------------------------------------------------------------------- state */

/** @brief What the session shows, from the page's state. */
typedef esc_wire_stage_t ewp_stage_t;

/** @brief A row of settings: a byte's field or an ESCape32 key. */
typedef struct {
    const ew_field_t *f; /**< the field, or NULL                              */
    const ew_key_t   *k; /**< the key, or NULL                                */
    uint8_t slot;        /**< the key's place in ew_map_e32                   */
} ewp_row_t;

/** @brief A hold page's kinds. */
enum {
    EWP_PAGE_NONE = 0,
    EWP_PAGE_WRITE,      /**< WRITE n VALUES and its warning                  */
    EWP_PAGE_NO_BACKUP,  /**< no card: write without a backup                 */
    EWP_PAGE_LEAVE,      /**< LEAVE                                           */
    EWP_PAGE_RELEASE,    /**< a pin another writer holds                      */
    EWP_PAGE_RESTORE,    /**< RESTORE FROM BACKUP                             */
};

/** @brief What the choice list chooses. */
enum {
    EWP_CH_NONE = 0,
    EWP_CH_TARGET,  /**< the target                                           */
    EWP_CH_POWER,   /**< SWITCHED or BY HAND                                  */
    EWP_CH_FIELD,   /**< a value of a row                                     */
    EWP_CH_FILES,   /**< a file                                               */
};

/** @brief What the keypad enters. */
enum {
    EWP_KP_NONE = 0,
    EWP_KP_VOLT,
    EWP_KP_AMP,
    EWP_KP_HOLD,
    EWP_KP_OFF,
    EWP_KP_ROW,
};

/** @brief The writes' steps, between the hold and the command. */
enum {
    EWP_W_NONE = 0,
    EWP_W_BACKUP, /**< the backup is on its way to the card                   */
    EWP_W_RECORD, /**< the record is on its way                               */
};

/** @brief Entries of a choice list at most: two columns of 7. */
#define EWP_CHOICE_MAX 14
/** @brief Lines of a hold page at most. */
#define EWP_PAGE_LINES 14
/** @brief Cells of a line at most, with the terminator. */
#define EWP_LINE 101
/** @brief Tabs at most. */
#define EWP_TABS 7
/** @brief Changes on a page of the WRITE list. */
#define EWP_WRITE_PER_PAGE 12
/** @brief A value above any of a choice's own: enter a number on the keypad. */
#define EWP_CHOICE_NUMBER 0x7FFF
/** @brief A choice's value that goes to the next page of files. */
#define EWP_CHOICE_NEXT 0x7FFE

/** @brief The state. */
typedef struct {
    gfx_rect_t area;      /**< the session's area                             */
    bool       open;      /**< a row opened it                                */
    bool       armed;     /**< the bench is armed or asked to                 */
    uint32_t   rev;       /**< changes when a render would show something else */
    uint32_t   hold_rev;  /**< changes while a hold fills                     */
    uint32_t   sig;       /**< what the page showed at the last feed          */
    esc_prog_t prog;      /**< the model                                      */
    /* The connect page's values. */
    esc_wire_target_t targets[ESC_WIRE_TARGETS]; /**< what can be programmed  */
    uint8_t    target_count; /**< entries of @c targets                       */
    bool       targets_known; /**< the binding was read                       */
    uint8_t    target_at; /**< the chosen entry                               */
    uint8_t    power;     /**< a link_esc_power_t                             */
    bool       supply_typed; /**< the operator typed the session's values     */
    uint16_t   mv;        /**< the session's voltage, mV; SUPPLY's until typed */
    uint16_t   ma;        /**< the session's limit, mA; SUPPLY's until typed  */
    uint32_t   hold_ms;   /**< ENTRY HOLD                                     */
    uint16_t   off_ms;    /**< OFF TIME                                       */
    /* The session. */
    uint8_t    draft[ESC_LINK_PAGE_MAX]; /**< the edits on the read copy      */
    uint32_t   draft_gen; /**< the read copy it was made from                 */
    ui_tabs_t  tabs;      /**< the tabs                                       */
    int        tab_set;   /**< what they were laid out for: fw x 256 + layout */
    int        tab_count; /**< tabs                                           */
    /* The presses. */
    bool       have;      /**< a press is taken                               */
    uint8_t    id;        /**< its finger                                     */
    int        pressed;   /**< what it is on                                  */
    int        which;     /**< its row                                        */
    ui_hold_t  hold;      /**< the 2s hold                                    */
    bool       hold_down; /**< a press fills it                               */
    /* The hold page. */
    int        page;      /**< an EWP_PAGE_*                                  */
    int        page_lines; /**< its lines                                     */
    char       lines[EWP_PAGE_LINES][EWP_LINE]; /**< them                     */
    bool       line_warn[EWP_PAGE_LINES]; /**< in the warning colour          */
    int        w_page;    /**< the WRITE list's page shown                    */
    bool       w_seen_last; /**< its last page was shown: the hold is offered */
    /* The editors. */
    ui_keypad_t kp;       /**< the keypad                                     */
    int        kp_what;   /**< an EWP_KP_*                                    */
    ewp_row_t  edit;      /**< the row in an editor                           */
    int        ch_what;   /**< an EWP_CH_*                                    */
    int        ch_count;  /**< entries                                        */
    int        ch_values[EWP_CHOICE_MAX]; /**< their values                   */
    char       ch_labels[EWP_CHOICE_MAX][ESC_CARD_NAME_MAX + 8]; /**< labels  */
    /* The card. */
    bool       card_look; /**< a tap looks for a card                         */
    bool       card_want; /**< a request waits to be taken                    */
    bool       card_out;  /**< a request is on the card's task                */
    uint32_t   card_id;   /**< the request's number                           */
    esc_card_req_t card;  /**< the request                                    */
    int        w_step;    /**< an EWP_W_*                                     */
    uint8_t    w_draft[ESC_LINK_PAGE_MAX]; /**< what the WRITE page lists: the
                                                hold writes this, not the draft */
    ewp_stage_t stage_seen; /**< the page's stage at the last feed            */
    bool       files_for_restore; /**< the list's choice restores             */
    bool       no_card_ok; /**< the operator goes without a card this session */
    char       file_name[ESC_CARD_NAME_MAX]; /**< the file chosen             */
    uint16_t   files_first; /**< the list's first file                        */
    uint16_t   files_total; /**< the card's files                             */
    char       names[ESC_CARD_FILES][ESC_CARD_NAME_MAX]; /**< the list        */
    char       file_text[EW_FILE_MAX + 1u]; /**< a file loaded                */
    size_t     file_len;  /**< its bytes                                      */
    /* The session's own word. */
    char       note[EWP_LINE]; /**< what the card or the editor said          */
    esc_prog_msg_t msg_seen; /**< the model's message at the last feed        */
    uint16_t   detail_seen; /**< its code                                     */
    bool       note_warn; /**< in the warning colour                          */
} ewp_state_t;

/** @brief The state. */
extern ewp_state_t ewp_s;

/* --------------------------------------------------- esc_wire_panel.c: stages */

/**
 * @brief Where the session is, from the page and the model.
 * @return The stage.
 */
ewp_stage_t ewp_stage(void);

/**
 * @brief ABORT: a write waiting for the card is given up, else the page is cut.
 */
void ewp_abort(void);

/**
 * @brief Whether a request is with the card or waits to go.
 * @return true then.
 */
bool ewp_card_busy(void);

/**
 * @brief The text for a write or a save with no backup to make.
 * @return The text.
 */
const char *ewp_no_backup_text(void);

/**
 * @brief A note under the rows.
 * @param s    The text.
 * @param warn In the warning colour.
 */
void ewp_note(const char *s, bool warn);

/**
 * @brief Hands a request to the card's task.
 * @param op An esc_card_op_t, the rest of @c card filled in.
 */
void ewp_card_ask(uint8_t op);

/**
 * @brief The hold reached its end: what it stands for goes.
 */
void ewp_hold_apply(void);

/**
 * @brief The list of files asked of the card.
 * @param restore A choice restores, on an erased AM32.
 */
void ewp_files_open(bool restore);

/**
 * @brief The next step of the write the hold started, after the backup.
 */
void ewp_write_go(void);

/**
 * @brief A file chosen in the list.
 * @param value The choice: a name's index, -1 to save, or the next page.
 */
void ewp_file_chosen(int value);

/* ----------------------------------------------------- esc_wire_touch.c: presses */

/**
 * @brief One touch event.
 * @param evt The event.
 */
void ewp_event(const touch_event_t *evt);

/**
 * @brief Time for the hold.
 * @param dt_s Seconds since the last.
 */
void ewp_tick(float dt_s);

/** @brief Touch events were lost: the press goes. */
void ewp_cancel(void);

/** @brief Home tag or BACK: an editor closes, a held session asks first. */
void ewp_back(void);

/**
 * @brief Whether a press at (x, y) lands on ABORT.
 * @param x In body coordinates.
 * @param y In body coordinates.
 * @return true then.
 */
bool ewp_safe_at(int x, int y);

/* ------------------------------------------------------ esc_wire_draw.c: pages */

/**
 * @brief The session drawn.
 * @param c The canvas.
 */
void ewp_render(gfx_canvas_t *c);

/* ------------------------------------------------------ esc_wire_edit.c: rows */

/**
 * @brief The tabs for the firmware and layout the page reports; laid out once.
 */
void ewp_tabs_follow(void);

/**
 * @brief The tab's text id.
 * @param tab The tab.
 * @return Its text.
 */
ui_text_id_t ewp_tab_text(int tab);

/**
 * @brief The row of a tab.
 * @param tab The tab, from 0; tab 0 is IDENTITY and holds none.
 * @param i   The row, from 0.
 * @param out Takes it.
 * @return false past the last.
 */
bool ewp_row(int tab, int i, ewp_row_t *out);

/**
 * @brief A row's name.
 * @param r The row.
 * @return Its name, English in every language.
 */
const char *ewp_row_name(const ewp_row_t *r);

/**
 * @brief A row's value in the draft.
 * @param r    The row.
 * @param out  Takes the text.
 * @param size Bytes at @p out.
 */
void ewp_row_text(const ewp_row_t *r, char *out, size_t size);

/**
 * @brief Whether a row's draft differs from the read copy.
 * @param r The row.
 * @return true then.
 */
bool ewp_row_changed(const ewp_row_t *r);

/**
 * @brief Whether a row takes an edit.
 * @param r The row.
 * @return true on a page that is written, for an editable field.
 */
bool ewp_row_live(const ewp_row_t *r);

/**
 * @brief The fields the draft changes.
 * @return Their count.
 */
int ewp_changed_count(void);

/**
 * @brief Whether the session's firmware has rows at all: else the hex page.
 * @return true then.
 */
bool ewp_has_rows(void);

/**
 * @brief Opens the row's editor: a list of its values, or the keypad.
 * @param r The row.
 */
void ewp_edit_open(const ewp_row_t *r);

/**
 * @brief A choice taken.
 * @param i The entry.
 */
void ewp_choice_done(int i);

/**
 * @brief The keypad's value taken.
 * @param v The value.
 */
void ewp_keypad_done(float v);

/**
 * @brief The connect page's row tapped.
 * @param row Its index: 0 TARGET, 1 POWER, 2 voltage, 3 limit, 4 ENTRY HOLD, 5 OFF
 *            TIME.
 */
void ewp_connect_row(int row);

/**
 * @brief Adds a choice.
 * @param label Its label.
 * @param value Its value.
 */
void ewp_choice_add(const char *label, int value);

/**
 * @brief Closes the editors.
 */
void ewp_editors_close(void);

/**
 * @brief The hold page of WRITE n VALUES: takes the draft as the snapshot the hold
 *        writes and lists its changes, second-look fields first, in the warning
 *        colour with the word CHECK, EWP_WRITE_PER_PAGE to a page.
 */
void ewp_page_write(void);

/**
 * @brief How many pages the WRITE list has.
 * @return Their count, at least 1.
 */
int ewp_write_pages(void);

/**
 * @brief The WRITE list's NEXT or PREV.
 * @param delta +1 or -1.
 */
void ewp_write_turn(int delta);

/**
 * @brief A hold page's start.
 * @param kind An EWP_PAGE_*.
 */
void ewp_page_open(int kind);

/**
 * @brief A hold page's line.
 * @param s The text.
 */
void ewp_page_add(const char *s);

/* -------------------------------------------------- esc_wire_text.c: messages */

/**
 * @brief The model's last message in words.
 * @return The text; "" for none.
 */
const char *ewp_message_text(void);

/**
 * @brief Whether the model's last message is an error.
 * @return true then.
 */
bool ewp_message_warns(void);

/**
 * @brief Why the page is not written.
 * @param why An ew_why_t.
 * @param out Takes the text.
 * @param size Bytes at @p out.
 */
void ewp_why_text(uint8_t why, char *out, size_t size);

/**
 * @brief A firmware's name.
 * @param fw An ew_fw_t.
 * @return Its name; English in every language.
 */
const char *ewp_fw_name(uint8_t fw);

/**
 * @brief The line of the page as the screen says it: `GPIO 13 HIGH`.
 * @param out  Takes the text.
 * @param size Bytes at @p out.
 */
void ewp_line_text(char *out, size_t size);

/**
 * @brief The page's state in words.
 * @param state A link_esc_state_t.
 * @return The text.
 */
const char *ewp_state_text(uint8_t state);

/**
 * @brief A row's help line.
 * @param r The row.
 * @return The text; "" for none.
 */
const char *ewp_help_text(const ewp_row_t *r);

/* ----------------------------------------------------- esc_wire_help.c: lines */

/**
 * @brief The help line of a byte.
 * @param fw  An ew_fw_t.
 * @param off The byte.
 * @return The text; "" for none.
 */
const char *esc_wire_help_field(uint8_t fw, uint8_t off);

/**
 * @brief The help line of an ESCape32 key.
 * @param slot Its place in ew_map_e32.
 * @return The text; "" past the last.
 */
const char *esc_wire_help_key(size_t slot);

/* ------------------------------------------------ esc_wire_draw.c: geometry */

/**
 * @brief A row's rectangle.
 * @param i The row in the tab, from 0.
 * @return Its rectangle.
 */
gfx_rect_t ewp_row_rect(int i);

/**
 * @brief A row's value.
 * @param i The row in the tab, from 0.
 * @return The part that takes a tap.
 */
gfx_rect_t ewp_value_rect(int i);

/**
 * @brief The action row's button.
 * @param i From the left, 0 to 3.
 * @return Its rectangle.
 */
gfx_rect_t ewp_action_rect(int i);

/**
 * @brief A choice's rectangle.
 * @param i The entry.
 * @return Its rectangle.
 */
gfx_rect_t ewp_choice_rect(int i);

/**
 * @brief The choice list's CANCEL.
 * @return Its rectangle.
 */
gfx_rect_t ewp_choice_cancel_rect(void);

/**
 * @brief The tabs' row.
 * @return Its rectangle.
 */
gfx_rect_t ewp_tabs_rect(void);

/**
 * @brief The area an editor opens over.
 * @return Its rectangle.
 */
gfx_rect_t ewp_editor_area(void);

#endif /* RCBENCH_ESC_WIRE_INT_H */
