/*
 * The outputs screen: choose a protocol, then tick the pins it drives.
 *
 * One protocol and a set of pins rather than eight independent slots, because
 * that is the shape of the job in front of the operator -- four servo leads,
 * or one ESC (electronic speed controller).  out_bind.c turns the choice into
 * the OUTPUTS and CHAN_CFG pages; this file is only the touching.
 *
 * The screen writes nothing to the wire.  It calls the apply function it was
 * given whenever the binding changes, and the application does the writing,
 * so the whole screen renders and is driven on the host with no link at all.
 * Which protocol is selected is this screen's own and is not a change: a
 * pick in the list calls nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bind_link.h"
#include "out_bind.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

const ui_screen_t *outputs_screen(void);

/** Repaint the cached chrome in every framebuffer. */
void outputs_screen_invalidate(void);

/** The binding shown: the last one read, plus edits not yet read back. */
const outbind_t *outputs_screen_binding(void);

/**
 * A binding read from the coprocessor, or one the screen is posed with.
 * The screen then holds a confirmed binding and takes edits.
 */
void outputs_screen_set_binding(const outbind_t *b);

/**
 * What a read of the two output pages gave (bind_link_read()).
 *
 * BIND_READ_OK is outputs_screen_set_binding() of the binding read.
 * BIND_READ_NONE and BIND_READ_ODD leave the screen on the last binding a
 * read confirmed, with any edit made since taken back, drawn dimmed with
 * the reason under the protocol; no pin can be ticked or unticked until a
 * read is BIND_READ_OK again.  A reading for another board than the one
 * shown has no last binding to keep and shows that board with nothing
 * bound.  BIND_READ_NO_BOARD shows no pins.
 */
void outputs_screen_set_reading(const bind_reading_t *r);

/** The state of the last reading; BIND_READ_OK after a set_binding(). */
bind_read_t outputs_screen_read_state(void);

/** Whether a tap on a pin can change the binding: the last reading was
 *  BIND_READ_OK. */
bool outputs_screen_editable(void);

/**
 * Called with the binding to write after a pin is ticked or unticked, and
 * with a binding that holds no pin when UNBIND ALL PINS is held for
 * UI_HOLD_S (2 s) in the BIND_READ_ODD state.  Never for a protocol pick.
 *
 * The screen does not know whether a link exists or whether the far end took
 * the pages; outputs_screen_set_result() is how it is told, so the operator
 * sees a refusal on the screen that caused it.
 */
typedef void (*outputs_apply_fn)(const outbind_t *b);
void outputs_screen_set_apply(outputs_apply_fn fn);

/** What became of the last write: the text is shown under the protocol. */
typedef enum {
    OUTPUTS_IDLE = 0,   /**< nothing written yet, or a write not sent     */
    OUTPUTS_OK,         /**< the coprocessor took it                      */
    OUTPUTS_NO_LINK,    /**< nothing answered                             */
    OUTPUTS_REFUSED,    /**< it answered and would not have it            */
} outputs_result_t;

void outputs_screen_set_result(outputs_result_t r);

/** What a pin's cell shows. */
typedef enum {
    OUTPUTS_CELL_NONE = 0,  /**< no such pin on this board                  */
    OUTPUTS_CELL_FREE,      /**< bound to nothing: "PAD n"                  */
    OUTPUTS_CELL_TICKED,    /**< bound in the selected protocol: "PAD n"    */
    OUTPUTS_CELL_HELD,      /**< bound in another protocol: its name; or a
                                 PWM pin the coprocessor refuses: the pin
                                 it clashes with (outputs_clash_label())   */
    OUTPUTS_CELL_RESERVED,  /**< the coprocessor's own: what holds it       */
} outputs_cell_t;

/**
 * The state of the cell of catalogue entry @p index and the line printed
 * under its GPIO name, as the screen draws them.  @p label may be NULL.
 */
outputs_cell_t outputs_screen_cell(uint8_t index, char *label, size_t cap);

/** No pin, for outputs_screen_set_prog_pin(). */
#define OUTPUTS_NO_PIN 0xFFu

/**
 * The GPIO the coprocessor's programming port is bound to, as its page reads
 * it, or OUTPUTS_NO_PIN (the start value).  While no protocol holds that pin
 * its cell reads OUTPUTS_CELL_RESERVED with the port's name and takes no
 * tick, and a PWM protocol is not offered the other pin of its slice.  It
 * stays across a reset of the screen.
 */
void outputs_screen_set_prog_pin(uint8_t gpio);

/**
 * The line under the protocol that says why no pin can be ticked, or why the
 * PWM pin last tapped cannot be (outputs_clash_reason()), until another pick
 * or tick, as the screen draws it in the language in force; an empty string
 * when there is none.  Returns whether there is one.
 */
bool outputs_screen_reason(char *buf, size_t cap);

/**
 * The part of that line the protocol's rules give, for binding @p b: OFF
 * edits nothing, the protocol has all its pins, all 8 slots are in use, or
 * the protocol needs more channels than are free.  The pin picker draws the
 * same line from its own binding.  An empty string and false when a pin can
 * still be added.
 */
bool outputs_reason_of(const outbind_t *b, char *buf, size_t cap);

/**
 * The line under a pin's GPIO name for a pin the coprocessor refuses the
 * PWM generator (outbind_pwm_clash() with @p prog_gpio): "SLICE OF GP13"
 * beside the programming port's pin, "PWM CH OF GP0" beside a pin bound as
 * PWM.  An empty string and false for any other pin.  The pin picker names
 * the holder with the same line.
 */
bool outputs_clash_label(const outbind_t *b, uint8_t index,
                         uint8_t prog_gpio, char *buf, size_t cap);

/**
 * The line under the protocol for such a pin, catalogue entry @p index:
 * "GP28: PWM SLICE OF PROG PORT GP13", "GP16: SAME PWM CHANNEL AS GP0".  An
 * empty string and false when that pin is not refused, or @p index is no
 * entry (-1).  Both screens pass the pin whose tap was refused last, and
 * show it after outputs_reason_of(): a protocol that can take no pin at all
 * says that first.  The cell names the holder before any tap.
 */
bool outputs_clash_reason(const outbind_t *b, int index, uint8_t prog_gpio,
                          char *buf, size_t cap);

/**
 * Select protocol @p proto, as a pick in the list does: the set shown and
 * the group the next tick joins.  Writes nothing and calls nothing.  OFF
 * (0) stays selected until another protocol is selected or this screen or
 * the pin picker is left.  An index past the table is ignored.  The pin
 * picker's list selects through this, so the two screens show one
 * selection.
 */
void outputs_screen_select(uint8_t proto);

/**
 * End a picked OFF as leaving this screen does: the selection moves to the
 * lowest-numbered protocol that holds a pin, or stays OFF when none does.
 * Nothing changes when OFF was not picked.  Writes nothing and calls
 * nothing.  The pin picker calls it when it is left, so OFF picked there
 * ends with that visit too.
 */
void outputs_screen_end_off(void);

#ifdef __cplusplus
}
#endif
