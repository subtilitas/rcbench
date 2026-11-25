/**
 * @file
 * @brief The seam between the files of programming for YGE ESCs.
 *
 * yge_panel.c holds the state, the stages, the page set and the card's flow;
 * yge_panel_edit.c the tabs, the rows, the draft and the editors;
 * yge_panel_text.c the words; yge_panel_touch.c the presses;
 * yge_panel_draw.c the pages drawn.  The functions one calls of another are
 * prefixed ygp_.  Nothing outside these files and the tests includes this
 * header; the interface is yge_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_PANEL_INT_H
#define RCBENCH_YGE_PANEL_INT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "openyge_map.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"
#include "yge_card.h"
#include "yge_panel.h"
#include "yge_prog.h"

/* ------------------------------------------------------------------ geometry */

/**
 * @brief The session's layout in body coordinates, 1024px wide, the area's
 *        top at PROG_FULL_Y: tabs and BACK 48px tall, 2 columns of 4 rows 48px
 *        tall at a 56px pitch, two lines over the footer, and the footer whose
 *        foot is UI_KEYS_FOOT.
 */
#define YGP_TAB_DY   4    /**< the tabs' top under the area's, px             */
#define YGP_TAB_H    UI_TAP_MIN /**< their height                             */
#define YGP_BACK_W   110  /**< BACK's width                                   */
#define YGP_ROW_DY   60   /**< the first row's top under the area's           */
#define YGP_PITCH    56   /**< a row's pitch                                  */
#define YGP_ROW_H    UI_TAP_MIN /**< a row's height                           */
#define YGP_VAL_W    232  /**< a value's width: 23 cells                      */
#define YGP_ROWS     4    /**< rows in a column                               */
#define YGP_COLS     2    /**< columns                                        */
#define YGP_ACT_H    UI_TAP_MIN /**< the footer's height                      */
#define YGP_FONT     (&gfx_font_10x20) /**< rows, values and lines            */
#define YGP_LINE_H   24   /**< a text line's pitch                            */

/** @brief Rows on a tab at most. */
#define YGP_TAB_ROWS (YGP_ROWS * YGP_COLS)
/** @brief Tabs: IDENTITY, MODE & BEC, PROTECTION, START, GOVERNOR & RPM,
 *         STICK, RAW. */
#define YGP_TABS 7
/** @brief The flag bits of index 26 that rows of their own show. */
#define YGP_BIT_DIRECTION 0x0001u /**< bit 0: direction, CHECK                */
#define YGP_BIT_F3C       0x0002u /**< bit 1: F3C autorotation                */
#define YGP_BIT_KEEP_MAH  0x0004u /**< bit 2: keep mAh, read-only             */
#define YGP_BIT_BEC12V    OY_FLAG_BEC12V /**< bit 3: BEC up to 12V, read-only */

/** @brief A row: an index, or one bit of index 26. */
typedef struct {
    uint8_t  index; /**< the parameter index                                  */
    uint16_t bit;   /**< a bit of index 26 the row shows; 0 for the index     */
} ygp_row_t;

/** @brief A hold page's kinds. */
enum {
    YGP_PAGE_NONE = 0,
    YGP_PAGE_LEAVE,   /**< LEAVE over edits not written                       */
    YGP_PAGE_READ,    /**< READ AGAIN over edits not written                  */
    YGP_PAGE_RELEASE, /**< a pin another writer holds                         */
};

/** @brief What the choice list chooses. */
enum {
    YGP_CH_NONE = 0,
    YGP_CH_PORT,   /**< the programming port                                  */
    YGP_CH_POWER,  /**< SWITCHED or BY HAND                                   */
    YGP_CH_FIELD,  /**< a value of a row                                      */
    YGP_CH_FILES,  /**< a file                                                */
};

/** @brief What the keypad enters. */
enum {
    YGP_KP_NONE = 0,
    YGP_KP_VOLT,
    YGP_KP_AMP,
    YGP_KP_BOOT,
    YGP_KP_ROW,
};

/** @brief What a press is on. */
enum {
    YGP_P_NONE = 0,
    YGP_P_TAB,
    YGP_P_BACK,
    YGP_P_ROW,
    YGP_P_CONNECT_ROW,
    YGP_P_ACT,
    YGP_P_HOLD,
    YGP_P_CANCEL,
    YGP_P_CHOICE,
    YGP_P_CHOICE_CANCEL,
    YGP_P_KEYPAD,
};

/** @brief Entries of a choice list at most: two columns of 7. */
#define YGP_CHOICE_MAX 14
/** @brief Lines of a hold page at most. */
#define YGP_PAGE_LINES 12
/** @brief Cells of a line at most, with the terminator. */
#define YGP_LINE 101
/** @brief A choice's value that goes to the next page of files. */
#define YGP_CHOICE_NEXT 0x7FFE
/** @brief Files a page of the list shows: NEXT fills the rest. */
#define YGP_FILES_PAGE (YGE_CARD_FILES - 1u)
/** @brief The connect page's rows. */
enum {
    YGP_C_PORT = 0,
    YGP_C_POWER,
    YGP_C_VOLT,
    YGP_C_AMP,
    YGP_C_BOOT,
    YGP_C_ROWS,
};

/** @brief The state. */
typedef struct {
    gfx_rect_t area;      /**< the session's area                             */
    bool       open;      /**< a row opened it                                */
    bool       armed;     /**< the bench is armed or asked to                 */
    uint32_t   rev;       /**< changes when a render would show something else */
    uint32_t   hold_rev;  /**< changes while a hold fills                     */
    uint32_t   sig;       /**< what the page showed at the last feed          */
    yge_prog_t prog;      /**< the model                                      */
    /** @brief The gate yge_panel_gate() sets; the page set's init keeps it */
    yge_prog_gate_t *gate;
    /* The connect page's values. */
    uint8_t    port;      /**< the programming port chosen, 0 to 3            */
    uint8_t    power;     /**< a link_yge_power_t                             */
    bool       supply_typed; /**< the operator typed the session's values     */
    uint16_t   mv;        /**< the session's voltage, mV; SUPPLY's until typed */
    uint16_t   ma;        /**< the session's limit, mA; SUPPLY's until typed  */
    uint16_t   boot_ms;   /**< BOOT WAIT                                      */
    /* The session. */
    uint16_t   draft[YGE_LINK_VALUES_MAX]; /**< the edits on the read copy    */
    uint32_t   draft_gen; /**< the read copy it was made from                 */
    uint16_t   draft_reads; /**< LINK_YP_READS of that copy                   */
    uint16_t   draft_crc; /**< LINK_YP_READ_CRC of that copy                  */
    ui_tabs_t  tabs;      /**< the tabs                                       */
    bool       tabs_set;  /**< laid out                                       */
    /* The presses. */
    bool       have;      /**< a press is taken                               */
    uint8_t    id;        /**< its finger                                     */
    int        pressed;   /**< what it is on                                  */
    int        which;     /**< its row                                        */
    ui_hold_t  hold;      /**< the 2s hold                                    */
    bool       hold_down; /**< a press fills it                               */
    gfx_rect_t touched;   /**< the button a press last showed on, for hold_rev */
    /* The hold page. */
    int        page;      /**< a YGP_PAGE_*                                   */
    int        page_lines; /**< its lines                                     */
    char       lines[YGP_PAGE_LINES][YGP_LINE]; /**< them                     */
    /* The editors. */
    ui_keypad_t kp;       /**< the keypad                                     */
    int        kp_what;   /**< a YGP_KP_*                                     */
    ygp_row_t  edit;      /**< the row in an editor                           */
    int        ch_what;   /**< a YGP_CH_*                                     */
    int        ch_count;  /**< entries                                        */
    int        ch_values[YGP_CHOICE_MAX]; /**< their values                   */
    char       ch_labels[YGP_CHOICE_MAX][YGE_CARD_NAME_MAX + 8]; /**< labels  */
    /* The card. */
    bool       card_look; /**< a tap looks for a card                         */
    bool       card_want; /**< a request waits to be taken                    */
    bool       card_out;  /**< a request is on the card's task                */
    uint32_t   card_id;   /**< the screen's last request's number             */
    uint32_t   backup_id; /**< the model's backup request out; 0 none         */
    yge_card_req_t card;  /**< the request                                    */
    uint16_t   files_first; /**< the list's first file                        */
    uint16_t   files_total; /**< the card's files                             */
    uint8_t    files_count; /**< names of the page held                       */
    char       names[YGE_CARD_FILES][YGE_CARD_NAME_MAX]; /**< the list        */
    /* What the stages and the link were at the last feed. */
    yge_panel_stage_t stage_seen; /**< the stage                              */
    bool       link_seen; /**< the link was up                                */
    bool       no_again;  /**< this hold saw a stop or a fault, or the
                               supply's latch: PIN BACK is the way on         */
    bool       held_stop; /**< of those, a stop or a fault                    */
    /* The session's own word. */
    char       note[YGP_LINE]; /**< what the card or the editor said          */
    bool       note_warn; /**< in the warning colour                          */
    uint32_t   says_hidden; /**< the model's message count the operator acted
                                 on: that message no longer shows             */
    uint32_t   says_seen; /**< the count at the last feed                     */
} ygp_state_t;

/** @brief The state. */
extern ygp_state_t ygp_s;

/* ------------------------------------------------------- yge_panel.c: stages */

/**
 * @brief Where the session is, from the page and the model.
 * @return The stage.
 */
yge_panel_stage_t ygp_stage(void);

/**
 * @brief The session's values, as CONNECT and ENTER AGAIN send them and as
 *        their pages show them.  BY HAND has no voltage and no limit.
 * @param c Takes them.
 */
void ygp_connect_values(yge_prog_connect_t *c);

/**
 * @brief The operator acts: what the screen said goes.
 */
void ygp_words_clear(void);

/**
 * @brief A note over the footer.
 * @param s    The text.
 * @param warn In the warning colour.
 */
void ygp_note(const char *s, bool warn);

/**
 * @brief The hold reached its end: what it stands for goes.
 */
void ygp_hold_apply(void);

/**
 * @brief The list of files asked of the card, from @c files_first on.
 */
void ygp_files_open(void);

/**
 * @brief A file chosen in the list.
 * @param value The choice: a name's index, or the next page.
 */
void ygp_file_chosen(int value);

/**
 * @brief Whether the operator's own request is with the card or waits to go.
 * @return true then.
 */
bool ygp_card_busy(void);

/* --------------------------------------------------- yge_panel_touch.c: presses */

/**
 * @brief One touch event.
 * @param evt The event.
 */
void ygp_event(const touch_event_t *evt);

/**
 * @brief Time for the hold.
 * @param dt_s Seconds since the last.
 */
void ygp_tick(float dt_s);

/** @brief Touch events were lost: the press goes. */
void ygp_cancel(void);

/** @brief Home tag or BACK: an editor closes, a held session asks first. */
void ygp_back(void);

/**
 * @brief LEAVE: at once with no edits not written, else its page.
 */
void ygp_leave(void);

/**
 * @brief Whether a press at (x, y) lands on ABORT.
 * @param x In body coordinates.
 * @param y In body coordinates.
 * @return true then.
 */
bool ygp_safe_at(int x, int y);

/**
 * @brief Whether the stage shows ABORT, which acts on the press.
 * @return true then.
 */
bool ygp_abort_shows(void);

/* ---------------------------------------------------- yge_panel_draw.c: pages */

/**
 * @brief The session drawn.
 * @param c The canvas.
 */
void ygp_render(gfx_canvas_t *c);

/**
 * @brief A row's rectangle.
 * @param i The row in the tab, from 0.
 * @return Its rectangle.
 */
gfx_rect_t ygp_row_rect(int i);

/**
 * @brief A row's value.
 * @param i The row in the tab, from 0.
 * @return The part that takes a tap.
 */
gfx_rect_t ygp_value_rect(int i);

/**
 * @brief The footer's button.
 * @param i From the left, 0 to 3.
 * @return Its rectangle.
 */
gfx_rect_t ygp_action_rect(int i);

/**
 * @brief A choice's rectangle.
 * @param i The entry.
 * @return Its rectangle.
 */
gfx_rect_t ygp_choice_rect(int i);

/**
 * @brief The tabs' row.
 * @return Its rectangle.
 */
gfx_rect_t ygp_tabs_rect(void);

/**
 * @brief The area an editor opens over.
 * @return Its rectangle.
 */
gfx_rect_t ygp_editor_area(void);

/* ------------------------------------------------------ yge_panel_edit.c: rows */

/**
 * @brief The tabs, laid out once.
 */
void ygp_tabs_follow(void);

/**
 * @brief The row of a tab.
 * @param tab The tab, from 0; tab 0 is IDENTITY and holds none.
 * @param i   The row, from 0.
 * @param out Takes it.
 * @return false past the last.
 */
bool ygp_row(int tab, int i, ygp_row_t *out);

/**
 * @brief A row's name, English in every language.
 * @param r The row.
 * @return Its name.
 */
const char *ygp_row_name(const ygp_row_t *r);

/**
 * @brief A value of a row as the screen writes it.
 * @param r    The row.
 * @param v    The index's value: of the draft or of the read copy.
 * @param out  Takes the text.
 * @param size Bytes at @p out.
 */
void ygp_value_text(const ygp_row_t *r, uint16_t v, char *out, size_t size);

/**
 * @brief Whether a row is a CHECK field.
 * @param r The row.
 * @return true then.
 */
bool ygp_row_check(const ygp_row_t *r);

/**
 * @brief Whether a row's draft differs from the read copy.
 * @param r The row.
 * @return true then.
 */
bool ygp_row_changed(const ygp_row_t *r);

/**
 * @brief Whether a row takes an edit.
 * @param r The row.
 * @return true on a table that is written, for a writable field.
 */
bool ygp_row_live(const ygp_row_t *r);

/**
 * @brief The indices the draft changes.
 * @return Their count.
 */
int ygp_changed_count(void);

/**
 * @brief The draft follows the read copy: a new copy replaces it.
 */
void ygp_draft_follow(void);

/**
 * @brief The line that names the draft's CHECK fields.
 * @param out  Takes it; "" for none.
 * @param size Bytes at @p out.
 */
void ygp_check_line(char *out, size_t size);

/**
 * @brief The changed fields as `NAME: from -> to` lines on the page open.
 */
void ygp_page_changes(void);

/**
 * @brief Opens the row's editor: a list of its values, or the keypad.
 * @param r The row.
 */
void ygp_edit_open(const ygp_row_t *r);

/**
 * @brief A choice taken.
 * @param i The entry.
 */
void ygp_choice_done(int i);

/**
 * @brief The keypad's value taken.
 * @param v The value.
 */
void ygp_keypad_done(float v);

/**
 * @brief The connect page's row tapped.
 * @param row A YGP_C_*.
 */
void ygp_connect_row(int row);

/**
 * @brief Adds a choice.
 * @param label Its label.
 * @param value Its value.
 */
void ygp_choice_add(const char *label, int value);

/**
 * @brief Closes the editors.
 */
void ygp_editors_close(void);

/**
 * @brief A hold page's start.
 * @param kind A YGP_PAGE_*.
 */
void ygp_page_open(int kind);

/**
 * @brief A hold page's line.
 * @param s The text.
 */
void ygp_page_add(const char *s);

/* ------------------------------------------------------ yge_panel_text.c: words */

/**
 * @brief The model's last message in words.
 * @return The text; "" for none.
 */
const char *ygp_message_text(void);

/**
 * @brief Whether the model's last message is an error.
 * @return true then.
 */
bool ygp_message_warns(void);

/**
 * @brief The page's state in words.
 * @return The text.
 */
const char *ygp_state_text(void);

/**
 * @brief What the session does now, under the state: the boot wait, the
 *        listening, the read with its count, the write with its index.
 * @param out  Takes the text; "" for nothing to add.
 * @param size Bytes at @p out.
 */
void ygp_session_text(char *out, size_t size);

/**
 * @brief The line of the page as the screen says it: `GPIO 13 LISTEN`.
 * @param out  Takes the text.
 * @param size Bytes at @p out.
 */
void ygp_line_text(char *out, size_t size);

/**
 * @brief Why the table is read-only.
 * @param why An oy_ref_t.
 * @return The text.
 */
const char *ygp_ro_text(uint8_t why);

/**
 * @brief A card's answer in words.
 * @param r A yge_card_result_t.
 * @return The text.
 */
const char *ygp_card_text(uint8_t r);

/**
 * @brief The connect page's value of a row as it shows it.
 * @param row  A YGP_C_*.
 * @param out  Takes the text.
 * @param size Bytes at @p out.
 */
void ygp_connect_text(int row, char *out, size_t size);

#endif /* RCBENCH_YGE_PANEL_INT_H */
