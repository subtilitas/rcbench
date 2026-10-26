/**
 * @file hitec_panel.h
 * @brief The overlay of the programmer for servos by Hitec on SERVO: its
 *        interface.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.
 *
 * A session over the SERVO screen's left column, opened from its row on
 * SERVO > PROGRAM (prog_registry.h) one level down.  It draws hitec_prog.h's
 * model and turns touches into its calls.  Its controls are the screen's:
 * tabs, rows, the number keypad, the choice list and a page whose button is
 * held UI_HOLD_S.
 *
 * Five tabs.  SERVO: the port and the pin it drives, the series, the read,
 * the programming voltage and current, CLEAR after a stop.  MOTION, TRAVEL,
 * PROTECT and INFO: the servo's values as read, the rows of the series in
 * use; a decoded value shows its stored value beside it, and a stored value
 * that fits no encoding shows raw.  Nothing is written to the servo.
 *
 * The overlay stays open while the port is held, by this panel or by
 * another session: PWM and programming are exclusive states of the pin,
 * and CLOSE asks for LEAVE, or for RELEASE PORT, first.
 *
 * The panel's control task owns the link.  It hands this a feed every frame
 * and takes the commands.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "hitec_prog.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_keypad.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Lines a message takes at most, and the bytes of one. */
#define HITEC_PANEL_MSG_LINES 2
/** @brief The bytes of one line of a message, a note or a page. */
#define HITEC_PANEL_LINE      128

/** @brief The tabs. */
typedef enum {
    HITEC_PANEL_SERVO = 0,
    HITEC_PANEL_MOTION,
    HITEC_PANEL_TRAVEL,
    HITEC_PANEL_PROTECT,
    HITEC_PANEL_INFO,
    HITEC_PANEL_PAGES
} hitec_panel_page_t;

/**
 * @brief Start over: closed, nothing held, laid out in @p area.
 * @param area the overlay's area, body pixels
 */
void hitec_panel_init(gfx_rect_t area);
/** @brief The SERVO screen was entered: the options are read from the
 *         settings. */
void hitec_panel_screen_entry(void);
/** @brief Opens the overlay. */
void hitec_panel_open(void);
/**
 * @brief Whether the overlay is open.
 * @return true when open
 */
bool hitec_panel_is_open(void);
/** @brief CLOSE: closes while the port is not held; asks for LEAVE on this
 *         panel's port and for RELEASE PORT on another's. */
void hitec_panel_close(void);
/** @brief The home tag or BACK: an editor closes as its CANCEL does,
 *         otherwise as hitec_panel_close(). */
void hitec_panel_back(void);
/**
 * @brief A port is held, by this panel or another session, or a request
 *        is on its way: the overlay is shown, no other row on the port
 *        opens and the bench takes no arm.
 * @return true while held
 */
bool hitec_panel_holds_port(void);
/**
 * @brief The GPIO the port the overlay names drives.
 * @return the pin, or PROG_REG_NO_PIN
 */
uint8_t hitec_panel_pin(void);
/**
 * @brief None of the session's controls stops what the bench drives: it
 *        works on a SAFE bench only.  Always false.
 * @param x the column, px
 * @param y the row, px
 * @return false
 */
bool hitec_panel_safe_at(int x, int y);
/** @brief The session's hooks for its row on SERVO > PROGRAM. */
extern const prog_page_set_t hitec_panel_pages;
/** @brief The screen is left: presses and editors go; the port stays. */
void hitec_panel_leave(void);
/** @brief Touch events were lost: the press and its hold are dropped. */
void hitec_panel_cancel(void);
/**
 * @brief One frame: the hold of a page.
 * @param dt_s the time since the last frame, s
 */
void hitec_panel_tick(float dt_s);
/**
 * @brief Whether a press that began in the overlay is on the glass.
 * @param id the touch id
 * @return true for the overlay's press
 */
bool hitec_panel_has_press(uint8_t id);
/**
 * @brief A press in the overlay's area, or the rest of one it owns.
 * @param evt the event
 */
void hitec_panel_event(const touch_event_t *evt);
/**
 * @brief Draws the overlay.
 * @param c the canvas
 */
void hitec_panel_render(gfx_canvas_t *c);
/**
 * @brief Changes whenever a render would show something else.
 * @return the revision
 */
uint32_t hitec_panel_revision(void);
/**
 * @brief Changes while a page's hold fills; hitec_panel_apply_rect() is
 *        where.
 * @return the hold's revision
 */
uint32_t hitec_panel_hold_revision(void);

/* --- the control task's side ------------------------------------------- */

/**
 * @brief The link task's feed for this frame.
 * @param f the feed
 */
void hitec_panel_feed(const hitec_prog_feed_t *f);
/**
 * @brief The next command for the link task.
 * @param out the command
 * @return true when one was taken
 */
bool hitec_panel_take(hitec_prog_cmd_t *out);
/**
 * @brief The gate the link task claims each command at; kept over
 *        hitec_panel_init().  NULL for none.
 * @param g the gate
 */
void hitec_panel_gate(hitec_prog_gate_t *g);
/**
 * @brief The bench is armed or asked to arm: CONNECT and READ ALL are
 *        refused.
 * @param armed true while it is
 */
void hitec_panel_bench_armed(bool armed);

/* --- for the tests and the renderer ------------------------------------ */

/**
 * @brief The model.
 * @return the programmer's state
 */
hitec_prog_t *hitec_panel_model(void);
/**
 * @brief A tab, one fifth of the row left of CLOSE, 48px tall.
 * @param page hitec_panel_page_t
 * @return its rectangle
 */
gfx_rect_t hitec_panel_tab_rect(int page);
/**
 * @brief CLOSE, 120 x 48px at the top right.
 * @return its rectangle
 */
gfx_rect_t hitec_panel_close_rect(void);
/**
 * @brief The control of a row: a button, or the box of a value.
 * @param page the tab
 * @param row  the row, from the top
 * @param col  the column, 0 or 1
 * @return its rectangle; empty for no row there in the series shown
 */
gfx_rect_t hitec_panel_value_rect(int page, int row, int col);
/**
 * @brief The label of a row as it is drawn now.
 * @param page the tab
 * @param row  the row
 * @param col  the column
 * @return the label; NULL for no row there
 */
const char *hitec_panel_label(int page, int row, int col);
/**
 * @brief The text in the control of a row: a button's, or the value.
 * @param page the tab
 * @param row  the row
 * @param col  the column
 * @return the text; NULL for no row there
 */
const char *hitec_panel_value(int page, int row, int col);
/**
 * @brief Whether the control of a row takes a tap now.
 * @param page the tab
 * @param row  the row
 * @param col  the column
 * @return true when live
 */
bool hitec_panel_row_live(int page, int row, int col);
/**
 * @brief Line @p i of the PORT row's label: 0 the port, the pin and the
 *        series, 1 the receive mode and the supply CONNECT asks.
 * @param i the line
 * @return the line; NULL past the last
 */
const char *hitec_panel_port_line(int i);
/**
 * @brief The note lines of the tab shown, from the top, the message's
 *        lines after them.
 * @param i the line
 * @return the line; NULL past the last
 */
const char *hitec_panel_note(int i);
/**
 * @brief The rows the notes and the message of the tab shown take, from
 *        the first note's top to the last line's foot.
 * @return the rectangle; empty for none
 */
gfx_rect_t hitec_panel_notes_drawn(void);
/**
 * @brief A message's lines, in the language in force.
 * @param m   the message
 * @param out receives the lines
 * @return the lines used, 0 for no message
 */
int hitec_panel_message_lines(const hitec_prog_msg_t *m,
                              char out[HITEC_PANEL_MSG_LINES]
                                      [HITEC_PANEL_LINE]);
/**
 * @brief The rows of 10 x 20 cells a line takes, wrapped at a space to the
 *        overlay's width less 10px a side: 60 cells on SERVO's left card.
 * @param s the line
 * @return its rows, 0 for an empty line
 */
int hitec_panel_text_rows(const char *s);
/**
 * @brief What is open over the page.
 * @return 0 none, 1 keypad, 2 choice list, 3 a hold page
 */
int hitec_panel_editor(void);
/**
 * @brief The tab shown.
 * @return hitec_panel_page_t
 */
int hitec_panel_page(void);
/**
 * @brief Entry @p i of the open list, 48px tall at a 56px pitch.
 * @param i the entry
 * @return its rectangle
 */
gfx_rect_t hitec_panel_choice_rect(int i);
/**
 * @brief The open list's entry @p i.
 * @param i the entry
 * @return its label; NULL past the last or with no list open
 */
const char *hitec_panel_choice_label(int i);
/**
 * @brief A list's CANCEL, 150 x 48px at the foot's right.
 * @return its rectangle
 */
gfx_rect_t hitec_panel_choice_cancel_rect(void);
/**
 * @brief A hold page's button, 300 x 56px at the foot's left.
 * @return its rectangle
 */
gfx_rect_t hitec_panel_apply_rect(void);
/**
 * @brief A hold page's CANCEL, 180 x 56px at the foot's right.
 * @return its rectangle
 */
gfx_rect_t hitec_panel_cancel_rect(void);
/**
 * @brief Line @p i of the open hold page, its title first.
 * @param i the line
 * @return the line; NULL past the last or with no page open
 */
const char *hitec_panel_page_line(int i);
/**
 * @brief The keypad.
 * @return the overlay's keypad
 */
ui_keypad_t *hitec_panel_keypad(void);

#ifdef __cplusplus
}
#endif
