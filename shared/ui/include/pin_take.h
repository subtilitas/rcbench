/**
 * @file
 * @brief OUTPUTS' question card: move a pin another protocol holds to the
 *        protocol selected.
 *
 * A tap on a pin another protocol holds asks first.  The card names the
 * pin and its holder, asks to move the pin to the protocol selected, says
 * what the holder keeps, the channel's new role when it changes and, in
 * amber, the lead on the pad that gets the new signal from the next ARM.
 * MOVE PIN writes the binding outbind_take_check() returns, once, through
 * the view's apply hook; CANCEL, the home tag or a press anywhere off the
 * card closes it and writes nothing.  A tap MOVE PIN confirms: the move
 * switches nothing on, and the ARM that drives the new signal is a hold.
 *
 * A tap the card does not ask about says why on the view's reason line: a
 * programming port's pin, an interface's pin (SENSOR BUS, PD MINI, PHASE
 * TAP, each naming the screen that sets it), OFF selected, a protocol at
 * its pin cap, out of slots or channels, a PWM (pulse-width modulation)
 * rule broken by the move.  The rules are out_bind_take.h's.
 *
 * One card serves both views of OUTPUTS: the board view (picker_screen.c)
 * puts it over the board, the grid view (outputs_screen.c) over the two
 * grid columns away from the tapped cell.  It belongs to the view that
 * opened it and closes, writing nothing, when that view's binding changes
 * under it (a read-back, a write, a read that failed), when the protocol
 * selected changes, when the views switch, and when the screen is left or
 * reset.  MOVE PIN checks the move again on the binding as it stands and
 * writes nothing when the pin, its holder or the verdict moved.
 *
 * UI task only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PIN_TAKE_H
#define RCBENCH_PIN_TAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "out_bind.h"
#include "out_bind_take.h"
#include "touch_types.h"
#include "ui_focus.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The view a card or a reason belongs to. */
typedef enum {
    PIN_TAKE_BOARD = 0, /**< the board view, picker_screen.c */
    PIN_TAKE_GRID,      /**< the grid view, outputs_screen.c */
} pin_take_view_t;

/** @brief What a touch event did on an open card. */
typedef enum {
    PIN_TAKE_PASS = 0, /**< no card is open: the view takes the event     */
    PIN_TAKE_TAKEN,    /**< the card took it; nothing to write            */
    PIN_TAKE_MOVE,     /**< MOVE PIN tapped and the move stands: write it */
} pin_take_act_t;

/** @brief The card's buttons. */
typedef enum {
    PIN_TAKE_BTN_MOVE = 0, /**< MOVE PIN */
    PIN_TAKE_BTN_CANCEL,   /**< CANCEL   */
} pin_take_btn_t;

/** @brief The card's lines, top to bottom. */
typedef enum {
    PIN_TAKE_LINE_TITLE = 0, /**< GPIO n: the holder                    */
    PIN_TAKE_LINE_ASK,       /**< the question                          */
    PIN_TAKE_LINE_KEEPS,     /**< what the holder keeps                 */
    PIN_TAKE_LINE_ROLE,      /**< the channel's new role, "" for none   */
    PIN_TAKE_LINE_LEAD,      /**< the lead that gets the new signal     */
} pin_take_line_t;

/** @brief How the card lays its lines out (pin_take_layout()). */
typedef struct {
    int  title_h;    /**< the title's face height, px                    */
    int  body_h;     /**< the question's and the holder's face height    */
    int  small_h;    /**< the role's and the lead's face height          */
    int  text_end;   /**< the row the last line of text ends on          */
    int  buttons_y;  /**< the top of the highest button                  */
    bool cut;        /**< a line ran out of rows or past the card's side */
} pin_take_layout_t;

/** @brief Close the card and forget the last refusal: the screen is
 *         reset. */
void pin_take_reset(void);

/**
 * @brief A tap on catalogue entry @p index of binding @p b in view @p view.
 *
 * Runs outbind_take_check() with the protocol @p b selects as the taker.
 * A move that stands opens the card over @p card; a refusal with a reason
 * keeps it for pin_take_reason().  A pin no protocol holds, the taker's
 * own pin, a reserved pin, a binding not read and a soldered board without
 * roles are not the card's: the view handles the tap as before.
 *
 * @param view  The view tapped.
 * @param b     Its binding as it stands.
 * @param index The pin's catalogue entry.
 * @param ctx   The ports, interfaces and read state beside @p b.
 * @param card  Where the card goes, in the view's coordinates.
 * @return true when the tap is the card's (opened or refused with a
 *         reason); false for a tap the view handles.
 */
bool pin_take_tap(pin_take_view_t view, const outbind_t *b, uint8_t index,
                  const outbind_take_ctx_t *ctx, gfx_rect_t card);

/**
 * @brief Whether the card is open.
 * @return true while it is
 */
bool pin_take_is_open(void);

/**
 * @brief Whether the card is open in view @p view.
 * @param view the view
 * @return true while it is open there
 */
bool pin_take_open_in(pin_take_view_t view);

/**
 * @brief The catalogue entry the card asks about.
 * @return the entry, or -1 while no card is open
 */
int pin_take_index(void);

/** @brief Close the card without writing. */
void pin_take_close(void);

/**
 * @brief Forget the last refusal, so the reason line no longer names it: a
 *        pick, a tick or another tap.
 */
void pin_take_forget_why(void);

/** @brief Drop a press on the card's buttons: touch events were lost.  The
 *         card stays open. */
void pin_take_cancel(void);

/**
 * @brief View @p view's binding as it stands now: the card it opened
 *        closes, writing nothing, when @p b is not read, names another
 *        board or protocol, or holds other pins than when it opened.
 * @param view the view
 * @param b    its binding
 * @param read whether a read confirmed it
 */
void pin_take_follow(pin_take_view_t view, const outbind_t *b, bool read);

/**
 * @brief A touch event while the card may be open.
 *
 * Every press is the card's while it is open: a press off the card closes
 * it and is spent, one on a button presses it.  A release on the button
 * its press began on, by the same contact and with a tap, acts: CANCEL
 * closes; MOVE PIN checks the move again on @p b and fills @p out with the
 * binding to write when the pin, its holder, the taker and the board are
 * the same and the move still stands, and closes the card either way.
 *
 * @param evt The event.
 * @param b   The view's binding as it stands.
 * @param ctx The ports, interfaces and read state beside it.
 * @param out Takes the binding to write on PIN_TAKE_MOVE.
 * @return What the event did.
 */
pin_take_act_t pin_take_event(const touch_event_t *evt, const outbind_t *b,
                              const outbind_take_ctx_t *ctx, outbind_t *out);

/**
 * @brief The reason line for view @p view's last refused tap: the port or
 *        the interface and the screen that sets it, OFF selected, the
 *        protocol's cap, slots or channels, the PWM rule.  Checked again on
 *        @p b, so a refusal the binding no longer gives says nothing.
 * @param view The view.
 * @param b    Its binding.
 * @param ctx  The ports, interfaces and read state beside it.
 * @param buf  Takes the line, or an empty string.
 * @param cap  Bytes at @p buf.
 * @return true when there is a line
 */
bool pin_take_reason(pin_take_view_t view, const outbind_t *b,
                     const outbind_take_ctx_t *ctx, char *buf, size_t cap);

/**
 * @brief The card's box.
 * @return the box, empty while no card is open
 */
gfx_rect_t pin_take_rect(void);

/**
 * @brief Where button @p which is on the open card.
 * @param which the button
 * @return its box, empty while no card is open
 */
gfx_rect_t pin_take_button(pin_take_btn_t which);

/**
 * @brief Line @p which of the open card, in the language in force.
 * @param which the line
 * @param buf   takes it, or an empty string
 * @param cap   bytes at @p buf
 * @return false while no card is open
 */
bool pin_take_line(pin_take_line_t which, char *buf, size_t cap);

/**
 * @brief How the open card lays its lines out, in the language in force.
 * @param out takes the layout
 * @return false while no card is open
 */
bool pin_take_layout(pin_take_layout_t *out);

/**
 * @brief Draw the open card; nothing while none is open.
 * @param c the view's canvas
 */
void pin_take_draw(gfx_canvas_t *c);

/**
 * @brief The Dial's list while the card is open: MOVE PIN and CANCEL,
 *        starting on CANCEL.
 * @param out takes the list
 * @return false while no card is open
 */
bool pin_take_focus(ui_focus_list_t *out);

/**
 * @brief Bumped by every change of what the card draws; both views repaint
 *        on a change of it.
 * @return the count
 */
uint32_t pin_take_revision(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PIN_TAKE_H */
